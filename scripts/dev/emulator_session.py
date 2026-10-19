"""Bounded, isolated emulator session for original homebrew integration tests."""
from contextlib import contextmanager
import os
from pathlib import Path
import signal
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


@contextmanager
def session(nro, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command = ['xvfb-run', '-a', '-s', '-screen 0 1280x720x24',
        str(ROOT / '.local/emulators/ryujinx/Ryujinx'), '--no-gui',
        '--root-data-dir', str(directory / 'profile'), '--enable-gdb-stub',
        '--gdb-stub-port', str(port), '--suspend-on-start', '--disable-ptc',
        '--disable-shader-cache', '--disable-main-input-config', str(nro)]
    env = dict(os.environ, VK_DRIVER_FILES='/usr/share/vulkan/icd.d/lvp_icd.json', SDL_AUDIODRIVER='dummy')
    log_path = directory / 'emulator.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 45
            while 'Application is suspended on start' not in log_path.read_text(errors='replace'):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f'Emulator failed to start: {log_path}')
                time.sleep(0.1)
            yield port
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
