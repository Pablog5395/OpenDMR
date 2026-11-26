"""Builds and runs the host test for the YSF frame, TX sequence and voice layers.

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
GCC = os.environ.get('GCC', 'gcc')

SOURCES = [
    os.path.join(HERE, 'test_ysf.c'),
    os.path.join(SERVICES, 'ysf', 'ysf_frame.c'),
    os.path.join(SERVICES, 'ysf', 'ysf_tx.c'),
    os.path.join(SERVICES, 'ysf', 'ysf_voice.c'),
    os.path.join(AMBE, 'ambe_frame.c'),
    os.path.join(AMBE, 'ambe_tables.c'),
]


def main():
    exe = os.path.join(tempfile.gettempdir(), 'test_ysf')
    subprocess.run([GCC, '-O2', '-Wall', '-Wextra', '-Werror', '-I', SERVICES, '-I', AMBE,
                    '-o', exe] + SOURCES, check=True)
    sys.exit(subprocess.run([exe]).returncode)


if __name__ == '__main__':
    main()
