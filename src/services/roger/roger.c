/*
 * Copyright (C) 2026 Junqiang Shi, VA3BPF / BG8AM
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
 *    following disclaimer.
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
 * OpenDMR, service: the DMR over-the-air roger beep (the fork's). A sine pushed through AMBE's speech encoder sounds
 * like a duck: the codec models a vocal tract, not a pure tone. AMBE has a native tone frame instead
 * (TIA-102.BABA-1, the 1031 Hz test vector in vocoder.c): six ones, a 7-bit log amplitude, and the frequency index
 * repeated four times. The decoder regenerates a clean sine, so every DMR radio hears the same beep. The radio task
 * arms a melody; the engine (codec.c) and the software transmitter (dmr_soft.c) ask for one 9-byte frame at a time
 * instead of calling vocoderEncodeFrame().
 *
 * The air path has its own short patterns, authored here as (Hz, ms) pairs: a rising three-note alert, a single high
 * beep, three single tones and a double 1 kHz beep. No audio is stored. They are only what goes out over DMR; the
 * speaker keeps the stock beeps.
 *
 * Kept in services/roger/ so the fork's additions stay together and upstream merges only touch the few call sites.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "custom_data.h"
#include "sound.h"
#include "ambe.h"
#include "roger.h"

#define ROGER_AIR_AMP  104 // about -16 dBFS; 127 would be full scale
#define ROGER_AIR_FADE 20  // softer first and last frame of a note: no click at the start or end

// Unique "DT" blocks: 2 is the keymap, 3 the FM radio, 4 the old language code, 5 scan, 6 NXDN ID, 7 the theme, 8 the
// language choice, 9 the sound style. Ours use 11 and 12 so none of them can be read back as another feature's.
#define ROGER_AIR_DATA_TYPE     0x4454000B // ("DT", 11): DMR over-the-air chirp mode
#define ROGER_PATTERN_DATA_TYPE 0x4454000C // ("DT", 12): the DMR air melody

// These settings change in the menu but are written only when it is left, or at power off
enum {
    ROGER_DIRTY_AIR     = 1 << 0,
    ROGER_DIRTY_PATTERN = 1 << 1
};

uint8_t roger_air = ROGER_AIR_OFF;
uint8_t roger_pattern = ROGER_PATTERN_HIGH; // default until a pattern is stored
static uint8_t roger_dirty;

static const int16_t AIR_RISING[] = {900, 40, 0, 20, 940, 40, 0, 20, 1060, 60, -1, -1};
static const int16_t AIR_HIGH[]   = {2300, 150, -1, -1};
static const int16_t AIR_850[]    = {850, 180, -1, -1};
static const int16_t AIR_1150[]   = {1150, 180, -1, -1};
static const int16_t AIR_1850[]   = {1850, 180, -1, -1};
static const int16_t AIR_DOUBLE[] = {1000, 110, 0, 30, 1000, 120, -1, -1};
static const int16_t *const air_patterns[ROGER_PATTERNS] = {
    AIR_RISING, AIR_HIGH, AIR_850, AIR_1150, AIR_1850, AIR_DOUBLE,
};

static struct {
    const int16_t *melody;
    int pair;
    int16_t hz;
    int frames;      // 20 ms AMBE frames left in the current note
    int note_frames; // the note's length, for the amplitude fade
} air;
static volatile bool air_busy;
static volatile uint8_t air_pending; // event requested from the timeslot ISR, armed by the frame consumer

// Build the 49 vocoder bits of a tone frame and encode them into the 9 channel bytes
static void tone_frame(uint8_t out9[9], int16_t hz, int amp)
{
    uint8_t d49[49] = {0};
    uint8_t idx;
    int i, c;

    if (hz == 0) {
        idx = 255; // the tone frame's silence
    } else {
        uint32_t n = (((uint32_t)hz * 4U) + 62U) / 125U; // round(hz / 31.25)

        if (n < 5) {
            n = 5;
        } else if (n > 122) {
            n = 122;
        }
        idx = (uint8_t)n;
    }
    for (i = 0; i < 6; i++) {
        d49[i] = 1;
        d49[6 + i] = (amp >> (6 - i)) & 1; // amplitude bits 6 .. 1
    }
    for (c = 0; c < 4; c++) {
        for (i = 0; i < 8; i++) {
            d49[12 + 8 * c + i] = (idx >> (7 - i)) & 1;
        }
    }
    d49[44] = amp & 1; // amplitude bit 0; 45 .. 48 stay 0
    ambe_encode_frame(d49, out9);
}

static bool air_wanted(uint8_t event)
{
    return ((event == ROGER_EVENT_TX_START) &&
            ((roger_air == ROGER_AIR_START) || (roger_air == ROGER_AIR_BOTH))) ||
           ((event == ROGER_EVENT_TX_STOP) &&
            ((roger_air == ROGER_AIR_STOP) || (roger_air == ROGER_AIR_BOTH)));
}

// Called with interrupts disabled by the two arming paths, and by the frame consumer
static void air_next_locked(void)
{
    int16_t hz, ms;

    if (air.melody == NULL) {
        return;
    }
    hz = air.melody[air.pair];
    ms = air.melody[air.pair + 1];
    if ((hz < 0) || (ms < 0)) {
        air.melody = NULL;
        air.hz = 0;
        air.frames = 0;
        return;
    }
    air.hz = hz;
    // Two frames (40 ms) is the minimum a native tone needs to be heard and open the other radio's squelch.
    ms = (ms < 40) ? 40 : ms;
    air.frames = (ms + 19) / 20;
    air.note_frames = air.frames;
    air.pair += 2;
}

// The whole state change, including the first note, is one atomic step. Stop the consumer first (air_busy = false)
// so no frame can be stepped twice when Both is selected and the PTT is released mid-melody.
static void air_arm_locked(void)
{
    air_busy = false;
    air.melody = air_patterns[(roger_pattern < ROGER_PATTERNS) ? roger_pattern : ROGER_PATTERN_HIGH];
    air.pair = 0;
    air.hz = 0;
    air.frames = 0;
    air.note_frames = 0;
    air_next_locked();
    air_busy = true;
}

void roger_load(void)
{
    uint8_t stored;

    if (custom_data_read(ROGER_AIR_DATA_TYPE, &stored, sizeof(stored)) && (stored < ROGER_AIR_MODES)) {
        roger_air = stored;
    }
    if (custom_data_read(ROGER_PATTERN_DATA_TYPE, &stored, sizeof(stored)) && (stored < ROGER_PATTERNS)) {
        roger_pattern = stored;
    }
}

void roger_save(void)
{
    if ((roger_dirty & ROGER_DIRTY_AIR) && custom_data_write(ROGER_AIR_DATA_TYPE, &roger_air, sizeof(roger_air))) {
        roger_dirty &= (uint8_t)~ROGER_DIRTY_AIR;
    }
    if ((roger_dirty & ROGER_DIRTY_PATTERN) &&
        custom_data_write(ROGER_PATTERN_DATA_TYPE, &roger_pattern, sizeof(roger_pattern))) {
        roger_dirty &= (uint8_t)~ROGER_DIRTY_PATTERN;
    }
}

void roger_air_set(uint8_t mode)
{
    if ((mode >= ROGER_AIR_MODES) || (mode == roger_air)) {
        return;
    }
    roger_air = mode;
    roger_dirty |= ROGER_DIRTY_AIR;
}

void roger_pattern_set(uint8_t pattern)
{
    if ((pattern >= ROGER_PATTERNS) || (pattern == roger_pattern)) {
        return;
    }
    roger_pattern = pattern;
    roger_dirty |= ROGER_DIRTY_PATTERN;
}

void roger_preview(void)
{
    sound_play(air_patterns[(roger_pattern < ROGER_PATTERNS) ? roger_pattern : ROGER_PATTERN_HIGH]);
}

// From the timeslot ISR: no critical section, no state to touch. The frame consumer arms it in task context.
void roger_request(uint8_t event)
{
    if (air_wanted(event)) {
        air_pending = event;
    }
}

bool roger_begin(uint8_t event)
{
    if (!air_wanted(event)) {
        return false;
    }
    taskENTER_CRITICAL();
    air_pending = 0;
    air_arm_locked();
    taskEXIT_CRITICAL();
    return true;
}

bool roger_active(void)
{
    return air_busy;
}

bool roger_tone(uint8_t out9[9])
{
    int16_t hz;
    int amp;

    taskENTER_CRITICAL();
    if (air_pending != 0) {
        uint8_t pending = (uint8_t)air_pending;

        air_pending = 0;
        if (air_wanted(pending)) {
            air_arm_locked();
        }
    }
    if (!air_busy) {
        taskEXIT_CRITICAL();
        return false;
    }
    if (air.frames <= 0) {
        air_next_locked();
        if (air.melody == NULL) {
            air_busy = false;
            taskEXIT_CRITICAL();
            return false;
        }
    }
    hz = air.hz;
    amp = ROGER_AIR_AMP;
    if ((air.frames == air.note_frames) || ((air.frames == 1) && (air.note_frames > 2))) {
        amp -= ROGER_AIR_FADE;
    }
    air.frames--;
    taskEXIT_CRITICAL();

    tone_frame(out9, hz, amp);
    return true;
}

void roger_cancel(void)
{
    taskENTER_CRITICAL();
    air_pending = 0;
    air.melody = NULL;
    air.frames = 0;
    air.note_frames = 0;
    air_busy = false;
    taskEXIT_CRITICAL();
}
