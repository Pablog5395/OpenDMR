#!/usr/bin/env python3
"""Where a radio's repeater mode bursts fall against the repeater's own (ETSI TS 102 361-1 4.2, 10.2.3: an MS's burst
centred where the BS's are). Another radio, the observer (DMR in software, its VFO), listens to the repeater's output
(--dl), then to the radio's input (--ul), then to the output again, each for --dwell s, and reads its burst log
(src/services/dmr_soft.c dmr_soft_burst_log: each burst's symbol 0 in its systime_get_us()). Its own receive delay is
the same on both frequencies and drops out; its clock and the repeater's drift by some us over the few seconds.

The radio under test transmits (in repeater mode) meanwhile, longer than the whole measurement: its PTT held by hand
or with --ptt PORT (the test interface). The repeater keeps sending while it repeats.

    py tools/debug/dmr_rmo_timing.py --observer COM23 --dl 439 --ul 434 [--ptt COM22]

Printed: the output's TS1 bursts' phase (their spread: the observer's clock), each input burst's start against the
output's TS1 bursts (0: at the same time; 30000: with TS2's), its mean and spread.
"""
import argparse
import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dm32dbg  # noqa: E402
from radio import Radio  # noqa: E402

LOG = 32
ENTRY = 41
SYNC_NAMES = ['none', 'BS voice', 'BS data', 'MS voice', 'MS data', 'TS1 voice', 'TS1 data', 'TS2 voice', 'TS2 data']
FRAME = 60000


def mem(r):
    d = dm32dbg.Radio.__new__(dm32dbg.Radio)
    d.p = r.port
    return d


def read_log(d, s, since):
    """The bursts logged since index since: (index, sync, ts, mse, since_sync, us)"""
    hold = s.resolve('dmr_soft_burst_log_hold')
    d.value_write(hold, 4, 1)
    try:
        n = d.value_read(s.resolve('dmr_soft_burst_log_n'), 4)
        base = s.resolve('dmr_soft_burst_log')
        raw = b''
        while len(raw) < LOG * ENTRY:
            raw += d.mem_read(base + len(raw), min(240, LOG * ENTRY - len(raw)))
    finally:
        d.value_write(hold, 4, 0)
    out = []
    for i in range(max(n - LOG, since), n):
        e = raw[(i % LOG) * ENTRY:(i % LOG + 1) * ENTRY]
        us = struct.unpack('<I', e[37:41])[0]
        out.append((i, e[33], e[34], e[35], e[36], us))
    return n, out


def dwell(r, d, s, mhz, seconds):
    r.tune(mhz)
    n0 = d.value_read(s.resolve('dmr_soft_burst_log_n'), 4)
    time.sleep(seconds)
    return read_log(d, s, n0)[1]


def circ_mean(values, period):
    a = [2 * math.pi * v / period for v in values]
    c, sn = sum(math.cos(x) for x in a), sum(math.sin(x) for x in a)
    return (math.atan2(sn, c) * period / (2 * math.pi)) % period


def wrap(v, period):
    v %= period
    return v - period if v >= period / 2 else v


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--observer', required=True)
    ap.add_argument('--dl', type=float, required=True, help='MHz: the repeater transmits here')
    ap.add_argument('--ul', type=float, required=True, help='MHz: it receives here (the radio under test transmits)')
    ap.add_argument('--dwell', type=float, default=1.5)
    ap.add_argument('--ptt', help='the radio under test (its PTT held over the test interface meanwhile)')
    ap.add_argument('--lead', type=float, default=1.5, help='s of transmitting before the observer starts')
    ap.add_argument('--elf', default=dm32dbg.ELF)
    a = ap.parse_args()

    s = dm32dbg.Symbols(a.elf)
    r = Radio(a.observer)
    d = mem(r)
    tx = Radio(a.ptt) if a.ptt else None
    try:
        if tx:
            tx.ptt(True)
            time.sleep(a.lead)
        dl1 = dwell(r, d, s, a.dl, a.dwell)
        ul = dwell(r, d, s, a.ul, a.dwell)
        dl2 = dwell(r, d, s, a.dl, a.dwell)
    finally:
        if tx:
            tx.ptt(False)

    def show(name, bursts):
        print('%s: %d bursts' % (name, len(bursts)))
        for i, k, ts, mse, since, us in bursts:
            print('  %5d %-9s ts%d mse %3d since %3d  %10d' % (i, SYNC_NAMES[k] if k < 9 else k, ts + 1, mse, since, us))

    for name, b in (('output', dl1), ('input', ul), ('output again', dl2)):
        show(name, b)

    def phase_of(bursts):
        """The output's TS1 burst start (mod 60 ms): the median of the bursts' 30 ms phase, the TS from the majority"""
        b = [(us, ts) for (_, k, ts, _, _, us) in bursts if k in (1, 2) and us]
        if len(b) < 3:
            return None
        p30 = sorted(us % 30000 for us, _ in b)[len(b) // 2]
        votes = [((us - p30 + 15000) // 30000 + ts) & 1 for us, ts in b]  # 0: TS1 at p30 (mod 60 ms)
        p = p30 if votes.count(0) >= votes.count(1) else p30 + 30000
        res = [wrap(us - p30, 30000) for us, _ in b]
        print('  %d bursts: TS1 at %d us mod 60 ms, within %.0f .. %.0f us, TS votes %d / %d' %
              (len(b), p, min(res), max(res), votes.count(0), votes.count(1)))
        return p

    print()
    p1, p2 = phase_of(dl1), phase_of(dl2)
    if p1 is None or p2 is None:
        print('too few output bursts with a sync')
        return
    print('drift between the output dwells: %+.0f us' % wrap(p2 - p1, FRAME))
    phase = (p1 + wrap(p2 - p1, FRAME) / 2) % FRAME
    ins = [(k, (us - phase) % FRAME) for (_, k, _, _, _, us) in ul if k in (3, 4) and us]
    if not ins:
        print('no input bursts with an MS sync')
        return
    print('input bursts (us after the output TS1 burst starts, the output dwells averaged):')
    for k, v in ins:
        print('  %-9s %8.0f  (%+.0f from TS1, %+.0f from TS2)' % (SYNC_NAMES[k], v, wrap(v, FRAME), wrap(v - 30000, FRAME)))
    vals = [v for _, v in ins]
    m = circ_mean(vals, FRAME)
    sd = (sum(wrap(v - m, FRAME) ** 2 for v in vals) / len(vals)) ** 0.5
    print('mean %.0f us (%+.0f from TS1, %+.0f from TS2), sd %.0f us, %d bursts' %
          (m, wrap(m, FRAME), wrap(m - 30000, FRAME), sd, len(vals)))
    print('output voice bursts (the repeater repeating): %d' %
          sum(1 for (_, k, _, _, _, _) in dl1 + dl2 if k == 1))


if __name__ == '__main__':
    main()
