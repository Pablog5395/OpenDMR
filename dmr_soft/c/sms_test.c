// DMR text messages (src/services/dmr_data.c, dmr_sms.c) against captured radios:
//   gcc -I../../src/services -o sms_test sms_test.c ../../src/services/dmr_data.c ../../src/services/dmr_sms.c \
//       ../../src/services/dmr_l2.c && ./sms_test
// - a TYT MD-380 receiving a TMS message from 3147092 to 99 (md380tools applet/src/dmr.c dmr_sms_arrive_hook())
// - an AnyTone M-SMS "MOTO TEST" datagram (dmrgo v2/internal/appcapture TMS)
// - TMS PDUs of dmrgo's and ok-dmrlib's tests; AnyTone H-SMS "HYT TEST" (dmrgo appcapture)
#include <stdio.h>
#include <string.h>

#include "dmr_l2.h"
#include "dmr_data.h"
#include "dmr_sms.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static int unhex(const char *h, uint8_t *out)
{
    int n = 0;
    while (h[0] && h[1]) {
        if (h[0] == ' ') {
            h++;
            continue;
        }
        unsigned v;
        sscanf(h, "%2x", &v);
        out[n++] = (uint8_t)v;
        h += 2;
    }
    return n;
}

static void dump(const char *what, const uint8_t *d, int n)
{
    printf("  %s:", what);
    for (int i = 0; i < n; i++) {
        printf(" %02x", d[i]);
    }
    printf("\n");
}

// the user data of a built packet: its blocks less the pad and the CRC-32 (checked)
static int packet_data(const dmr_data_tx_t *tx, uint8_t head[12], uint8_t *out)
{
    int h = -1, n = 0;
    for (int i = 0; i < tx->count; i++) {
        if (tx->burst[i].type == DMR_DATA_HEADER) {
            h = i;
        }
    }
    dmr_crc_ccitt_make(tx->burst[h].payload, ((tx->burst[h].payload[0] & 0x0F) == DMR_DPF_DEFINED) ? 0xCCCC : 0xCCCC,
                       head);
    for (int i = h + 1; i < tx->count; i++) {
        memcpy(&out[n], tx->burst[i].payload, 12);
        n += 12;
    }
    uint32_t c = out[n - 4] | (out[n - 3] << 8) | (out[n - 2] << 16) | ((uint32_t)out[n - 1] << 24);
    int pad = ((head[0] & 0x0F) == DMR_DPF_DEFINED) ? head[9] / 8 : (((head[0] & 0x10) ? 16 : 0) | (head[1] & 0x0F));
    check(dmr_crc32(out, n - 4) == c, "the CRC-32 of the blocks");
    return n - 4 - pad;
}

int main(void)
{
    uint8_t want[512], got[512], head[12];
    dmr_data_tx_t tx;
    dmr_sms_rx_t rx;

    // 1. the MD-380's message, built
    dmr_ip_identification(3);
    check(dmr_sms_build(&tx, DMR_SMS_MOTOROLA, 3147092, 99, false, "This is a test from kk4vcz!", 18, true),
          "MD-380 message built");
    check(tx.count == 2 + 1 + 8, "2 preambles, a header, 8 blocks");
    int wn = unhex("02 40 00 00 63 30 05 54 88 00 83 0c", want);
    int n = packet_data(&tx, head, got);
    check(memcmp(head, want, (size_t)wn) == 0, "the U_HEAD and its CRC 83 0C");
    dump("header", head, 12);
    wn = unhex("45 00 00 5c 00 03 00 00 40 11 5c a8 0c 30 05 54 0c 00 00 63 0f a7 0f a7 00 48 d1 dc 00 3e e0 00 92 04 "
               "0d 00 0a 00 54 00 68 00 69 00 73 00 20 00 69 00 73 00 20 00 61 00 20 00 74 00 65 00 73 00 74 00 20 00 "
               "66 00 72 00 6f 00 6d 00 20 00 6b 00 6b 00 34 00 76 00 63 00 7a 00 21 00 9e 21 5a 5c", want);
    {
        uint8_t all[512];
        int k = 0;
        for (int i = 3; i < tx.count; i++) {
            memcpy(&all[k], tx.burst[i].payload, 12);
            k += 12;
        }
        check((k == wn) && (memcmp(all, want, (size_t)wn) == 0), "the 8 blocks as the MD-380 received them (IP, UDP, "
              "TMS, CRC-32 9E 21 5A 5C)");
        if (memcmp(all, want, (size_t)wn)) {
            dump("got ", all, k);
            dump("want", want, wn);
        }
    }
    // ... and read back
    dmr_data_rx_t p = {.dpf = head[0] & 0x0F, .sap = head[1] >> 4, .data = got, .length = n};
    memcpy(p.head, head, 12);
    check(dmr_sms_decode(&p, &rx) && !rx.ack && rx.ack_requested && (rx.seq == 18) &&
          !strcmp(rx.text, "This is a test from kk4vcz!"), "MD-380 message read");
    printf("  text \"%s\" seq %d\n", rx.text, rx.seq);

    // 2. AnyTone M-SMS datagram read
    n = unhex("4500003a000d00004011a5260c2dedcf0c2dcf560fa70fa7002633b0001ca00081040d000a004d004f0054004f0020005400"
              "4500530054000000", got);
    p = (dmr_data_rx_t){.dpf = DMR_DPF_UNCONFIRMED, .sap = DMR_SAP_IP, .data = got, .length = n};
    check(dmr_sms_decode(&p, &rx) && !rx.ack_requested && (rx.seq == 1) && !strcmp(rx.text, "MOTO TEST"),
          "AnyTone M-SMS read");
    printf("  text \"%s\" seq %d\n", rx.text, rx.seq);

    // 3. TMS PDUs: dmrgo's "\r\nHello" seq 5 with ack; the acks of 5 and 53
    dmr_ip_identification(0);
    check(dmr_sms_build(&tx, DMR_SMS_MOTOROLA, 1, 2, false, "Hello", 5, true), "Hello built");
    n = packet_data(&tx, head, got);
    wn = unhex("0012e00085040d000a00480065006c006c006f00", want);
    check((n == 28 + wn) && !memcmp(&got[28], want, (size_t)wn), "TMS PDU as dmrgo's");
    check(dmr_sms_build_ack(&tx, 2, 1, 5), "ack 5 built");
    n = packet_data(&tx, head, got);
    wn = unhex("00039f0005", want);
    check((n == 28 + wn) && !memcmp(&got[28], want, (size_t)wn), "TMS ack 5: 00 03 9F 00 05");
    p = (dmr_data_rx_t){.dpf = DMR_DPF_UNCONFIRMED, .sap = DMR_SAP_IP, .data = got, .length = n};
    check(dmr_sms_decode(&p, &rx) && rx.ack && (rx.seq == 5), "ack 5 read");
    check(dmr_sms_build_ack(&tx, 2, 1, 53), "ack 53 built");
    n = packet_data(&tx, head, got);
    wn = unhex("00049f009520", want);
    check((n == 28 + wn) && !memcmp(&got[28], want, (size_t)wn), "TMS ack 53: 00 04 9F 00 95 20 (ok-dmrlib)");

    // 4. H-SMS: AnyTone's capture read; ours built and read back
    n = unhex("4500 4800590054002000540045005300540000 00", got);
    p = (dmr_data_rx_t){.dpf = DMR_DPF_DEFINED, .sap = DMR_SAP_SHORT_DATA, .data = got, .length = n};
    p.head[8] = (uint8_t)(0x01 << 2 | 1);
    check(dmr_sms_decode(&p, &rx) && (rx.format == DMR_SMS_HYTERA) && !strcmp(rx.text, "HYT TEST"),
          "AnyTone H-SMS read");
    check(dmr_sms_build(&tx, DMR_SMS_HYTERA, 3027527, 3027528, false, "\xe4\xbd\xa0\xe5\xa5\xbd 73", 0, false),
          "H-SMS built");
    n = packet_data(&tx, head, got);
    p = (dmr_data_rx_t){.dpf = head[0] & 0x0F, .sap = head[1] >> 4, .data = got, .length = n};
    memcpy(p.head, head, 12);
    check(dmr_sms_decode(&p, &rx) && !strcmp(rx.text, "\xe4\xbd\xa0\xe5\xa5\xbd 73"), "H-SMS 你好 73 back");
    dump("H-SMS header", head, 12);

    // 5. "DMR" (AnyTone DMR standard) built and read back; an ETSI big endian text read
    check(dmr_sms_build(&tx, DMR_SMS_DMR, 1, 9, true, "TG msg", 0, false), "DMR standard to TG 9 built");
    n = packet_data(&tx, head, got);
    check((got[8] == 1) && (got[16] == 0xE1) && (got[22] == 0x13) && (got[23] == 0x98), "TTL 1, 225.x, port 5016");
    p = (dmr_data_rx_t){.dpf = head[0] & 0x0F, .sap = head[1] >> 4, .data = got, .length = n};
    check(dmr_sms_decode(&p, &rx) && !strcmp(rx.text, "TG msg"), "DMR standard read back");

    // 6. the longest message: 140 characters in 34 bursts or fewer
    char longest[600] = "";
    for (int i = 0; i < 140; i++) {
        strcat(longest, "\xe4\xb8\xad"); // 中
    }
    check(dmr_sms_build(&tx, DMR_SMS_MOTOROLA, 1, 2, false, longest, 0, true) && (tx.count <= DMR_DATA_TX_MAX),
          "140 characters fit (TMS)");
    printf("  %d bursts\n", tx.count);
    strcat(longest, "x");
    check(!dmr_sms_build(&tx, DMR_SMS_MOTOROLA, 1, 2, false, longest, 0, true), "141 refused");
    check(dmr_sms_units("a\xe4\xb8\xad\xf0\x9f\x98\x80") == 4, "UTF-16 units: a 中 😀 = 4");

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
