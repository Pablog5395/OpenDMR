// M17 packet mode (src/services/m17_core.c) through its own receiver: an SMS packet's frames as dibits back in
//   gcc -I../../src/services -o m17_sms_test m17_sms_test.c ../../src/services/m17_core.c && ./m17_sms_test
#include <stdio.h>
#include <string.h>

#include "m17_core.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

static int run(const char *text, const char *dst, int flip_every)
{
    uint8_t lsf[M17_LSF_BYTES], pkt[M17_PACKET_MAX], f[M17_FRAME_DIBITS];
    static m17_rx r;
    m17_rx_init(&r);
    int n = m17_packet_sms(text, pkt, sizeof(pkt));
    m17_lsf_make_packet(lsf, dst, "VA3BPF", 3);
    int got = 0, frames = 0, count = 0;
    for (int part = 0;; part++) {
        int more = 2;
        if (part == 0) {
            m17_tx_preamble(f);
        } else if (part == 1) {
            m17_tx_lsf(lsf, f);
        } else {
            more = m17_tx_packet_frame(pkt, n, part - 2, f);
            if (!more) {
                m17_tx_eot(f);
            } else {
                frames++;
            }
        }
        for (int i = 0; i < M17_FRAME_DIBITS; i++) {
            uint8_t d = f[i];
            if (flip_every && ((++count % flip_every) == 0) && (i >= 8)) {
                d ^= 2; // a symbol's sign wrong (payload only)
            }
            m17_rx_event_t e = m17_rx_dibit(&r, d);
            if (e == M17_RX_LSF) {
                char s[10], t[10];
                m17_address(r.lsf + 6, s);
                m17_address(r.lsf, t);
                printf("  LSF %s -> %s, packet %d\n", s, t, r.packet_mode);
            }
            if (e == M17_RX_PACKET) {
                got = (r.packet[0] == M17_PACKET_SMS) && !strcmp((const char *)&r.packet[1], text);
                printf("  packet %d octets (sent %d with CRC), %d frames: \"%s\"\n", r.packet_len, n, frames,
                       (const char *)&r.packet[1]);
            }
        }
        if ((part >= 2) && !more) {
            break;
        }
    }
    return got;
}

int main(void)
{
    char longest[500] = "";
    for (int i = 0; i < 140; i++) {
        strcat(longest, "\xe4\xb8\xad");
    }
    check(run("Hello M17", "", 0), "short SMS to ALL");
    check(run("M17 \xe7\x9f\xad\xe4\xbf\xa1 test 25 octets ok!", "BG8AM", 0), "a text of 25 octets and more");
    check(run("exactly 21 chars here", "", 0), "a packet of exactly 25 octets (EOF counter 25)");
    check(run(longest, "", 0), "140 CJK characters (17 frames)");
    check(run("with errors: every 40th symbol's sign wrong", "", 40), "Viterbi puts the errors right");
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
