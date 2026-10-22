#!/usr/bin/env python3
"""Official LLDB bounded startup allocator provenance. Caller owns launch/save2/restore."""
from pathlib import Path
import hashlib
import argparse
import json
import os
import re
import selectors
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

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def owned_pid(current, expected):
    return current.isdecimal() and current == expected


def validate_startup(config):
    p=config.get('startup_allocation')
    require(isinstance(p,dict) and set(p)=={'mode','minimum_bytes','max_returns','natural_wait_seconds','metadata_budget_bytes'}
            and p['mode']=='f75-main-allocator-large-return-v1' and p['minimum_bytes']==134217728
            and type(p['max_returns']) is int and 1<=p['max_returns']<=16
            and p['natural_wait_seconds']==60 and p['metadata_budget_bytes']==1048576,
            'unknown/unbounded startup allocation policy')
    require(config['ELF_sha256']=='4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618'
            and config['ELF_BuildID']=='6e6f12c5c5f7a858704a84ee294b19d95aa3f894',
            'startup allocator requires exact reviewed f75 host')
    require(config['sdk_original']['sha256']=='1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017'
            and config['sdk_flat']['sha256']=='f5682a032c0884fc7d8553adb0f7445c3f0ae92c961a9f95b6593d7f487b6ea0',
            'startup allocator SDK differs from original static proof')

def wait_jdwp_pid(adb_base, pid, out, timeout=20):
    """Consume official adb output, without requiring EOF after a complete PID line."""
    require(pid.isdecimal(), 'invalid owned JDWP PID')
    out = Path(out)
    command = [*adb_base, 'jdwp']
    result = dict(command=command, expected_PID=int(pid), timeout_seconds=timeout,
                  complete_PID_line_observed=False, stdout_EOF=False, stderr_EOF=False,
                  byte_limit=65536, observed_bytes=0, timed_out=False,
                  scope='Observed official adb output only; no JDWP protocol implementation')
    captured = {'stdout': bytearray(), 'stderr': bytearray()}
    pending = bytearray()
    child = None
    selector = selectors.DefaultSelector()
    try:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 stdin=subprocess.DEVNULL, bufsize=0)
        result['owned_local_reader_PID'] = child.pid
        for name in captured:
            pipe = getattr(child, name)
            os.set_blocking(pipe.fileno(), False)
            selector.register(pipe, selectors.EVENT_READ, name)
        deadline = time.monotonic() + timeout
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                result['timed_out'] = True
                break
            for key, _ in selector.select(remaining):
                chunk = os.read(key.fileobj.fileno(), 4096)
                name = key.data
                if not chunk:
                    result[name + '_EOF'] = True
                    selector.unregister(key.fileobj)
                    continue
                if result['observed_bytes'] + len(chunk) > result['byte_limit']:
                    result['output_truncated_at_byte_limit'] = True
                    raise RuntimeError('official adb JDWP output exceeded bounded budget')
                captured[name].extend(chunk)
                result['observed_bytes'] += len(chunk)
                if name == 'stdout':
                    pending.extend(chunk)
                    while b'\n' in pending:
                        line, _, tail = pending.partition(b'\n')
                        pending = bytearray(tail)
                        if line.removesuffix(b'\r') == pid.encode('ascii'):
                            result['complete_PID_line_observed'] = True
            if result['complete_PID_line_observed']:
                break
        result['returncode_before_owned_cleanup'] = child.poll()
        require(result['returncode_before_owned_cleanup'] in (None, 0),
                'official adb JDWP reader failed')
        require(result['complete_PID_line_observed'],
                'owned process has no complete waiting JDWP PID line within bounded output/window')
        result['reader_live_at_match'] = child.poll() is None
        result['qualified'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        try:
            if child is not None:
                if child.poll() is None:
                    result['owned_reader_terminate_requested'] = True
                    child.terminate()
                    try:
                        child.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        result['owned_reader_kill_requested'] = True
                        child.kill()
                        child.wait(timeout=2)
                else:
                    child.wait(timeout=2)
                result['owned_reader_reaped'] = child.poll() is not None
                result['returncode_after_owned_cleanup'] = child.returncode
        except BaseException as error:
            result['qualified'] = False
            result['cleanup_error'] = repr(error)
            raise
        finally:
            selector.close()
            if child is not None:
                for name in captured:
                    getattr(child, name).close()
            for name, data in captured.items():
                (out / ('official-adb-jdwp-' + name + '.bin')).write_bytes(data)
                result[name + '_sha256'] = hashlib.sha256(data).hexdigest()
                result[name + '_bytes'] = len(data)
            (out / 'official-adb-jdwp-readiness.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def release_official_jdb(child, log, timeout=20):
    """NDK SocketAttach/print/settle/exit sequence, with both writes flushed."""
    magic = b'__verify_jdb_has_started__'
    selector = selectors.DefaultSelector()
    selector.register(child.stdout, selectors.EVENT_READ)
    os.set_blocking(child.stdout.fileno(), False)
    deadline = time.monotonic() + timeout
    output = bytearray()
    acknowledged = False
    try:
        child.stdin.write(b'print "' + magic + b'"\n')
        child.stdin.flush()
        while True:
            remaining = deadline - time.monotonic()
            require(remaining > 0, 'official JDB readiness/exit deadline')
            require(selector.select(remaining), 'official JDB readiness/exit deadline')
            chunk = os.read(child.stdout.fileno(), 4096)
            if not chunk:
                break
            log.write(chunk); log.flush()
            require(len(output) + len(chunk) <= 65536, 'official JDB output exceeded bound')
            output.extend(chunk)
            if magic in output and not acknowledged:
                acknowledged = True
                time.sleep(.3)  # Original NDK settling delay for Android debugger polling.
                child.stdin.write(b'exit\n')
                child.stdin.flush()
        child.wait(timeout=max(.01, deadline-time.monotonic()))
        require(acknowledged and child.returncode == 0, 'official JDB release not acknowledged')
        return dict(command_writes_flushed=True,magic_observed=True,
                    settle_seconds=.3,returncode=child.returncode,output_bytes=len(output))
    finally:
        selector.close()
        child.stdin.close()
        child.stdout.close()


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
    require(required <= set(config) <= required|{'startup_allocation'},'unknown/missing config fields')
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
    validate_startup(config)
    ELF=Path(config['ELF']).resolve();ELF_SHA=config['ELF_sha256'];BUILD_ID=config['ELF_BuildID'];APK_SHA=config['APK_sha256'];SERIAL=config['serial']
    require(sha(ELF)==ELF_SHA and sha(config['canonical_source_ready'])==config['canonical_source_ready_sha256'],'local canonical input changed')
    ready=json.loads(Path(config['canonical_source_ready']).read_text())
    require(ready['canonical_APK_sha256']==APK_SHA and ready['canonical_native_library_sha256']['libdmhost_android.so']==ELF_SHA and ready['canonical_host_ELF_build_id']==BUILD_ID,'canonical source-ready identity mismatch')
    for name in ('sdk_original', 'sdk_flat'):
        row=config[name]
        require(set(row)=={'path','sha256'} and sha(row['path'])==row['sha256'], 'SDK input changed: '+name)
    if args.plan:
        print(json.dumps({'status':'LOCAL_INPUTS_VALID_NO_ADB','config':str(args.config),'config_sha256':args.config_sha256,'ELF_sha256':ELF_SHA,'APK_sha256':APK_SHA,'scope':'early pending host breakpoint, official JDWP release, bounded configured (max16) original-SDK allocator returns; no target calls/profile/reclamation'}));return 0
    pid, out = args.pid, args.output.resolve()
    require(out.is_dir(), 'caller must create owned output directory')
    require(not (out / 'debugger-result.json').exists(), 'output already used')
    adb_base = [str(R / '.local/android-sdk/platform-tools/adb'), '-s', SERIAL]
    server = client = java_release = None
    java_forward_owned = False
    forward = False
    java_argv = ['/usr/lib/jvm/java-21-openjdk-amd64/bin/jdb', '-connect',
                 'com.sun.jdi.SocketAttach:hostname=localhost,port=65534']
    d = {'PID': int(pid), 'ELF_sha256': sha(ELF), 'ELF_BuildID': BUILD_ID,
         'capture_protocol': 'sdk-startup-large-returns-v1', 'config_sha256': args.config_sha256,
         'scope': 'Only bounded official startup debugger; caller owns original game/save/restore', 'cleanup_errors': []}

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
        require('tcp:65534' not in adb('forward','--list'), 'NDK official JDWP port already owned')
        d['official_JDWP_readiness'] = wait_jdwp_pid(adb_base, pid, out)
        maps=adb('exec-out','run-as',PKG,'cat','/proc/'+pid+'/maps')
        (out/'native-debugger-maps-before-JDWP.txt').write_text(maps+'\n')
        require('/libdmhost_android.so' not in maps, 'native host already loaded; startup hold not proven')
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
            'script e=lldb.SBError();t=lldb.debugger.CreateTarget(None,"aarch64-linux-android",None,False,e);assert e.Success() and t.IsValid() and not t.GetExecutable().IsValid()',
            'gdb-remote 127.0.0.1:' + PORT,
            'script t=lldb.debugger.GetSelectedTarget();assert t.GetExecutable().IsValid() and t.GetExecutable().GetFilename()!="libdmhost_android.so"',
            'target modules add ' + shlex.quote(str(ELF)),
            'script t=lldb.debugger.GetSelectedTarget();m=t.FindModule(lldb.SBFileSpec(' + repr(str(ELF)) + '));assert m.IsValid() and m.GetUUIDString().replace("-", "").lower()==' + repr(BUILD_ID) + ' and m.GetObjectFileHeaderAddress().GetLoadAddress(t)==lldb.LLDB_INVALID_ADDRESS',
            'settings set auto-confirm true',
            'process handle SIGSEGV -s false -n false -p true',
            'process handle SIGBUS -s false -n false -p true',
            'process handle SIGCONT -s false -n false -p true',
            'process handle SIGSTOP -s false -n false -p false',
            'command script import ' + shlex.quote(str(HERE / 'sdk_startup_allocations.py')),
            'script sdk_startup_allocations.snapshot(lldb.debugger)', ''])
        cmd = out / 'native-debugger-commands.txt'
        cmd.write_text(commands)
        env = os.environ.copy()
        env.update(LD_LIBRARY_PATH=str(R / '.local/tools/lldb19/usr/lib/x86_64-linux-gnu'),
            PYTHONPATH=str(R / '.local/tools/lldb19/usr/lib/llvm-19/lib/python3.13/site-packages'),
            DM_SDK_FREE_PYTHON=sys.executable, DM_SDK_FREE_OUTPUT=str(out), DM_SDK_FREE_PID=pid,
            DM_SDK_FREE_BUILD_ID=BUILD_ID, DM_SDK_FREE_ELF_SHA256=ELF_SHA, DM_SDK_FREE_SERIAL=SERIAL,
            DM_SDK_FREE_ADB=adb_base[0], DM_SDK_FREE_CONFIG=str(Path(args.config).resolve()))
        with (out / 'native-debugger.log').open('w') as log:
            client = subprocess.Popen([str(R / '.local/tools/lldb19/usr/lib/llvm-19/bin/lldb'),
                '--batch', '-s', str(cmd)], stdout=log, stderr=subprocess.STDOUT, env=env)
            arm_deadline=time.monotonic()+20
            while not (out/'pending-startup-armed.json').exists():
                require(client.poll() is None, 'official LLDB exited before pending startup breakpoint was armed')
                require(time.monotonic()<arm_deadline, 'pending startup arm handshake deadline')
                time.sleep(.05)
            armed=json.loads((out/'pending-startup-armed.json').read_text())
            require(armed['PID']==int(pid) and armed['ELF_sha256']==ELF_SHA
                    and armed['pending_host_breakpoint']=='guest_thread_init' and armed['inferior_calls'] is False,
                    'pending startup arm identity differs')
            d['pending_startup_armed']=armed
            require(owned_pid(current(),pid), 'owned PID changed before JDWP release')
            require('tcp:65534' not in adb('forward','--list'), 'JDWP port changed ownership before official release')
            d['official_JDWP_release_argv']=java_argv
            adb('forward', 'tcp:65534', 'jdwp:' + pid)
            java_forward_owned=True
            with (out/'official-NDK-jdb-release.log').open('wb') as jlog:
                # Official JDB, same original NDK SocketAttach/print/settle/exit.
                # Explicit pipe flush avoids the packaged NDK28 buffered-write block.
                java_release=subprocess.Popen(java_argv,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                                              stderr=subprocess.STDOUT,start_new_session=True)
                d['official_JDWP_release'] = release_official_jdb(java_release,jlog)
            d['official_JDWP_release_returncode']=java_release.returncode
            try:
                client.wait(timeout=110)
            except subprocess.TimeoutExpired:
                d['bounded_tool_timeout']=True
            try:
                client.wait(timeout=1)
            except subprocess.TimeoutExpired:
                d['bounded_tool_timeout'] = True
        p = out / 'startup-allocations.json'
        snapshot = json.loads(p.read_text()) if p.exists() else None
        require(snapshot and snapshot['PID']==int(pid) and snapshot['ELF_sha256']==ELF_SHA,'missing/mismatched one-stop snapshot')
        d['snapshot']=snapshot
        require(snapshot.get('status')=='SDK_STARTUP_LARGE_RETURNS_CAPTURE_COMPLETE' and snapshot.get('complete') is True
                and snapshot.get('inferior_calls') is False and not snapshot['errors'],
                'startup allocation capture incomplete; inspect original errors')
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
        if java_release is not None and java_release.poll() is None:
            try:
                require(os.getpgid(java_release.pid)==java_release.pid, 'owned NDK wakeup group changed')
                os.killpg(java_release.pid,signal.SIGTERM)
                try:java_release.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(java_release.pid,signal.SIGKILL);java_release.wait(timeout=3)
                d['owned_NDK_jdb_group_cleaned']=True
            except BaseException as e:d['cleanup_errors'].append('owned NDK jdb: '+repr(e))
        if java_forward_owned:
            try:
                rows=[line.split() for line in adb('forward','--list').splitlines() if 'tcp:65534' in line.split()]
                require(not rows or rows==[[SERIAL,'tcp:65534','jdwp:'+pid]], 'JDWP forward ownership changed; preserve replacement')
                if rows:adb('forward','--remove','tcp:65534')
                d['owned_JDWP_forward_removed']=True
            except BaseException as e:d['cleanup_errors'].append('owned JDWP forward: '+repr(e))
        d['owned_debugger_cleanup_complete'] = not d['cleanup_errors']
        d['snapshot_complete'] = (not d.get('tool_error') and d.get('snapshot',{}).get('status')=='SDK_STARTUP_LARGE_RETURNS_CAPTURE_COMPLETE' and not d['cleanup_errors'])
        d['inferior_calls'] = False
        d['status'] = 'passed' if d['snapshot_complete'] else 'inconclusive_tool_or_metadata'
        (out / 'debugger-result.json').write_text(json.dumps(d, indent=2) + '\n')
    # No-stops/late attach is diagnostic inconclusive, never a runtime failure.
    return 1 if d.get('tool_error') or d['cleanup_errors'] else 0


if __name__ == '__main__':
    sys.exit(main())
