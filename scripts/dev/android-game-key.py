#!/usr/bin/env python3
# 通过安卓输入发布按键，并等待原 UI 观测门的实际事件；不按前端意图冒充游戏响应。
# Publish an Android key and wait for an actual original UI observation event;
# frontend intent is not treated as a game response.
import argparse
import json
import re
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('key', nargs='?')
parser.add_argument('--tap', nargs=2, type=int, metavar=('X', 'Y'))
parser.add_argument('--wait', required=True)
parser.add_argument('--host-event', action='store_true',
                    help='also accept original host events, e.g. observed map-mode changes')
parser.add_argument('--hold-ms', type=int, default=0)
parser.add_argument('--timeout', type=float, default=60)
parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
args = parser.parse_args()
if (args.key is None) == (args.tap is None):
    parser.error('provide an Android key or --tap X Y')
if args.key is not None and not re.fullmatch(r'[A-Z0-9_]+', args.key):
    parser.error('invalid Android key')
if not 0 <= args.hold_ms <= 30000:
    parser.error('invalid Android key or hold duration')
if args.tap is not None and (args.hold_ms or any(not 0 <= n < 20000 for n in args.tap)):
    parser.error('tap coordinates must be physical pixels; --hold-ms is for keys')
pattern = re.compile(args.wait)
package = 'com.d2rmobile.nativeport'
path = 'files/runtime/native.log'
base = ['adb', '-s', args.serial]
def adb(*parts):
    return subprocess.check_output([*base, *parts], text=True, stderr=subprocess.STDOUT)


def wait_release():
    # 连续同键必须经过真实的松开采样，否则原游戏会把两下合成一次。
    # Consecutive identical keys need actual release samples or the game
    # treats both presses as one hold.
    baseline = None
    release_deadline = time.monotonic() + min(10, args.timeout)
    while time.monotonic() < release_deadline:
        lines = adb('exec-out', 'run-as', package, 'tail', '-n', '4',
                    'files/runtime/hid.jsonl').splitlines()
        for line in reversed(lines):
            try:
                sample = json.loads(line)
            except json.JSONDecodeError:
                continue
            if sample.get('event') == 'limit_reached':
                raise SystemExit('HID receipt log reached its explicit limit; restart the debug APK before input verification')
            if sample.get('event') != 'npad_return':
                continue
            if baseline is None:
                baseline = sample['sampling']
            if sample['sampling'] >= baseline + 2 and sample['buttons'] == 0:
                return
            break
        if not adb('shell', 'pidof', package).strip():
            raise SystemExit('native game process exited before key release')
        time.sleep(.2)
    raise SystemExit('UI event received, but neutral input samples did not arrive')
offset = int(adb('shell', 'run-as', package, 'stat', '-c', '%s', path))
if args.tap is not None:
    adb('shell', 'input', 'tap', *map(str, args.tap))
else:
    command = ['shell', 'input', 'keyevent']
    if args.hold_ms:
        command += ['--duration', str(args.hold_ms)]
    adb(*command, args.key)
deadline = time.monotonic() + args.timeout
pending = ''
while time.monotonic() < deadline:
    data = adb('exec-out', 'run-as', package, 'tail', '-c', f'+{offset+1}', path)
    offset += len(data.encode())
    pending += data
    complete = pending.split('\n')
    pending = complete.pop()
    for line in complete:
        if 'FATAL' in line or 'native fault' in line:
            raise SystemExit(line)
        if (args.host_event or 'UIMSG' in line) and pattern.search(line):
            wait_release()
            print(line)
            raise SystemExit(0)
    if not adb('shell', 'pidof', package).strip():
        raise SystemExit('native game process exited')
    time.sleep(0.4)
raise SystemExit('timed out waiting for original UI event: ' + args.wait)
