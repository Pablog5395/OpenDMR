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
 * OpenDMR, service: the software receivers' common running (afrx.h).
 *
 * The task looks at the recording every POLL_MS: each half the modem finished goes to the receiver; a half more than
 * 48 ms after the last means halves went by unread (counted). The PTT held (and the receiver's tx_ready()): the
 * receiver's call ended, the playback and the recording off, its transmit() (which returns once the PTT is released
 * and its transmission is out, or, DMR in repeater mode, once the repeater's wake-up is sent), then the recording,
 * the receiver's state and the playback again.
 *
 * The samples' time (afrx_sample_us()): each half's end as seen, less its samples' time (125 us each), the earliest of
 * the last CLOCK_HALVES. Polling alone sees a half's end late by a fixed amount: the 32 ms halves and the 1 ms ticks keep
 * their phase (the modem's clock and the CPU's ~15 ppm apart: it moves 4 ms in ~4.5 minutes), so the least of many
 * waits is no better than one (repeater mode's bursts were off by up to 3 ms, 2026-11-24). With precise_clock the task
 * polls every 1 ms and, a half's end due within the poll (32 ms after the last), watches for it (~0.6 ms of spinning a
 * half on average): its time to some us from the second half on.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "modem.h"
#include "systime.h"
#include "vocoder.h"
#include "p25_phy.h"
#include "dmr_compat.h"
#include "afrx.h"

#define POLL_MS     4    // a half is 32 ms
#define CLOCK_HALVES 32  // the samples' clock from the last this many halves (~1 s)
#define STACK_WORDS 2304 // 9 KB: frame decoding and our AMBE decoder (NXDN, DMR); the AMBE encoder ~6.5 KB (NXDN, DMR
                         // transmit); Codec 2 decoding ~3.5 KB, encoding ~2 KB (M17). 7 KB overflowed with DMR's
                         // transmit (2026-11-20: the flags below the stack overwritten, the receiver stopped);
                         // afrx_debug[4]: the least of it left unused, measured

static StaticTask_t task_tcb;
static StackType_t task_stack[STACK_WORDS];
static TaskHandle_t task;
static const afrx_ops_t *volatile current;
static volatile bool running;
static volatile bool busy; // the task is using the receiver (its overlay)
static volatile bool tx_want, tx_busy;
static uint32_t clk_samples;           // samples given to the receiver since receive_again()
static uint32_t clk_off[CLOCK_HALVES]; // each half's end as seen, less its samples' time (125 us each)
static uint32_t clk_n;                 // halves in clk_off since receive_again() or a half lost
static uint32_t next_half_us;          // precise_clock: the next half's end due (the last's + 32 ms)
static int16_t half[256]; // off the stack
// over the cable: [0] running | busy << 1 | tx_want << 2 | tx_busy << 3 | a receiver set << 4; [1] the recording not
// started again after a transmission; [2] transmissions; [3] stops; [4] the stack never used (bytes, the least seen)
volatile uint32_t afrx_debug[5] = {0, 0, 0, 0, UINT32_MAX};

// the stack's bytes still as FreeRTOS filled them (0xA5) from its bottom
static void stack_check(void)
{
    const uint8_t *p = (const uint8_t *)task_stack;
    uint32_t n = 0;
    while ((n < sizeof(task_stack)) && (p[n] == 0xA5)) {
        n++;
    }
    if (n < afrx_debug[4]) {
        afrx_debug[4] = n;
    }
}

static void debug_state(void)
{
    afrx_debug[0] = (uint32_t)running | ((uint32_t)busy << 1) | ((uint32_t)tx_want << 2) | ((uint32_t)tx_busy << 3) |
                    ((uint32_t)(current != NULL) << 4);
}

static void receive_again(const afrx_ops_t *o)
{
    clk_samples = 0;
    clk_n = 0;
    o->rx_init();
    if (o->play) {
        voice_play_start(o->play);
    }
}

static volatile bool data_want;

// the PTT's transmission (send: o->transmit) or a data transmission (o->data): the recording and the playback off
static void transmit(const afrx_ops_t *o, void (*send)(void))
{
    tx_busy = true;
    if (o->end_call) {
        o->end_call();
    }
    voice_play_stop();
    afrec_stop();
    afrx_debug[2]++;
    debug_state();
    send();
    stack_check();
    if (!afrec_start(o->bw)) {
        afrx_debug[1]++;
    }
    receive_again(o);
    tx_busy = false;
}

static void task_main(void *arg)
{
    TickType_t last_half = 0;

    (void)arg;
    for (;;) {
        const afrx_ops_t *p = current;
        vTaskDelay(pdMS_TO_TICKS((p && p->precise_clock) ? 1 : POLL_MS));
        debug_state();
        busy = true;
        const afrx_ops_t *o = current;
        if (!running || !o) {
            last_half = 0;
            busy = false;
            continue;
        }
        if (o->precise_clock && clk_n) { // a half's end due before the next poll: watched for
            int32_t until = (int32_t)(next_half_us - systime_get_us());
            if ((until > -2000) && (until <= 1100)) {
                uint32_t end = next_half_us + 2000;
                while (!afrec_half_switched() && ((int32_t)(end - systime_get_us()) > 0)) {
                }
            }
        }
        if (tx_want && o->transmit && (!o->tx_ready || o->tx_ready())) {
            transmit(o, o->transmit);
            last_half = 0;
        }
        if (data_want && o->data && (!o->tx_ready || o->tx_ready())) {
            data_want = false;
            transmit(o, o->data);
            last_half = 0;
        }
        int n;
        bool half_end;
        while (running && ((n = afrec_read(half, &half_end)) > 0)) {
            uint32_t t0 = systime_get_us();
            TickType_t now = xTaskGetTickCount();
            const afrx_stats_t *s = &o->stats;
            if (half_end) {
                if (last_half && ((now - last_half) > pdMS_TO_TICKS(48))) {
                    clk_n = 0; // the samples no longer the time since the start
                    if (s->missed) {
                        *s->missed += (now - last_half) / pdMS_TO_TICKS(32) - 1;
                    }
                }
                if (s->halves) {
                    (*s->halves)++;
                }
                last_half = now;
            }
            clk_samples += (uint32_t)n;
            if (half_end) {
                clk_off[clk_n % CLOCK_HALVES] = afrec_half_us - clk_samples * 125u;
                clk_n++;
                next_half_us = afrec_half_us + 32000u;
            }
            o->samples(half, n);
            uint32_t took = systime_get_us() - t0;
            if (s->busy_us_sum) {
                *s->busy_us_sum += took;
            }
            if (s->busy_us_max && (took > *s->busy_us_max)) {
                *s->busy_us_max = took;
            }
        }
        if (running && o->poll) {
            o->poll();
        }
        stack_check();
        busy = false;
    }
}

bool afrx_start(const afrx_ops_t *ops)
{
    if (running) {
        return current == ops;
    }
    if (!task) {
        task = xTaskCreateStatic(task_main, "afrx", STACK_WORDS, NULL, tskIDLE_PRIORITY + 3, task_stack, &task_tcb);
    }
    // the modem to FM recording first: the DMR engine stands aside from then on, then the state into the overlay
    if (!afrec_start(ops->bw)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    overlay_claim(ops->overlay);
    tx_want = false;
    current = ops;
    receive_again(ops);
    running = true;
    return true;
}

void afrx_stop(void)
{
    const afrx_ops_t *o = current;
    if (!running || !o) {
        return;
    }
    running = false;
    afrx_debug[3]++;
    while (busy) { // the task done with the overlay
        vTaskDelay(1);
    }
    voice_play_stop();
    afrec_stop();
    if (o->end_call) {
        o->end_call();
    }
    overlay_release(o->overlay);
    current = NULL;
}

volatile uint32_t afrx_clock[2]; // the last afrx_sample_us(): its halves, their offsets' spread (us)

bool afrx_sample_us(uint32_t at, uint32_t *us)
{
    uint32_t n = (clk_n < CLOCK_HALVES) ? clk_n : CLOCK_HALVES;
    if (n < 3) {
        return false;
    }
    uint32_t best = clk_off[0], worst = clk_off[0];
    for (uint32_t i = 1; i < n; i++) {
        if ((int32_t)(clk_off[i] - best) < 0) {
            best = clk_off[i];
        }
        if ((int32_t)(clk_off[i] - worst) > 0) {
            worst = clk_off[i];
        }
    }
    afrx_clock[0] = clk_n;
    afrx_clock[1] = worst - best;
    // sample i ends at (i + 1) * 125 us: the last of a half ends when the modem switches halves
    *us = best + (uint32_t)(((uint64_t)at * 125u + 125u * 16u) >> 4);
    return true;
}

bool afrx_running(const afrx_ops_t *ops)
{
    return running && (current == ops);
}

void afrx_ptt(bool on)
{
    tx_want = on;
}

void afrx_data(void)
{
    data_want = true;
}

bool afrx_ptt_held(void)
{
    return tx_want && running;
}

bool afrx_transmitting(void)
{
    return tx_want || tx_busy;
}

void afrx_fifo_prepare(uint32_t mod_scale)
{
    sound_mic_start();
    p25_phy_tx_reset();
    modem_set_mod_scale(mod_scale);
}

bool afrx_fifo_key(bool low_power)
{
    return p25_phy_tx_begin(low_power) == 0;
}

bool afrx_fifo_key_at(bool low_power, uint32_t at)
{
    return p25_phy_tx_begin_at(low_power, at) == 0;
}

uint32_t afrx_fifo_finish(bool keyed)
{
    uint32_t underruns = 0;
    if (keyed) {
        p25_phy_tx_pad(); // the last block whole: sent
        while ((p25_phy_tx_level() >= P25_PHY_BLOCK_BYTES * 4) && (p25_phy_tx_underrun < 10)) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelay(pdMS_TO_TICKS(60)); // the last blocks out of the modem
        p25_phy_tx_end();
        underruns = p25_phy_tx_underrun;
    }
    modem_set_mod_scale(MODEM_MOD_SCALE_ONE);
    soundStopMicCapture();
    return underruns;
}
