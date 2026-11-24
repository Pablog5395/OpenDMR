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
 * The LRRP report and the DMR data packet formats of this file are ported from RadioFoundry
 * (https://github.com/gzalo/RadioFoundry), Copyright (c) The RadioFoundry Authors, and are used under the MIT
 * License. The full notice is in the NOTICE file at the root of the project. Keep it with the ported code.
 */

/*
 * OpenDMR, service: APRS over DMR (aprs_dmr.h). The RadioFoundry LRRP report is an MBXML document inside an IPv4/UDP
 * packet (port 4001) in an unconfirmed DMR data transmission (dmr_data.c): two preamble CSBKs, a data header, then
 * Rate 1/2 blocks with the CRC-32; their 10 / 12 octets (the CRC-16, the FEC and the sync added when sent).
 */

#include <string.h>
#include "aprs_dmr.h"

#define LRRP_PORT             4001u
#define DOC_TRIGGERED_REPORT  0x0Du
#define TOKEN_REQUEST_ID      0x22u
#define TOKEN_POSITION_RADIUS 0x51u
#define TOKEN_SPEED_HOR       0x6Cu
#define TOKEN_DIRECTION       0x56u

#define LRRP_MAX_DOCUMENT     64

static const uint8_t REPORT_ID[4] = {0, 0, 0, 1};

static int put_uintvar(uint8_t *out, uint32_t v)
{
    uint8_t tmp[5];
    int n = 0;

    do {
        tmp[n++] = (uint8_t)(v & 0x7Fu);
        v >>= 7;
    } while (v != 0);
    for (int i = 0; i < n; i++) {
        out[i] = (uint8_t)(tmp[n - 1 - i] | ((i < (n - 1)) ? 0x80u : 0u));
    }
    return n;
}

// 1e-7 degrees -> degrees and 1/10000 minute, as RadioFoundry's lrrpPositionFromE7()
static void degrees_minutes(int32_t e7, uint8_t *degrees, uint32_t *minutes)
{
    uint32_t value = (e7 < 0) ? (uint32_t)(-(int64_t)e7) : (uint32_t)e7;

    *degrees = (uint8_t)(value / 10000000u);
    *minutes = (((value % 10000000u) * 60u) + 500u) / 1000u;
    if (*minutes >= 600000u) {
        // Rounded up to a whole degree: the minutes go back to 0, not 59.9999 with the degree added.
        *minutes = 0;
        if (*degrees < 255u) {
            (*degrees)++;
        }
    }
}

// The position token's raw 32 bit coordinates (2^31 per 90 degrees latitude, 2^32 per 360 longitude)
static uint32_t raw_coordinate(int32_t e7, bool latitude)
{
    uint8_t degrees;
    uint32_t minutes;
    uint64_t n;
    uint64_t magnitude;
    uint32_t raw;

    degrees_minutes(e7, &degrees, &minutes);
    n = ((uint64_t)degrees * 600000u) + minutes;
    magnitude = latitude ? (((n << 25) + 421875u) / (2u * 421875u))
                         : (((n << 24) + 421875u) / (2u * 421875u));
    raw = (magnitude > 0x7FFFFFFFu) ? 0x7FFFFFFFu : (uint32_t)magnitude;
    if (e7 < 0) {
        // Latitude is sign-magnitude; longitude is a signed two's-complement value
        raw = latitude ? (raw | 0x80000000u) : (uint32_t)(-(int64_t)raw);
    }
    return raw;
}

static int build_lrrp(int32_t lat_e7, int32_t lon_e7, uint16_t speed_knots, uint16_t course, uint8_t *doc)
{
    uint8_t tokens[LRRP_MAX_DOCUMENT];
    uint32_t lat = raw_coordinate(lat_e7, true);
    uint32_t lon = raw_coordinate(lon_e7, false);
    int n = 0;

    tokens[n++] = TOKEN_REQUEST_ID;
    n += put_uintvar(&tokens[n], (uint32_t)sizeof(REPORT_ID));
    memcpy(&tokens[n], REPORT_ID, sizeof(REPORT_ID));
    n += (int)sizeof(REPORT_ID);

    tokens[n++] = TOKEN_POSITION_RADIUS;
    tokens[n++] = (uint8_t)(lat >> 24);
    tokens[n++] = (uint8_t)(lat >> 16);
    tokens[n++] = (uint8_t)(lat >> 8);
    tokens[n++] = (uint8_t)lat;
    tokens[n++] = (uint8_t)(lon >> 24);
    tokens[n++] = (uint8_t)(lon >> 16);
    tokens[n++] = (uint8_t)(lon >> 8);
    tokens[n++] = (uint8_t)lon;
    n += put_uintvar(&tokens[n], 10); // accuracy radius: 10 m
    tokens[n++] = 0;

    if (speed_knots != APRS_DMR_SPEED_UNKNOWN) {
        uint32_t mps128 = (((uint64_t)speed_knots * 65848832u) + 500000u) / 1000000u;

        tokens[n++] = TOKEN_SPEED_HOR;
        n += put_uintvar(&tokens[n], mps128 >> 7);
        tokens[n++] = (uint8_t)(mps128 & 0x7Fu);
    }
    if (course != APRS_DMR_COURSE_UNKNOWN) {
        tokens[n++] = TOKEN_DIRECTION;
        tokens[n++] = (uint8_t)((course % 360u) / 2u);
    }

    {
        uint8_t out[LRRP_MAX_DOCUMENT + 2];
        int len = put_uintvar(out, DOC_TRIGGERED_REPORT);

        len += put_uintvar(&out[len], (uint32_t)n);
        memcpy(&out[len], tokens, (size_t)n);
        memcpy(doc, out, (size_t)(len + n));
        return len + n;
    }
}

bool aprs_dmr_build_position(int32_t latitude_e7, int32_t longitude_e7, uint16_t speed_knots, uint16_t course,
                             uint32_t destination, uint32_t source, aprs_dmr_tx_t *tx)
{
    uint8_t doc[LRRP_MAX_DOCUMENT];
    uint8_t ip[28 + LRRP_MAX_DOCUMENT];

    if ((tx == NULL) || (destination == 0) || (destination > 0xFFFFFFu) || (source == 0) || (source > 0xFFFFFFu)) {
        return false;
    }
    tx->count = 0;
    int doc_length = build_lrrp(latitude_e7, longitude_e7, speed_knots, course, doc);
    if (doc_length <= 0) {
        return false;
    }
    // to the gateway's radio address, TTL 64 (an individual datagram), port 4001 both ways
    int length = dmr_ip_udp(ip, (int)sizeof(ip), DMR_IP_RADIO(source), DMR_IP_RADIO(destination), 64, LRRP_PORT, doc,
                            doc_length);
    return (length > 0) &&
           dmr_data_unconfirmed(tx, DMR_DATA_PREAMBLES, false, DMR_SAP_IP, destination, source, ip, length) &&
           (tx->count <= APRS_DMR_MAX_BURSTS);
}
