"""Builds and runs the host tests for the YSF frame, TX, voice and C4FM demodulator layers.

  python tools/ysf/run.py          GCC=... to use another gcc
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
SERVICES = os.path.join(ROOT, 'src', 'services')
AMBE = os.path.join(ROOT, 'vocoder', 'c', 'ambe')
IMBE = os.path.join(ROOT, 'p25', 'c', 'imbe')
GCC = os.environ.get('GCC', 'gcc')

CORE = [
    os.path.join(SERVICES, 'ysf', 'ysf_frame.c'),
    os.path.join(SERVICES, 'ysf', 'ysf_tx.c'),
    os.path.join(SERVICES, 'ysf', 'ysf_voice.c'),
    os.path.join(AMBE, 'ambe_frame.c'),
    os.path.join(AMBE, 'ambe_tables.c'),
    # VW's 18-byte VCH is an IMBE frame: the tests use the same frame layer as the firmware's P25 overlay.
    os.path.join(IMBE, 'imbe_frame.c'),
    os.path.join(IMBE, 'imbe_parms.c'),
    os.path.join(IMBE, 'imbe_dec.c'),
    os.path.join(IMBE, 'imbe_tables.c'),
]

TESTS = [
    ('test_ysf', [os.path.join(HERE, 'test_ysf.c')] + CORE),
    ('test_ysf_demod', [os.path.join(HERE, 'test_ysf_demod.c'),
                        os.path.join(SERVICES, 'ysf_demod.c'),
                        os.path.join(SERVICES, 'fsk4.c')] + CORE),
]


def main():
    for name, sources in TESTS:
        exe = os.path.join(tempfile.gettempdir(), name)
        subprocess.run([GCC, '-O2', '-Wall', '-Wextra', '-Werror', '-I', SERVICES,
                        '-I', os.path.join(SERVICES, 'ysf'), '-I', AMBE, '-I', IMBE,
                        '-o', exe] + sources, check=True)
        result = subprocess.run([exe])
        if result.returncode:
            sys.exit(result.returncode)
    sys.exit(0)


if __name__ == '__main__':
    main()
