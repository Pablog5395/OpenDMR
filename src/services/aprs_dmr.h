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

#ifndef DM32_APRS_DMR_H
#define DM32_APRS_DMR_H

#include <stdbool.h>
#include <stdint.h>

#include "dmr_data.h"

/*
 * The LRRP report and the DMR data packet formats here are ported from RadioFoundry
 * (https://github.com/gzalo/RadioFoundry), Copyright (c) The RadioFoundry Authors, used under the MIT License.
 * See the NOTICE file at the root of the project.
 *
 * APRS position over DMR, RadioFoundry style: an unsolicited Motorola LRRP triggered report carried in an
 * unconfirmed DMR IP packet (UDP port 4001). This file only builds the bursts; the HR_C7000 engine sends them.
 */

#define APRS_DMR_MAX_BURSTS       12
#define APRS_DMR_SPEED_UNKNOWN    0xFFFFu
#define APRS_DMR_COURSE_UNKNOWN   0xFFFFu

// the bursts of a position report (dmr_data.h); at most APRS_DMR_MAX_BURSTS (the HR_C7000's way takes 12)
typedef dmr_data_tx_t aprs_dmr_tx_t;

// Build the preamble CSBKs, data header and Rate 1/2 blocks for one position. Coordinates are 1e-7 degrees.
// Speed is knots, course degrees; use the *_UNKNOWN values when they aren't available.
bool aprs_dmr_build_position(int32_t latitude_e7, int32_t longitude_e7, uint16_t speed_knots, uint16_t course,
                             uint32_t destination, uint32_t source, aprs_dmr_tx_t *tx);

#endif
