#!/usr/bin/env python3
"""The way back from a forgotten PIN (General options -> Power-on PIN / Lock PIN): the PIN cleared.

  py tools/debug/pin_wipe.py COM6

The firmware answers nothing on the cable while it waits for its PIN, so this goes through the chip's bootloader at
power on, as tools/backup/dm32_dump.py does: switch the radio off, start this, then switch the radio on (no key)
within 30 s. It reads the 4 KB sector of the PIN block (src/services/security.c, at 0x6100 of the firmware's data
flash window), blanks the block only, writes the sector back and reads it again. Then switch the radio off and on:
no PIN, no lock. The codeplug and the settings stay as they are.
"""
import argparse
import os
import struct
import sys
import zlib

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'backup'))
import dm32_dump as dump  # noqa: E402  the bootloader handshake and the reader

WINDOW = 0x700000  # src/drivers/flash.h FLASH_WINDOW_BASE
BLOCK_AT = 0x6100  # src/services/security.c ADDRESS
BLOCK_SIZE = 24    # its block_t (the salted hash of the PIN, the switches)
SECTOR = WINDOW + (BLOCK_AT & ~(dump.BLOCK - 1))
OFFSET = BLOCK_AT % dump.BLOCK
DATA = dump.CHIPS['data']


def fail(screen, text, why):
    """Shown on the radio too (it stays in the bootloader's reader: switch it off and on)."""
    screen.line('chip', text, dump.RED)
    screen.line('left', 'Switch off and on', dump.RED)
    sys.exit(why)


def read_sector(port, screen):
    for _ in range(5):
        try:
            data = dump.read_blocks(port, DATA, SECTOR, 1)[0]
        except IOError:
            dump.drain(port)
            continue
        if data is not None:
            return data
    fail(screen, 'Read failed', 'the PIN sector could not be read')


def write_sector(port, screen, data):
    for _ in range(3):
        port.write(b'X' + bytes([DATA]) + struct.pack('>I', SECTOR) + data + struct.pack('>I', zlib.crc32(data)))
        if port.read(1) == b'K':
            return
        dump.drain(port)
    fail(screen, 'Write failed', 'the PIN sector could not be written')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('port', help='the programming cable, e.g. COM6')
    args = parser.parse_args()

    port = serial.Serial(args.port, dump.SLOW, timeout=2)
    if hasattr(port, 'set_buffer_size'):
        port.set_buffer_size(rx_size=1 << 20)
    print('switch the radio off, then on (no key)')
    dump.wait_for_bootloader(port)
    with open(dump.DUMPER, 'rb') as f:
        dump.upload(port, f.read())
    port.timeout = 3
    divisor = dump.hello(port)
    if divisor is None:
        sys.exit('the reader did not start')
    dump.speed_up(port, divisor, 250000)

    screen = dump.Screen(port)  # the radio's screen: what goes on, as the backup tool shows it
    screen.line('title', 'PIN reset')
    screen.line('chip', 'Reading', dump.GREY)
    sector = read_sector(port, screen)
    block = sector[OFFSET:OFFSET + BLOCK_SIZE]
    if block == b'\xff' * BLOCK_SIZE:
        screen.line('chip', 'No PIN set', dump.GREEN)
        screen.line('left', 'Switch off and on', dump.GREEN)
        print('no PIN block: nothing to clear')
        return
    screen.line('chip', 'Clearing the PIN', dump.GREY)
    sector = sector[:OFFSET] + b'\xff' * BLOCK_SIZE + sector[OFFSET + BLOCK_SIZE:]
    write_sector(port, screen, sector)
    if read_sector(port, screen) != sector:
        fail(screen, 'Check failed', 'the PIN sector reads back different: run again')
    screen.line('title', 'PIN cleared', dump.GREEN)
    screen.line('chip', 'Codeplug kept', dump.GREEN)
    screen.line('left', 'Switch off and on', dump.GREEN)
    print('the PIN cleared (the codeplug and the settings kept): switch the radio off and on')


if __name__ == '__main__':
    main()
