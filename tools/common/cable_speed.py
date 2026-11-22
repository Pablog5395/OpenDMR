"""The programming cable's speed with the running firmware (src/services/cable_link.c 'D' 'S' / 'D' 'Y'), shared by the
tools that write over it (lang_tool.py, id_db.py, write_keps.py).

--baud auto (the default): the fastest speed the cable keeps, tried from 750000 down and checked with short requests;
slower again when writes fail there. --baud <number>: that speed for everything, or the tool stops when the radio
doesn't keep it. Either way the radio is asked back to 115200 at the end (it keeps a confirmed speed until 5 s
without a request, and the next tool would start at 115200).

The radio's side: 'D' 'S' + divisor (big endian, of the 84 MHz / 16 UART clock) answered 'D' 'S' 1 at the old speed;
the new one is kept once 'D' 'Y' comes at it, else back to 115200 after 0.4 s.
"""
import struct
import time

UART_CLOCK = 84000000 // 16    # the radio's UART: baud = UART_CLOCK / divisor
BAUD = 115200                  # where the radio starts, and goes back to
SPEED_DIVISORS = (7, 14, 21)   # auto: 750000, 375000, 250000 baud (12 MHz / n for a CH340 too)
MIN_DIVISOR = 4                # cable_link.c SPEED_MIN_DIVISOR: 1.3 Mbaud
UNCONFIRMED_BACK_S = 0.5       # an unconfirmed speed: the radio is back at 115200 after 0.4 s
QUIET_BACK_S = 5.5             # a confirmed one: after 5 s without a request


def divisor_of(baud):
    return (UART_CLOCK + (baud // 2)) // baud  # as the firmware's uart0_divisor()


def parse_baud(text):
    """'auto', or a speed the radio's UART can make (argparse type)"""
    if text == 'auto':
        return text
    try:
        baud = int(text)
    except ValueError:
        raise ValueError('--baud: auto or a number') from None
    if not MIN_DIVISOR <= divisor_of(baud) <= divisor_of(BAUD):
        raise ValueError('--baud %d: the radio takes %d to %d' % (baud, BAUD, UART_CLOCK // MIN_DIVISOR))
    return baud


def add_argument(parser):
    def baud_type(text):
        import argparse
        try:
            return parse_baud(text)
        except ValueError as e:
            raise argparse.ArgumentTypeError(str(e)) from None
    parser.add_argument('--baud', type=baud_type, default='auto',
                        help='auto (default: the fastest the cable keeps) or a speed, e.g. 115200, 375000')


def _ask(port, divisor, timeout):
    port.reset_input_buffer()
    port.write(b'DS' + struct.pack('>H', divisor))
    old = port.timeout
    port.timeout = timeout  # a late 'D' 'S' would change the radio's speed behind our back
    answer = port.read(3)
    port.timeout = old
    return answer == b'DS\x01'


def _confirm(port):
    port.reset_input_buffer()
    port.write(b'DY')
    return port.read(2) == b'DY'


def speed_down(port):
    """Back to 115200: asked for and confirmed at it; else waited for (the radio goes back by itself)"""
    if port.baudrate == BAUD:
        return
    asked = _ask(port, divisor_of(BAUD), 0.5)
    time.sleep(0.02)
    port.baudrate = BAUD
    time.sleep(0.05)
    if asked and _confirm(port):
        return
    time.sleep(QUIET_BACK_S)


def set_speed(port, divisor, check, checks=4):
    """That speed asked, taken at our end, confirmed, then check() (a short request answered at it) passing checks
    times. False: back at 115200 (asked back when the radio had confirmed it)."""
    if divisor == divisor_of(port.baudrate):
        return True
    if port.baudrate != BAUD:
        speed_down(port)
    if divisor == divisor_of(BAUD):
        return True
    if not _ask(port, divisor, 1.0):
        return False
    time.sleep(0.02)
    port.baudrate = UART_CLOCK // divisor
    time.sleep(0.05)
    confirmed = _confirm(port)
    if confirmed and all(check() for _ in range(checks)):
        return True
    if confirmed:
        speed_down(port)
    else:
        port.baudrate = BAUD
        time.sleep(UNCONFIRMED_BACK_S)
    return False


def speed_up(port, check, below=None):
    """auto: the fastest speed both ends keep, slower than below if given; 115200 when none"""
    for divisor in SPEED_DIVISORS:
        if (below is None or UART_CLOCK // divisor < below) and set_speed(port, divisor, check):
            return port.baudrate
    if port.baudrate != BAUD:
        speed_down(port)
    return BAUD


def start(port, baud, check):
    """--baud: auto negotiated, or the speed given (SystemExit when the radio doesn't keep it); the speed in use"""
    if baud == 'auto':
        return speed_up(port, check)
    if not set_speed(port, divisor_of(baud), check):
        raise SystemExit('the radio did not keep %d baud on this cable (try a slower one, or auto)' % baud)
    return port.baudrate


def slower(port, baud, check):
    """auto, after writes failed at this speed: the next slower one (115200 at the end); a fixed speed is kept"""
    if baud != 'auto' or port.baudrate == BAUD:
        return port.baudrate
    below = port.baudrate
    speed_down(port)
    return speed_up(port, check, below)
