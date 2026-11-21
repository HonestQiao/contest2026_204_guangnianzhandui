#!/usr/bin/env python3
"""
ATK-DNN647 固件一键自测

用法（只需要这一条命令）:
    python3 一键自测.py

可选参数:
    python3 一键自测.py <串口> <固件> <入口地址>
    默认: /dev/cu.usbserial-1420  nuttx_nsh_restore.bin  341806c1

它会依次做两轮真机验证（每轮烧录一次）:
  第一轮 nsh_verify.py —— 启动 / NSH 基本命令 / userleds 设备节点
  第二轮 led_verify.py —— leds 全部子命令 + 非法参数校验

最后汇总成一句话结论。全程约 70 秒。
"""

import os
import glob
import re
import subprocess
import time
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-1420'
FIRMWARE = sys.argv[2] if len(sys.argv) > 2 else 'nuttx_nsh_restore.bin'
ENTRY = sys.argv[3] if len(sys.argv) > 3 else '341806c1'

PY = sys.executable or 'python3'


def banner(title):
    print('\n' + '=' * 64)
    print(f'  {title}')
    print('=' * 64)


def run_script(script, label):
    path = os.path.join(HERE, script)
    if not os.path.exists(path):
        print(f'  [FAIL] 找不到 {script}')
        return None, '', [], []

    banner(label)
    cmd = [PY, '-u', path, PORT, FIRMWARE, ENTRY, '6']

    # 上一轮刚关闭串口，稍等再打开，否则偶尔会卡住
    time.sleep(3.0)

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=150)
        out = (proc.stdout or '') + (proc.stderr or '')
    except subprocess.TimeoutExpired as e:
        out = ''
        for part in (e.stdout, e.stderr):
            if part:
                out += part.decode() if isinstance(part, bytes) else part
        print(out.rstrip())
        print(f'  [超时] {script} 超过 150 秒未完成')
        print('       多半是串口没有正确释放。拔插一次 USB 后重试。')
        return False, out, [], [f'{script} 超时']

    print(out.rstrip())

    passed = []      # 通过项
    failed = []      # 失败项
    for line in out.replace('\r', '\n').split('\n'):
        m = re.match(r'^\s*(\S.*?)\s*:\s*\d+B\s+expect .*?:\s*(OK|MISS)', line)
        if m:
            name = m.group(1).strip()
            if m.group(2) == 'OK':
                passed.append(name)
            else:
                failed.append(name)
    return ('ALL PASS' in out), out, passed, failed


def main():
    fw_path = os.path.join(HERE, FIRMWARE)
    if not os.path.exists(fw_path):
        print(f'找不到固件文件: {fw_path}')
        print('请先把固件放到本脚本同目录，或用第 2 个参数指定路径。')
        return 1

    # 先检查串口是否存在，给出可直接复制的可用端口列表
    if not os.path.exists(PORT):
        print(f'串口 {PORT} 不存在。')
        cands = sorted(glob.glob('/dev/cu.*')) + sorted(glob.glob('/dev/tty.usb*'))
        # 排除系统虚拟口，优先 USB 串口
        cands = [c for c in cands
                 if 'Bluetooth' not in c and 'debug-console' not in c]
        usb = [c for c in cands
               if any(k in c for k in ('usbserial', 'usbmodem', 'wchusbserial'))]
        others = [c for c in cands if c not in usb]

        if usb:
            print('\n检测到 USB 串口（板子应该用这个）:')
            for c in usb:
                print(f'  {c}')
            print(f'\n请运行:\n  python3 一键自测.py {usb[0]}')
        elif others:
            print('\n当前可用串口（未识别到 USB 串口，请确认板子已插好）:')
            for c in others:
                print(f'  {c}')
        else:
            print('\n当前没有检测到任何串口，请检查 USB 线是否插好。')
        return 1

    size = os.path.getsize(fw_path)
    print('=' * 64)
    print('  ATK-DNN647 固件一键自测')
    print('=' * 64)
    print(f'  串口: {PORT}   (115200 8E1 偶校验)')
    print(f'  固件: {FIRMWARE}  ({size} 字节)')
    print(f'  入口: 0x{ENTRY}')
    if size > 130000:
        print(f'  [警告] 固件 {size} 字节偏大，超过约 128KB 后 Boot ROM 可能拒绝启动')

    r1 = run_script('nsh_verify.py', '第一轮: 启动 + NSH + 设备节点')
    r2 = run_script('led_verify.py', '第二轮: LED 全部子命令')

    banner('结论')
    ok1 = bool(r1 and r1[0])
    ok2 = bool(r2 and r2[0])

    all_pass = []
    all_fail = []
    for r in (r1, r2):
        if r and len(r) >= 4:
            all_pass += r[2]
            all_fail += r[3]

    print(f'  通过 {len(all_pass)} 项，失败 {len(all_fail)} 项')
    if all_fail:
        print('  失败项:')
        for f in all_fail:
            print(f'    - {f}')

    print()
    if ok1 and ok2:
        print('  ✔ 固件有效')
        print('    启动链路、NSH 命令解析、userleds 设备节点、LED 全部子命令均正常。')
        print('    建议再肉眼确认一次: 运行 n6_console.py 后输入 leds 0 on / leds 1 on')
        return 0

    print('  ✘ 存在问题，请对照上面两轮的输出定位')
    if not ok1:
        print('    - 第一轮失败: 先确认串口是 8E1、固件确实是 nuttx_nsh_restore.bin')
    if not ok2:
        print('    - 第二轮失败: 若第一轮通过而本轮失败，多半是固件不含 LED 功能')
    return 2


if __name__ == '__main__':
    sys.exit(main())
