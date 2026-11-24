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

// DMR text messages, the formats the radios send (dmr_sms.c): built into a packet's bursts (dmr_data.h), and read
// from a packet received. Pure functions: also built on the PC (dmr_soft/c/sms_test.c).
#ifndef DM32_DMR_SMS_H
#define DM32_DMR_SMS_H

#include <stdbool.h>
#include <stdint.h>

#include "dmr_data.h"

// The formats sent, in the DM-32 stock firmware's order (its codeplug's SMS format)
enum {
    DMR_SMS_HYTERA = 0,   // defined short data, DD format BCD: a 2-octet field, UTF-16LE, NUL (AnyTone "H-SMS")
    DMR_SMS_MOTOROLA = 1, // IPv4 / UDP 4007, Motorola TMS, UTF-16LE (MOTOTRBO, AnyTone "M-SMS", TYT, Brandmeister)
    DMR_SMS_DMR = 2,      // IPv4 / UDP 5016, CR LF then UTF-16LE (AnyTone "DMR standard")
    DMR_SMS_FORMATS
};

#define DMR_SMS_CHARS     140 // UTF-16 units of a message sent at most (30 bursts with TMS)
#define DMR_SMS_TEXT_MAX  560 // UTF-8 octets of a message received (DMR_SMS_CHARS characters of 3 octets, and more)
#define DMR_SMS_PORT_TMS  4007
#define DMR_SMS_PORT_TEXT 5016

// A message (utf8) from src to dst (a radio, or a talk group) as bursts; seq: TMS's sequence number (0 - 127),
// ack_request: TMS asks the receiver for an acknowledgement (a private message). false: too long, or empty
bool dmr_sms_build(dmr_data_tx_t *tx, int format, uint32_t src, uint32_t dst, bool group, const char *utf8,
                   uint8_t seq, bool ack_request);
// TMS's acknowledgement of message seq from dst (to it, from src: us)
bool dmr_sms_build_ack(dmr_data_tx_t *tx, uint32_t src, uint32_t dst, uint8_t seq);

typedef struct {
    bool ack;           // a TMS acknowledgement (of seq), no text
    bool ack_requested; // a TMS text whose sender wants the acknowledgement
    uint8_t seq;
    int format;         // DMR_SMS_xxx it came in (the ETSI IP text and other defined data: DMR_SMS_DMR)
    char text[DMR_SMS_TEXT_MAX + 1]; // UTF-8, NUL terminated
} dmr_sms_rx_t;

// A packet received as a message; false: not a text message (nor a TMS acknowledgement)
bool dmr_sms_decode(const dmr_data_rx_t *p, dmr_sms_rx_t *out);

// UTF-16 units of a UTF-8 text (what DMR_SMS_CHARS counts)
int dmr_sms_units(const char *utf8);

#endif
