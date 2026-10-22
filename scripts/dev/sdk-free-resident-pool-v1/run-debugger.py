#!/usr/bin/env python3
"""One official LLDB SDK-free-span snapshot. Caller owns launch/save2/restore."""
from pathlib import Path
import hashlib
import argparse
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
SERVER_LOCAL = R / '.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/lldb-server'
SERVER_APP = 'files/runtime/d2r-lldb-server'
PKG = 'com.d2rmobile.nativeport'
PORT = '39486'
SOCKET = 'd2r-sdk-free-owned-' + str(os.getpid())
Q735_MUTEX_MODE = 'f75-q735-libc-02969f76-state16-v2'
Q735_LIBC_SHA = '02969f76be8e6ed8a44be78083735801448a6e0f972792da63b93e4a6de8d5b0'
Q735_LIBC_PATH = '/apex/com.android.runtime/lib64/bionic/libc.so'


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
        require(not ('/libdmhost_android.so' in line and '(deleted)' in line), 'deleted host mapping')
        bits = line.split()
        if len(bits) >= 6 and bits[-1].endswith('/libdmhost_android.so'):
            require('(deleted)' not in line, 'deleted module mapping')
            rows.append((int(bits[0].split('-')[0], 16) - int(bits[2], 16), bits[-1]))
    require(rows and len({p for _, p in rows}) == 1, 'host module absent or ambiguous')
    return min(s for s, _ in rows), rows[0][1]


def validate_optional_nvn(config, inputs):
    if 'nvn_pools' not in config:
        return
    p = config['nvn_pools']
    require(isinstance(p, dict) and
            {'enabled','mode','module','ELF_sha256','ELF_BuildID'} <= set(p) <=
            {'enabled','mode','module','ELF_sha256','ELF_BuildID','qualify_unlocked','libc_sha256'},
            'unknown/missing optional NVN config fields')
    require(p['enabled'] is True and p['mode'] in ('f75-bionic-normal-resources-zero-v1', Q735_MUTEX_MODE), 'unqualified optional NVN mode/enable')
    if p['mode'] == Q735_MUTEX_MODE:
        require(p.get('libc_sha256') == Q735_LIBC_SHA, 'actual Q735 libc ABI pin missing/differs')
    else:
        require('libc_sha256' not in p, 'old mutex mode must not imply another libc ABI')
    require(type(p.get('qualify_unlocked',False)) is bool, 'optional NVN entry qualification is not bool')
    require(p['ELF_sha256']==config['ELF_sha256']=='4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618' and
            p['ELF_BuildID']==config['ELF_BuildID']=='6e6f12c5c5f7a858704a84ee294b19d95aa3f894', 'optional NVN ELF identity differs from reviewed f75')
    m=p['module']; name='sdk_nvn_pool_registry.py'
    require(isinstance(m,dict) and set(m)=={'path','sha256'} and
            Path(m['path']).resolve()==(HERE/name).resolve() and name in inputs['files'] and
            m['sha256']==inputs['files'][name]['sha256']==sha(HERE/name), 'optional NVN source is not the frozen companion')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid');parser.add_argument('output',type=Path)
    parser.add_argument('--config',type=Path,default=os.environ.get('DM_SDK_FREE_CONFIG'))
    parser.add_argument('--config-sha256',default=os.environ.get('DM_SDK_FREE_CONFIG_SHA256'))
    parser.add_argument('--plan',action='store_true',help='Validate only local inputs; no ADB')
    args=parser.parse_args()
    require(args.pid.isdecimal(), 'FIRST_PID must be numeric')
    require(args.config and args.config_sha256 and sha(args.config)==args.config_sha256,'exact caller-owned config hash required')
    config=json.loads(Path(args.config).read_text())
    required={'schema','serial','APK_sha256','ELF','ELF_sha256','ELF_BuildID','canonical_source_ready','canonical_source_ready_sha256','tool_inputs_sha256','sdk_original','sdk_flat'}
    require(required <= set(config) <= required|{'nvn_pools'},'unknown/missing config fields')
    require(config['schema']==1 and config['serial']=='10CG570J59001U1','schema/Q device mismatch')
    for key in ['APK_sha256','ELF_sha256','canonical_source_ready_sha256','tool_inputs_sha256']:
        require(re.fullmatch('[0-9a-f]{64}',config[key]), 'invalid '+key)
    require(re.fullmatch('[0-9a-f]{40}',config['ELF_BuildID']), 'invalid ELF BuildID')
    tool_inputs=HERE/'tool-inputs.json'
    require(sha(tool_inputs)==config['tool_inputs_sha256'],'frozen tool input manifest mismatch')
    inputs=json.loads(tool_inputs.read_text())
    require(inputs['schema']==1 and inputs['source_pinned'],'tool sources not independently pinned')
    for name,row in inputs['files'].items():
        require(Path(name).name==name and sha(HERE/name)==row['sha256'],'frozen helper source changed: '+name)
    for row in inputs['tools']:
        require(sha(row['path'])==row['sha256'],'official tool input changed: '+row['path'])
    validate_optional_nvn(config,inputs)
    ELF=Path(config['ELF']).resolve();ELF_SHA=config['ELF_sha256'];BUILD_ID=config['ELF_BuildID'];APK_SHA=config['APK_sha256'];SERIAL=config['serial']
    require(sha(ELF)==ELF_SHA and sha(config['canonical_source_ready'])==config['canonical_source_ready_sha256'],'local canonical input changed')
    ready=json.loads(Path(config['canonical_source_ready']).read_text())
    require(ready['canonical_APK_sha256']==APK_SHA and ready['canonical_native_library_sha256']['libdmhost_android.so']==ELF_SHA and ready['canonical_host_ELF_build_id']==BUILD_ID,'canonical source-ready identity mismatch')
    for name in ('sdk_original', 'sdk_flat'):
        row=config[name]
        require(set(row)=={'path','sha256'} and sha(row['path'])==row['sha256'], 'SDK input changed: '+name)
    if args.plan:
        print(json.dumps({'status':'LOCAL_INPUTS_VALID_NO_ADB','config':str(args.config),'config_sha256':args.config_sha256,'ELF_sha256':ELF_SHA,'APK_sha256':APK_SHA,'scope':'natural locked SDK metadata plus two existing kernel pagemap views; no target calls or reclamation; profile separate'}));return 0
    pid, out = args.pid, args.output.resolve()
    require(out.is_dir(), 'caller must create owned output directory')
    require(not (out / 'debugger-result.json').exists(), 'output already used')
    adb_base = [str(R / '.local/android-sdk/platform-tools/adb'), '-s', SERIAL]
    server = client = None
    forward = False
    d = {'PID': int(pid), 'ELF_sha256': sha(ELF), 'ELF_BuildID': BUILD_ID,
         'capture_protocol': 'sdk-free-resident-once-v1', 'config_sha256': args.config_sha256,
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
        require(adb('shell', 'sha256sum', path[8:]).split()[0] == APK_SHA, 'installed APK identity mismatch')
        if config.get('nvn_pools',{}).get('mode') == Q735_MUTEX_MODE:
            actual_libc = adb('shell', 'sha256sum', Q735_LIBC_PATH).split()[0]
            require(actual_libc == config['nvn_pools']['libc_sha256'] == Q735_LIBC_SHA,
                    'actual device libc differs from qualified state16 ABI')
            d['device_libc'] = dict(path=Q735_LIBC_PATH, sha256=actual_libc, verified_before_attach=True)
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
        # Observe the official server's existing listen socket before its one
        # LLDB connection. A connect probe would consume the sole client slot.
        # This is tool startup readiness, never game timing or sampling.
        ready_deadline = time.monotonic() + 15
        while True:
            require(server.poll() is None, 'owned official server exited before listen readiness')
            require(owned_pid(current(), pid), 'owned PID changed while server starts')
            sockets = adb('shell', 'cat', '/proc/net/unix')
            if any(line.split() and line.split()[-1] == '@' + SOCKET
                   for line in sockets.splitlines()):
                d['official_server_listen_socket_observed'] = True
                break
            require(time.monotonic() < ready_deadline, 'owned server listen socket readiness deadline')
            time.sleep(.1)
        commands = '\n'.join([
            'target create --arch aarch64 ' + shlex.quote(str(ELF)),
            'gdb-remote 127.0.0.1:' + PORT,
            'target modules load --file ' + shlex.quote(str(ELF)) + ' --slide ' + hex(slide),
            'settings set auto-confirm true',
            'process handle SIGSEGV -s false -n false -p true',
            'process handle SIGBUS -s false -n false -p true',
            'process handle SIGCONT -s false -n false -p true',
            'process handle SIGSTOP -s false -n false -p false',
            'command script import ' + shlex.quote(str(HERE / 'sdk_free_resident.py')),
            'script sdk_free_resident.snapshot(lldb.debugger)', ''])
        cmd = out / 'native-debugger-commands.txt'
        cmd.write_text(commands)
        env = os.environ.copy()
        env.update(LD_LIBRARY_PATH=str(R / '.local/tools/lldb19/usr/lib/x86_64-linux-gnu'),
            PYTHONPATH=str(R / '.local/tools/lldb19/usr/lib/llvm-19/lib/python3.13/site-packages'),
            DM_SDK_FREE_OUTPUT=str(out), DM_SDK_FREE_PID=pid,
            DM_SDK_FREE_BUILD_ID=BUILD_ID, DM_SDK_FREE_ELF_SHA256=ELF_SHA, DM_SDK_FREE_SERIAL=SERIAL,
            DM_SDK_FREE_ADB=adb_base[0], DM_SDK_FREE_CONFIG=str(Path(args.config).resolve()))
        with (out / 'native-debugger.log').open('w') as log:
            client = subprocess.Popen([str(R / '.local/tools/lldb19/usr/lib/llvm-19/bin/lldb'),
                '--batch', '-s', str(cmd)], stdout=log, stderr=subprocess.STDOUT, env=env)
            try:
                client.wait(timeout=150)
            except subprocess.TimeoutExpired:
                d['bounded_tool_timeout'] = True
        p = out / 'sdk-free-resident.json'
        snapshot = json.loads(p.read_text()) if p.exists() else None
        require(snapshot and snapshot['PID']==int(pid) and snapshot['ELF_sha256']==ELF_SHA,'missing/mismatched one-stop snapshot')
        d['snapshot']=snapshot
        require(snapshot.get('status')=='SDK_FREE_SPAN_CPU_VIEW_SNAPSHOT_COMPLETE' and snapshot.get('complete') is True and snapshot.get('inferior_calls') is False and not snapshot['errors'],'SDK metadata/pagemap incomplete; no resident-free or runtime-failure claim')
        if 'nvn_pools' in config:
            pools=snapshot.get('nvn_pools',{})
            d['nvn_pools_complete']=pools.get('complete') is True and pools.get('status')=='SAME_SDK_STOP_NVN_POOL_REGISTRY_METADATA_COMPLETE' and not pools.get('errors')
            require(d['nvn_pools_complete'], 'optional NVN registry unavailable; SDK success does not prove Pool attribution')
        d['client_returncode'] = client.poll()
        require(d['client_returncode']==0, 'official debugger did not finish successful detach')
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
                final_status=adb('exec-out','run-as',PKG,'cat','/proc/'+pid+'/status')
                tracer=re.search(r'^TracerPid:\s+(\d+)',final_status,re.M)
                require(tracer and tracer.group(1)=='0','owned first PID still traced after cleanup')
                d['owned_PID_TracerPid_zero_after_cleanup']=True
        except BaseException as e:
            d['cleanup_errors'].append('owned PID resume: ' + repr(e))
        d['owned_debugger_cleanup_complete'] = not d['cleanup_errors']
        d['snapshot_complete'] = (not d.get('tool_error') and d.get('snapshot',{}).get('status')=='SDK_FREE_SPAN_CPU_VIEW_SNAPSHOT_COMPLETE' and not d['cleanup_errors'])
        d['inferior_calls'] = False
        d['status'] = 'passed' if d['snapshot_complete'] else 'inconclusive_tool_or_metadata'
        (out / 'debugger-result.json').write_text(json.dumps(d, indent=2) + '\n')
    # No-stops/late attach is diagnostic inconclusive, never a runtime failure.
    return 1 if d.get('tool_error') or d['cleanup_errors'] else 0


if __name__ == '__main__':
    sys.exit(main())
