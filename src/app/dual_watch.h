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

#ifndef DM32_DUAL_WATCH_H
#define DM32_DUAL_WATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "ui.h"

// Dual watch (docs/ui-design.md 5.5): two paths, each a channel, VFO A / B or a zone, watched in turn on the one
// receiver. On (the quick menus), the channel and VFO screens show it over themselves (dual_watch_show()); off: back.
extern const ui_screen_t dual_watch_screen;

// settings.dual_watch_path[]: a channel number (1 ..), -1 VFO A, -2 VFO B, 0 the default, or a zone with its
// channel position packed into the same int16_t (settings_t has a fixed 108 byte layout, no room for more fields).
// Zone 0 .. 127 and position 0 .. 127 fit in -(3 + (zone << 7) + position), min -16386.
#define DW_PATH_VFO_A      (-1)
#define DW_PATH_VFO_B      (-2)
#define DW_PATH_ZONE_MIN   (-3)
#define DW_PATH_ZONE_ENCODE(zone, position) \
    (int16_t)(DW_PATH_ZONE_MIN - (((int)(zone) & 0x7F) << 7) - ((int)(position) & 0x7F))

bool dual_watch_is_on(void);
void dual_watch_set(bool on);
bool dual_watch_show(void); // from the enter of the channel / VFO screen: pushed when on; true: pushed

bool dual_watch_path_zone(int16_t ref, int *zone, int *position); // decode a zone path
int16_t dual_watch_path(int path);                                // 0 defaults to VFO A / VFO B
void dual_watch_path_name(int16_t ref, char *out, size_t size);   // "VFO A" / channel / zone name

// The path picker of the dual watch menu: 0 VFO A, 1 VFO B, 2 + zone (zone_list.c uses the same numbering)
int dual_watch_path_option(int path);
void dual_watch_set_path_option(int path, int option);

#endif
