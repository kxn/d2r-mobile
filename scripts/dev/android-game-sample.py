#!/usr/bin/env python3
"""只读采样稳定世界的真实宿主观测，不把目标帧率当实测帧率。
Sample original host observations in a stable world, without treating target FPS as measured FPS.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
p.add_argument('--seconds', type=float, default=45)
p.add_argument('--scale', type=float, default=.60)
p.add_argument('--ready-timeout', type=float, default=120)
p.add_argument('--report', required=True, type=Path)
a = p.parse_args()
if not 5 <= a.seconds <= 300 or not 1 <= a.ready_timeout <= 300 or not .1 <= a.scale <= 1:
    p.error('invalid duration or scale')
base = ['adb', '-s', a.serial]
package = 'com.d2rmobile.nativeport'


def adb(*args):
    return subprocess.check_output([*base, *args], text=True)


def read_state():
    return json.loads(adb('exec-out', 'run-as', package, 'tail', '-n', '1',
                          'files/runtime/game-state.jsonl'))


pid = adb('shell', 'pidof', package).strip()
if not pid:
    raise SystemExit('game process is not running')
log = adb('exec-out', 'run-as', package, 'cat', 'files/runtime/native.log')
if f'Android native start pid={pid}\n' not in log:
    raise SystemExit('host log does not belong to the current process')
deadline = time.monotonic() + a.ready_timeout
while True:
    first = read_state()
    if first.get('world_input_ready') and abs(first['scale_3d'] - a.scale) < .001:
        break
    if time.monotonic() >= deadline:
        raise SystemExit('world did not reach requested scale')
    time.sleep(.5)
origin = (first['area'], first['x'], first['y'])
samples = [first]
battery_before = adb('shell', 'dumpsys', 'battery')
thermal_before = adb('shell', 'dumpsys', 'thermalservice')
deadline = time.monotonic() + a.seconds
while time.monotonic() < deadline:
    s = read_state()
    if s['time_ns'] != samples[-1]['time_ns']:
        if (not s['world_input_ready'] or abs(s['scale_3d'] - a.scale) >= .001 or
                (s['area'], s['x'], s['y']) != origin):
            raise SystemExit('world/scale/position changed during sample')
        samples.append(s)
    time.sleep(.5)
if pid != adb('shell', 'pidof', package).strip() or len(samples) < 4:
    raise SystemExit('process changed or insufficient fresh observations')
last = samples[-1]
if last['time_ns'] - first['time_ns'] < (a.seconds - 3) * 1e9:
    raise SystemExit('state samples stopped advancing')
intervals = {s['present_sample_ns']: s['present_interval_ms'] for s in samples
             if s['present_interval_ms'] > 0 and s['present_sample_ns'] > first['time_ns']}
rate = None
if all(s['runtime_extrap']['enabled'] for s in samples):
    def boundaries(s):
        r = s['runtime_extrap']
        return r['full'] + r['candidates']
    rate = (boundaries(last) - boundaries(first)) * 1e9 / (last['time_ns'] - first['time_ns'])
report = {'pid': int(pid), 'position': origin, 'scale': a.scale,
          'duration_seconds': (last['time_ns'] - first['time_ns']) / 1e9,
          'renderer_boundaries_per_second': rate,
          'sampled_present_interval_ms_median': statistics.median(intervals.values()) if intervals else None,
          'measurement_limits': 'Renderer boundaries count replay frames, not confirmed display frames. '
                                'Presentation intervals are sparse host observations, not a complete frame-time trace. '
                                'Sequential runs do not control thermal state or world lighting.',
          'battery_before': battery_before, 'battery_after': adb('shell', 'dumpsys', 'battery'),
          'thermal_before': thermal_before, 'thermal_after': adb('shell', 'dumpsys', 'thermalservice'),
          'samples': samples}
a.report.parent.mkdir(parents=True, exist_ok=True)
a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: v for k, v in report.items() if k not in
                  ('samples', 'battery_before', 'battery_after', 'thermal_before', 'thermal_after')}))
