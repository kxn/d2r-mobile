#!/usr/bin/env python3
# 真触点驱动原摇杆，以原拍入口的区域/坐标记录结果，结束/异常必释放。
# Drive the original stick through real touches and report observed game state;
# release the touch on completion or error.
import argparse
import json
import math
import re
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('direction', choices=['n', 'ne', 'e', 'se', 's', 'sw', 'w', 'nw'])
p.add_argument('--seconds', type=float, default=8)
p.add_argument('--until-area', type=int)
p.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
a = p.parse_args()
if not 0 < a.seconds <= 60:
    p.error('seconds must be within (0, 60]')
base = ['adb', '-s', a.serial]
package = 'com.d2rmobile.nativeport'


def adb(*parts):
    return subprocess.check_output([*base, *parts], text=True, stderr=subprocess.STDOUT)


def state():
    text = adb('exec-out', 'run-as', package, 'tail', '-n', '1',
               'files/runtime/game-state.jsonl').strip()
    result = json.loads(text)
    if not result.get('valid'):
        raise RuntimeError('original game observer has no controlled player')
    return result


size = re.findall(r'(\d+)x(\d+)', adb('shell', 'wm', 'size'))
if not size:
    raise SystemExit('Android display size unavailable')
short, long = sorted(map(int, size[-1]))
origin = (round(short * .274), round(short * .742))
dx, dy = {'n': (0, -1), 'ne': (1, -1), 'e': (1, 0), 'se': (1, 1),
          's': (0, 1), 'sw': (-1, 1), 'w': (-1, 0), 'nw': (-1, -1)}[a.direction]
travel = short * .16 / math.hypot(dx, dy)
target = (round(origin[0] + dx * travel), round(origin[1] + dy * travel))
before = state()
print(json.dumps({'before': before, 'direction': a.direction}), flush=True)
last = before
reached = False
try:
    adb('shell', 'input', 'touchscreen', 'motionevent', 'DOWN', *map(str, origin))
    adb('shell', 'input', 'touchscreen', 'motionevent', 'MOVE', *map(str, target))
    deadline = time.monotonic() + a.seconds
    changed_at = time.monotonic()
    while time.monotonic() < deadline:
        time.sleep(.3)
        current = state()
        if current['time_ns'] != last['time_ns']:
            changed_at = time.monotonic()
        elif time.monotonic() - changed_at > 8:
            raise RuntimeError('original game state stopped advancing')
        last = current
        if a.until_area is not None and last['area'] == a.until_area:
            reached = True
            break
finally:
    adb('shell', 'input', 'touchscreen', 'motionevent', 'UP', *map(str, target))
print(json.dumps({'after': last, 'target_area_reached': reached,
                  'moved': (last['x'], last['y']) != (before['x'], before['y'])}), flush=True)
if a.until_area is not None and not reached:
    raise SystemExit(2)
