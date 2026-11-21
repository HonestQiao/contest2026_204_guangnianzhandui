#!/usr/bin/env python3
"""Correct NSH interaction verifier.

Key fixes vs the broken n6_e2e.py:
  1. Flash + capture 6s FIRST so NSH fully initializes before sending commands.
  2. Use the SAME serial session as the flasher (no reopen -> no framer reset).
  3. Send '\\r' (not '\\r\\n'); NSH accepts CR.
  4. dtr/rts=False after flash (avoid boot-pin disturbance).
  5. Decode: print raw latin-1 (tick noise b3/b4 is harmless; real ASCII is clean).

Usage: /usr/local/bin/python3 nsh_verify.py [PORT] [IMAGE] [ENTRY] [CAPTURE]
"""
import sys
import os
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-1420'
IMAGE = sys.argv[2] if len(sys.argv) > 2 else 'nuttx_bootrom.bin'
ENTRY = sys.argv[3] if len(sys.argv) > 3 else '341806c1'
CAPTURE = int(sys.argv[4]) if len(sys.argv) > 4 else 6

# Reuse n6_flash's exact Rom class / build_image by exec-ing its module up to main().
src = open('n6_flash.py').read()
mod = {}
exec(compile(src[:src.index('def main():')], 'n6_flash', 'exec'), mod)

build_image = mod['build_image']
Rom = mod['Rom']
cksum = mod['cksum']
FSBL = 'fsbl.bin'


def main():
    payload = open(IMAGE, 'rb').read()
    fsbl = open(FSBL, 'rb').read()
    img = build_image(payload, fsbl, int(ENTRY, 16))
    n = len(img) // 256
    print(f'image {len(img)}B, {n} blocks', flush=True)
    rom = Rom(PORT)
    s = rom.s

    # enter boot ROM
    s.dtr = True; s.rts = True; time.sleep(0.15)
    s.dtr = False; s.rts = False; time.sleep(0.3)
    s.reset_input_buffer()
    ok = False
    for _ in range(5):
        s.write(b'\x7f')
        if rom.rx_ack() == 0x79:
            ok = True; break
        time.sleep(0.2)
    if not ok:
        print('sync FAILED'); return 1
    for c in (0x02, 0x00, 0x03):
        s.write(bytes([c, c ^ 0xFF])); time.sleep(0.05); rom.rx(16, 0.3)

    fails = 0
    for i in range(n):
        data = img[i*256:(i+1)*256]
        idx = i.to_bytes(4, 'big')
        good = False
        for _ in range(8):
            s.write(bytes([0x31, 0x31 ^ 0xFF]))
            if rom.rx_ack() != 0x79: continue
            s.write(idx + bytes([cksum(idx)]))
            if rom.rx_ack() != 0x79: continue
            s.write(bytes([0xFF]) + data + bytes([0xFF ^ cksum(data)]))
            if rom.rx_ack() == 0x79:
                good = True; break
        if not good: fails += 1
    print(f'downloaded {n} blocks, fails={fails}', flush=True)

    s.write(bytes([0x21, 0x21 ^ 0xFF])); a1 = rom.rx_ack()
    s.write(b'\xff\xff\xff\xff\x00'); a2 = rom.rx_ack()
    print(f'start ACKs: {a1!r} {a2!r}', flush=True)
    if fails or a1 != 0x79 or a2 != 0x79: return 1

    # capture boot trace (NSH needs ~6s to fully init)
    boot = b''
    end = time.time() + CAPTURE
    while time.time() < end:
        d = s.read(512)
        if d: boot += d
    bt = boot.decode('latin-1')
    print(f'boot trace: {len(boot)}B  NuttShell={"NuttShell" in bt}  nsh>={"nsh>" in bt}', flush=True)
    if os.environ.get('N6_SHOW_BOOT'):
        print('--- boot trace raw ---', flush=True)
        print(repr(boot), flush=True)
        print('--- end ---', flush=True)

    # same session, dtr/rts low, send commands with CR
    s.dtr = False; s.rts = False
    time.sleep(0.3); s.reset_input_buffer()
    allpass = True
    checks = [
        (b'\r', 'nsh>'),
        (b'help\r', 'help usage'),
        (b'uname -a\r', 'NuttX'),
        (b'free\r', 'Umem'),
        (b'ls\r', 'nsh>'),
        (b'ps\r', 'STACK COMMAND'),
        (b'ls /dev/userleds\r', '/dev/userleds'),
    ]
    for cmd, expect in checks:
        s.reset_input_buffer(); s.write(cmd)
        resp = b''
        e = time.time() + 2.5
        while time.time() < e:
            d = s.read(1024)
            if d: resp += d
        txt = resp.decode('latin-1')
        hit = expect in txt
        # negative check: a "command not found" means the test itself is bogus
        if 'command not found' in txt:
            hit = False
            print('   !! command not found -- assertion rejected', flush=True)
        allpass = allpass and hit
        print(f'{cmd.decode().strip():10s}: {len(resp):5d}B  expect {expect!r}: {"OK" if hit else "MISS"}', flush=True)
        if cmd == b'help\r' and hit:
            print('   ' + txt[:120].replace('\r', '\\r').replace('\n', '\\n'), flush=True)
        if cmd == b'uname -a\r' and hit:
            line = [l for l in txt.split('\r\n') if 'NuttX' in l]
            if line: print('   ' + line[0], flush=True)
    s.close()
    print(f'\nRESULT: {"ALL PASS" if allpass else "SOME FAILED"}', flush=True)
    return 0 if allpass else 2


sys.exit(main())
