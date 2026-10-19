#!/usr/bin/env python3
"""Install the development probe on an authorized ADB device and collect JSON."""
import argparse
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
ADB = ROOT / '.local/android-sdk/platform-tools/adb'
PACKAGE = 'com.d2rmobile.devprobe'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial')
parser.add_argument('--out', type=Path, default=ROOT / '.local/logs/android-device-probe.json')
args = parser.parse_args()
if not args.serial:
    lines = subprocess.check_output([str(ADB), 'devices'], text=True).splitlines()[1:]
    devices = [line.split()[0] for line in lines if len(line.split()) >= 2 and line.split()[1] == 'device']
    if len(devices) != 1:
        parser.error('Connect and authorize one phone, or select an authorized device with --serial')
    args.serial = devices[0]
base = [str(ADB), '-s', args.serial]
apk = ROOT / '.local/build/device-probe/d2r-device-probe.apk'
subprocess.run(base + ['install', '-r', str(apk)], check=True)
subprocess.run(base + ['shell', 'am', 'force-stop', PACKAGE], check=True)
subprocess.run(base + ['shell', 'run-as', PACKAGE, 'rm', '-f', 'files/report.json', 'files/report.tmp'], check=True)
subprocess.run(base + ['shell', 'am', 'start', '-W', '-n', PACKAGE + '/.ProbeActivity'], check=True)
deadline = time.monotonic() + 45
while time.monotonic() < deadline:
    result = subprocess.run(base + ['shell', 'run-as', PACKAGE, 'cat', 'files/report.json'], capture_output=True, text=True, timeout=5)
    if result.returncode == 0:
        report = json.loads(result.stdout)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        print(f'Report saved: {args.out}')
        raise SystemExit(0 if report.get('passed') is True else 1)
    time.sleep(0.5)
raise SystemExit('No report within 45 seconds. Inspect adb logcat for app or driver failure.')
