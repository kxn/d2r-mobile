#!/usr/bin/env python3
"""Finite official debugger hook. Caller owns cold launch/save2/restore."""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import signal
import subprocess
import sys
import time

R = Path('/home/dl/d2r-mobile')
HERE = Path(__file__).resolve().parent
ARCHIVE = R / '.local/build/native-image-window-lane-bb4ac54-canonical-source'
ELF = ARCHIVE / 'six-libraries/libdmhost_android.so'
APK_SHA = 'ab2a3e4e6e7a41f3f38a74ffc97c313fcff0d93c3c8128737be4f4fca765d11c'
ELF_SHA = 'dc4442c34f873745c2ea376b796a2c1678f7a58d9ef8ffaf01104be0784abde0'
BUILD_ID = '243f00c4491b21bc3bde4a7a582c163d2cf876b7'
SERVER_LOCAL = R / '.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/lldb-server'
SERVER_APP = 'files/runtime/temp/d2r-lldb-server'
PKG = 'com.d2rmobile.nativeport'
SERIAL = '10CG570J59001U1'
PORT = '39485'
SOCKET = 'd2r-astc-copy-bb4-closure-v1'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def owned_pid(current, expected):
    return current.isdecimal() and current == expected


def module_slide(maps):
    rows = []
    for line in maps.splitlines():
        bits = line.split()
        if len(bits) >= 6 and bits[-1].endswith('/libdmhost_android.so'):
            require('(deleted)' not in line, 'deleted module mapping')
            rows.append((int(bits[0].split('-')[0], 16) - int(bits[2], 16), bits[-1]))
    require(rows and len({p for _, p in rows}) == 1, 'host module absent or ambiguous')
    return min(s for s, _ in rows), rows[0][1]


def validate_stops(data):
    events = data.get('events', [])
    if data.get('errors') or len(events) != 1:
        return {'status': 'inconclusive_no_complete_refusal_snapshot', 'runtime_correctness_proven': False}
    row = events[0]
    required = ['parameters', 'source', 'destination', 'observation', 'cell_history', 'alias_scan', 'pools']
    if row.get('read_error') or any(key not in row for key in required):
        return {'status': 'inconclusive_incomplete_DWARF_metadata', 'runtime_correctness_proven': False}
    for group in ['parameters', 'source', 'destination', 'observation']:
        if any('error' in value for value in row[group].values()):
            return {'status': 'inconclusive_typed_field_unavailable', 'runtime_correctness_proven': False}
    if not row['cell_history']['complete'] or not row['alias_scan']['complete']:
        return {'status': 'inconclusive_bounded_walk_incomplete', 'runtime_correctness_proven': False}
    return {'status': 'actual_ASTC_copy_refusal_metadata_captured',
            'scope': 'One actual rejected operation; preserves original failure. Not a fixed copy or frame/performance proof.',
            'runtime_correctness_proven': False}


def main():
    require(len(sys.argv) == 3 and sys.argv[1].isdecimal(), 'usage: run-debugger.py FIRST_PID OUTPUT')
    pid, out = sys.argv[1], Path(sys.argv[2]).resolve()
    require(out.is_dir(), 'caller must create owned output directory')
    require(not (out / 'debugger-result.json').exists(), 'output already used')
    adb_base = [str(R / '.local/android-sdk/platform-tools/adb'), '-s', SERIAL]
    server = client = None
    forward = False
    d = {'PID': int(pid), 'ELF_sha256': sha(ELF), 'ELF_BuildID': BUILD_ID,
         'capture_protocol': 'astc-copy-bb4-rejected-v1',
         'scope': 'Only bounded official debugger; caller owns game/save/restore', 'cleanup_errors': []}

    def adb(*args):
        return subprocess.check_output(adb_base + list(args), text=True, timeout=20).strip()

    def current():
        return adb('shell', 'pidof ' + PKG + ' || true')

    def cancelled(signum, _frame):
        raise RuntimeError('caller cancelled owned debugger hook: signal ' + str(signum))

    signal.signal(signal.SIGTERM, cancelled)
    signal.signal(signal.SIGINT, cancelled)
    try:
        require(d['ELF_sha256'] == ELF_SHA and owned_pid(current(), pid), 'exact ELF/owned firstPID mismatch')
        path = adb('shell', 'pm', 'path', PKG)
        require(path.startswith('package:') and '\n' not in path, 'ambiguous installed APK')
        require(adb('shell', 'sha256sum', path[8:]).split()[0] == APK_SHA, 'installed APK is not exact bb4')
        uid = adb('shell', 'run-as', PKG, 'id', '-u')
        status = adb('exec-out', 'run-as', PKG, 'cat', '/proc/' + pid + '/status')
        (out / 'native-debugger-owned-status.txt').write_text(status + '\n')
        match = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
        require(uid.isdecimal() and 10000 <= int(uid) < 200000 and match and
                all(v == uid for v in match.groups()), 'ordinary UID mismatch')
        tracer = re.search(r'^TracerPid:\s+(\d+)', status, re.M)
        require(tracer and tracer.group(1) == '0', 'owned game already has another tracer')
        d['ordinary_UID'] = int(uid)
        require('tcp:' + PORT not in adb('forward', '--list') and
                not re.search(r'\b' + re.escape(SOCKET) + r'\b', adb('shell', 'cat', '/proc/net/unix')),
                'debugger port/socket already in use')
        require(adb('shell', 'run-as', PKG, 'sha256sum', SERVER_APP).split()[0] == sha(SERVER_LOCAL),
                'transaction did not publish exact NDK lldb-server')
        for _ in range(50):
            require(owned_pid(current(), pid), 'owned PID changed before attach')
            maps = adb('exec-out', 'run-as', PKG, 'cat', '/proc/' + pid + '/maps')
            if '/libdmhost_android.so' in maps:
                break
            time.sleep(.1)  # original bounded startup-tool readiness, not a measurement
        else:
            raise RuntimeError('host ELF did not map during bounded attach readiness')
        (out / 'native-debugger-maps.txt').write_text(maps + '\n')
        slide, module = module_slide(maps)
        d.update(slide=hex(slide), module_path=module)
        require(adb('shell', 'sha256sum', module).split()[0] == ELF_SHA, 'mapped host ELF hash mismatch')
        adb('forward', 'tcp:' + PORT, 'localabstract:' + SOCKET)
        forward = True
        with (out / 'native-debugger-server.log').open('w') as log:
            remote = ['run-as', PKG, 'sh', '-c',
                'printf "OWNED_SERVER_PID=%s\\n" "$$"; exec "$@"', 'sh', SERVER_APP,
                'gdbserver', 'unix-abstract://' + SOCKET, '--attach', pid]
            server = subprocess.Popen(adb_base + ['shell', shlex.join(remote)],
                stdout=log, stderr=subprocess.STDOUT)
        commands = '\n'.join([
            'target create --arch aarch64 ' + shlex.quote(str(ELF)),
            'gdb-remote 127.0.0.1:' + PORT,
            'target modules load --file ' + shlex.quote(str(ELF)) + ' --slide ' + hex(slide),
            'process handle SIGSEGV -s false -n false -p true',
            'process handle SIGBUS -s false -n false -p true',
            'command script import ' + shlex.quote(str(HERE / 'capture_astc.py')),
            'script capture_astc.install(lldb.debugger)', 'continue', 'process detach', ''])
        cmd = out / 'native-debugger-commands.txt'
        cmd.write_text(commands)
        env = os.environ.copy()
        env.update(LD_LIBRARY_PATH=str(R / '.local/tools/lldb19/usr/lib/x86_64-linux-gnu'),
            PYTHONPATH=str(R / '.local/tools/lldb19/usr/lib/llvm-19/lib/python3.13/site-packages'),
            DM_ASTC_COPY_OUTPUT=str(out), DM_ASTC_COPY_SLIDE=hex(slide), DM_ASTC_COPY_PID=pid)
        with (out / 'native-debugger.log').open('w') as log:
            client = subprocess.Popen([str(R / '.local/tools/lldb19/usr/lib/llvm-19/bin/lldb'),
                '--batch', '-s', str(cmd)], stdout=log, stderr=subprocess.STDOUT, env=env)
            try:
                client.wait(timeout=65)
            except subprocess.TimeoutExpired:
                d['bounded_tool_timeout'] = True
        p = out / 'astc-copy-stops.json'
        stops = json.loads(p.read_text()) if p.exists() else None
        if stops:
            require(all(row['PID'] == int(pid) for row in stops['events']), 'stop is not owned first PID')
        d['proof'] = validate_stops(stops) if stops else {
            'status': 'inconclusive_attach_or_tool_no_stops', 'runtime_failure_proven': False}
        d['client_returncode'] = client.poll()
    except BaseException as e:
        d['tool_error'] = repr(e)
    finally:
        # Let the caller's one graceful cancellation complete owned cleanup.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if client is not None and client.poll() is None:
            client.send_signal(signal.SIGINT)
            try:
                client.wait(timeout=5)
            except subprocess.TimeoutExpired:
                client.terminate()
                try:
                    client.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    client.kill()
                    client.wait(timeout=5)
        # The shell execs the owned server, so its printed PID is the actual
        # remote tracer PID. Never pkill a package or another debugger.
        if server is not None:
            try:
                log = (out / 'native-debugger-server.log').read_text()
                matches = re.findall(r'^OWNED_SERVER_PID=(\d+)$', log, re.M)
                require(len(matches) == 1 and matches[0] != pid, 'owned remote server PID unavailable')
                remote_pid = matches[0]
                d['owned_server_PID'] = int(remote_pid)
                cmdline = adb('exec-out', 'run-as', PKG, 'cat', '/proc/' + remote_pid + '/cmdline') if \
                    adb('shell', 'run-as', PKG, 'sh', '-c',
                        shlex.quote('test -e /proc/' + remote_pid + '/cmdline && echo yes || true')) == 'yes' else ''
                if cmdline:
                    expected = [SERVER_APP, 'gdbserver', 'unix-abstract://' + SOCKET, '--attach', pid]
                    require(cmdline.rstrip('\0').split('\0') == expected, 'owned server command changed; preserve it')
                    adb('shell', 'run-as', PKG, 'kill', '-TERM', remote_pid)
                d['owned_server_cleanup_requested'] = True
            except BaseException as e:
                d['cleanup_errors'].append('owned remote server: ' + repr(e))
        if server is not None and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=4)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=4)
        if d.get('owned_server_PID'):
            try:
                remote_pid = str(d['owned_server_PID'])
                # One post-cleanup existence check, never a resident sampler.
                exists = adb('shell', 'run-as', PKG, 'sh', '-c',
                    shlex.quote('test -e /proc/' + remote_pid + '/cmdline && echo yes || true'))
                if exists == 'yes':
                    cmdline = adb('exec-out', 'run-as', PKG, 'cat', '/proc/' + remote_pid + '/cmdline')
                    require(cmdline.rstrip('\0').split('\0') ==
                        [SERVER_APP, 'gdbserver', 'unix-abstract://' + SOCKET, '--attach', pid],
                        'remote server PID changed; do not kill replacement')
                    adb('shell', 'run-as', PKG, 'kill', '-KILL', remote_pid)
                    require(adb('shell', 'run-as', PKG, 'sh', '-c',
                        shlex.quote('test ! -e /proc/' + remote_pid + '/cmdline && echo gone || true')) == 'gone',
                        'owned remote server still present after cleanup')
                d['owned_server_absent_after_cleanup'] = True
            except BaseException as e:
                d['cleanup_errors'].append('remote server final check: ' + repr(e))
        if forward:
            try:
                adb('forward', '--remove', 'tcp:' + PORT)
            except BaseException as e:
                d['cleanup_errors'].append('owned forward: ' + repr(e))
        try:
            if server is not None and owned_pid(current(), pid):
                adb('shell', 'run-as', PKG, 'kill', '-CONT', pid)
                d['owned_PID_resumed_for_caller_save'] = True
        except BaseException as e:
            d['cleanup_errors'].append('owned PID resume: ' + repr(e))
        d['owned_debugger_cleanup_complete'] = not d['cleanup_errors']
        d['rejected_ASTC_copy_metadata_captured'] = (not d.get('tool_error') and
            d.get('proof', {}).get('status') == 'actual_ASTC_copy_refusal_metadata_captured')
        d['status'] = 'captured' if d['rejected_ASTC_copy_metadata_captured'] else 'inconclusive_attach_race'
        (out / 'debugger-result.json').write_text(json.dumps(d, indent=2) + '\n')
    # No-stops/late attach is diagnostic inconclusive, never a runtime failure.
    return 1 if d.get('tool_error') or d['cleanup_errors'] else 0


if __name__ == '__main__':
    sys.exit(main())
