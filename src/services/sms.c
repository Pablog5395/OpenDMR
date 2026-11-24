/*
 * Copyright (C) 2026 Junqiang Shi, VA3BPF / BG8AM
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 *    disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 *    following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden without the written
 *    permission of the copyright holder. This includes, without limitation, incorporation in a commercial product or
 *    incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * OpenDMR, service: text messages (sms.h).
 *
 * The boxes: SMS_BOX records each in the flash (SMS_FLASH_BASE: the CPS's to read, delete, write the presets). A
 * message goes into a free record, else over the oldest; the index (the records in order, newest first) is kept in
 * RAM from their seq. A received message comes from a receiver's task (sms_dmr_packet(), sms_m17_text(): decoded
 * there, queued) and is stored by the input task (sms_tick()), which writes the flash (an erase takes tens of ms: not
 * in a receiver's task). A TMS message asking for it is acknowledged (dmr_sms.h: Brandmeister's Text Capture resends
 * until then); the same message again within REPEAT_MS is dropped (a resend whose acknowledgement was lost).
 *
 * Sending: on the channel's mode (DMR in the options' format, from our ID; M17 from the callsign), a record in the
 * outbox SMS_SENDING, then SMS_SENT once on the air, SMS_DELIVERED when the acknowledgement asked for came back
 * (within ACK_WAIT_MS), SMS_FAILED when it couldn't be sent (no repeater).
 */

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "flash.h"
#include "datetime.h"
#include "codeplug.h"
#include "radio.h"
#include "dmr_compat.h"
#include "dmr.h"
#include "dmr_sms.h"
#include "extra_options.h"
#include "m17_radio.h"
#include "sms.h"
#include "ui.h"
#include "sound.h"
#include "dmr_ids.h"

#define RX_QUEUE    2
#define ACK_WAIT_MS 10000 // a TMS acknowledgement waited for this long after the message is out
#define ACK_SEND_MS 5000  // our acknowledgement tried this long (the channel busy sending)
#define ACK_DELAY_MS 1000 // ... sent this much after the message: a hotspot (MMDVM_HS duplex) lost or garbled the
                          // blocks of one sent right after its last data (0 of 3 through; 0.7 s and 1.5 s: all)
#define REPEAT_MS   120000
#define ECHO_MS     30000 // our own message heard back (a repeater repeating it) this long after sending it

_Static_assert(sizeof(sms_t) == SMS_RECORD, "a record is SMS_RECORD octets");
_Static_assert(SMS_OUTBOX_AT + (SMS_BOX * SMS_RECORD) <= SMS_FLASH_BASE + SMS_FLASH_SIZE, "the boxes fit");

static StaticSemaphore_t mutex_buffer;
static SemaphoreHandle_t mutex;

// the records of each box in order, newest first
static struct {
    uint8_t slot[SMS_BOX];
    uint8_t unread[SMS_BOX]; // the inbox: per slot
    int count;
} boxes[2];
static uint32_t next_seq = 1;

static void lock(void)
{
    if (mutex) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (mutex) {
        xSemaphoreGive(mutex);
    }
}

static uint32_t address(int box, int slot)
{
    return ((box == SMS_INBOX) ? SMS_INBOX_AT : SMS_OUTBOX_AT) + (uint32_t)slot * SMS_RECORD;
}

static void index_box(int box)
{
    uint32_t seqs[SMS_BOX];
    boxes[box].count = 0;
    for (int slot = 0; slot < SMS_BOX; slot++) {
        uint8_t h[8];
        if (!flash_read(address(box, slot), h, sizeof(h)) || (h[0] != SMS_USED)) {
            continue;
        }
        uint32_t seq;
        memcpy(&seq, &h[4], 4);
        if (seq >= next_seq) {
            next_seq = seq + 1;
        }
        int i = boxes[box].count++;
        while ((i > 0) && (seqs[i - 1] < seq)) { // newest first
            seqs[i] = seqs[i - 1];
            boxes[box].slot[i] = boxes[box].slot[i - 1];
            i--;
        }
        seqs[i] = seq;
        boxes[box].slot[i] = (uint8_t)slot;
        boxes[box].unread[slot] = (box == SMS_INBOX) && (h[1] == SMS_UNREAD);
    }
}

void sms_init(void)
{
    if (!mutex) {
        mutex = xSemaphoreCreateMutexStatic(&mutex_buffer);
    }
    lock();
    index_box(SMS_INBOX);
    index_box(SMS_OUTBOX);
    unlock();
}

int sms_count(int box)
{
    return boxes[box & 1].count;
}

static int slot_of(int box, int index)
{
    return ((index >= 0) && (index < boxes[box].count)) ? boxes[box].slot[index] : -1;
}

bool sms_get(int box, int index, sms_t *out)
{
    lock();
    int slot = slot_of(box & 1, index);
    bool ok = (slot >= 0) && flash_read(address(box & 1, slot), (uint8_t *)out, sizeof(*out));
    unlock();
    if (ok) {
        out->text[SMS_TEXT_MAX] = 0;
        out->peer[sizeof(out->peer) - 1] = 0;
    }
    return ok;
}

bool sms_delete(int box, int index)
{
    box &= 1;
    lock();
    int slot = slot_of(box, index);
    uint8_t zero = 0;
    bool ok = (slot >= 0) && flash_write(address(box, slot), &zero, 1);
    if (ok) {
        boxes[box].unread[slot] = 0;
        memmove(&boxes[box].slot[index], &boxes[box].slot[index + 1], (size_t)(boxes[box].count - index - 1));
        boxes[box].count--;
    }
    unlock();
    return ok;
}

void sms_mark_read(int index)
{
    lock();
    int slot = slot_of(SMS_INBOX, index);
    if ((slot >= 0) && boxes[SMS_INBOX].unread[slot]) {
        uint8_t zero = 0;
        (void)flash_write(address(SMS_INBOX, slot) + 1, &zero, 1);
        boxes[SMS_INBOX].unread[slot] = 0;
    }
    unlock();
}

int sms_unread(void)
{
    int n = 0;
    for (int i = 0; i < boxes[SMS_INBOX].count; i++) {
        n += boxes[SMS_INBOX].unread[boxes[SMS_INBOX].slot[i]];
    }
    return n;
}

// m into box: a free record, else the oldest; its seq set; the slot (-1: not written)
static int store(int box, sms_t *m)
{
    lock();
    int slot = -1;
    bool taken[SMS_BOX] = {false};
    for (int i = 0; i < boxes[box].count; i++) {
        taken[boxes[box].slot[i]] = true;
    }
    for (int s = 0; (s < SMS_BOX) && (slot < 0); s++) {
        if (!taken[s]) {
            slot = s;
        }
    }
    if (slot < 0) { // full: over the oldest
        slot = boxes[box].slot[--boxes[box].count];
    }
    m->used = SMS_USED;
    m->seq = next_seq++;
    if (flash_write(address(box, slot), (const uint8_t *)m, sizeof(*m))) {
        memmove(&boxes[box].slot[1], &boxes[box].slot[0], (size_t)boxes[box].count);
        boxes[box].slot[0] = (uint8_t)slot;
        boxes[box].count++;
        boxes[box].unread[slot] = (box == SMS_INBOX) && (m->unread == SMS_UNREAD);
    } else {
        slot = -1;
    }
    unlock();
    return slot;
}

static void set_status(int slot, uint8_t status)
{
    lock();
    (void)flash_write(address(SMS_OUTBOX, slot) + 3, &status, 1);
    unlock();
}

/* ---- the presets ---- */

int sms_presets(void)
{
    uint32_t magic = 0;
    if (!flash_read(SMS_FLASH_BASE, (uint8_t *)&magic, 4) || (magic != SMS_MAGIC)) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < SMS_PRESETS; i++) {
        uint8_t c;
        if (flash_read(SMS_PRESETS_AT + (uint32_t)i * SMS_PRESET_SIZE, &c, 1) && (c != 0xFF) && (c != 0)) {
            n = i + 1;
        }
    }
    return n;
}

bool sms_preset(int index, char *out, int size)
{
    if ((index < 0) || (index >= SMS_PRESETS) || (size < 1)) {
        return false;
    }
    char p[SMS_PRESET_SIZE];
    if (!flash_read(SMS_PRESETS_AT + (uint32_t)index * SMS_PRESET_SIZE, (uint8_t *)p, sizeof(p)) ||
        ((uint8_t)p[0] == 0xFF)) {
        return false;
    }
    p[SMS_PRESET_SIZE - 1] = 0;
    for (int i = 0; i < SMS_PRESET_SIZE; i++) {
        if ((uint8_t)p[i] == 0xFF) {
            p[i] = 0;
        }
    }
    strncpy(out, p, (size_t)size - 1);
    out[size - 1] = 0;
    return out[0] != 0;
}

/* ---- receiving ---- */

typedef struct {
    uint8_t mode;
    bool group;
    uint32_t src, dst;
    char peer[24];
    dmr_sms_rx_t r;
} rx_t;

static rx_t rx_queue[RX_QUEUE];
static volatile uint32_t rx_in, rx_out;
volatile uint32_t sms_stats[6]; // received, dropped (queue full), repeats, acks sent, acks received, sent

void sms_dmr_packet(int ts, const dmr_data_rx_t *p)
{
    (void)ts;
    if ((rx_in - rx_out) >= RX_QUEUE) {
        sms_stats[1]++;
        return;
    }
    // a private message to us, or one to a talk group the receive filters take
    if (p->group ? !dmr_tg_filter(p->dst, p->src) : (p->dst != trxDMRID)) {
        return;
    }
    rx_t *q = &rx_queue[rx_in % RX_QUEUE];
    if (!dmr_sms_decode(p, &q->r)) {
        return;
    }
    q->mode = SMS_MODE_DMR;
    q->group = p->group;
    q->src = p->src;
    q->dst = p->dst;
    q->peer[0] = 0;
    rx_in++;
}

void sms_m17_text(const char *src, const char *dst, const char *text)
{
    if ((rx_in - rx_out) >= RX_QUEUE) {
        sms_stats[1]++;
        return;
    }
    rx_t *q = &rx_queue[rx_in % RX_QUEUE];
    memset(q, 0, sizeof(*q));
    q->mode = SMS_MODE_M17;
    q->group = !strcmp(dst, "ALL");
    strncpy(q->peer, src, sizeof(q->peer) - 1);
    strncpy(q->r.text, text, sizeof(q->r.text) - 1);
    rx_in++;
}

/* ---- sending ---- */

static struct {
    bool active;      // a message being sent
    uint8_t mode;     // on SMS_MODE_xxx
    bool on_air;      // its data transmission done
    bool want_ack;
    int slot;
    uint32_t dst;
    uint8_t tms_seq;
    TickType_t since;
} out;
static uint32_t hash(const char *s);
static uint32_t sent_hash;   // the last message we sent: its text's hash, when
static TickType_t sent_time;
static volatile int sending_state = SMS_SENT;
static uint8_t tms_seq_next;
static dmr_data_tx_t tx; // the bursts being sent (off the callers' stacks)

static struct {
    bool pending;
    uint32_t dst;
    uint8_t seq;
    TickType_t since;
} ack;

uint8_t sms_dmr_format(void)
{
    return extra_options_sms_format();
}

void sms_set_dmr_format(uint8_t format)
{
    extra_options_set_sms_format(format);
}

int sms_sending_state(void)
{
    return sending_state;
}

bool sms_send(uint32_t dst, bool group, const char *m17_dst, const char *text)
{
    (void)m17_dst;
    codeplug_channel_t channel;
    int units = dmr_sms_units(text);
    if (out.active || (units == 0) || (units > SMS_CHARS) || !radio_get_channel(&channel)) {
        return false;
    }
    static sms_t m;
    memset(&m, 0, sizeof(m));
    m.unread = 0;
    m.time = datetime_now();
    strncpy(m.text, text, SMS_TEXT_MAX);
    if (channel.mode == CODEPLUG_MODE_DMR) {
        uint8_t format = extra_options_sms_format();
        uint8_t seq = tms_seq_next;
        bool want_ack = (format == DMR_SMS_MOTOROLA) && !group;
        if ((dst == 0) || (dst > 0xFFFFFF) || !dmr_sms_build(&tx, format, trxDMRID, dst, group, text, seq, want_ack) ||
            !dmr_data_send(&tx)) {
            return false;
        }
        tms_seq_next = (uint8_t)((tms_seq_next + 1) & 0x7F);
        m.mode = SMS_MODE_DMR;
        m.src = trxDMRID;
        m.dst = dst;
        m.group = group;
        m.format = format;
        m.tms_seq = seq;
        out.want_ack = want_ack;
        out.tms_seq = seq;
        out.dst = dst;
    } else if (channel.mode == CODEPLUG_MODE_M17) {
        const char *to = m17_dst ? m17_dst : "";
        if (!m17_radio_send_sms(to, text)) {
            return false;
        }
        m.mode = SMS_MODE_M17;
        m.group = (to[0] == 0);
        strncpy(m.peer, to, sizeof(m.peer) - 1);
        out.want_ack = false;
    } else {
        return false;
    }
    out.mode = m.mode;
    m.status = SMS_SENDING;
    sending_state = SMS_SENDING;
    out.slot = store(SMS_OUTBOX, &m);
    out.on_air = false;
    out.since = xTaskGetTickCount();
    out.active = true;
    sent_hash = hash(text);
    sent_time = out.since;
    sms_stats[5]++;
    return true;
}

static void finish(uint8_t status)
{
    static const text_id_t shown[] = {TEXT_SMS_SENDING, TEXT_SMS_SENT, TEXT_SMS_DELIVERED, TEXT_SMS_FAILED};
    out.active = false;
    sending_state = status;
    if (out.slot >= 0) {
        set_status(out.slot, status);
    }
    ui_notify(TEXT_SMS, shown[status & 3], (status == SMS_FAILED) ? UI_NOTICE_ERROR : UI_NOTICE_OK, 3000);
    ui_event_t ev = {.type = UI_EVENT_DATA, .what = UI_DATA_SMS};
    ui_post(&ev);
}

/* ---- the input task's ---- */

static uint32_t last_src, last_hash;
static TickType_t last_time;

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h;
}

sms_t sms_last_received;

static void received(rx_t *q)
{
    TickType_t now = xTaskGetTickCount();
    if (q->r.ack) { // an acknowledgement of ours
        sms_stats[4]++;
        if (out.active && out.want_ack && (q->r.seq == out.tms_seq) && (q->src == out.dst)) {
            finish(SMS_DELIVERED);
        }
        return;
    }
    if ((q->src == trxDMRID) && (hash(q->r.text) == sent_hash) && ((now - sent_time) < pdMS_TO_TICKS(ECHO_MS))) {
        sms_stats[2]++; // ours, repeated by the repeater: not stored, not acknowledged
        return;
    }
    if (q->r.ack_requested && !q->group) { // acknowledged even when a repeat
        ack.pending = true;
        ack.dst = q->src;
        ack.seq = q->r.seq;
        ack.since = now;
    }
    uint32_t h = hash(q->r.text) ^ q->src;
    if ((h == last_hash) && (q->src == last_src) && ((now - last_time) < pdMS_TO_TICKS(REPEAT_MS))) {
        sms_stats[2]++;
        return;
    }
    last_hash = h;
    last_src = q->src;
    last_time = now;
    sms_t *m = &sms_last_received; // kept for tests over the cable
    memset(m, 0, sizeof(*m));
    m->unread = SMS_UNREAD;
    m->mode = q->mode;
    m->time = datetime_now();
    m->src = q->src;
    m->dst = q->dst;
    m->group = q->group;
    m->format = (uint8_t)q->r.format;
    m->tms_seq = q->r.seq;
    strncpy(m->peer, q->peer, sizeof(m->peer) - 1);
    strncpy(m->text, q->r.text, SMS_TEXT_MAX);
    if (store(SMS_INBOX, m) >= 0) {
        sms_stats[0]++;
        char who[UI_NOTICE_TEXT_MAX + 1];
        if (m->mode == SMS_MODE_M17) {
            strncpy(who, m->peer, sizeof(who) - 1);
            who[sizeof(who) - 1] = 0;
        } else {
            dmr_ids_caller_text(m->src, who, sizeof(who));
            if (!who[0]) {
                snprintf(who, sizeof(who), "%lu", (unsigned long)m->src);
            }
        }
        ui_notify_text(TEXT_SMS_NEW, who, UI_NOTICE_MAIL, 8000);
        sound_play(SOUND_SMS);
        ui_event_t ev = {.type = UI_EVENT_DATA, .what = UI_DATA_SMS};
        ui_post(&ev);
    }
}

void sms_tick(void)
{
    while (rx_out != rx_in) {
        received(&rx_queue[rx_out % RX_QUEUE]);
        rx_out++;
    }
    TickType_t now = xTaskGetTickCount();
    if (ack.pending && !dmr_data_busy() && ((now - ack.since) >= pdMS_TO_TICKS(ACK_DELAY_MS))) {
        if (dmr_sms_build_ack(&tx, trxDMRID, ack.dst, ack.seq) && dmr_data_send(&tx)) {
            sms_stats[3]++;
            ack.pending = false;
        }
    }
    if (ack.pending && ((now - ack.since) > pdMS_TO_TICKS(ACK_DELAY_MS + ACK_SEND_MS))) {
        ack.pending = false;
    }
    bool busy = (out.mode == SMS_MODE_M17) ? m17_radio_sms_busy() : dmr_data_busy();
    bool failed = (out.mode == SMS_MODE_M17) ? m17_radio_sms_failed() : dmr_data_failed();
    if (out.active && !out.on_air && !busy) {
        if (failed) {
            finish(SMS_FAILED);
        } else if (!out.want_ack) {
            finish(SMS_SENT);
        } else {
            out.on_air = true; // waiting for the acknowledgement
            out.since = now;
        }
    }
    if (out.active && out.on_air && ((now - out.since) > pdMS_TO_TICKS(ACK_WAIT_MS))) {
        finish(SMS_SENT);
    }
}
