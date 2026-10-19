#!/usr/bin/env python3
# 复用原 dm_world_watch 的单位投影，注入真正 Android 触点，再等原 UI 事件。
# Use original dm_world_watch projections, inject a real Android touch, and
# require an original UI event. Start the APK with --ei watch_class CLASS first.
import argparse
import json
import re
import subprocess
import time

p = argparse.ArgumentParser()
acceptance = p.add_mutually_exclusive_group()
acceptance.add_argument('--wait')
acceptance.add_argument('--until-area', type=int)
p.add_argument('--timeout', type=float, default=60)
p.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
a = p.parse_args()
pattern = re.compile(a.wait or 'OpenPanel NpcDialogPanel')
base = ['adb', '-s', a.serial]
package = 'com.d2rmobile.nativeport'


def adb(*parts):
    return subprocess.check_output([*base, *parts], text=True, stderr=subprocess.STDOUT)


def state():
    try:
        return json.loads(adb('exec-out', 'run-as', package, 'tail', '-n', '1',
                              'files/runtime/game-state.jsonl'))
    except json.JSONDecodeError:
        return {}


before = state().get('time_ns', 0)
deadline = time.monotonic() + a.timeout
while time.monotonic() < deadline:
    current = state()
    if (current.get('time_ns', 0) > before and current.get('valid') and
            current.get('world_input_ready') and current.get('watch_visible')):
        break
    time.sleep(.2)
else:
    raise SystemExit('no fresh visible original unit projection')
sizes = re.findall(r'(\d+)x(\d+)', adb('shell', 'wm', 'size'))
height, width = sorted(map(int, sizes[-1]))
x, y = round(current['watch_x'] * width), round(current['watch_y'] * height)
if a.until_area is not None and current['area'] == a.until_area:
    raise SystemExit('already in requested area; cannot accept a transition')
path = 'files/runtime/native.log'
offset = int(adb('shell', 'run-as', package, 'stat', '-c', '%s', path))
print(json.dumps({'observed_state': current, 'android_tap': [x, y]}), flush=True)
adb('shell', 'input', 'tap', str(x), str(y))
pending = ''
settled_since = None
while time.monotonic() < deadline:
    if a.until_area is not None:
        observed = state()
        if (observed.get('time_ns', 0) > current['time_ns'] and
                observed.get('valid') and observed.get('world_input_ready') and
                observed.get('area') == a.until_area):
            if settled_since is None:
                settled_since = observed['time_ns']
            # 还须有下一份就绪读数，防服务器先换图而客户端稍后才开载入页。
            # Require a subsequent ready sample; the loading panel can open
            # after the server changes area.
            if observed['time_ns'] - settled_since >= 1000000000:
                print(json.dumps({'area_transition': observed}), flush=True)
                raise SystemExit(0)
        else:
            settled_since = None
    data = adb('exec-out', 'run-as', package, 'tail', '-c', f'+{offset+1}', path)
    offset += len(data.encode())
    pending += data
    lines = pending.split('\n')
    pending = lines.pop()
    for line in lines:
        if 'FATAL' in line or 'native fault' in line:
            raise SystemExit(line)
        if a.until_area is None and 'UIMSG' in line and pattern.search(line):
            print(line)
            raise SystemExit(0)
    if not adb('shell', 'pidof', package).strip():
        raise SystemExit('native game process exited')
    time.sleep(.3)
raise SystemExit('no original confirmation: ' + (str(a.until_area) if
                 a.until_area is not None else pattern.pattern))
