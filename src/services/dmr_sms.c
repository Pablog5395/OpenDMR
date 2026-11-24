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
 * OpenDMR, service: DMR text messages (dmr_sms.h). No standard says what amateur radios send; the layouts are those
 * of radios captured and of the libraries that read them (USA-RedDragon/dmrgo v2/sms, mototrbo/tms, shortdata;
 * dmrshark; md380tools):
 *
 * - Motorola TMS: an unconfirmed packet (U_HEAD, SAP IP, Rate 1/2) with an IPv4 datagram 12.<src> -> 12.<dst> (a talk
 *   group 225.<tg>), TTL 64, UDP 4007 both ways; then the TMS PDU: its length (16 bits, the octets after it), the
 *   header (ext, ack requested, reserved 1, type 0 text: 0xE0 / 0xA0), the address length 0 (radio to radio), the
 *   sequence number's low 5 bits (ext 1) and high 2 bits with the encoding 4 (UTF-16LE), then CR LF and the text in
 *   UTF-16LE. The acknowledgement goes back the same way: 0x9F (ext, type 0x1F), address length 0, the sequence
 *   number (a second octet above 31). Brandmeister's Text Capture waits for it.
 * - "DMR" (AnyTone's DMR standard): the same packet and IPv4, TTL 1, UDP 5016, 00 0D 00 0A then UTF-16LE. The ETSI
 *   IP text (TS 102 361-3) on that port is UTF-16BE without it: both read.
 * - Hytera (AnyTone's H-SMS): a defined short data packet (DD_HEAD, SAP short data) of DD format 1 (BCD, though it
 *   isn't): a 2-octet field (0), the text in UTF-16LE, a NUL. Other DD formats read as their table says.
 */

#include <string.h>

#include "dmr_sms.h"

#define TMS_TEXT     0x00
#define TMS_ACK      0x1F
#define TMS_UTF16LE  4
#define DD_BCD       0x01
#define DD_7BIT      0x02
#define DD_ISO_FIRST 0x03 // .. 0x11: ISO 8859 parts (read as Latin-1)
#define DD_ISO_LAST  0x11
#define DD_UTF8      0x12
#define DD_UTF16     0x13
#define DD_UTF16BE   0x14
#define DD_UTF16LE   0x15
#define PAYLOAD_MAX  (DMR_DATA_TX_MAX * DMR_DATA_BLOCK)

/* ---- UTF-8 <-> UTF-16 ---- */

// the next code point of a UTF-8 text (malformed octets taken as Latin-1), 0 at its end
static uint32_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t c = s[0];
    int n = 0;
    if (c == 0) {
        return 0;
    }
    if ((c >= 0xC0) && (c < 0xE0)) {
        n = 1;
        c &= 0x1F;
    } else if ((c >= 0xE0) && (c < 0xF0)) {
        n = 2;
        c &= 0x0F;
    } else if ((c >= 0xF0) && (c < 0xF8)) {
        n = 3;
        c &= 0x07;
    }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *p += 1;
            return s[0];
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *p += 1 + n;
    return c;
}

int dmr_sms_units(const char *utf8)
{
    int n = 0;
    uint32_t c;
    while ((c = utf8_next(&utf8)) != 0) {
        n += (c > 0xFFFF) ? 2 : 1;
    }
    return n;
}

// a code point onto a UTF-8 text of room octets (its NUL kept): false when it doesn't fit
static bool put_utf8(char *out, int *at, int room, uint32_t c)
{
    uint8_t b[4];
    int n;
    if (c < 0x80) {
        b[0] = (uint8_t)c;
        n = 1;
    } else if (c < 0x800) {
        b[0] = (uint8_t)(0xC0 | (c >> 6));
        b[1] = (uint8_t)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        b[0] = (uint8_t)(0xE0 | (c >> 12));
        b[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
        b[2] = (uint8_t)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        b[0] = (uint8_t)(0xF0 | (c >> 18));
        b[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3F));
        b[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
        b[3] = (uint8_t)(0x80 | (c & 0x3F));
        n = 4;
    }
    if ((*at + n) > room) {
        return false;
    }
    memcpy(&out[*at], b, (size_t)n);
    *at += n;
    out[*at] = 0;
    return true;
}

// UTF-16 (n octets, big or little endian) into UTF-8: up to a NUL, the control characters but TAB and LF dropped
static void utf16_to_utf8(const uint8_t *d, int n, bool be, char *out, int room)
{
    int at = 0;
    out[0] = 0;
    for (int i = 0; (i + 1) < n; i += 2) {
        uint32_t c = be ? (uint32_t)((d[i] << 8) | d[i + 1]) : (uint32_t)(d[i] | (d[i + 1] << 8));
        if (c == 0) {
            break;
        }
        if ((c >= 0xD800) && (c < 0xDC00) && ((i + 3) < n)) { // a surrogate pair
            uint32_t lo = be ? (uint32_t)((d[i + 2] << 8) | d[i + 3]) : (uint32_t)(d[i + 2] | (d[i + 3] << 8));
            if ((lo >= 0xDC00) && (lo < 0xE000)) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if ((c < 0x20) && (c != '\t') && (c != '\n')) {
            continue;
        }
        if (!put_utf8(out, &at, room, c)) {
            break;
        }
    }
}

// UTF-8 into UTF-16LE (with the surrogate pairs): its octets; -1 when more than max_units
static int utf8_to_utf16le(const char *utf8, uint8_t *out, int max_units)
{
    int n = 0;
    uint32_t c;
    while ((c = utf8_next(&utf8)) != 0) {
        uint16_t u[2];
        int k = 1;
        if (c > 0xFFFF) {
            c -= 0x10000;
            u[0] = (uint16_t)(0xD800 | (c >> 10));
            u[1] = (uint16_t)(0xDC00 | (c & 0x3FF));
            k = 2;
        } else {
            u[0] = (uint16_t)c;
        }
        if ((n + k) > max_units) {
            return -1;
        }
        for (int i = 0; i < k; i++, n++) {
            out[2 * n] = (uint8_t)u[i];
            out[2 * n + 1] = (uint8_t)(u[i] >> 8);
        }
    }
    return 2 * n;
}

/* ---- sending ---- */

bool dmr_sms_build(dmr_data_tx_t *tx, int format, uint32_t src, uint32_t dst, bool group, const char *utf8,
                   uint8_t seq, bool ack_request)
{
    uint8_t ip[28 + 10 + 2 * DMR_SMS_CHARS + 2]; // the payload built after the IPv4 / UDP header's room
    uint8_t *p = &ip[28];
    int n = 0;
    tx->count = 0;
    if (format == DMR_SMS_HYTERA) {
        p[0] = p[1] = 0;
        int t = utf8_to_utf16le(utf8, &p[2], DMR_SMS_CHARS);
        if (t <= 0) {
            return false;
        }
        n = 2 + t;
        p[n++] = 0;
        p[n++] = 0;
        return dmr_data_defined(tx, DMR_DATA_PREAMBLES, group, DD_BCD, dst, src, p, n);
    }
    uint16_t port = DMR_SMS_PORT_TEXT;
    uint8_t ttl = 1;
    if (format == DMR_SMS_MOTOROLA) {
        port = DMR_SMS_PORT_TMS;
        ttl = 64;
        p[2] = (uint8_t)(ack_request ? 0xE0 : 0xA0); // ext, ack requested, reserved, type 0: text
        p[3] = 0;                                   // no address
        p[4] = (uint8_t)(0x80 | (seq & 0x1F));      // ext: the high bits and the encoding follow
        p[5] = (uint8_t)(((seq >> 5) & 0x03) << 5 | TMS_UTF16LE);
        p[6] = '\r';
        p[7] = 0;
        p[8] = '\n';
        p[9] = 0;
        int t = utf8_to_utf16le(utf8, &p[10], DMR_SMS_CHARS);
        if (t <= 0) {
            return false;
        }
        n = 10 + t;
        p[0] = (uint8_t)((n - 2) >> 8);
        p[1] = (uint8_t)(n - 2);
    } else {
        p[0] = 0;
        p[1] = 0x0D;
        p[2] = 0;
        p[3] = 0x0A;
        int t = utf8_to_utf16le(utf8, &p[4], DMR_SMS_CHARS);
        if (t <= 0) {
            return false;
        }
        n = 4 + t;
    }
    int len = dmr_ip_udp(ip, (int)sizeof(ip), DMR_IP_RADIO(src), group ? DMR_IP_GROUP(dst) : DMR_IP_RADIO(dst), ttl,
                         port, p, n);
    return (len > 0) && dmr_data_unconfirmed(tx, DMR_DATA_PREAMBLES, group, DMR_SAP_IP, dst, src, ip, len);
}

bool dmr_sms_build_ack(dmr_data_tx_t *tx, uint32_t src, uint32_t dst, uint8_t seq)
{
    uint8_t p[6] = {0, 0, 0x9F, 0, (uint8_t)(((seq > 31) ? 0x80 : 0) | (seq & 0x1F)), (uint8_t)(((seq >> 5) & 3) << 5)};
    int n = (seq > 31) ? 6 : 5;
    uint8_t ip[28 + sizeof(p)];
    p[1] = (uint8_t)(n - 2);
    int len = dmr_ip_udp(ip, (int)sizeof(ip), DMR_IP_RADIO(src), DMR_IP_RADIO(dst), 64, DMR_SMS_PORT_TMS, p, n);
    return (len > 0) && dmr_data_unconfirmed(tx, DMR_DATA_PREAMBLES, false, DMR_SAP_IP, dst, src, ip, len);
}

/* ---- receiving ---- */

static bool tms(const uint8_t *d, int n, dmr_sms_rx_t *out)
{
    if (n < 3) {
        return false;
    }
    int len = (d[0] << 8) | d[1];
    if ((len + 2) < n) {
        n = len + 2;
    }
    uint8_t hdr = d[2];
    uint8_t type = hdr & 0x1F;
    int at = 3;
    if (at >= n) {
        return false;
    }
    at += 1 + d[at]; // the address (its length, then it)
    uint8_t seq = 0;
    if ((hdr & 0x80) && (type != 0x10) && (at < n)) { // the sequence number (not in an availability message)
        uint8_t lo = d[at++];
        seq = lo & 0x1F;
        if ((lo & 0x80) && (at < n)) {
            seq |= (uint8_t)(((d[at++] >> 5) & 0x03) << 5);
        }
    }
    out->seq = seq;
    out->format = DMR_SMS_MOTOROLA;
    if (type == TMS_ACK) {
        out->ack = true;
        return true;
    }
    if ((type != TMS_TEXT) || (at > n)) {
        return false;
    }
    out->ack_requested = (hdr & 0x40) != 0;
    if (((at + 4) <= n) && (d[at] == 0x0D) && (d[at + 1] == 0) && (d[at + 2] == 0x0A) && (d[at + 3] == 0)) {
        at += 4; // the CR LF before the text (a subject's end)
    }
    utf16_to_utf8(&d[at], n - at, false, out->text, DMR_SMS_TEXT_MAX);
    return true;
}

static bool hytera_layout(const uint8_t *d, int n)
{
    if ((n < 6) || (n & 1) || ((d[2] == 0) && (d[3] == 0))) {
        return false;
    }
    for (int i = 4; (i + 1) < n; i += 2) {
        if ((d[i] == 0) && (d[i + 1] == 0)) {
            for (int k = i + 2; k < n; k++) {
                if (d[k]) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

static bool defined(const dmr_data_rx_t *p, dmr_sms_rx_t *out)
{
    uint8_t dd = p->head[8] >> 2;
    const uint8_t *d = p->data;
    int n = p->length;
    out->format = DMR_SMS_DMR;
    if ((dd == DD_BCD) && hytera_layout(d, n)) {
        out->format = DMR_SMS_HYTERA;
        utf16_to_utf8(&d[2], n - 2, false, out->text, DMR_SMS_TEXT_MAX);
    } else if ((dd == DD_UTF16LE) || (dd == DD_UTF16)) {
        if ((dd == DD_UTF16) && (n >= 2) && (d[0] == 0xFE) && (d[1] == 0xFF)) {
            utf16_to_utf8(&d[2], n - 2, true, out->text, DMR_SMS_TEXT_MAX);
        } else {
            utf16_to_utf8(d, n, false, out->text, DMR_SMS_TEXT_MAX);
        }
    } else if (dd == DD_UTF16BE) {
        utf16_to_utf8(d, n, true, out->text, DMR_SMS_TEXT_MAX);
    } else if ((dd == DD_UTF8) || (dd == DD_7BIT) || ((dd >= DD_ISO_FIRST) && (dd <= DD_ISO_LAST))) {
        int at = 0;
        out->text[0] = 0;
        const char *s = (const char *)d;
        for (int i = 0; (i < n) && d[i]; ) {
            uint32_t c;
            if (dd == DD_UTF8) {
                const char *q = &s[i];
                c = utf8_next(&q);
                i = (int)(q - s);
            } else {
                c = d[i++] & ((dd == DD_7BIT) ? 0x7F : 0xFF);
            }
            if (((c >= 0x20) || (c == '\t') || (c == '\n')) && !put_utf8(out->text, &at, DMR_SMS_TEXT_MAX, c)) {
                break;
            }
        }
    } else {
        return false;
    }
    return out->text[0] != 0;
}

bool dmr_sms_decode(const dmr_data_rx_t *p, dmr_sms_rx_t *out)
{
    memset(out, 0, sizeof(*out));
    if (p->dpf == DMR_DPF_DEFINED) {
        return defined(p, out);
    }
    if (((p->dpf != DMR_DPF_UNCONFIRMED) && (p->dpf != DMR_DPF_CONFIRMED)) || (p->sap != DMR_SAP_IP)) {
        return false;
    }
    uint32_t sip, dip;
    uint16_t port;
    const uint8_t *d;
    int n;
    if (!dmr_ip_udp_parse(p->data, p->length, &sip, &dip, &port, &d, &n)) {
        return false;
    }
    if (port == DMR_SMS_PORT_TMS) {
        return tms(d, n, out);
    }
    if (port != DMR_SMS_PORT_TEXT) {
        return false;
    }
    out->format = DMR_SMS_DMR;
    if ((n >= 4) && (d[0] == 0) && (d[1] == 0x0D) && (d[2] == 0) && (d[3] == 0x0A)) {
        utf16_to_utf8(&d[4], n - 4, false, out->text, DMR_SMS_TEXT_MAX); // AnyTone: CR LF, then little endian
    } else {
        utf16_to_utf8(d, n, true, out->text, DMR_SMS_TEXT_MAX);          // ETSI: big endian
    }
    return out->text[0] != 0;
}
