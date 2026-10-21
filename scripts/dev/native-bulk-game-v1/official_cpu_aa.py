"""Arrange existing official record/Perfetto processes; no target timing code."""
import hashlib
import json
import os
import select
import signal
import subprocess
import sys
import uuid
from pathlib import Path


def started(process, console, wait=10):
    """Use Simpleperf's official --start_profiling_fd acknowledgement."""
    output = bytearray()
    while b'STARTED' not in output:
        readable, _, _ = select.select([process.stdout], [], [], wait)
        if not readable:
            raise RuntimeError('official Simpleperf STARTED acknowledgement timed out')
        chunk = os.read(process.stdout.fileno(), 4096)
        if not chunk:
            raise RuntimeError('record exited before official STARTED acknowledgement')
        output.extend(chunk)
        console.write(chunk)
        console.flush()
        if len(output) > 65536:
            raise RuntimeError('STARTED absent from bounded official console prefix')
    return bytes(output)


def bracket(first, last):
    result = {'target': [v['frame_pacing']['target_fps'] for v in (first, last)],
              'PSO': [v['pipeline_prepare']['pipelines'] for v in (first, last)],
              'variants': [v['uniform_specialization']['variants'] for v in (first, last)],
              'scope': 'Original game-state endpoint bracket, not tool elapsed time or frame count'}
    result['stable_target60_PSO_variants'] = (result['target'] == [60, 60] and
        result['PSO'][0] == result['PSO'][1] and result['variants'][0] == result['variants'][1])
    return result


def run(*, folder, base, package, pid, stack, phase, endpoint, app, ad, exact,
        snapshot, save, env, scheduler):
    assert stack == 49152
    phase.update(scope='Same-binary same-PID A/A: three official30s cpu-clock:u99Hz records; '
                       'each contains an intended original8s Perfetto window, plus Perfetto-only bookends',
                 AA_record_seconds=30, AA_repetitions=3, AA_DWARF_bytes=stack,
                 AA_keep_failed_debug_info=True,
                 AA_method_change='49152B stacks and official failed-unwind debug info; original8s16KiB record is not a strict A/A counterpart',
                 AA_windows=[], AA_overlap_accepted=False,
                 AA_overlap_scope='Pending offline verification: official sample MONOTONIC timestamps '
                                  'must be mapped using Perfetto clock_snapshot; live processes alone are insufficient')
    stop_handlers = {n: signal.getsignal(n) for n in (signal.SIGINT, signal.SIGTERM)}

    def stop(signum, _):
        raise KeyboardInterrupt('owned A/A capture cancelled by signal ' + str(signum))

    def trace(name):
        first = endpoint('before-' + name)
        assert first['frame_pacing']['target_fps'] == 60, 'A/A target changed; do not override thermal/runtime policy'
        snapshot(name + '-before')
        output = folder / (name + '.pftrace')
        command = [sys.executable, str(scheduler), '--serial', base[-1], '--seconds', '8',
                   '--buffer-mb', '64', '--app-markers', '--clean-atrace-print',
                   '--frame-timeline', '--output', str(output)]
        with (folder / (name + '-console.log')).open('w') as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                assert process.wait(timeout=50) == 0, 'official scheduler failed'
            finally:
                # The existing helper owns an8s trace and its remote pull/removal.
                # Let that bounded cleanup finish before the original game save/restore.
                if process.poll() is None:
                    try:
                        process.wait(timeout=50)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        process.wait(timeout=10)
                        raise
        exact()
        report = json.loads(output.with_suffix('.json').read_text())
        assert report['pid'] == int(pid) and report['seconds_requested'] == 8
        assert report['shader_chain'] == '9507875007c6'
        assert all(report[k] for k in ('app_markers_requested', 'clean_atrace_print_requested',
                                       'frame_timeline_requested'))
        snapshot(name + '-after')
        last = endpoint('after-' + name)
        phase[name + '_bracket'] = bracket(first, last)
        save()
        return {'command': command, 'report': str(output.with_suffix('.json')),
                'trace_sha256': report['trace_sha256'], 'bracket': phase[name + '_bracket']}

    for name in stop_handlers:
        signal.signal(name, stop)
    try:
        phase['AA_Perfetto_only_before'] = trace('AA-Perfetto-only-before')
        for ordinal in range(3):
            name = 'AA-record-' + str(ordinal + 1)
            first = endpoint('before-' + name)
            assert first['frame_pacing']['target_fps'] == 60, 'A/A target changed; no continued comparison'
            token = 'native-AA-' + uuid.uuid4().hex
            remote = 'files/runtime/profiles/' + token + '.data'
            remote_pid = 'files/runtime/profiles/' + token + '.pid'
            existed = app('sh', '-c', 'if test -d files/runtime/profiles; then echo DIR; fi') == 'DIR'
            assert app('sh', '-c', 'if test -e "$1" || test -e "$2"; then echo EXISTS; fi',
                       'sh', remote, remote_pid) == ''
            prop = ad('shell', 'getprop', 'security.perf_harden')
            assert prop in ('0', '1')
            command = ['/system/bin/simpleperf', 'record', '--in-app', '-p', pid,
                       '--duration', '30', '-e', 'cpu-clock:u', '-f', '99',
                       '--call-graph', 'dwarf,' + str(stack), '--post-unwind=yes',
                       '--no-callchain-joiner', '--keep-failed-unwinding-result',
                       '--keep-failed-unwinding-debug-info',
                       '--start_profiling_fd', '1', '-o', remote]
            row = {'ordinal': ordinal + 1, 'command': command, 'owned_PID': int(pid),
                   'duration_seconds_requested': 30, 'sample_frequency_Hz': 99,
                   'DWARF_bytes': stack, 'overlap_accepted': False}
            phase['AA_windows'].append(row)
            save()
            process = None
            try:
                app('mkdir', '-p', 'files/runtime/profiles')
                if prop != '0':
                    ad('shell', 'setprop', 'security.perf_harden', '0')
                # Original owned PID-file pattern; inherited stdout is the official ready fd.
                import shlex
                shell = 'echo $$ > ' + shlex.quote(remote_pid) + '; exec ' + shlex.join(command)
                with (folder / (name + '-simpleperf-console.log')).open('wb') as log:
                    process = subprocess.Popen([*base, 'shell', shlex.join(
                        ['run-as', package, 'sh', '-c', shell])], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT)
                    row['host_adb_PID'] = process.pid
                    row['official_ready_prefix_hex'] = started(process, log).hex()
                    row['official_STARTED_received'] = True
                    save()
                    assert process.poll() is None, 'record exited before concurrent trace'
                    row['Perfetto'] = trace(name + '-Perfetto')
                    row['record_process_alive_after_scheduler'] = process.poll() is None
                    tail, _ = process.communicate(timeout=90)
                    log.write(tail)
                    row['record_returncode'] = process.returncode
                    assert process.returncode == 0, 'official record failed'
                exact()
                raw = subprocess.run([*base, 'exec-out', 'run-as', package, 'cat', remote],
                                     capture_output=True, check=True, timeout=90).stdout
                assert raw[:8] == b'PERFILE2' and len(raw) > 1024, 'invalid official record'
                path = folder / (name + '.data')
                path.write_bytes(raw)
                row.update(data=str(path), data_sha256=hashlib.sha256(raw).hexdigest(),
                           bytes=len(raw), capture_complete=True)
            except BaseException as error:
                row['error'] = repr(error)
                raise
            finally:
                try:
                    # Same original command+token ownership check; never kill the game PID.
                    tool_pid = app('sh', '-c', 'if test -f "$1"; then cat "$1"; fi', 'sh', remote_pid)
                    row['owned_simpleperf_PID'] = tool_pid
                    if process is not None:
                        row['record_returncode'] = process.poll()
                    if tool_pid.isdecimal():
                        cmd = app('sh', '-c', 'if test -f /proc/"$1"/cmdline; then cat /proc/"$1"/cmdline; fi',
                                  'sh', tool_pid)
                        if '/system/bin/simpleperf' in cmd and token in cmd:
                            app('kill', '-TERM', tool_pid)
                    if process is not None and process.poll() is None:
                        try:
                            process.communicate(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.communicate(timeout=5)
                            raise
                    for path in (remote_pid, remote):
                        present = app('sh', '-c', 'if test -f "$1"; then sha256sum "$1"; fi', 'sh', path)
                        if present:
                            app('sh', '-c', 'set -eu; test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"',
                                'sh', path, present.split()[0])
                    if not existed:
                        app('rmdir', 'files/runtime/profiles')
                except Exception as error:
                    row['cleanup_error'] = repr(error)
                    raise
                finally:
                    try:
                        if prop != '0':
                            ad('shell', 'setprop', 'security.perf_harden', prop)
                        assert ad('shell', 'getprop', 'security.perf_harden') == prop
                    finally:
                        row['record_returncode'] = process.poll() if process is not None else None
                        (folder / (name + '.metadata.json')).write_text(json.dumps(row, indent=2) + '\n')
                        save()
            row['bracket'] = bracket(first, endpoint('after-' + name))
            (folder / (name + '.metadata.json')).write_text(json.dumps(row, indent=2) + '\n')
            save()
        phase['AA_Perfetto_only_after'] = trace('AA-Perfetto-only-after')
        phase['stable_target60_PSO_variants_all_windows'] = all(
            v['stable_target60_PSO_variants'] for k, v in phase.items() if k.endswith('_bracket')) and all(
                w['bracket']['stable_target60_PSO_variants'] for w in phase['AA_windows'])
        phase['public_thermal_only'] = True
        phase['complete'] = True  # Capture only; actual overlap and A/A stability remain offline checks.
        save()
    finally:
        for name, handler in stop_handlers.items():
            signal.signal(name, handler)
        try:
            (folder / 'thermal-public-api.log').write_text(ad('logcat', '-d', '--pid=' + pid,
                                                            '-s', 'DmThermal:I', 'AndroidRuntime:E'))
        except Exception as error:
            phase['AA_public_thermal_error'] = repr(error)
        save()
