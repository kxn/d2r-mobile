#!/usr/bin/env python3
# 沿原 front_ios.m walk_to 的方向公式，以实际坐标闭环推真实 Android 摇杆。
# Use original front_ios.m's walk_to direction formula and observed coordinates
# to drive the real Android touch stick. This never teleports the character.
import argparse
import json
import math
import re
import subprocess
import time

from game_state_json import latest_complete_state

p = argparse.ArgumentParser()
p.add_argument('x', type=int)
p.add_argument('y', type=int)
p.add_argument('--radius', type=float, default=4)
p.add_argument('--timeout', type=float, default=20)
p.add_argument('--ready-timeout', type=float, default=30)
p.add_argument('--until-area', type=int)
p.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
a = p.parse_args()
if not 0 < a.timeout <= 60 or not 0 < a.ready_timeout <= 60 or not 0 < a.radius <= 30:
    p.error('invalid radius or timeout')
base = ['adb', '-s', a.serial]
package = 'com.d2rmobile.nativeport'


def adb(*parts):
    return subprocess.check_output([*base, *parts], text=True, stderr=subprocess.STDOUT)


def state(require_ready=True):
    result = latest_complete_state(adb('exec-out', 'run-as', package, 'tail', '-n', '2',
                            'files/runtime/game-state.jsonl'))
    if require_ready and (not result.get('valid') or not result.get('world_input_ready')):
        raise RuntimeError('original player/world UI is not ready')
    return result


sizes = re.findall(r'(\d+)x(\d+)', adb('shell', 'wm', 'size'))
short, long = sorted(map(int, sizes[-1]))
origin = (round(short * .274), round(short * .742))
target = origin
ready_deadline = time.monotonic() + a.ready_timeout
while True:
    try:
        before = last = state()
        break
    except RuntimeError:
        # 关面板事件先于下一份服务端读数；等真实状态就绪再按摇杆。
        # Panel-close events precede the next server observation. Wait for
        # actual readiness before touching the stick.
        if time.monotonic() >= ready_deadline:
            raise
        time.sleep(.2)
print(json.dumps({'before': before, 'target': [a.x, a.y]}), flush=True)
deadline = time.monotonic() + a.timeout
changed_at = time.monotonic()
down = reached = False
try:
    while time.monotonic() < deadline:
        current = state(False)
        if current['time_ns'] != last['time_ns']:
            changed_at = time.monotonic()
        elif time.monotonic() - changed_at > 8:
            raise RuntimeError('original state stopped advancing')
        last = current
        if not last.get('valid') or not last.get('world_input_ready'):
            if down:
                adb('shell', 'input', 'touchscreen', 'motionevent', 'UP', *map(str, target))
                down = False
            if a.until_area is None:
                raise RuntimeError('original player/world UI is no longer ready')
            # 真实换图时先松摇杆，等客户端也到新图，再认区域到达。
            # Release during a real transition and wait for client readiness
            # before accepting the server's new area.
            time.sleep(.2)
            continue
        dx, dy = a.x - last['x'], a.y - last['y']
        if math.hypot(dx, dy) <= a.radius or (
                a.until_area is not None and last['area'] == a.until_area):
            reached = True
            break
        if not down:
            adb('shell', 'input', 'touchscreen', 'motionevent', 'DOWN', *map(str, origin))
            down = True
        sx, sy = dx - dy, (dx + dy) / 2
        length = math.hypot(sx, sy)
        travel = 40 * short / 440
        target = (round(origin[0] + sx / length * travel),
                  round(origin[1] + sy / length * travel))
        adb('shell', 'input', 'touchscreen', 'motionevent', 'MOVE', *map(str, target))
        time.sleep(.2)
finally:
    if down:
        adb('shell', 'input', 'touchscreen', 'motionevent', 'UP', *map(str, target))
print(json.dumps({'after': last, 'reached': reached}), flush=True)
if not reached:
    raise SystemExit(2)
