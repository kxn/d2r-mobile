#!/usr/bin/env python3
"""Launch Ryujinx on CPU Vulkan, test the real GDB connection, then stop it."""
from pathlib import Path
import os
import signal
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[2]
subprocess.run([sys.executable, str(root / 'scripts/dev/make-gdb-probe.py')], check=True)
log_path = root / '.local/logs/ryujinx-probe.log'
log_path.parent.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, VK_DRIVER_FILES='/usr/share/vulkan/icd.d/lvp_icd.json',
           SDL_AUDIODRIVER='dummy')
command = ['xvfb-run', '-a', '-s', '-screen 0 1280x720x24',
           str(root / '.local/emulators/ryujinx/Ryujinx'), '--no-gui',
           '--root-data-dir', str(root / '.local/ryujinx-profile'),
           '--enable-gdb-stub', '--gdb-stub-port', '55571', '--suspend-on-start',
           '--disable-ptc', '--disable-shader-cache', '--disable-main-input-config',
           str(root / '.local/build/gdb-probe/d2r-debug-probe.nro')]
with log_path.open('w') as log:
    process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        deadline = time.monotonic() + 30
        while 'Application is suspended on start' not in log_path.read_text(errors='replace'):
            if process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError(f'Ryujinx did not load probe; see {log_path}')
            time.sleep(0.2)
        result = subprocess.run([sys.executable, str(root / 'scripts/dev/probe-gdb.py')],
                                capture_output=True, text=True, timeout=60)
        (root / '.local/logs/gdb-probe-result.json').write_text(result.stdout)
        (root / '.local/logs/gdb-probe-error.log').write_text(result.stderr)
        print(result.stdout, end='')
        print(result.stderr, end='', file=sys.stderr)
        result.check_returncode()
    finally:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
