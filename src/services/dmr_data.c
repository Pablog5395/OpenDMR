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
 * OpenDMR, service: DMR packet data (dmr_data.h). The layouts checked against a TYT MD-380's text message as it
 * received it (md380tools applet/src/dmr.c, dmr_sms_arrive_hook(): its U_HEAD with CRC 83 0C, the IPv4 / UDP 4007
 * datagram, the CRC-32 9E 21 5A 5C), dmr_soft/c/sms_test.c.
 */

#include <string.h>

#include "dmr_l2.h"
#include "dmr_data.h"

#define CSBK_PREAMBLE 0x3D // CSBKO of the preamble CSBK (ETSI TS 102 361-2 7.2.17)
#define IP_UDP        17

static uint16_t ip_identification;

void dmr_ip_identification(uint16_t next)
{
    ip_identification = next;
}

static void put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_id(uint8_t *p, uint32_t id)
{
    p[0] = (uint8_t)(id >> 16);
    p[1] = (uint8_t)(id >> 8);
    p[2] = (uint8_t)id;
}

static uint32_t get_u16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t sum16(uint32_t sum, const uint8_t *d, int n)
{
    for (int i = 0; i < n; i += 2) {
        sum += (uint32_t)((d[i] << 8) | (((i + 1) < n) ? d[i + 1] : 0));
    }
    return sum;
}

static uint16_t fold(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)sum;
}

int dmr_ip_udp(uint8_t *out, int max, uint32_t src_ip, uint32_t dst_ip, uint8_t ttl, uint16_t port,
               const uint8_t *payload, int length)
{
    int udp = 8 + length, total = 20 + udp;
    if ((length < 0) || (total > max)) {
        return 0;
    }
    memset(out, 0, 28);
    out[0] = 0x45;
    put_u16(&out[2], (uint32_t)total);
    put_u16(&out[4], ip_identification++);
    out[8] = ttl;
    out[9] = IP_UDP;
    put_u16(&out[12], src_ip >> 16);
    put_u16(&out[14], src_ip);
    put_u16(&out[16], dst_ip >> 16);
    put_u16(&out[18], dst_ip);
    put_u16(&out[10], (uint16_t)~fold(sum16(0, out, 20)));
    put_u16(&out[20], port);
    put_u16(&out[22], port);
    put_u16(&out[24], (uint32_t)udp);
    memmove(&out[28], payload, (size_t)length); // the payload may be in out already (built there)
    // the pseudo header: the addresses, the protocol, the UDP length
    uint16_t sum = (uint16_t)~fold(sum16(sum16(IP_UDP + (uint32_t)udp, &out[12], 8), &out[20], udp));
    put_u16(&out[26], (sum == 0) ? 0xFFFF : sum);
    return total;
}

bool dmr_ip_udp_parse(const uint8_t *d, int length, uint32_t *src_ip, uint32_t *dst_ip, uint16_t *port,
                      const uint8_t **payload, int *payload_length)
{
    if ((length < 28) || (d[0] != 0x45) || (d[9] != IP_UDP)) {
        return false;
    }
    int total = (int)get_u16(&d[2]), udp = (int)get_u16(&d[24]);
    if ((total > length) || (udp < 8) || ((20 + udp) > total)) {
        return false;
    }
    *src_ip = get_u32(&d[12]);
    *dst_ip = get_u32(&d[16]);
    *port = (uint16_t)get_u16(&d[22]);
    *payload = &d[28];
    *payload_length = udp - 8;
    return true;
}

static void add(dmr_data_tx_t *tx, uint8_t type, const uint8_t *payload)
{
    dmr_data_burst_t *b = &tx->burst[tx->count++];
    b->type = type;
    b->length = (type == DMR_DATA_RATE_12) ? DMR_DATA_BLOCK : 10;
    memset(b->payload, 0, sizeof(b->payload));
    memcpy(b->payload, payload, b->length);
}

// the preambles, the header (its 10 octets with *blocks set), then the blocks: data, pad octets, the CRC-32 (B.3.9,
// least significant octet first)
static bool packet(dmr_data_tx_t *tx, int preambles, bool group, uint32_t dst, uint32_t src, uint8_t head[10],
                   int blocks, int pad, const uint8_t *data, int length)
{
    tx->count = 0;
    if ((length < 0) || (blocks < 1) || ((preambles + 1 + blocks) > DMR_DATA_TX_MAX)) {
        return false;
    }
    for (int i = 0; i < preambles; i++) {
        uint8_t c[10] = {0x80 | CSBK_PREAMBLE, 0x00, (uint8_t)(0x80 | (group ? 0x40 : 0)),
                         (uint8_t)((preambles - 1 - i) + 1 + blocks)};
        put_id(&c[4], dst);
        put_id(&c[7], src);
        add(tx, DMR_DATA_CSBK, c);
    }
    add(tx, DMR_DATA_HEADER, head);
    // the blocks: the data, the pad octets (zeros), then the CRC over both (dmr_crc32()'s B.3.9: the octets in pairs,
    // the second of each first), read from the blocks: no copy of the packet on the caller's stack (the radio task's
    // is 1 KB)
    int first = tx->count;
    uint8_t zero[DMR_DATA_BLOCK] = {0};
    for (int i = 0; i < blocks; i++) {
        add(tx, DMR_DATA_RATE_12, zero);
    }
    for (int i = 0; i < length; i++) {
        tx->burst[first + i / DMR_DATA_BLOCK].payload[i % DMR_DATA_BLOCK] = data[i];
    }
    int n = length + pad;
    uint32_t crc = 0;
    for (int i = 0; i < ((n + 1) & ~1); i++) {
        int k = i ^ 1;
        uint8_t b = (k < n) ? tx->burst[first + k / DMR_DATA_BLOCK].payload[k % DMR_DATA_BLOCK] : 0;
        crc ^= (uint32_t)b << 24;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80000000UL) ? ((crc << 1) ^ 0x04C11DB7UL) : (crc << 1);
        }
    }
    for (int i = 0; i < 4; i++) {
        tx->burst[first + (n + i) / DMR_DATA_BLOCK].payload[(n + i) % DMR_DATA_BLOCK] = (uint8_t)(crc >> (8 * i));
    }
    return true;
}

static void blocks_for(int length, int *blocks, int *pad)
{
    *blocks = (length + 4 + DMR_DATA_BLOCK - 1) / DMR_DATA_BLOCK;
    *pad = (*blocks * DMR_DATA_BLOCK) - 4 - length;
}

bool dmr_data_unconfirmed(dmr_data_tx_t *tx, int preambles, bool group, uint8_t sap, uint32_t dst, uint32_t src,
                          const uint8_t *data, int length)
{
    int blocks, pad;
    blocks_for(length, &blocks, &pad);
    if (blocks > 127) {
        return false;
    }
    uint8_t h[10] = {(uint8_t)((group ? 0x80 : 0) | ((pad & 0x10) ? 0x10 : 0) | DMR_DPF_UNCONFIRMED),
                     (uint8_t)((sap << 4) | (pad & 0x0F))};
    put_id(&h[2], dst);
    put_id(&h[5], src);
    h[8] = (uint8_t)(0x80 | blocks); // the full message, the blocks to follow
    h[9] = 0;                        // the fragment sequence number: one fragment, unconfirmed
    return packet(tx, preambles, group, dst, src, h, blocks, pad, data, length);
}

bool dmr_data_defined(dmr_data_tx_t *tx, int preambles, bool group, uint8_t dd_format, uint32_t dst, uint32_t src,
                      const uint8_t *data, int length)
{
    int blocks, pad;
    blocks_for(length, &blocks, &pad);
    if (blocks > 63) {
        return false;
    }
    // the appended blocks: their high 2 bits in octet 0, the low 4 in octet 1; octet 8: the DD format, S (SARQ)
    // 0, F 1; octet 9: the padding bits
    uint8_t h[10] = {(uint8_t)((group ? 0x80 : 0) | ((blocks >> 4) & 0x03) << 4 | DMR_DPF_DEFINED),
                     (uint8_t)((DMR_SAP_SHORT_DATA << 4) | (blocks & 0x0F))};
    put_id(&h[2], dst);
    put_id(&h[5], src);
    h[8] = (uint8_t)((dd_format << 2) | 0x01);
    h[9] = (uint8_t)(pad * 8);
    return packet(tx, preambles, group, dst, src, h, blocks, pad, data, length);
}
