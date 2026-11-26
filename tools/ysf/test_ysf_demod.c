/*
 * Host test: the YSF C4FM demodulator and the radio service's frame alignment.
 *
 * Builds frames with ysf_tx.c, modulates their dibits into 8 kHz samples with the same RRC filter the receiver uses
 * (at 3x the sample rate, where a symbol is exactly 5 samples), runs ysf_demod.c over them in 128-sample halves as
 * afrx.c does, and checks that the sync and every frame's FICH and DCH survive.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsk4.h"
#include "ysf_demod.h"
#include "ysf_frame.h"
#include "ysf_tx.h"

#define FRAMES      8
#define DIBITS      (FRAMES * YSF_FRAME_DIBITS)
#define SAMPLES_24  (DIBITS * 5)
#define SAMPLES_8   (SAMPLES_24 / 3)
#define GAIN        768 // a unit level in Q8, as vocoderDecodeFrame's 9-byte frames expect

static uint8_t got[DIBITS + YSF_FRAME_DIBITS];
static size_t got_n;
static int lost;

static void on_dibit(void *ctx, int dibit)
{
    (void)ctx;
    if (dibit == YSF_DEMOD_LOST) {
        lost++;
        return;
    }
    if (got_n < sizeof(got)) {
        got[got_n++] = (uint8_t)dibit;
    }
}

static void modulate(const uint8_t *dibits, int16_t *out)
{
    static const int8_t level[4] = {1, 3, -1, -3};
    int32_t y24[SAMPLES_24];

    memset(y24, 0, sizeof(y24));
    for (int t = 0; t < SAMPLES_24; t++) {
        double acc = 0;

        // The RRC taps are at 8 kHz; a 24 kHz symbol is every 5 samples, so the pulse is read at u = (t - 5n) / 3.
        for (int n = (t - 3 * (FSK4_RRC_TAPS - 1) + 4) / 5; n <= t / 5; n++) {
            double u = (t - 5.0 * n) / 3.0;
            int j;

            if ((n < 0) || (n >= DIBITS) || (u < 0) || (u >= FSK4_RRC_TAPS - 1)) {
                continue;
            }
            j = (int)u;
            acc += level[dibits[n]] * ((1.0 - (u - j)) * fsk4_rrc_4800[j] + (u - j) * fsk4_rrc_4800[j + 1]);
        }
        y24[t] = (int32_t)acc;
    }
    for (int k = 0; k < SAMPLES_8; k++) {
        int32_t v = (y24[3 * k] * GAIN) / 4096;

        out[k] = (int16_t)((v > 32767) ? 32767 : (v < -32767) ? -32767 : v);
    }
}

static void voice_cb(void *ctx, unsigned frame_no, unsigned block, uint8_t *vch, unsigned len)
{
    (void)ctx;
    memset(vch, (int)(frame_no + block) & 0x7f, len);
}

int main(void)
{
    ysf_tx_config cfg;
    uint8_t sent[FRAMES][YSF_FRAME_BYTES];
    uint8_t dibits[DIBITS];
    int16_t samples[SAMPLES_8];
    ysf_demod dm;
    uint8_t sync_dibits[YSF_SYNC_BYTES * 4];
    unsigned decoded = 0, expect = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.dt = YSF_DT_VD_MODE1;
    cfg.cs = YSF_CS_CSD;
    cfg.cm = YSF_CM_GROUP;
    cfg.dgid = 42;
    cfg.cycles = FRAMES;
    cfg.voice = voice_cb;
    memcpy(cfg.dest, "ALL       ", 10);
    memcpy(cfg.src, "XE1POC    ", 10);
    memcpy(cfg.down, "          ", 10);
    memcpy(cfg.up, "          ", 10);
    memcpy(cfg.rem1, "     ", 5);
    memcpy(cfg.rem2, "     ", 5);
    memcpy(cfg.rem3, "     ", 5);
    memcpy(cfg.rem4, "     ", 5);

    unsigned total = ysf_tx_total_frames(&cfg);

    if (total < FRAMES) {
        fprintf(stderr, "bad frame count %u\n", total);
        return 1;
    }
    for (unsigned f = 0; f < FRAMES; f++) {
        if (!ysf_tx_frame_build(&cfg, f, sent[f])) {
            fprintf(stderr, "frame %u not built\n", f);
            return 1;
        }
        ysf_bytes_to_dibits(sent[f], YSF_FRAME_BYTES, &dibits[f * YSF_FRAME_DIBITS]);
    }
    ysf_bytes_to_dibits(ysf_sync_bytes, YSF_SYNC_BYTES, sync_dibits);
    modulate(dibits, samples);
    ysf_demod_init(&dm, on_dibit, NULL);
    for (int k = 0; k < SAMPLES_8; k += 128) {
        int n = (SAMPLES_8 - k < 128) ? SAMPLES_8 - k : 128;

        ysf_demod_samples(&dm, &samples[k], n);
    }

    for (size_t i = 0; (i + YSF_FRAME_DIBITS) <= got_n; i++) {
        unsigned errors = 0;

        for (unsigned j = 0; j < YSF_SYNC_BYTES * 4; j++) {
            errors += (got[i + j] != sync_dibits[j]);
        }
        if (errors > YSF_SYNC_MAX_ERRORS) {
            continue;
        }
        uint8_t frame[YSF_FRAME_BYTES];

        for (unsigned b = 0; b < YSF_FRAME_BYTES; b++) {
            uint8_t x = 0;

            for (unsigned j = 0; j < 4; j++) {
                x = (uint8_t)((x << 2) | got[i + 4 * b + j]);
            }
            frame[b] = x;
        }
        ysf_fich fi, fj;
        uint8_t d1[20], d2[20], e1[20], e2[20];
        unsigned n1, n2;

        if (!ysf_frame_decode(frame, &fi, d1, d2, &n1)) {
            fprintf(stderr, "frame at dibit %zu did not decode\n", i);
            return 1;
        }
        if (expect >= FRAMES) {
            break;
        }
        if (!ysf_frame_decode(sent[expect], &fj, e1, e2, &n2)) {
            fprintf(stderr, "sent frame %u did not decode\n", expect);
            return 1;
        }
        if (memcmp(&fi, &fj, sizeof(fi)) || (n1 != n2) || memcmp(d1, e1, n1)) {
            fprintf(stderr, "frame at dibit %zu differs from sent frame %u\n", i, expect);
            return 1;
        }
        expect++;
        decoded++;
        i += YSF_FRAME_DIBITS - 1;
    }
    printf("ysf demod: %zu dibits, %u frames decoded, %d losses\n", got_n, decoded, lost);
    if (decoded < FRAMES - 1) {
        fprintf(stderr, "only %u of %d frames decoded\n", decoded, FRAMES);
        return 1;
    }
    return 0;
}
