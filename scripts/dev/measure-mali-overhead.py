#!/usr/bin/env python3
"""用 SurfaceFlinger 完整上屏记录对照厂商采集开关的影响。
Compare vendor collection on/off using complete SurfaceFlinger present records.
"""
import argparse
import json
from pathlib import Path
import shlex
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = 'com.d2rmobile.nativeport'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
    parser.add_argument('--seconds', type=int, default=12)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 15:
        parser.error('--seconds must be 5..15 (128-entry SurfaceFlinger history)')
    report = args.report.resolve()
    capture = report.with_suffix('.apc')
    if report.exists() or capture.exists():
        parser.error('report/capture already exists')
    report.parent.mkdir(parents=True, exist_ok=True)
    base = ['adb', '-s', args.serial]

    def adb(*parts):
        return subprocess.check_output([*base, *parts], text=True, timeout=20)

    pid = adb('shell', 'pidof', PACKAGE).strip()
    lines = adb('shell', 'dumpsys', 'SurfaceFlinger', '--list').splitlines()
    candidates = [v for v in lines if PACKAGE in v and 'SurfaceView[' in v and '(BLAST)' in v]
    if len(candidates) != 1:
        raise SystemExit('expected one game SurfaceView BLAST layer')
    layer = candidates[0]
    if layer.startswith('RequestedLayerState{'):
        layer = layer.split('{', 1)[1].split(' parentId=', 1)[0]

    def presents():
        raw = adb('shell', shlex.join(['dumpsys', 'SurfaceFlinger', '--latency', layer]))
        values = []
        for row in raw.splitlines():
            words = row.split()
            if len(words) == 3 and all(v.isdecimal() for v in words):
                t = int(words[1])
                if 0 < t < 2**63 - 1:
                    values.append(t)
        return sorted(set(values)), raw

    def state():
        raw = adb('exec-out', 'run-as', PACKAGE, 'tail', '-n', '1', 'files/runtime/game-state.jsonl')
        data = json.loads(raw)
        return {k: data.get(k) for k in ['time_ns', 'area', 'x', 'y', 'hp', 'scale_3d']}

    def measure(name):
        before = state()
        thermal_before = adb('shell', 'dumpsys', 'thermalservice')
        old, raw_before = presents()
        if not old:
            raise SystemExit('no valid present history')
        start = time.monotonic()
        time.sleep(args.seconds)
        history, raw_after = presents()
        elapsed = time.monotonic() - start
        after = state()
        if adb('shell', 'pidof', PACKAGE).strip() != pid:
            raise SystemExit('game process changed')
        if not history or history[0] > old[-1]:
            raise SystemExit('present history overflow: shorten measurement')
        actual = [v for v in history if v > old[-1]]
        times = [old[-1], *actual]
        intervals = [(b-a)/1e6 for a,b in zip(times, times[1:])]
        if len(intervals) < 20:
            raise SystemExit('insufficient completed game presents')
        for k in ['area', 'x', 'y', 'hp', 'scale_3d']:
            if before[k] != after[k]:
                raise SystemExit('game state/scale moved during measurement')
        quantiles = statistics.quantiles(intervals, n=100, method='inclusive')
        value = {'name': name, 'host_elapsed_s': elapsed, 'frames': len(intervals),
                 'fps': 1000/statistics.mean(intervals), 'p50_ms': statistics.median(intervals),
                 'p95_ms': quantiles[94], 'p99_ms': quantiles[98],
                 'mean_ms': statistics.mean(intervals), 'intervals_ms': intervals,
                 'actual_present_ns': times, 'state_before': before, 'state_after': after,
                 'thermal_before': thermal_before,
                 'thermal_after': adb('shell', 'dumpsys', 'thermalservice')}
        report.with_name(report.stem + '-' + name + '-surface.txt').write_text(raw_before + '\nAFTER\n' + raw_after)
        print(json.dumps({k: value[k] for k in ['name', 'frames', 'fps', 'p50_ms', 'p95_ms']}), flush=True)
        return value

    results = [measure('off-before')]
    existing = set(adb('shell', 'run-as', PACKAGE, 'ls', 'files/runtime/profiles').splitlines())
    collector = subprocess.Popen([sys.executable, str(ROOT/'scripts/dev/capture-mali.py'),
        '--serial', args.serial, '--seconds', str(args.seconds + 12), '--output', str(capture)],
        stdout=report.with_suffix('.collector.log').open('w'), stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 12
        active = None
        while time.monotonic() < deadline:
            now = set(adb('shell', 'run-as', PACKAGE, 'ls', 'files/runtime/profiles').splitlines())
            new = [v for v in now-existing if v.startswith('d2r-') and v.endswith('.apc')]
            if len(new) == 1:
                path = 'files/runtime/profiles/' + new[0] + '/0000000000'
                ready = subprocess.run([*base, 'shell', 'run-as', PACKAGE, 'test', '-s', path],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
                if ready:
                    active = path
                    break
            if collector.poll() is not None:
                raise SystemExit('vendor collector exited before readiness')
            time.sleep(.5)
        if not active:
            raise SystemExit('live nonempty vendor capture not observed')
        results.append(measure('on'))
        collector.wait(timeout=40)
        if collector.returncode:
            raise SystemExit('vendor capture failed')
    finally:
        if collector.poll() is None:
            collector.terminate()
            collector.wait(timeout=30)
    results.append(measure('off-after'))
    # 顺序短窗口只能给本次影响估计，不证明所有温度/场景均无扰动。
    # Sequential short windows estimate this run; they cannot prove no overhead
    # at every temperature/scene. CPU/GPU clocks and lighting can still drift.
    off_mean = statistics.mean(r['mean_ms'] for r in [results[0], results[2]])
    result = {'pid': int(pid), 'layer': layer, 'collector': str(capture),
              'scope': 'off/on/off short windows; system actual presents, no per-frame ADB polling',
              'limitation': 'sequential windows; lighting/frequency/temperature may drift; not a speedup benchmark',
              'mean_interval_change_percent': (results[1]['mean_ms']/off_mean-1)*100,
              'samples': results}
    report.write_text(json.dumps(result, indent=2) + '\n')
    print('mean_interval_change_percent:', result['mean_interval_change_percent'], flush=True)


if __name__ == '__main__':
    main()
