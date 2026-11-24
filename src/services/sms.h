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

// Text messages (sms.c), the modes' common part: the inbox and the outbox in the flash (the CPS reads and writes
// them: SMS_FLASH_BASE), sending on the channel's mode (DMR: dmr_sms.h; M17), the messages received from the
// receivers, the preset phrases (written by the CPS).
#ifndef DM32_SMS_H
#define DM32_SMS_H

#include <stdbool.h>
#include <stdint.h>

#include "dmr_data.h"

// The flash (window addresses, flash.h): a header sector (SMS_MAGIC, the presets), then the inbox and the outbox,
// SMS_BOX records of SMS_RECORD octets each (sms_t). Free: 0xFF; the CPS may erase or rewrite any of it
#define SMS_FLASH_BASE   0x30000
#define SMS_FLASH_SIZE   0x10000
#define SMS_MAGIC        0x31534D53 // "SMS1"
#define SMS_PRESETS      16
#define SMS_PRESET_SIZE  64         // UTF-8, NUL terminated (0xFF: none)
#define SMS_PRESETS_AT   (SMS_FLASH_BASE + 0x40)
#define SMS_INBOX_AT     (SMS_FLASH_BASE + 0x1000)
#define SMS_OUTBOX_AT    (SMS_FLASH_BASE + 0x5000)
#define SMS_BOX          32
#define SMS_RECORD       512
#define SMS_TEXT_MAX     463        // UTF-8 octets kept of a message
#define SMS_CHARS        140        // characters (UTF-16 units) of a message sent at most

enum { SMS_INBOX = 0, SMS_OUTBOX = 1 };
enum { SMS_MODE_DMR = 0, SMS_MODE_M17 = 1 };
enum { SMS_SENDING = 0, SMS_SENT = 1, SMS_DELIVERED = 2, SMS_FAILED = 3 };
#define SMS_USED   0xA5 // sms_t.used of a message (0xFF free, 0x00 deleted)
#define SMS_UNREAD 0xFF // sms_t.unread (0x00 read): cleared without an erase

typedef struct {
    uint8_t used;
    uint8_t unread;
    uint8_t mode;     // SMS_MODE_xxx
    uint8_t status;   // the outbox: SMS_SENDING ...
    uint32_t seq;     // its order: the newest the highest
    uint32_t time;    // UTC seconds (datetime.h); 0: no clock
    uint32_t src, dst; // DMR: IDs (dst a talk group with group)
    uint8_t group;
    uint8_t format;   // DMR: dmr_sms.h DMR_SMS_xxx
    uint8_t tms_seq;  // DMR: TMS's sequence number (the outbox: the one sent)
    uint8_t reserved;
    char peer[24];    // M17: the other end's callsign (NUL terminated)
    char text[SMS_TEXT_MAX + 1];
} sms_t;

void sms_init(void);              // the boxes' index from the flash (at start)
int sms_count(int box);
bool sms_get(int box, int index, sms_t *out); // index 0: the newest
bool sms_delete(int box, int index);
void sms_mark_read(int index);    // the inbox
int sms_unread(void);

// A message on the channel now (DMR: to dst, a radio or with group a talk group, in the format of the options; M17:
// to the callsign m17_dst, "" for all); false: not a DMR / M17 channel, empty, too long, or the last still going
bool sms_send(uint32_t dst, bool group, const char *m17_dst, const char *text);
int sms_sending_state(void);      // the last sent: SMS_SENDING .. SMS_FAILED
uint8_t sms_dmr_format(void);     // the DMR format sent (the options), DMR_SMS_xxx
void sms_set_dmr_format(uint8_t format);

int sms_presets(void);            // the preset phrases written
bool sms_preset(int index, char *out, int size);

// From the receivers' tasks: a DMR packet whole (its CRCs right; ts its timeslot), an M17 text
void sms_dmr_packet(int ts, const dmr_data_rx_t *p);
void sms_m17_text(const char *src, const char *dst, const char *text);

// The input task's (input.c): the messages received stored and shown, the acknowledgements sent, the sending followed
void sms_tick(void);

#endif
