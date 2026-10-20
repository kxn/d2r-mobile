#!/usr/bin/env python3
"""Two fixed-.60 original-game resource phases; offline unless --execute."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shlex
import subprocess
import sys
import tarfile
import time
import uuid
import zipfile

R = Path('/home/dl/d2r-mobile')
HERE = Path(__file__).resolve().parent
OLD = HERE.parent / 'native-one-game-v1'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


heap = module('official_heap_capture', HERE / 'heap-capture.py')
malloc = module('official_malloc_debug', HERE / 'malloc-debug.py')
m = module('accepted_native_game_transaction', OLD / 'transaction.py')
installer = module('accepted_native_game_installer', OLD / 'installer.py')
sha, read, require, PKG = m.sha, m.read, m.require, m.PKG
DIRS = [('driver', m.DRIVER_DIR), ('temp', 'files/runtime/temp'),
        ('cache', 'cache'), ('modules', 'files/runtime/modules'),
        ('overlay', m.OVERLAY), ('native', 'files/runtime/native-one-resources')]
ALLOWED_LEAVES = {'ctrl_touch', 'touch_strings.tsv', 'data/.build.config',
                  'data/00-00000007.data', 'data/00-00000008.data',
                  'data/00-00000009.data'}


def pinned(row):
    path = Path(row['path']).resolve()
    require(sha(path) == row['sha256'], 'pinned input changed: ' + str(path))
    return path


def proof(path, digest):
    require(re.fullmatch('[a-f0-9]{64}', digest or '') is not None,
            'explicit input SHA256 required')
    require(sha(path) == digest, 'input manifest changed')
    p = read(path)
    require(p['schema'] == 1 and p['serial'] == '10CG570J59001U1', 'Q735 scope')
    baseline = read(pinned(p['baseline']))
    require(baseline['game_PID_absent'] is True, 'baseline must be stopped')
    ready = read(pinned(p['canonical']))
    raw = read(pinned(p['raw']))
    require(ready['commit'] == raw['source_commit'] == p['runtime_commit'],
            'raw module / host source revision differs')
    p['apk'] = str(pinned(ready['canonical_APK']['archive']))
    p['new_apk'] = ready['canonical_APK_sha256']
    p['new_elf'] = ready['canonical_native_library_sha256']['libdmhost_android.so']
    p['old_apk'] = str(pinned(p['original_apk']))
    p['driver_path'] = str(pinned(p['driver']))
    require(p['driver']['sha256'] ==
            'aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db',
            'exact AA native-image provider required')
    with zipfile.ZipFile(p['apk']) as apk:
        for name, h in ready['canonical_native_library_sha256'].items():
            require(hashlib.sha256(apk.read('lib/arm64-v8a/' + name)).hexdigest() == h,
                    'APK native library mismatch ' + name)
    files = raw['28_deployment_file_hashes']
    require(len(files) == 28, 'complete original-NSO bundle required')
    for name, h in files.items():
        require(sha(Path(raw['bundle']) / name) == h, 'raw module changed ' + name)
    p['raw_bundle'] = raw['bundle']
    p['modules'] = files
    p['baseline_data'] = baseline
    p['old'] = {'apk': baseline['APK_sha256'], 'elf': p['original_host_elf'],
                'chain': baseline['chain'], 'module_artifacts': {
                    Path(n).name: {'sha256': h} for n, h in
                    baseline['inventories']['files/runtime/modules'].items()
                    if not n.endswith('/.d2r-deploy-managed')}}
    p['new'] = {'apk': p['new_apk'], 'elf': p['new_elf'],
                'chain': baseline['chain'], 'module_artifacts': {
                    n: {'sha256': h} for n, h in files.items()}}
    require(len(p['prerequisites']) >= 2, 'actual fixture and bake receipts required')
    for row in p['prerequisites']:
        pinned(row)
    if 'counter_snapshot' in p:
        counter = p['counter_snapshot']
        require(set(counter) == {'ready', 'config', 'server'}, 'counter snapshot fields differ')
        counter_ready = read(pinned(counter['ready']))
        require(counter_ready['revision'] == '4984509f629b9ec71b3a3b6366fee7895f83bd02',
                'reviewed once-only counter helper required')
        counter['helper_path'] = str(pinned(counter_ready['helper']))
        pinned(counter_ready['capture_module'])
        tool_inputs = read(pinned(counter_ready['tool_inputs']))
        for row in tool_inputs['tools']:
            pinned(row)
        config = read(pinned(counter['config']))
        require(config['serial'] == p['serial'] and config['APK_sha256'] == p['new_apk']
                and config['ELF_sha256'] == p['new_elf']
                and config['canonical_source_ready_sha256'] == p['canonical']['sha256']
                and config['tool_inputs_sha256'] == counter_ready['tool_inputs']['sha256'],
                'counter helper target differs from paired runtime')
        require(counter['server']['sha256'] ==
                'fb970fd0800b1f886ce7a366cf361e57f302ef08bb769f9eb0b85e5fc6823d3e',
                'exact reviewed official NDK server required')
        pinned(counter['server'])
        subprocess.run([sys.executable, counter['helper_path'], '12345', '/tmp',
                        '--config', counter['config']['path'], '--config-sha256',
                        counter['config']['sha256'], '--plan'], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    require([v['kind'] for v in p['phases']] == ['control', 'native'], 'matched pair order')
    for phase in p['phases']:
        composition = read(pinned(phase['composition']))
        require(composition['complete'] is True and composition['game_acceptance'] is False,
                'offline composition status changed')
        require(composition['selected_logicals_sha256'] == p['selected_logicals_sha256'],
                'different selected resource set')
        require(set(phase['overlay']) == ALLOWED_LEAVES, 'exact six resource delta leaves required')
        root = Path(phase['overlay_root'])
        for n, h in phase['overlay'].items():
            # UI leaves in the composer are deliberate links to immutable,
            # separately hashed upstream files. Add only these named leaves
            # with dereference=True; never walk the linked raw archive tree.
            require((root / n).resolve().is_file() and sha(root / n) == h,
                    'overlay delta changed ' + n)
        if phase['kind'] == 'native':
            table = pinned(phase['native_table'])
            require(sha(table) == composition['native_table_sha256'], 'table mismatch')
            require(composition['driver_sha256'] == p['driver']['sha256'], 'native provider mismatch')
            phase['pack_sha'] = composition['native_pack_content_sha256']
            phase['producer_sha'] = composition['producer_source_sha256']
        else:
            require(composition['native_table_present'] is False and composition['DMN2_markers'] is False,
                    'control contains native markers')
        phase['composition_data'] = composition
    p['controller_sources'] = {str(q): sha(q) for q in
                               [Path(__file__), OLD / 'transaction.py', OLD / 'installer.py',
                                OLD / 'identity.py', m.COMMON,
                                m.SOURCE / 'scripts/dev/android-game-launch.py',
                                R / 'scripts/dev/android-game-background-save.py',
                                R / 'scripts/dev/android-game-walk-to.py']}
    if 'heap_profile' in p:
        p['controller_sources'].update(heap.proof(p, pinned, read, require))
    if 'malloc_debug' in p:
        p['controller_sources'].update(malloc.proof(p, pinned, read, require))
    return p


def expected_baseline(p, target):
    value = p['baseline_data']['inventories'].get(target)
    return None if value is None else {n.removeprefix(target + '/'): h for n, h in value.items()}


def owned(label, inv, d, p, phase):
    if inv is None or inv.get(m.OWNER) != d['owner_sha256']:
        return False
    if label == 'modules':
        return inv == {m.OWNER: d['owner_sha256'], **p['modules']}
    if label == 'overlay':
        return inv == {m.OWNER: d['owner_sha256'], **phase['overlay']}
    if label == 'native':
        extra = {'native-layouts.bin': phase['native_table']['sha256']} if phase['kind'] == 'native' else {}
        return inv == {m.OWNER: d['owner_sha256'], **extra}
    if label == 'driver':
        return inv.get('libvulkan_freedreno.so') == p['driver']['sha256'] and all(
            n in (m.OWNER, 'libvulkan_freedreno.so') or n.startswith(
                ('shader-cache/', 'mesa_shader_cache/', '.cache/')) for n in inv)
    return True  # Only the token-owned new cache/temp, never original backups.


def phase_game(dev, out, phase, p, token):
    out.mkdir()
    d = {'serial': p['serial'], 'kind': phase['kind'], 'remote_screenshot':
         '/data/local/tmp/d2r-native-bulk-' + token + '-' + phase['kind'] + '.png'}
    report = out / 'phase.json'
    save = lambda: m.journal(report, d)
    command = [sys.executable, str(m.SOURCE / 'scripts/dev/android-game-launch.py'),
               '--serial', p['serial'], '--external-profile', '--custom-vulkan-driver',
               '--vulkan-provider', 'adreno', '--shared-guest-heap', '--no-legacy-astc-copy',
               '--no-astc-decode-unorm8', '--no-defer-pool-publication', '--scale-3d', '.60',
               '--fixed-3d', '--pool-copy-accounting', '--game-res', '2750x1260',
               '--timeout', '90', '--report', str(out / 'launch.json'),
               '--pool-window-bytes', '16777216', '--expect-shared-import-windows']
    if phase['kind'] == 'native':
        command += ['--native-resource-table', '/data/user/0/' + PKG + '/' + DIRS[-1][1] + '/native-layouts.bin',
                    '--native-resource-pack-sha256', phase['pack_sha'],
                    '--native-resource-producer-sha256', phase['producer_sha']]
    d['launch_command'] = command
    save()
    own = None
    ambiguous = False
    launcher = None
    capture = None

    def profile_journal(value):
        d['heap_profile'] = value
        save()

    def pid():
        return dev.adb('shell', 'pidof ' + PKG + ' || true')

    def observe():
        nonlocal own, ambiguous
        now = pid()
        try:
            own = m.claim_pid(d, now, own)
        except RuntimeError:
            ambiguous = True
            raise
        if own:
            d['first_PID'] = int(own)
        save()
        return now

    try:
        if p.get('heap_profile'):
            require(not pid(), 'heap startup profile requires no game PID')
            capture = heap.Capture(dev, out / 'official-heap-profile', p['heap_profile'], profile_journal)
            if not capture.start():
                d['heap_profile_arm_failed_no_game_launch'] = True
                save()
                return d
            require(not pid(), 'game started outside owned launcher while arming profile')
        with (out / 'launch.log').open('w') as f:
            launcher = subprocess.Popen(command, stdout=f, stderr=subprocess.STDOUT, cwd=R)
            deadline = time.monotonic() + 120  # A bounded tool observation, not a profiler.
            while launcher.poll() is None:
                observe()
                require(time.monotonic() < deadline, 'owned launcher observation deadline')
                time.sleep(.3)
            observe()
            require(launcher.returncode == 0, 'original HUD launcher failed')
        launch = read(out / 'launch.json')
        require(own and launch['pid'] == int(own) and launch.get('original_hud') is True,
                'original game HUD / owned PID mismatch')
        uid = dev.app('id', '-u')
        status = dev.app('cat', '/proc/' + own + '/status')
        ids = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
        require(uid.isdecimal() and 10000 <= int(uid) < 200000 and ids
                and all(v == uid for v in ids.groups()), 'ordinary app UID mismatch')
        (out / 'owned-status.txt').write_text(status)
        d['ordinary_UID'] = int(uid)
        require(launch['actual_guest_heap']['shared'] is True
                and launch['actual_vulkan_provider'] == 'adreno'
                and launch['requested_legacy_astc_copy'] is False, 'shared/provider contract mismatch')
        initial = read_state(dev, out, 'before-route')
        m.screenshot_collect(dev, own, d, report, out, 'HUD-before-route')
        m.original_route(dev, own, d, report, out, initial, None)
        current = read_state(dev, out, 'after-route')
        if p.get('malloc_debug'):
            d['malloc_debug']=malloc.collect(dev,out/'official-malloc-debug',p['malloc_debug'],own,d['ordinary_UID'],lambda value:(d.update(malloc_debug=value),save()))
            save()
        if capture:
            require(pid() == own, 'PID changed before official profile endpoint')
            capture.finish(own)
        if p.get('counter_snapshot') and (capture is None or capture.state['cleanup_confirmed']):
            require(pid() == own, 'PID changed before once-only counter snapshot')
            counter = p['counter_snapshot']
            snapshot_out = out / 'native-import-counter-snapshot'
            snapshot_out.mkdir()
            command = [sys.executable, counter['helper_path'], own, str(snapshot_out),
                       '--config', counter['config']['path'], '--config-sha256',
                       counter['config']['sha256']]
            d['counter_snapshot_command'] = command
            save()
            # No concurrent profile window: one official debugger attach reads
            # existing globals, detaches, and removes its owned server/forward.
            with (snapshot_out / 'console.txt').open('w') as log:
                # The helper bounds its official debugger/ADB operations and
                # owns tracer cleanup. Do not SIGKILL it via run(timeout=...).
                subprocess.run(command, check=True, stdout=log,
                               stderr=subprocess.STDOUT)
            counter_result = read(snapshot_out / 'debugger-result.json')
            require(counter_result['counter_snapshot_accepted'] is True
                    and not counter_result['cleanup_errors']
                    and counter_result['owned_PID_TracerPid_zero_after_cleanup'] is True,
                    'once-only counter snapshot / detach failed')
            d['native_import_counters'] = counter_result['snapshot']
            save()
        require(pid() == own, 'PID changed before residency snapshot')
        (out / 'official-dumpsys-meminfo.txt').write_text(dev.adb('shell', 'dumpsys', 'meminfo', own))
        m.screenshot_collect(dev, own, d, report, out, 'HUD-after-route')
        require(current['valid'] and current['world_input_ready'], 'original world not input-ready')
        require(current['scale_3d'] == .60 and current['game_output'] == current['surface_output']
                == {'width': 2750, 'height': 1260}, 'fixed resolution mismatch')
        require(current['pool_windows']['window_bytes'] == 16777216
                and current['guest_heap']['shared'] is True, 'actual shared-window mismatch')
        native = dev.app('cat', 'files/runtime/native.log')
        (out / 'native.log').write_text(native)
        require('[vulkan] device=Turnip Adreno (TM) 735 ' in native and 'FATAL' not in native,
                'wrong driver or native failure')
        if phase['kind'] == 'native':
            require('[native resources] fixed table installed,' in native,
                    'authenticated native table was not installed')
        else:
            require('[native resources] fixed table installed,' not in native,
                    'native table unexpectedly present in standard control')
        d['original_HUD_reached'] = True
        d['game_modules_executed'] = True
        d['scope'] = 'Functional original-game HUD/route and one system residency snapshot; not GPU sampling or FPS/heat acceptance'
        save()
    finally:
        if capture:
            # Even invalid/partial official data is not a runtime failure.
            # Complete tool cleanup first; if it cannot be confirmed, still do
            # the game's independent safe lifecycle cleanup below.
            capture.finish(own)
        if launcher is not None and launcher.poll() is None:
            launcher.terminate()
            launcher.wait(timeout=15)
        for leaf, args in [('native.log', None), ('crash-logcat.txt', ['logcat', '-b', 'crash', '-d', '-v', 'threadtime'])]:
            try:
                (out / leaf).write_text(dev.adb(*args) if args else dev.app('cat', 'files/runtime/native.log'))
            except Exception as e:
                d.setdefault('collection_errors', []).append(repr(e))
        for shot in d.get('screenshots', []):
            try:
                m.screenshot_pull_remove(dev, shot, report, d)
            except Exception as e:
                # A failed host pull must not bypass the original save/stop
                # below. Preserve its path/hash for explicit later cleanup.
                d.setdefault('screenshot_collection_errors', []).append(repr(e))
                save()
        try:
            now = pid()
            if now:
                require(not ambiguous and own and now == own, 'unknown/replacement PID; preserve transaction')
                if capture or p.get('malloc_debug'):
                    status = dev.app('cat', '/proc/' + own + '/status')
                    tracer = re.search(r'^TracerPid:\s+(\d+)\s*$', status, re.M)
                    require(tracer and tracer[1] == '0' and pid() == own, 'game tracer/nonowned PID prevents lifecycle cleanup')
                    d['game_cleanup_independent_of_profile_health'] = True
                    save()
                cmd = [sys.executable, str(R / 'scripts/dev/android-game-background-save.py'),
                       '--serial', p['serial'], '--report', str(out / 'fresh-save.json')]
                with (out / 'fresh-save.log').open('w') as f:
                    subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, check=True, timeout=45)
                fresh = read(out / 'fresh-save.json')
                require(fresh['fresh_original_lifecycle_save'] is True and fresh['save_commits'] == 2
                        and fresh['pid'] == int(own) and pid() == own, 'fresh save2 / owned PID failed')
                dev.adb('shell', 'am', 'force-stop', PKG)
                require(not pid(), 'owned game process still alive')
                d['fresh_save_then_stopped'] = True
            else:
                d['already_exited_no_save_restart'] = True
            save()
        finally:
            if capture and not capture.state['cleanup_confirmed']:
                capture.finish(own)  # Late receipt cleanup after independently stopping the game.
            if p.get('malloc_debug') and d.get('malloc_debug',{}).get('dump_complete') and (d.get('fresh_save_then_stopped') or d.get('already_exited_no_save_restart')):
                d['malloc_debug']['shim_process_exited']=True
                malloc.analyze_after_game_stop(out/'official-malloc-debug',p['malloc_debug'],d['malloc_debug'],lambda value:(d.update(malloc_debug=value),save()))
            save()
    return d


def read_state(dev, out, name):
    text = dev.app('cat', 'files/runtime/game-state.jsonl')
    (out / (name + '.jsonl')).write_text(text)
    return json.loads(text.splitlines()[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--inputs-sha256', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    p = proof(args.inputs, args.inputs_sha256)
    out = args.report.parent.resolve()
    require(out.is_relative_to(R / '.local/logs'), 'private report directory required')
    plan = {'offline': True, 'controller_sources': p['controller_sources'],
            'inputs_sha256': args.inputs_sha256, 'runtime': p['runtime_commit'],
            'phases': ['standard-ASTC4', 'native-ASTC4'], 'scale_3d': .60,
            'output': '2750x1260', 'profiling': bool(p.get('heap_profile') or p.get('malloc_debug')), 'recursive_RomFS_copy': False,
            'scope': 'Functional and system residency only, not sustained thermal/FPS proof'}
    if p.get('heap_profile'):
        plan.update(heap_profile_mode=p['heap_profile']['mode'],
                    heap_profile_effective_config=p['heap_profile']['effective_config'],
                    heap_profile_command_template=heap.capture_command(p['heap_profile'], 'UNIQUE_PHASE_EMPTY_OUTPUT'),
                    no_profiled_FPS_claim=True,
                    effective_phase_kinds=['control'] if p['heap_profile']['mode'] == 'control-only' else ['control', 'native'])
    if p.get('malloc_debug'):
        plan.update(malloc_debug=True,malloc_debug_mode='control-only',effective_phase_kinds=['control'],
                    malloc_debug_intrusive=True,no_profiled_FPS_or_PSS_claim=True,
                    malloc_debug_options=p['malloc_debug']['malloc_options'])
    if not args.execute:
        plan['execution_ready'] = p.get('execution_ready') is True
        print(json.dumps(plan, indent=2))
        return
    require(p.get('execution_ready') is True, 'actual candidate-driver fixture / root execution review still pending')
    require(not args.report.exists(), 'no retry same transaction')
    out.mkdir(parents=True, exist_ok=True)
    dev = m.common.Device(p['serial'])
    dev.identity(p['old'])
    require(dev.adb('shell', 'getprop', 'ro.product.model') == p['baseline_data']['model'], 'physical model changed')
    require(m.inventory(dev, 'shared_prefs') == expected_baseline(p, 'shared_prefs'), 'preferences changed')
    require(dev.adb('shell', 'settings', 'get', 'global', 'enable_gpu_debug_layers') in ('null', '0'), 'GPU layer active')
    require(not any(re.match(r'\[debug\.mesa\.[^]]*\]: \[.+\]', line)
                    for line in dev.adb('shell', 'getprop').splitlines()), 'Mesa override active')
    token = uuid.uuid4().hex
    stage = 'files/runtime/.native-bulk-' + token
    d = {'token': token, 'stage': 'prepared', 'plan': plan, 'cleanup_errors': [], 'phases': [],
         'owner_sha256': hashlib.sha256((token + '\n').encode()).hexdigest(), 'old_dirs': {},
         'private_stage': stage, 'inputs': str(args.inputs.resolve()), 'inputs_sha256': args.inputs_sha256,
         'published_kind': {}}
    write = lambda: m.journal(args.report, d)

    def original_check():
        dev.identity(p['old'])
        for _, target in DIRS:
            require(m.inventory(dev, target) == expected_baseline(p, target), 'original directory differs ' + target)

    original_check()
    for label, target in DIRS:
        d['old_dirs'][label] = m.inventory(dev, target)
    dev.app('mkdir', m.LOCK)
    dev.app('sh', '-c', 'printf "%s\\n" "$2" > "$1"', 'sh', m.LOCK + '/owner', token)
    write()
    created = False
    active = None
    try:
        dev.app('sh', '-c', 'test ! -e "$1"', 'sh', stage)
        dev.app('mkdir', '-p', stage + '/backup', stage + '/incoming')
        created = True
        owner = out / 'owner'
        owner.write_text(token + '\n')
        phase_plan = p['phases'][:1] if p.get('malloc_debug') or p.get('heap_profile', {}).get('mode') == 'control-only' else p['phases']
        for phase in phase_plan:
            active = phase
            name = phase['kind']
            incoming = stage + '/incoming/' + name
            payload = out / (name + '.tar')
            with tarfile.open(payload, 'x', dereference=True) as tar:
                tar.add(p['driver_path'], arcname='driver/libvulkan_freedreno.so', recursive=False)
                for n in p['modules']:
                    tar.add(Path(p['raw_bundle']) / n, arcname='modules/' + n, recursive=False)
                for n in sorted(phase['overlay']):
                    tar.add(Path(phase['overlay_root']) / n, arcname='overlay/' + n, recursive=False)
                if name == 'native':
                    tar.add(phase['native_table']['path'], arcname='native/native-layouts.bin', recursive=False)
                if p.get('counter_snapshot'):
                    tar.add(p['counter_snapshot']['server']['path'], arcname='temp/d2r-lldb-server', recursive=False)
                for label, _ in DIRS:
                    tar.add(owner, arcname=label + '/' + m.OWNER, recursive=False)
            remote = '/data/local/tmp/d2r-native-bulk-' + token + '-' + name + '.tar'
            d.setdefault('transfers', []).append({'path': remote, 'sha256': sha(payload)})
            write()
            dev.adb('push', str(payload), remote)
            require(dev.digest(remote) == sha(payload), 'resource transfer changed')
            dev.app('mkdir', incoming)
            dev.app('tar', 'xf', remote, '-C', incoming)
            if p.get('counter_snapshot'):
                dev.app('chmod', '700', incoming + '/temp/d2r-lldb-server')
            if name == 'control':
                for label, target in DIRS:
                    if d['old_dirs'][label] is not None:
                        m.rename_dir(dev, target, stage + '/backup/' + label, d['old_dirs'][label])
                    d.setdefault('backed_up', []).append(label)
                    write()
            else:
                for label, target in DIRS:
                    inv = m.inventory(dev, target)
                    require(owned(label, inv, d, p, p['phases'][0]), 'unowned control outputs')
                    m.rename_dir(dev, target, stage + '/after-control-' + label, inv)
                    d['published_kind'].pop(label, None)
                    write()
            for label, target in DIRS:
                inv = m.inventory(dev, incoming + '/' + label)
                require(owned(label, inv, d, p, phase), 'incoming resource mismatch ' + label)
                m.rename_dir(dev, incoming + '/' + label, target, inv)
                d['published_kind'][label] = name
                write()
            # Seed only the same known original PSO files. Never copy original logs or saves.
            if d['old_dirs']['temp'] is not None:
                for n, h in d['old_dirs']['temp'].items():
                    if not n.startswith('vulkan-pso/'):
                        continue
                    src = stage + '/backup/temp/' + n
                    require(dev.digest(src) == h, 'original PSO seed changed')
                    dest = 'files/runtime/temp/' + n
                    dev.app('mkdir', '-p', str(Path(dest).parent))
                    dev.app('cp', src, dest)
                    require(dev.digest(dest) == h, 'PSO seed copy mismatch')
            if name == 'control':
                installer.install(dev, Path(p['apk']), p['new_apk'], out / 'install-candidate.log', require, PKG)
            dev.identity(p['new'])
            d.setdefault('phase_reports', []).append(str(out / name / 'phase.json'))
            write()
            phase_result = phase_game(dev, out / name, phase, p, token)
            d['phases'].append(phase_result)
            write()
            if p.get('malloc_debug') and not phase_result.get('malloc_debug',{}).get('accepted'):
                d['profile_outcome']='malloc-debug-invalid-no-next-phase';d['stage']='malloc-debug-invalid-original-game-cleaned'
                write();break
            if p.get('heap_profile') and not phase_result.get('heap_profile', {}).get('accepted'):
                d['profile_outcome'] = 'invalid-or-partial-no-next-phase'
                d['stage'] = 'official-heap-profile-invalid-original-game-cleaned'
                write()
                break
            require(phase_result.get('original_HUD_reached') and phase_result.get('fresh_save_then_stopped'),
                    'phase did not reach HUD / fresh save2')
        owner.unlink()
        if p.get('malloc_debug'):
            if d.get('profile_outcome') is None:
                d['profile_outcome']='malloc-debug-control-only-valid';d['stage']='official-malloc-debug-control-only-valid'
            d['not_original_unprofiled_pair_acceptance']=True
        elif p.get('heap_profile'):
            if d.get('profile_outcome') is None:
                d['profile_outcome'] = 'control-only-valid' if p['heap_profile']['mode'] == 'control-only' else 'pair-valid'
                d['stage'] = 'official-heap-profile-' + d['profile_outcome']
            d['not_original_unprofiled_pair_acceptance'] = True
        else:
            d['stage'] = 'matched-resource-original-game-functional-pair-complete'
        write()
    except BaseException as e:
        d['error'] = repr(e)
        write()
    finally:
        try:
            require(not installer.UNRESOLVED, 'installer unresolved; preserve backups, do not restore')
            dev.stopped()
            require(dev.app('cat', m.LOCK + '/owner') == token, 'transaction owner changed')
            if created:
                for label, target in reversed(DIRS):
                    backup = stage + '/backup/' + label
                    old = d['old_dirs'][label]
                    stored = m.inventory(dev, backup)
                    inv = m.inventory(dev, target)
                    if stored is None and old is not None:
                        require(inv == old, 'missing original backup ' + label)
                        continue
                    require(stored == old, 'original backup changed ' + label)
                    if inv is not None:
                        current_phase = next((v for v in p['phases']
                                              if v['kind'] == d['published_kind'].get(label)), None)
                        require(current_phase and owned(label, inv, d, p, current_phase),
                                'unowned phase outputs ' + label)
                        m.rename_dir(dev, target, stage + '/after-final-' + label, inv)
                    if stored is not None:
                        m.rename_dir(dev, backup, target, stored)
            loc = dev.adb('shell', 'pm', 'path', PKG).removeprefix('package:')
            current_apk = dev.adb('shell', 'sha256sum', loc).split()[0]
            require(current_apk in (p['old']['apk'], p['new_apk']), 'unknown APK preserve')
            if current_apk == p['new_apk']:
                installer.install(dev, Path(p['old_apk']), p['old']['apk'], out / 'restore-original.log', require, PKG)
            original_check()
            require(m.inventory(dev, 'shared_prefs') == expected_baseline(p, 'shared_prefs'), 'preferences changed')
            d['original_restored'] = True
            write()
            for phase_report in d.get('phase_reports', []):
                if not Path(phase_report).is_file():
                    continue
                phase_data = read(phase_report)
                for shot in phase_data.get('screenshots', []):
                    require(shot['remote'].startswith('/data/local/tmp/d2r-native-bulk-' + token + '-'),
                            'unknown screenshot path; preserve')
                    m.screenshot_pull_remove(dev, shot, Path(phase_report), phase_data)
                    require(dev.digest(shot['remote']) is None, 'owned screenshot still present')
            if created:
                allowed = [stage + '/incoming/'] + [stage + '/after-' + n + '-' + label + '/'
                           for n in ('control', 'final') for label, _ in DIRS]
                files = dev.app('find', stage, '-type', 'f').splitlines()
                require(all(n.startswith(tuple(allowed)) for n in files), 'unknown staging file; preserve')
                for start in range(0, len(files), 40):
                    entries = []
                    for n in files[start:start + 40]:
                        entries.extend([n, dev.digest(n)])
                    dev.app('sh', '-c', 'set -eu; test -z "$(pidof "$1" || true)"; shift; '
                            'while test "$#" -gt 0; do test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; '
                            'rm "$1"; shift 2; done', 'sh', PKG, *entries)
                for n in sorted(dev.app('find', stage, '-type', 'd').splitlines(),
                                key=lambda value: (value.count('/'), value), reverse=True):
                    dev.app('rmdir', n)
            for row in d.get('transfers', []):
                if dev.digest(row['path']) is not None:
                    require(dev.digest(row['path']) == row['sha256'], 'transfer changed')
                    dev.adb('shell', 'rm', row['path'])
            dev.app('rm', m.LOCK + '/owner')
            dev.app('rmdir', m.LOCK)
            d['resource_cleanup_complete'] = True
            phase_states = [read(Path(q)) for q in d.get('phase_reports', []) if Path(q).is_file()]
            unconfirmed = [v['kind'] for v in phase_states if 'heap_profile' in v and not v['heap_profile']['cleanup_confirmed']]
            if unconfirmed:
                d['cleanup_errors'].append('official heap collector cleanup unconfirmed: ' + ','.join(unconfirmed))
            d['cleanup_complete'] = not unconfirmed
            write()
        except BaseException as e:
            d['cleanup_errors'].append(repr(e))
            write()
    print(json.dumps({k: d.get(k) for k in ['stage', 'error', 'profile_outcome', 'cleanup_errors', 'original_restored']}, indent=2))
    require(not d.get('error') and not d['cleanup_errors'] and d.get('cleanup_complete'),
            'original-game pair incomplete; preserve journal')
    if d.get('profile_outcome') in ('invalid-or-partial-no-next-phase','malloc-debug-invalid-no-next-phase'):
        raise SystemExit(2)  # Distinct tool-invalid result; not a runtime failure.


if __name__ == '__main__':
    main()
