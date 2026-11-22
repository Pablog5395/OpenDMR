#!/usr/bin/env python3
"""Languages and fonts in the radio over the cable, as the CPS's Languages page does it (docs/i18n.en.md):

    py tools/lang/lang_tool.py COM23 list
    py tools/lang/lang_tool.py COM23 add-lang zh.json
    py tools/lang/lang_tool.py COM23 add-font cjk.bin
    py tools/lang/lang_tool.py COM23 del-lang zh
    py tools/lang/lang_tool.py COM23 del-font cjk
    py tools/lang/lang_tool.py COM23 --baud 375000 add-font cjk.bin

--baud: auto (the default) reads at 115200 and writes at the fastest speed the cable keeps ('D' 'S' / 'D' 'Y' and
sector CRCs), slower again when a sector fails twice; a number (115200, 250000, 375000, 750000 ...) is used for
everything, or the tool stops when the radio does not keep it. The radio is put back to 115200 at the end.

The firmware's side: 'R' area 10 (raw reads of the area), 'X' with the raw sector bit (writes), 'D' 'K' with the raw
address bit (sector CRC), 'C' 8 (read the area again) in src/services/cable_link.c.
"""
import os
import struct
import sys
import time
import zlib

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'text'))
sys.path.insert(0, os.path.join(HERE, '..', 'common'))
import langfile  # noqa: E402
import cable_speed  # noqa: E402

SECTOR = 4096
LANGUAGE_ADDRESS = 0x280000  # language.h
SLOT_SIZE = 0x4000
SLOTS = 8
FONT_DIRECTORY = 0x2A0000   # font_cjk.h
FONT_ADDRESS = 0x2A1000
FONT_END = 0x630000
FILES_MAX = 16
AREA_LANGUAGES = 10
RAW_SECTOR = 0x800000
RAW_CRC = 0x80000000
CHUNK = 1024


class Link:
    def __init__(self, port, baud='auto'):
        self.p = serial.Serial(port, cable_speed.BAUD, timeout=0.2)
        self.baud = baud   # --baud: 'auto' or a number (cable_speed.py)
        self.fast = False  # the write speed chosen (auto), or the fixed speed set
        time.sleep(0.1)
        self.p.reset_input_buffer()
        if baud != 'auto':  # a fixed speed: for the reads too
            print('link at %d baud' % cable_speed.start(self.p, baud, self.check))
            self.fast = True

    def close(self):
        """The radio back at 115200 (else it stays at the fast speed 5 s, and the next run gets no reply)"""
        try:
            cable_speed.speed_down(self.p)
        finally:
            self.p.close()

    def check(self):
        """A short request answered: the speed works"""
        try:
            self.sector_crc(FONT_DIRECTORY, 0.5)
            return True
        except IOError:
            return False

    def _receive(self, n, timeout=3.0):
        buf = b''
        end = time.time() + timeout
        while len(buf) < n and time.time() < end:
            buf += self.p.read(n - len(buf))
            if buf[:1] == b'-':
                raise IOError('refused')
        if len(buf) < n:
            raise IOError('no reply')
        return buf

    def read(self, address, length):
        out = b''
        while length > 0:
            n = min(length, 1024)
            self.p.write(struct.pack('>cBIH', b'R', AREA_LANGUAGES, address, n))
            head = self._receive(3)
            got = (head[1] << 8) | head[2]
            out += self._receive(got)
            address += got
            length -= got
        return out

    def _expect(self, request, reply):
        self.p.write(request)
        if self._receive(len(reply)) != reply:
            raise IOError('bad reply to %r' % request[:2])

    def sector_crc(self, address, timeout=3.0):
        self.p.write(b'DK' + struct.pack('>I', address | RAW_CRC))
        r = self._receive(6, timeout)
        return struct.unpack('>I', r[2:])[0]

    def write_sector(self, address, data):
        assert len(data) == SECTOR and address % SECTOR == 0
        if not self.fast:  # auto: faster for the writes only (a CH340 loses bytes of long replies at 375000 and up)
            self.fast = True
            print('link at %d baud' % cable_speed.start(self.p, self.baud, self.check))
        for attempt in range(6):
            try:
                self._expect(bytes([0x58, 1]) + ((address // SECTOR) | RAW_SECTOR).to_bytes(3, 'big'), b'X\x01')
                for off in range(0, SECTOR, CHUNK):
                    self._expect(bytes([0x58, 2]) + struct.pack('>IH', address + off, CHUNK) + data[off:off + CHUNK],
                                 b'X\x02')
                self._expect(bytes([0x58, 3]), b'X\x03')
                if self.sector_crc(address) == zlib.crc32(data):
                    return
                raise IOError('read back different')
            except IOError as e:
                print('  sector 0x%06X: %s, again' % (address, e))
                time.sleep(0.3)  # a request cut short: the radio drops it after 0.2 s
                self.p.reset_input_buffer()
                if (attempt % 2 == 1) and (self.baud == 'auto') and (self.p.baudrate != cable_speed.BAUD):
                    print('  link at %d baud' % cable_speed.slower(self.p, self.baud, self.check))  # twice: slower
        raise IOError('sector 0x%06X not written' % address)

    def reload(self):
        self.p.reset_input_buffer()
        self.p.write(bytes([0x43, 8]))
        self.p.read(1)  # '-', as for every 'C'
        self.p.write(bytes([0x43, 5]))  # the radio's CPS screen closed, as the CPS does at the end
        self.p.read(1)


def pad(data):
    return data + b'\xFF' * (-len(data) % SECTOR)


def cstr(b):
    return b.split(b'\0', 1)[0].decode('utf-8', 'replace')


def read_slots(link):
    slots = []
    for i in range(SLOTS):
        h = link.read(LANGUAGE_ADDRESS + (i * SLOT_SIZE), langfile.HEADER_SIZE)
        if h[:8] == langfile.MAGIC:
            slots.append({'slot': i, 'code': cstr(h[16:24]), 'name': cstr(h[24:56]),
                          'english_name': cstr(h[56:80]), 'font': cstr(h[80:88]),
                          'count': struct.unpack('<H', h[88:90])[0], 'length': struct.unpack('<I', h[8:12])[0]})
        else:
            slots.append(None)
    return slots


def read_directory(link):
    d = link.read(FONT_DIRECTORY, 12 + (FILES_MAX * 20))
    if d[:8] != b'DM32FDIR':
        return []
    out = []
    for i in range(min(d[8], FILES_MAX)):
        e = d[12 + (i * 20):32 + (i * 20)]
        address, length, crc = struct.unpack('<III', e[8:])
        out.append({'name': cstr(e[:8]), 'address': address, 'length': length, 'crc': crc})
    return out


def write_directory(link, files):
    d = b'DM32FDIR' + struct.pack('<B3x', len(files))
    for f in files:
        d += f['name'].encode('ascii').ljust(8, b'\0') + struct.pack('<III', f['address'], f['length'], f['crc'])
    link.write_sector(FONT_DIRECTORY, pad(d))


def write_area(link, address, data, what):
    data = pad(data)
    t = time.time()
    for off in range(0, len(data), SECTOR):
        link.write_sector(address + off, data[off:off + SECTOR])
        if (off // SECTOR) % 32 == 31:
            print('  %s: %d / %d KB' % (what, (off + SECTOR) // 1024, len(data) // 1024))
    print('  %s: %d KB in %.0f s' % (what, len(data) // 1024, time.time() - t))


def cmd_list(link):
    for s in read_slots(link):
        if s:
            print('slot %d: %s %s (%s), %d strings, %d bytes, font "%s"' % (
                s['slot'], s['code'], s['name'], s['english_name'], s['count'], s['length'], s['font']))
    used = 0
    for f in read_directory(link):
        print('font %s: 0x%06X, %d bytes' % (f['name'], f['address'], f['length']))
        used += len(pad(b'\0' * f['length']))
    print('font space: %d of %d KB used' % (used // 1024, (FONT_END - FONT_ADDRESS) // 1024))


def cmd_add_lang(link, path):
    doc = langfile.load(path)
    english = langfile.load(os.path.join(HERE, '..', 'text', 'languages', 'en.json'))
    errors, warnings = langfile.check(doc, english)
    for line in warnings:
        print(line)
    if errors:
        raise SystemExit('\n'.join(errors))
    slot_data = langfile.build_slot(doc)
    slots = read_slots(link)
    same = [s for s in slots if s and s['code'] == doc['code']]
    free = [i for i, s in enumerate(slots) if s is None]
    slot = same[0]['slot'] if same else (free[0] if free else None)
    if slot is None:
        raise SystemExit('no free slot: delete a language first')
    if doc.get('font') and doc['font'] not in [f['name'] for f in read_directory(link)]:
        print('note: font "%s" is not in the radio: the menu shows "%s" until it is' % (doc['font'],
                                                                                         doc['english_name']))
    address = LANGUAGE_ADDRESS + (slot * SLOT_SIZE)
    data = pad(slot_data)
    # the header's sector last: cut half way, the slot is not used
    write_area(link, address + SECTOR, data[SECTOR:], doc['code']) if len(data) > SECTOR else None
    link.write_sector(address, data[:SECTOR])
    link.reload()
    print('%s written to slot %d' % (doc['code'], slot))


def cmd_add_font(link, path):
    data = open(path, 'rb').read()
    if data[:8] != b'DM32FNT1' or struct.unpack('<I', data[8:12])[0] != len(data) or \
            zlib.crc32(data[64:]) != struct.unpack('<I', data[12:16])[0]:
        raise SystemExit('%s is not a font file (or it is damaged)' % path)
    name = cstr(data[16:24])
    files = read_directory(link)
    if any(f['name'] == name for f in files):
        files = [f for f in files if f['name'] != name]
        write_directory(link, files)  # not used while it is rewritten
    if len(files) >= FILES_MAX:
        raise SystemExit('the font directory is full')
    size = len(pad(data))
    address = FONT_ADDRESS
    for f in sorted(files, key=lambda f: f['address']):
        if address + size <= f['address']:
            break
        address = max(address, f['address'] + len(pad(b'\0' * f['length'])))
    if address + size > FONT_END:
        raise SystemExit('no room: %d KB needed' % (size // 1024))
    write_area(link, address, data, name)
    files.append({'name': name, 'address': address, 'length': len(data), 'crc': zlib.crc32(data) & 0xFFFFFFFF})
    write_directory(link, files)
    link.reload()
    print('font %s written at 0x%06X' % (name, address))


def cmd_del_lang(link, code):
    for s in read_slots(link):
        if s and s['code'] == code:
            link.write_sector(LANGUAGE_ADDRESS + (s['slot'] * SLOT_SIZE), b'\xFF' * SECTOR)
            link.reload()
            print('%s deleted' % code)
            return
    raise SystemExit('%s is not in the radio' % code)


def cmd_del_font(link, name):
    files = read_directory(link)
    if not any(f['name'] == name for f in files):
        raise SystemExit('%s is not in the radio' % name)
    write_directory(link, [f for f in files if f['name'] != name])
    link.reload()
    print('%s deleted' % name)


def main():
    argv = sys.argv[1:]
    baud = 'auto'
    if '--baud' in argv:
        i = argv.index('--baud')
        try:
            baud = cable_speed.parse_baud(argv[i + 1] if i + 1 < len(argv) else '')
        except ValueError as e:
            raise SystemExit(e)
        del argv[i:i + 2]
    commands = {'list': cmd_list, 'add-lang': cmd_add_lang, 'add-font': cmd_add_font,
                'del-lang': cmd_del_lang, 'del-font': cmd_del_font}
    if len(argv) < 2 or argv[1] not in commands:
        raise SystemExit(__doc__)
    link = Link(argv[0], baud)
    try:
        commands[argv[1]](link, *argv[2:])
    except IOError as e:
        hint = ' (this cable at %d baud: try a slower --baud, or auto)' % baud if baud != 'auto' else ''
        raise SystemExit('the radio: %s%s' % (e, hint))
    finally:
        link.close()


if __name__ == '__main__':
    main()
