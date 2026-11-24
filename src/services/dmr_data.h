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

// DMR packet data (ETSI TS 102 361-1 8.2, 9.2): a packet into the bursts of one transmission (preamble CSBKs, the
// data header, Rate 1/2 blocks with the message CRC-32), and the IPv4 / UDP datagram most packets carry (the DMR ID
// in an address's low 24 bits). Pure functions: also built on the PC (dmr_soft/c).
#ifndef DM32_DMR_DATA_H
#define DM32_DMR_DATA_H

#include <stdbool.h>
#include <stdint.h>

#define DMR_DATA_TX_MAX     34  // bursts of one transmission: preambles, a header, up to 31 blocks
#define DMR_DATA_BLOCK      12  // a Rate 1/2 block's octets
#define DMR_DATA_PREAMBLES  2   // preamble CSBKs before a packet (the HR_C7000 path's and APRS's)

// burst types: the Data Type of their Slot Type (dmr_l2.h DMR_DT_CSBK, _DATA_HEADER, _RATE_12)
enum { DMR_DATA_CSBK = 3, DMR_DATA_HEADER = 6, DMR_DATA_RATE_12 = 7 };

typedef struct {
    uint8_t type;     // DMR_DATA_xxx
    uint8_t length;   // 10 (a CSBK, a header: their CRC added when sent) or 12
    uint8_t payload[12];
} dmr_data_burst_t;

typedef struct {
    uint8_t count;
    dmr_data_burst_t burst[DMR_DATA_TX_MAX];
} dmr_data_tx_t;

// Data Packet Format (9.3.17) and SAP (9.3.18) of the headers used
#define DMR_DPF_UNCONFIRMED 0x02
#define DMR_DPF_CONFIRMED   0x03
#define DMR_DPF_DEFINED     0x0D
#define DMR_SAP_IP_COMPRESSED 0x03
#define DMR_SAP_IP          0x04
#define DMR_SAP_SHORT_DATA  0x0A

// IPv4 addresses (the networks MOTOTRBO and the others use): 12.<ID> a radio, 225.<ID> a talk group
#define DMR_IP_RADIO(id) ((0x0CUL << 24) | ((id) & 0xFFFFFFUL))
#define DMR_IP_GROUP(id) ((0xE1UL << 24) | ((id) & 0xFFFFFFUL))

// An IPv4 / UDP datagram (no options), the same port both ways, the next identification; its length (0: too long)
int dmr_ip_udp(uint8_t *out, int max, uint32_t src_ip, uint32_t dst_ip, uint8_t ttl, uint16_t port,
               const uint8_t *payload, int length);

// data (its octets: an IP datagram, SAP DMR_SAP_IP) as an unconfirmed packet: preambles, U_HEAD (9.2.6), Rate 1/2
// blocks, the pad octets and the CRC-32 in the last; false: too long for DMR_DATA_TX_MAX
bool dmr_data_unconfirmed(dmr_data_tx_t *tx, int preambles, bool group, uint8_t sap, uint32_t dst, uint32_t src,
                          const uint8_t *data, int length);
// data as a defined short data packet (DD_HEAD, 9.2.13; SAP short data): dd_format (9.3.36), the blocks as above
bool dmr_data_defined(dmr_data_tx_t *tx, int preambles, bool group, uint8_t dd_format, uint32_t dst, uint32_t src,
                      const uint8_t *data, int length);

// A packet received: its header (12 octets, its CRC checked) and its user data (the blocks' octets less the pad and
// the CRC-32, which were checked)
typedef struct {
    uint8_t head[12];
    uint8_t dpf, sap;
    bool group;
    uint32_t dst, src;
    const uint8_t *data;
    int length;
} dmr_data_rx_t;

void dmr_ip_identification(uint16_t next); // the next datagram's identification (tests)

// a received IPv4 / UDP datagram: its port and payload (no options, protocol UDP); false: not one
bool dmr_ip_udp_parse(const uint8_t *d, int length, uint32_t *src_ip, uint32_t *dst_ip, uint16_t *port,
                      const uint8_t **payload, int *payload_length);

#endif
