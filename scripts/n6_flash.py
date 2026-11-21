#!/usr/bin/env python3
"""Robust STM32N6 serial-boot flasher (payload -> UART).

Image layout handed to the boot ROM:
  [0x000..0x240)  576-byte STM32 image header (copied from the ATK FSBL)
  [0x240..end)   payload, padded so that the TOTAL length is a multiple
                 of 256.

Why the padding matters: the boot ROM only accepts 256-byte blocks that
lie *fully* inside the declared image length (576 + length field).  With a
576-byte header the total is normally not block aligned, so the last block
is a short one and the ROM NAKs it - which leaves the tail of the image
stale (previous build's bytes) in RAM.  Aligning the total to 256 makes
every block full and every block accepted.

Every ROM reply is read with an exact-length, retrying reader; the older
tool used a single read(1) that could return empty and desynchronised the
protocol, silently dropping chunks.
"""
import struct
import sys
import termios
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-1420'
PAYLOAD = sys.argv[2] if len(sys.argv) > 2 else 'nuttx_bootrom.bin'
FSBL = sys.argv[3] if len(sys.argv) > 3 else 'fsbl.bin'
ENTRY = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x34180401
CAPTURE = int(sys.argv[5]) if len(sys.argv) > 5 else 60

HDR_SIZE = 0x240


def cksum(data):
    c = 0
    for b in data:
        c ^= b
    return c


def set_line_8n1(fd):
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[3] = 0
    a[2] &= ~termios.PARENB
    a[2] &= ~termios.CSTOPB
    a[2] = (a[2] & ~termios.CSIZE) | 0o60
    a[2] &= ~termios.CRTSCTS
    termios.tcsetattr(fd, termios.TCSANOW, a)


def build_image(payload, fsbl, entry):
    # Pad the payload so that (header + payload) is block aligned.
    total = HDR_SIZE + len(payload)
    pad = (-total) % 256
    payload = payload + bytes(pad)
    hdr = bytearray(fsbl[:HDR_SIZE])
    struct.pack_into('<I', hdr, 0x6c, len(payload))       # payload length
    struct.pack_into('<I', hdr, 0x70, entry)               # entry point
    struct.pack_into('<I', hdr, 0x64, sum(payload) & 0xffffff)  # checksum
    return bytes(hdr) + payload


class Rom:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, parity=serial.PARITY_EVEN,
                               timeout=0.05)
        self.buf = b''

    def rx(self, n=1, timeout=2.0):
        end = time.time() + timeout
        while len(self.buf) < n and time.time() < end:
            d = self.s.read(n - len(self.buf))
            if d:
                self.buf += d
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def rx_ack(self, timeout=2.0):
        end = time.time() + timeout
        while time.time() < end:
            b = self.rx(1, 0.2)
            if b and b[0] in (0x79, 0x5F, 0x95):
                return b[0]
        return None


def main():
    payload = open(PAYLOAD, 'rb').read()
    fsbl = open(FSBL, 'rb').read()
    img = build_image(payload, fsbl, ENTRY)
    n = len(img) // 256
    assert len(img) % 256 == 0
    print(f'image: {len(img)} bytes = {n} blocks '
          f'(payload {len(payload) - len(payload) % 256 + (len(payload) % 256)} '
          f'-> padded {len(img) - HDR_SIZE})')

    rom = Rom(PORT)
    s = rom.s

    # Enter the boot ROM serial download mode (NRST pulse + BOOT0 high).
    s.dtr = True
    s.rts = True
    time.sleep(0.15)
    s.dtr = False
    s.rts = False
    time.sleep(0.3)
    rom.buf = b''
    s.reset_input_buffer()

    for attempt in range(5):
        s.write(b'\x7f')
        if rom.rx_ack() == 0x79:
            print('sync OK')
            break
        print(f'  sync attempt {attempt} failed')
        time.sleep(0.2)
    else:
        print('sync FAILED')
        return 1

    for c in (0x02, 0x00, 0x03):
        s.write(bytes([c, c ^ 0xFF]))
        time.sleep(0.05)
        print(f'query {c:02x}:', rom.rx(16, 0.3).hex(' '))

    fails = 0
    for i in range(n):
        data = img[i * 256:(i + 1) * 256]
        idx = i.to_bytes(4, 'big')
        ok = False
        for attempt in range(8):
            s.write(bytes([0x31, 0x31 ^ 0xFF]))
            if rom.rx_ack() != 0x79:
                continue
            s.write(idx + bytes([cksum(idx)]))
            if rom.rx_ack() != 0x79:
                continue
            s.write(bytes([0xFF]) + data + bytes([0xFF ^ cksum(data)]))
            ack = rom.rx_ack()
            if ack == 0x79:
                ok = True
                break
        if not ok:
            fails += 1
            print(f'  BLOCK {i} FAILED (last ack {ack!r})')
    print(f'downloaded {n} blocks, failures={fails}')

    s.write(bytes([0x21, 0x21 ^ 0xFF]))
    a1 = rom.rx_ack()
    s.write(b'\xff\xff\xff\xff\x00')
    a2 = rom.rx_ack()
    print('start ACKs:', hex(a1) if a1 else None, hex(a2) if a2 else None)

    # The boot ROM's download protocol is 8E1, but the USB-serial adapter
    # ignores the termios parity flag, so the wire is really 8N1 once the
    # firmware runs.  Switch the host line format so both ends use the same
    # frame length (otherwise the receiver samples a drifting bit phase).
    # b288 rollback: no format switch; host stays PARITY_EVEN (8E1)

    if fails or a1 != 0x79 or a2 != 0x79:
        return 1

    # The firmware runs 8E1 (the boot ROM's own format + BRR), so the host
    # stays at 8E1 throughout - no format switch, no byte loss.  The echo
    # diagnostic window in the firmware delays the boot by ~1-2 s; this
    # capture covers it and the whole boot trace.
    buf = b''
    deadline = time.time() + CAPTURE
    end = deadline
    last_save = time.time()
    while time.time() < end:
        d = s.read(512)
        if d:
            buf += d
        if time.time() - last_save > 5:
            open('/tmp/n6_trace_raw.bin', 'wb').write(buf)
            last_save = time.time()
            end = min(time.time() + 8, deadline)
    open('/tmp/n6_trace_raw.bin', 'wb').write(buf)
    print(f'trace: {len(buf)} bytes', flush=True)
    if buf:
        print(buf.hex(' ')[:600])
    s.close()
    return 0


sys.exit(main())
