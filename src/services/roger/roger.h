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

#ifndef DM32_ROGER_H
#define DM32_ROGER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * DMR over-the-air roger beep (the fork's): native AMBE tone frames instead of a sine pushed through the speech
 * encoder, so any DMR radio hears a clean melody. The generator is armed by the radio task and pulled one 9-byte frame
 * at a time by the engine (dmr/codec.c) and the software transmitter (services/dmr_soft.c).
 *
 * This module has no dependency on the rest of the fork except the vocoder's frame encoder and sound_play() for the
 * menu preview; it is kept in its own folder so upstream updates leave it alone.
 */

// Off / Start / Stop / Both
enum {
    ROGER_AIR_OFF,
    ROGER_AIR_START,
    ROGER_AIR_STOP,
    ROGER_AIR_BOTH,
    ROGER_AIR_MODES
};

// The air melodies: (Hz, ms) pairs chosen for this feature
enum {
    ROGER_PATTERN_RISING,
    ROGER_PATTERN_HIGH,
    ROGER_PATTERN_850,
    ROGER_PATTERN_1150,
    ROGER_PATTERN_1850,
    ROGER_PATTERN_DOUBLE,
    ROGER_PATTERNS
};

// TX events (the engine's start / stop)
enum {
    ROGER_EVENT_TX_START,
    ROGER_EVENT_TX_STOP
};

extern uint8_t roger_air;     // ROGER_AIR_x
extern uint8_t roger_pattern; // ROGER_PATTERN_x

void roger_load(void);                  // read the stored settings at boot
void roger_save(void);                  // write the changed settings (menu leave / power off)
void roger_air_set(uint8_t mode);       // change Off / Start / Stop / Both
void roger_pattern_set(uint8_t pattern); // change the melody
void roger_preview(void);               // hear the current melody on the speaker, without saving it

void roger_request(uint8_t event);      // from the timeslot ISR: only marks the event
bool roger_begin(uint8_t event);        // arm the melody for that TX event; false: not enabled
bool roger_active(void);                // frames still to send
bool roger_tone(uint8_t out9[9]);       // next 9-byte AMBE tone frame; false: the beep is over
void roger_cancel(void);                // aborted transmission: drop whatever was left

#endif
