// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
/*
 * OpenDMR, service: YSF (Fusion / C4FM) conventional channels (ysf_radio.h).
 *
 * Receive: the modem's discriminator is recorded at 8 kHz by afrx.c; ysf_demod.c (derived from p25_demod.c) locks
 * onto the 20-symbol frame sync and gives one 4-level dibit at a time. 480 dibits make a 100 ms frame, decoded by
 * ysf_frame.c; FICH SC is the DG-ID, and V/D-1 and DN voice blocks (9 bytes of DMR-format AMBE+2, or a DN field
 * converted by ysf_voice.c) go through our vocoder into voice_play.c's 20 ms queue. The demodulator, frame buffers
 * and voice queue live in the vocoder's overlay RAM, as NXDN's do (afrx claims OVERLAY_AMBE).
 *
 * Transmit: the mic's 20 ms frames through our AMBE+2 encoder, five per YSF frame, into ysf_tx.c's frame builder
 * (header, communication frames, terminator, CSD callsigns and DG-ID). The 480 dibits of each frame go into the P25
 * physical layer's FIFO at 4800 symbols/s, with the modem's 4FSK level scaled for YSF's deviation while sending.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "hw.h"
#include "modem.h"
#include "vocoder.h"
#include "radio.h"
#include "radio_hooks.h"
#include "radio_internal.h"
#include "p25_phy.h"
#include "codeplug.h"
#include "ui.h"
#include "last_heard.h"
#include "dmr_ids.h"
#include "dmr_compat.h"
#include "voice_play.h"
#include "afrx.h"
#include "ysf_demod.h"
#include "ysf_frame.h"
#include "ysf_voice.h"
#include "ysf_tx.h"
#include "ysf_radio.h"

#define PCM_FRAMES     20  // 400 ms of decoded voice
#define PLAY_START     3   // frames queued before playback starts: a frame is decoded in one burst after 100 ms
#define PLAY_START_MS  20  // keep that much of the burst's jitter
#define CALL_TIMEOUT   400 // ms without a frame: the call ended (a frame is 100 ms)
#define OVERLAY_BSS __attribute__((section(".ysf_bss"))) // shared with the other AMBE receivers, one at a time

#define TX_DEV_WIDE    1536 // MODEM_MOD_SCALE_ONE is P25's +-1800 Hz; YSF Wide is +-2700 Hz
#define TX_DEV_NARROW  768  // YSF Narrow is +-1350 Hz
#define TX_PREFILL     2    // frames in the FIFO before keying (the first voice frame is 100 ms away)

volatile ysf_radio_stats_t ysf_radio_stats;

/* The state in overlay RAM: only one digital receiver runs at a time, and all AMBE modes share this memory */
static ysf_demod dm OVERLAY_BSS;
static uint8_t rx_dibits[YSF_FRAME_DIBITS] OVERLAY_BSS; // the dibits in frame order (ring: rx_start, rx_head)
static uint8_t rx_frame[YSF_FRAME_BYTES] OVERLAY_BSS;
static uint8_t rx_voice[18] OVERLAY_BSS;
static int16_t pcm[PCM_FRAMES][VOICE_PLAY_SAMPLES] OVERLAY_BSS;
// Transmit buffers too: the afrx task's stack is tight with the AMBE encoder, and RX and TX never overlap.
static int16_t mic[10 * 80] OVERLAY_BSS; // 100 ms of mic at 8 kHz, ten 10 ms halves
static uint8_t tx_frame[YSF_FRAME_BYTES] OVERLAY_BSS;
static uint8_t tx_dibits[YSF_FRAME_DIBITS] OVERLAY_BSS;
static uint8_t tx_ambe[YSF_BLOCKS][9] OVERLAY_BSS;
static uint8_t tx_field[YSF_BLOCKS][13] OVERLAY_BSS;

static volatile unsigned rx_start, rx_head, rx_count;
static volatile bool frame_pending; // a sync was found; wait for the rest of its 480 dibits
static ysf_call_t call;
static volatile TickType_t last_frame_at, heard_at;
static volatile bool heard;
static uint8_t rx_dgid;
static uint8_t tx_dgid;
static char tx_dest[YSF_CALLSIGN_LEN + 1]; // NUL terminated, as call_field() takes a C string

static ysf_tx_config tx_cfg;
static uint32_t tx_total, tx_frame_no;
static uint32_t tx_dev = TX_DEV_NARROW;

static const voice_play_cfg_t play_cfg = {
    .frames = pcm,
    .count = PCM_FRAMES,
    .start = PLAY_START,
    .start_ms = PLAY_START_MS,
    .underruns = &ysf_radio_stats.underruns,
};

static const afrx_ops_t ops; // below

/* ---- received calls ---- */

static void call_publish(void)
{
    ui_event_t ev = {.type = UI_EVENT_DATA, .what = UI_DATA_SIGNAL};

    if (call.active && call.src[0]) {
        char text[LAST_HEARD_TEXT_SIZE];
        uint32_t id = 2166136261u; // a callsign isn't a DMR ID: a hash keeps one last-heard row per caller
        const char *p = call.src;

        while (*p) {
            id = (id ^ (uint8_t)*p++) * 16777619u;
        }
        snprintf(text, sizeof(text), "%s", call.src);
        last_heard_add(id, 0, LAST_HEARD_TS_YSF, text);
    }
    ui_post(&ev);
}

static void call_begin(void)
{
    if (!call.active) {
        memset(&call, 0, sizeof(call));
        call.active = true;
        call.dgid = rx_dgid;
        last_frame_at = xTaskGetTickCount();
        voice_play_prepare();
        vocoderReset();
    }
}

static void call_end(void)
{
    if (call.active) {
        call.active = false;
        voice_play_end();
        call_publish();
    }
}

static void voice(const uint8_t ambe[9])
{
    int16_t *out = voice_play_frame();

    if (!out) {
        ysf_radio_stats.pcm_drops++;
        return;
    }
    vocoderDecodeFrame(ambe, out);
    ysf_radio_stats.voice_blocks++;
    voice_play_put();
}

// A ten-character field on the air: pad with spaces, cut longer names (codeplug callsigns are at most nine).
// The ysf_tx_config fields are exactly YSF_CALLSIGN_LEN bytes, without room for a NUL: writing one there used to
// spill into the next field.
static void call_field_air(char out[YSF_CALLSIGN_LEN], const char *in)
{
    size_t n = strlen(in);

    if (n > YSF_CALLSIGN_LEN) {
        n = YSF_CALLSIGN_LEN;
    }
    memcpy(out, in, n);
    memset(out + n, ' ', YSF_CALLSIGN_LEN - n);
}

// Local, NUL-terminated copy (our call struct and tx_dest have one extra byte).
static void call_field(char out[YSF_CALLSIGN_LEN + 1], const char *in)
{
    call_field_air(out, in);
    out[YSF_CALLSIGN_LEN] = '\0';
}

// Received CSD units are fixed-width, space-padded and NOT NUL-terminated: never strlen() them.
static void call_field_fixed(char out[YSF_CALLSIGN_LEN + 1], const uint8_t in[YSF_CALLSIGN_LEN])
{
    memcpy(out, in, YSF_CALLSIGN_LEN);
    out[YSF_CALLSIGN_LEN] = '\0';
}

// Calls from the header or from CSD1/DN's first data units (DN carries one ten-byte unit per frame).
static void receive_csd(const ysf_fich *f, const uint8_t *dch, unsigned dch_len)
{
    if (f->fi == YSF_FI_HEADER || (f->dt == YSF_DT_VD_MODE1 && f->fn == 0)) {
        if (dch_len >= 20) {
            call_field_fixed(call.dst, dch);
            call_field_fixed(call.src, dch + 10);
        }
    } else if (f->dt == YSF_DT_VD_MODE2 && f->fn <= 1 && dch_len >= 10) {
        call_field_fixed(f->fn == 0 ? call.dst : call.src, dch);
    }
}

static void receive(void)
{
    ysf_fich f;
    uint8_t dch[20], dch2[20];
    unsigned dch_len = 0;

    if (!ysf_frame_decode(rx_frame, &f, dch, dch2, &dch_len)) {
        ysf_radio_stats.bad_frames++;
        return;
    }
    ysf_radio_stats.frames++;
    if (rx_dgid && (f.sc != rx_dgid)) {
        return; // DG-ID squelch: like CTCSS for the digital mode
    }
    last_frame_at = xTaskGetTickCount();
    heard_at = last_frame_at;
    heard = true;
    radio_eco_activity();
    call.dgid = f.sc;
    call.dt = f.dt;

    if (f.fi == YSF_FI_TERMINATOR) {
        call_end();
        return;
    }
    if ((f.fi != YSF_FI_HEADER) && (f.fi != YSF_FI_COMMUNICATION)) {
        return;
    }
    call_begin();
    receive_csd(&f, dch, dch_len);

    if ((f.dt != YSF_DT_VD_MODE1) && (f.dt != YSF_DT_VD_MODE2)) {
        return; // VW needs full-rate IMBE, which this tree does not have
    }
    for (unsigned b = 0; b < YSF_BLOCKS; b++) {
        uint8_t ambe[9];
        unsigned len = ysf_voice_get(rx_frame, f.dt, b, rx_voice);

        if (!len) {
            continue;
        }
        if (f.dt == YSF_DT_VD_MODE2) {
            ysf_voice_dn_to_frame(rx_voice, ambe, NULL);
        } else {
            memcpy(ambe, rx_voice, 9);
        }
        voice(ambe);
    }
}

// Check the last 20 dibits of the ring for the frame sync, without rebuilding a whole 480-dibit frame. Keeping this
// O(20) per symbol matters: the old code rebuilt 480 dibits on every symbol until it found a boundary, which could
// keep the afrx task busy for hundreds of thousands of operations on a marginal signal and starve the firmware.
static bool sync_at(unsigned start)
{
    uint8_t sync[YSF_SYNC_BYTES];

    for (unsigned i = 0; i < YSF_SYNC_BYTES; i++) {
        uint8_t b = 0;

        for (unsigned j = 0; j < 4; j++) {
            b = (uint8_t)((b << 2) | rx_dibits[(start + 4 * i + j) % YSF_FRAME_DIBITS]);
        }
        sync[i] = b;
    }
    return ysf_sync_errors(sync) <= YSF_SYNC_MAX_ERRORS;
}

static void on_dibit(void *ctx, int dibit)
{
    (void)ctx;

    if (dibit == YSF_DEMOD_LOST) {
        rx_start = rx_head = rx_count = 0;
        frame_pending = false;
        call_end();
        return;
    }
    rx_dibits[rx_head] = (uint8_t)dibit;
    rx_head = (rx_head + 1) % YSF_FRAME_DIBITS;
    // ysf_demod.c only emits dibits once it has locked on a sync, but check the 20-dibit window every time: after a
    // relock the first dibit is not necessarily ours, and a false sync in the data must not cost a whole frame.
    if (sync_at((rx_head + YSF_FRAME_DIBITS - 20) % YSF_FRAME_DIBITS)) {
        rx_start = (rx_head + YSF_FRAME_DIBITS - 20) % YSF_FRAME_DIBITS;
        rx_count = 20;
        frame_pending = true;
    }
    if (!frame_pending) {
        return;
    }
    if (rx_count < YSF_FRAME_DIBITS) {
        rx_count++;
    }
    if (rx_count < YSF_FRAME_DIBITS) {
        return;
    }
    frame_pending = false;
    rx_count = 0;
    for (unsigned i = 0; i < YSF_FRAME_BYTES; i++) {
        uint8_t b = 0;

        for (unsigned j = 0; j < 4; j++) {
            b = (uint8_t)((b << 2) | rx_dibits[(rx_start + 4 * i + j) % YSF_FRAME_DIBITS]);
        }
        rx_frame[i] = b;
    }
    receive();
}

static void rx_init(void)
{
    ysf_demod_init(&dm, on_dibit, NULL);
    rx_start = rx_head = rx_count = 0;
    frame_pending = false;
    memset(&call, 0, sizeof(call));
    memset(rx_dibits, 0, sizeof(rx_dibits));
    last_frame_at = xTaskGetTickCount();
}

static void samples(int16_t *x, int n)
{
    ysf_demod_samples(&dm, x, n);
}

static void poll(void)
{
    if (call.active && ((xTaskGetTickCount() - last_frame_at) > pdMS_TO_TICKS(CALL_TIMEOUT))) {
        call_end();
    }
}

/* ---- transmit ---- */

static void voice_cb(void *ctx, unsigned frame_no, unsigned block, uint8_t *vch, unsigned vch_len)
{
    (void)ctx;
    (void)frame_no;

    if (block >= YSF_BLOCKS) {
        return;
    }
    if (tx_cfg.dt == YSF_DT_VD_MODE2 && vch_len >= 13) {
        memcpy(vch, tx_field[block], 13);
    } else if (tx_cfg.dt == YSF_DT_VD_MODE1 && vch_len >= 9) {
        memcpy(vch, tx_ambe[block], 9);
    }
}

static void tx_setup(void)
{
    char own[YSF_CALLSIGN_LEN + 1];

    memset(&tx_cfg, 0, sizeof(tx_cfg));
    tx_cfg.dt = YSF_DT_VD_MODE1;
    tx_cfg.dev = (uint8_t)((rs.channel.flag4 & FLAG4_WIDE) ? 0 : 1);
    tx_cfg.cm = YSF_CM_GROUP;
    tx_cfg.mr = YSF_MR_DIRECT;
    tx_cfg.cs = YSF_CS_CSD;
    tx_cfg.sql = 0;
    tx_cfg.voip = 0;
    tx_cfg.dgid = tx_dgid;
    tx_cfg.cycles = 1;
    tx_cfg.voice = voice_cb;
    call_field_air(tx_cfg.dest, tx_dest[0] ? tx_dest : "ALL");
    codeplug_get_radio_name(own);
    call_field_air(tx_cfg.src, own);
    call_field_air(tx_cfg.down, "");
    call_field_air(tx_cfg.up, "");
    call_field_air(tx_cfg.rem1, "");
    call_field_air(tx_cfg.rem2, "");
    call_field_air(tx_cfg.rem3, "");
    call_field_air(tx_cfg.rem4, "");
    tx_dev = tx_cfg.dev ? TX_DEV_NARROW : TX_DEV_WIDE;
    tx_total = ysf_tx_total_frames(&tx_cfg);
    tx_frame_no = 0;
}

// One whole frame into the FIFO, with room to spare (p25_phy_tx_put() overwrites if the FIFO fills).
static void send_frame(unsigned frame_no)
{
    while (p25_phy_tx_room() < YSF_FRAME_DIBITS + 144) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!ysf_tx_frame_build(&tx_cfg, frame_no, tx_frame)) {
        return;
    }
    ysf_bytes_to_dibits(tx_frame, YSF_FRAME_BYTES, tx_dibits);
    p25_phy_tx_put(tx_dibits, YSF_FRAME_DIBITS);
    ysf_radio_stats.tx_frames++;
}

static void transmit(void)
{
    int halves = 0;
    bool ending = false;

    vocoderReset();
    tx_setup();
    afrx_fifo_prepare(tx_dev);

    // MMDVM (YSFTX.cpp) and the Yaesu air interface key up with 200 ms of 0x77 before the first frame sync;
    // without it an external MMDVM/Pi-Star receiver may never start its sync hunt. Fill it in chunks so no
    // extra overlay buffer is needed (the pattern repeats every two dibits).
    for (unsigned sent = 0; sent < YSF_PREAMBLE_DIBITS; sent += YSF_FRAME_DIBITS) {
        while (p25_phy_tx_room() < YSF_FRAME_DIBITS + 144) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        ysf_preamble_dibits(tx_dibits, YSF_FRAME_DIBITS);
        p25_phy_tx_put(tx_dibits, YSF_FRAME_DIBITS);
    }

    // The first encoder output lags one frame; these frames of silence cover it and the FIFO's lead-in.
    memset(mic, 0, sizeof(mic));
    for (unsigned b = 0; b < YSF_BLOCKS; b++) {
        vocoderEncodeFrame(&mic[160 * b], tx_ambe[b]);
        if (tx_cfg.dt == YSF_DT_VD_MODE2) {
            ysf_voice_dn_from_frame(tx_ambe[b], tx_field[b]);
        }
    }
    send_frame(0); // header
    for (unsigned i = 0; i < TX_PREFILL; i++) {
        while ((tx_frame_no + 2) >= tx_total) { // keep a terminator slot after the communication frames
            tx_cfg.cycles++;
            tx_total = ysf_tx_total_frames(&tx_cfg);
        }
        send_frame(++tx_frame_no);
    }
    bool keyed = afrx_fifo_key(false);

    while (keyed) {
        if (!afrx_ptt_held()) {
            ending = true;
        }
        memset(mic, 0, sizeof(mic));
        for (; halves < 10; halves++) {
            if (!ending && !sound_mic_take(&mic[80 * halves])) {
                vTaskDelay(pdMS_TO_TICKS(2)); // the mic has no buffer yet: don't spin waiting for it
                break;
            }
        }
        if (halves < 10 && !ending) {
            continue; // the next pass adds the rest of this frame's 10 ms buffers
        }
        halves = 0;
        for (unsigned b = 0; b < YSF_BLOCKS; b++) {
            vocoderEncodeFrame(&mic[160 * b], tx_ambe[b]);
            if (tx_cfg.dt == YSF_DT_VD_MODE2) {
                ysf_voice_dn_from_frame(tx_ambe[b], tx_field[b]);
            }
        }
        while ((tx_frame_no + 2) >= tx_total) {
            tx_cfg.cycles++;
            tx_total = ysf_tx_total_frames(&tx_cfg);
        }
        send_frame(++tx_frame_no);
        if (ending) {
            break;
        }
    }
    if (keyed) {
        send_frame(tx_total - 1); // terminator
    }
    ysf_radio_stats.tx_underruns = afrx_fifo_finish(keyed);
}

/* ---- the afrx service ---- */

static const afrx_ops_t ops = {
    .bw = AFREC_BW_FLAT,
    .overlay = OVERLAY_AMBE,
    .play = &play_cfg,
    .rx_init = rx_init,
    .samples = samples,
    .poll = poll,
    .end_call = call_end,
    .transmit = transmit,
};

void ysf_radio_start(void)
{
    if (afrx_running(&ops)) {
        return;
    }
    rx_dgid = codeplug_ysf_dgid(&rs.channel);
    heard = false;
    memset(&call, 0, sizeof(call));
    afrx_start(&ops);
}

void ysf_radio_stop(void)
{
    if (!afrx_running(&ops)) {
        return;
    }
    afrx_stop();
    heard = false;
    memset(&call, 0, sizeof(call));
}

bool ysf_radio_call(ysf_call_t *out)
{
    if (out) {
        *out = call;
    }
    return call.active;
}

bool ysf_radio_heard_within(uint32_t ms)
{
    return heard && ((xTaskGetTickCount() - heard_at) < pdMS_TO_TICKS(ms));
}

void ysf_radio_set_target(uint8_t dgid, const char *dest)
{
    tx_dgid = (dgid <= CODEPLUG_YSF_DGID_MAX) ? dgid : 0;
    call_field(tx_dest, dest ? dest : "ALL");
    tx_dest[YSF_CALLSIGN_LEN] = '\0';
}

void ysf_radio_transmit(bool on)
{
    afrx_ptt(on);
}

bool ysf_radio_transmitting(void)
{
    return afrx_transmitting();
}
