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
import signal
import struct
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


AA_DRIVER = 'aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
LINEAR_DRIVER = 'f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
DRIVER_LAYOUTS = {AA_DRIVER: '4529bbb870cdbdd02db214742b3e4170',
                  LINEAR_DRIVER: 'ab91c540847448360a44496b50045159'}
DRIVER_IDENTITIES = {
    AA_DRIVER: DRIVER_LAYOUTS[AA_DRIVER] + '1e63ed8a4bbaa1bb6e51b8904fb7eb2f98db069284bbaa392518bad6a2c5d644',
    LINEAR_DRIVER: DRIVER_LAYOUTS[LINEAR_DRIVER] + '1e63ed8a4bbaa1bb6e51b8904fb7eb2f3baa219f622918ab3ddda14a19a7cba6'}


def provider_gate(driver_sha, acceptance, filter_acceptance=None):
    require(driver_sha in DRIVER_LAYOUTS, 'exact reviewed native-image provider required')
    if driver_sha == AA_DRIVER:
        return
    require(acceptance is not None and acceptance['sha256'] ==
            '9089fbe5d64b3842e8ae387506e6a22c9cc2034881a76e2711a675407f05af0f',
            'f830 original-NVN positive and old-AA refusal root review required')
    review = read(pinned(acceptance))
    require(review['status'] == 'ROOT_REVIEWED_F830_ACTUAL_ORIGINAL_NVN_PASS_AND_OLD_AA_TABLE_REFUSAL'
            and review['ordinary_UID_only'] and review['original_game_unchanged']
            and review['cleanup_complete'], 'incomplete f830 original-NVN review')
    for key, stage in [('positive', 'actual-original-NVN-native-window-family-owned-passed'),
                       ('negative', 'actual-old-AA-native-table-refused-by-original-runtime')]:
        actual = read(pinned(review[key]))
        require(actual['stage'] == stage and not actual['cleanup_errors']
                and actual['original_unchanged'] and actual['fixture_PID_absent']
                and actual['fixture_uninstalled'] and actual['fixture_tmp_absent']
                and actual['driver_sha256'] == driver_sha, 'actual f830 fixture not closed')
    require(filter_acceptance is not None and filter_acceptance['sha256'] ==
            '02eff2027890cf9aff0ad479def9ae780e0fa966ec9f3a6a474824855c4f6261',
            'actual f830 fractional filter root review required')
    filtered = read(pinned(filter_acceptance))
    require(filtered['status'] == 'ROOT_REVIEWED_F830_NATIVE_BILINEAR_TRILINEAR_GPU_REFERENCE_PASS'
            and filtered['cleanup_complete'] and filtered['original_unchanged']
            and filtered['proof']['linear_trilinear_GPU_reference_floatbits_proven'],
            'incomplete fractional filter acceptance')
    actual = read(pinned(filtered['actual_result']))
    require(actual['stage'] == 'actual-native-ASTC-fully-linear-filter-reference-passed-not-game'
            and not actual['cleanup_errors'] and actual['original_unchanged']
            and actual['driver_sha256'] == driver_sha, 'filter actual result not closed')


def native_table_gate(data, composition, driver_sha):
    require(driver_sha in DRIVER_LAYOUTS, 'unknown native image layout provider')
    require(len(data) >= 160, 'truncated native table')
    magic, version, header, count, stride, size = struct.unpack_from('<8s4IQ', data)
    require((magic, version, header, stride, size) ==
            (b'DMNTBL2\0', 2, 160, 1040, len(data)) and
            1 <= count <= 65536 and count == composition['entry_count']
            and len(data) == 160 + count * 1056,
            'family-v2 native table extent differs')
    check = bytearray(data);check[128:160] = bytes(32)
    require(hashlib.sha256(check).digest() == data[128:160], 'native table checksum differs')
    identity = composition['driver_identity']
    require(isinstance(identity, str) and re.fullmatch('[0-9a-f]{96}', identity)
            and identity == DRIVER_IDENTITIES[driver_sha], 'opaque three UUIDs differ')
    require(data[32:64].hex() == composition['native_pack_content_sha256']
            and data[64:96].hex() == driver_sha == composition['driver_sha256']
            and data[96:128].hex() == composition['producer_source_sha256'],
            'native table pack/driver/producer differs')
    for number in range(count):
        entry = data[160 + number * 1040:160 + (number + 1) * 1040]
        wire = entry[48:]
        require(struct.unpack_from('<I', entry)[0] == number + 1
                and wire[:8] == b'DMNRES1\0' and len(wire) == 992
                and hashlib.sha256(wire[:960] + bytes(32)).digest() == wire[960:]
                and wire[112:160].hex() == identity
                and wire[288:320].hex() == composition['producer_source_sha256'],
                'native table entry recipe/three UUIDs/producer differs')
    return count


def driver_span_policy(p, state=None, where='input'):
    """Optional cold policy only; original constant caches and shader assets stay external."""
    if 'driver_span_reference' not in p:
        return None
    value = p['driver_span_reference']
    require(type(value) is int and value in (0, 1),
            'driver_span_reference must be startup-only0/1')
    if state is not None:
        require(state.get('driver_constants', {}).get('span_reference') is bool(value),
                'actual ' + where + ' driver_span_reference differs')
    return bool(value)


def readonly_memory_snapshot(p):
    if 'readonly_memory_snapshot' not in p:
        return
    snapshot = p['readonly_memory_snapshot']
    require(isinstance(snapshot, dict) and set(snapshot) == {'helper', 'config'}
            and 'counter_snapshot' not in p, 'read-only memory snapshot fields / counter conflict')
    helper = pinned(snapshot['helper'])
    config = read(pinned(snapshot['config']))
    require(config['serial'] == p['serial'] and config['APK_sha256'] == p['new_apk']
            and config['ELF_sha256'] == p['new_elf']
            and config['canonical_source_ready_sha256'] == p['canonical']['sha256'],
            'read-only memory snapshot target differs from paired runtime')
    subprocess.run([sys.executable, str(helper), '12345', '/tmp', '--config',
                    snapshot['config']['path'], '--config-sha256',
                    snapshot['config']['sha256'], '--plan'], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return True


def startup_allocation_capture(p):
    if 'startup_allocation_capture' not in p:
        return None
    row=p['startup_allocation_capture']
    require(isinstance(row,dict) and set(row)=={'helper','config'}
            and bool(p.get('readonly_memory_snapshot')) and bool(p.get('prior_actual_game_qualification'))
            and not any(p.get(k) for k in ('performance_tools','official_gpu_tools','official_cpu_aa',
                                         'official_l1i_record','counter_snapshot','sustained_play')),
            'startup capture requires prior qualified samePID memory flow without performance tools')
    helper=pinned(row['helper']); config=read(pinned(row['config']))
    require(config['serial']==p['serial'] and config['APK_sha256']==p['new_apk']
            and config['ELF_sha256']==p['new_elf']
            and config['canonical_source_ready_sha256']==p['canonical']['sha256'],
            'startup capture target differs from qualified runtime')
    # Require the later same-PID partition to use the actually successful 44c
    # snapshot semantics, not the superseded all-40-byte-zero ABI capture.
    later=read(pinned(p['readonly_memory_snapshot']['config']))
    require(later.get('nvn_pools',{}).get('mode')=='f75-q735-libc-02969f76-state16-v2'
            and later['nvn_pools'].get('qualify_unlocked') is True
            and later['nvn_pools']['module']['sha256']=='3ef3ee3e02daa71e788b72f18f4cb90065a56a79bc6450939d96a70d8297324f',
            'postroute partition must use successful same-stop Pool qualification')
    freeze=read(pinned({'path':str(Path(p['readonly_memory_snapshot']['helper']['path']).parent/'tool-inputs.json'),
                        'sha256':later['tool_inputs_sha256']}))
    require(freeze['files']['sdk_free_resident.py']['sha256']=='82ab628b0fad3f298c0a334ae911abb0eafafd686fd6ff37c8cb8da83dee6817',
            'postroute SDK capture is not qualified resume successor')
    subprocess.run([sys.executable,str(helper),'12345','/tmp','--config',row['config']['path'],
                    '--config-sha256',row['config']['sha256'],'--plan'],check=True,
                   stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    return True


def startup_allocation_result(result, pid):
    require(result.get('snapshot_complete') is True and result.get('PID')==int(pid)
            and result.get('inferior_calls') is False
            and result.get('owned_PID_TracerPid_zero_after_cleanup') is True
            and result.get('owned_debugger_cleanup_complete') is True and result.get('cleanup_errors')==[],
            'early allocator capture / owned cleanup incomplete')


def readonly_memory_snapshot_result(result):
    require(result.get('snapshot_complete') is True and result.get('inferior_calls') is False
            and result.get('owned_PID_TracerPid_zero_after_cleanup') is True
            and result.get('cleanup_errors') == [], 'read-only memory snapshot / detach incomplete')


def proof(path, digest):
    require(re.fullmatch('[a-f0-9]{64}', digest or '') is not None,
            'explicit input SHA256 required')
    require(sha(path) == digest, 'input manifest changed')
    p = read(path)
    require(not any(k in p for k in ('native_compiler_experiment', 'native_host_experiment')) or
            'native_candidate_validation' in p, 'host experiment must pass the reviewed candidate gate')
    require(p['schema'] == 1 and p['serial'] == '10CG570J59001U1', 'Q735 scope')
    p['pool_window_bytes'] = p.get('pool_window_bytes', 16777216)
    require(type(p['pool_window_bytes']) is int and
            p['pool_window_bytes'] in (1048576, 16777216),
            'reviewed finite shared-window size required')
    if 'shared_image_raw_observe' in p:
        require(type(p['shared_image_raw_observe']) is int and p['shared_image_raw_observe'] in (0, 1),
                'shared_image_raw_observe must be startup-only0/1')
    if 'shared_lazy_windows' in p:
        require(type(p['shared_lazy_windows']) is int and p['shared_lazy_windows'] in (0, 1),
                'shared_lazy_windows must be startup-only0/1')
    driver_span_policy(p)
    baseline = read(pinned(p['baseline']))
    require(baseline['game_PID_absent'] is True, 'baseline must be stopped')
    require(re.fullmatch('[0-9a-f]{12}', baseline['chain']) is not None, 'pinned stopped baseline shader chain differs')
    ready = read(pinned(p['canonical']))
    raw = read(pinned(p['raw']))
    require(ready['commit'] == p['runtime_commit'], 'host source revision differs')
    # Preserve the real raw producer revision for the reviewed fixture-only
    # successor. The candidate gate below verifies its exact compatible rules,
    # independent verify28 and unchanged outputs; never relabel provenance.
    raw_same_revision = raw.get('source_commit') == p['runtime_commit']
    raw_reviewed_cache = (p['runtime_commit'] == '9592893578e56e11c463445592d1e10f1f2e7c5c'
        and raw.get('guarded_runtime_commit') == p['runtime_commit']
        and raw.get('raw_producer_commit') == 'b16184c232b540b92e843be00137b262ae36841d'
        and 'native_candidate_validation' in p)
    require(raw_same_revision or raw_reviewed_cache,
            'raw module / host source revision differs without reviewed cache compatibility')
    p['apk'] = str(pinned(ready['canonical_APK']['archive']))
    p['new_apk'] = ready['canonical_APK_sha256']
    p['new_elf'] = ready['canonical_native_library_sha256']['libdmhost_android.so']
    p['old_apk'] = str(pinned(p['original_apk']))
    p['driver_path'] = str(pinned(p['driver']))
    provider_gate(p['driver']['sha256'], p.get('native_provider_acceptance'),
                  p.get('native_filter_acceptance'))
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
    memory_snapshot_validated = readonly_memory_snapshot(p) is True
    startup_allocation_capture(p)
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
    if 'official_memory_map' in p:
        require(p['official_memory_map'] == {'tool': '/system/bin/showmap', 'format': 'json'},
                'only the existing system showmap JSON memory snapshot is allowed')
    if 'official_l1i_record' in p:
        require(p['official_l1i_record'] is True and (p['runtime_commit'] in
                ('53c7d26628142a383c9c5f8d39a3edc23a1674c3',
                 'd14808f591b0de60c241340c3f5c5df708f4e36b',
                 '179c82dfa412153b6564974e29fc35036e0b3507') or
                 any(k in p for k in ('native_compiler_experiment', 'native_host_experiment'))) and p.get('performance_tools'),
                'independent raw-L1I record requires an exact reviewed runtime with official tools')
    if 'performance_tools' in p:
        tools = p['performance_tools']
        require(set(tools) == {'cpu_helper', 'scheduler_helper'}, 'performance tools fields differ')
        helper = pinned(tools['cpu_helper'])
        require(helper == HERE / 'capture-official-cpu.py', 'exact reviewed local helper required')
        require(pinned(tools['scheduler_helper']) == HERE.parent / 'capture-android-scheduler.py',
                'existing official scheduler orchestration required')
    if 'official_cpu_aa' in p:
        require((p['official_cpu_aa'] == {'repetitions': 3, 'record_seconds': 30,
                'sample_frequency_Hz': 99, 'DWARF_bytes': 49152, 'keep_failed_debug_info': True} or
                p['official_cpu_aa'] == {'repetitions': 3, 'record_seconds': 30,
                'sample_frequency_Hz': 99, 'callgraph': 'fp'})
                and p.get('performance_tools') and p.get('native_host_experiment')
                and not any(p.get(k) for k in ('official_gpu_tools', 'official_l1i_record',
                    'sustained_play', 'counter_snapshot')),
                'A/A requires original host qualification and fixed3x30s99Hz DWARF48KiB/debug or separateFP tools only')
    if 'official_gpu_tools' in p:
        gpu = p['official_gpu_tools']
        require(set(gpu) == {'helper', 'config', 'scheduler_helper'} and p.get('performance_tools'),
                'optional official GPU tools require completed original CPU tools')
        require(pinned(gpu['helper']) == HERE / 'sdp_realtime.py', 'exact local SDP helper required')
        require(pinned(gpu['scheduler_helper']) == R / 'scripts/dev/capture-android-scheduler.py',
                'existing official scheduler helper required')
        tool = module('source_pinned_official_sdp', gpu['helper']['path'])
        p['official_gpu_config'] = tool.config(gpu['config'])
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
            native_table_gate(table.read_bytes(), composition, p['driver']['sha256'])
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
    if p.get('readonly_memory_snapshot'):
        row = p['readonly_memory_snapshot']['helper']
        p['controller_sources'][str(Path(row['path']).resolve())] = row['sha256']
    if p.get('startup_allocation_capture'):
        row=p['startup_allocation_capture']['helper']
        p['controller_sources'][str(Path(row['path']).resolve())]=row['sha256']
    if p.get('official_cpu_aa'):
        p['controller_sources'][str(HERE / 'official_cpu_aa.py')] = sha(HERE / 'official_cpu_aa.py')
    p['_execution_phases'] = p['phases']
    if 'sustained_play' in p:
        sustained = module('source_pinned_sustained_play', HERE / 'sustained_play.py')
        sustained.validate(p, pinned, read, require)
        p['controller_sources'].update({str(q): sha(q) for q in
            [HERE / 'sustained_play.py', HERE / 'capture-sustained-scheduler.py']})
        # Keep both compositions authenticated above; execute only native.
        p['_execution_phases'] = [p['phases'][1]]
    compiler_runtime = None
    if 'native_candidate_validation' in p:
        candidate = module('source_pinned_native_candidate_gate', HERE / 'candidate_gate.py')
        compiler_runtime = candidate.validate(p, pinned, read, require,
            readonly_memory_snapshot_validated=memory_snapshot_validated)
        p['controller_sources'][str(HERE / 'candidate_gate.py')] = sha(HERE / 'candidate_gate.py')
        p['_execution_phases'] = [p['phases'][1]]
    if p['phases'][1]['composition']['sha256'] in ('8676e748175eecbc3bdcfc42038e428775cc5917c3293eec56bc5335f55806f3',
            'ffd05d83cba41506e41a374b92372a52b8ad14f7385a4ca50fa3844ffd8fdfe4'):
        require('qualified_native_asset_validation' in p, 'mixed32 assets require explicit bounded resource qualification')
    if 'qualified_native_asset_validation' in p:
        asset = module('source_pinned_qualified_native_asset_gate', HERE / 'qualified_asset_gate.py')
        p['_asset_validation'] = asset.validate(p, pinned, read, require, compiler_runtime=compiler_runtime)
        p['controller_sources'][str(HERE / 'qualified_asset_gate.py')] = sha(HERE / 'qualified_asset_gate.py')
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
    d = {'serial': p['serial'], 'kind': phase['kind'], 'transaction_token': token, 'remote_screenshot':
         '/data/local/tmp/d2r-native-bulk-' + token + '-' + phase['kind'] + '.png'}
    report = out / 'phase.json'
    save = lambda: m.journal(report, d)
    command = [sys.executable, str(m.SOURCE / 'scripts/dev/android-game-launch.py'),
               '--serial', p['serial'], '--external-profile', '--custom-vulkan-driver',
               '--vulkan-provider', 'adreno', '--shared-guest-heap', '--no-legacy-astc-copy',
               '--no-astc-decode-unorm8', '--no-defer-pool-publication', '--scale-3d', '.60',
               '--fixed-3d', '--pool-copy-accounting', '--game-res', '2750x1260',
               '--timeout', '90', '--report', str(out / 'launch.json'),
               '--pool-window-bytes', str(p['pool_window_bytes']), '--expect-shared-import-windows']
    if driver_span_policy(p):
        command += ['--driver-span-reference']
    if 'driver_span_reference' in p:
        d['driver_span_reference_requested'] = driver_span_policy(p)
    if p.get('shared_image_raw_observe', 0):
        command += ['--shared-image-raw-observe']
    if 'shared_image_raw_observe' in p:
        d['shared_image_raw_observe_requested'] = bool(p['shared_image_raw_observe'])
    if p.get('shared_lazy_windows', 0):
        command += ['--shared-lazy-windows']
    if 'shared_lazy_windows' in p:
        d['shared_lazy_windows_requested'] = bool(p['shared_lazy_windows'])
    if phase['kind'] == 'native':
        command += ['--native-resource-table', '/data/user/0/' + PKG + '/' + DIRS[-1][1] + '/native-layouts.bin',
                    '--native-resource-pack-sha256', phase['pack_sha'],
                    '--native-resource-producer-sha256', phase['producer_sha']]
    if p.get('startup_allocation_capture'):
        command += ['--wait-java-debugger']
    d['launch_command'] = command
    save()
    own = None
    ambiguous = False
    launcher = None
    early_started = False
    early_process = None
    launch_completed = False

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
        with (out / 'launch.log').open('w') as f:
            launcher = subprocess.Popen(command, stdout=f, stderr=subprocess.STDOUT, cwd=R)
            deadline = time.monotonic() + (240 if p.get('startup_allocation_capture') else 120)  # A bounded tool observation, not a profiler.
            while launcher.poll() is None:
                observe()
                if own and p.get('startup_allocation_capture') and not early_started:
                    early_started=True
                    capture=p['startup_allocation_capture']; early_out=out/'startup-allocation-capture';early_out.mkdir()
                    early_command=[sys.executable,str(pinned(capture['helper'])),own,str(early_out),'--config',
                                   capture['config']['path'],'--config-sha256',capture['config']['sha256']]
                    pinned(capture['config']);d['startup_allocation_command']=early_command;save()
                    with (early_out/'console.txt').open('w') as log:
                        early_process=subprocess.Popen(early_command,stdout=log,stderr=subprocess.STDOUT)
                        try:
                            early_process.wait(timeout=180)
                        except subprocess.TimeoutExpired:
                            early_process.terminate()  # accepted wrapper handles TERM with owned finally
                            early_process.wait(timeout=30)
                            raise RuntimeError('bounded startup capture deadline; owned helper cleanup requested')
                        require(early_process.returncode==0, 'startup helper failed; preserve its diagnostic output')
                    early_result=read(early_out/'debugger-result.json')
                    startup_allocation_result(early_result,own)
                    d['startup_allocation_capture']=early_result;save()
                require(time.monotonic() < deadline, 'owned launcher observation deadline')
                time.sleep(.3)
            observe()
            require(launcher.returncode == 0, 'original HUD launcher failed')
        launch = read(out / 'launch.json')
        launch_completed = True
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
        driver_span_policy(p, initial, 'startup')
        if 'shared_image_raw_observe' in p:
            require(initial.get('shared_image_raw_observe', {}).get('enabled_policy') is bool(p['shared_image_raw_observe']),
                    'actual startup shared_image_raw_observe differs')
        if 'shared_lazy_windows' in p:
            require(initial.get('shared_lazy_windows', {}).get('active') is bool(p['shared_lazy_windows']),
                    'actual startup shared_lazy_windows differs')
        m.screenshot_collect(dev, own, d, report, out, 'HUD-before-route')
        if p.get('sustained_play'):
            sustained = module('source_pinned_sustained_play_run', HERE / 'sustained_play.py')
            sustained.run(dev, own, d, report, out, initial, p, m)
        elif p.get('native_candidate_validation'):
            # Existing original input tool; safe B/C return proven by prior6f run.
            m.original_route(dev, own, d, report, out, initial, None,
                             points=[(5225, 6295), (5235, 6289), (5225, 6295)])
            require(d.get('original_route_completed') == 3, 'candidate original threewalks not completed')
        else:
            m.original_route(dev, own, d, report, out, initial, None)
        current = read_state(dev, out, 'after-route')
        if p.get('official_gpu_tools') and p.get('_official_gpu_disabled'):
            d['gpu_validity'] = False
            d['official_gpu_complete'] = False
            d['official_gpu_skipped'] = 'Earlier optional tool failed; no GUI restart or zero-data substitution'
            save()
        if p.get('performance_tools'):
            if p.get('official_gpu_tools') and not p.get('_official_gpu_disabled'):
                tool = module('source_pinned_official_sdp_pause', p['official_gpu_tools']['helper']['path'])
                try:
                    d['SDP_paused_before_CPU'] = tool.paused(p['official_gpu_config'])
                except Exception as e:
                    d['gpu_validity'] = False
                    d['official_gpu_complete'] = False
                    d['official_gpu_error'] = 'before CPU: ' + repr(e)
                    p['_official_gpu_disabled'] = True
                    d['official_gpu_early_cleanup_errors'] = tool.close(p['official_gpu_config'])
                    p['_official_gpu_unclosed'] = bool(d['official_gpu_early_cleanup_errors'])
                save()
            if p.get('_official_gpu_unclosed'):
                d['cpu_validity'] = False
                d['official_performance_complete'] = False
                d['official_performance_skipped'] = 'SDP neither confirmed paused nor confirmed closed; preserve CPU applicability gap'
                save()
        if p.get('performance_tools') and not p.get('_official_gpu_unclosed'):
            require(pid() == own, 'PID changed before independent performance windows')
            performance_out = out / 'official-performance'
            performance_out.mkdir()
            command = [sys.executable, p['performance_tools']['cpu_helper']['path'],
                       p['serial'], own, '1', str(performance_out), str(p['pool_window_bytes']), '1']
            if p.get('official_l1i_record'):
                command.append('1')  # Independent raw-L1I record; original cpu-clock command unchanged.
            if p.get('official_cpu_aa'):
                command.extend(['0', 'fp' if p['official_cpu_aa'].get('callgraph') == 'fp' else str(p['official_cpu_aa']['DWARF_bytes'])])
            if p.get('official_cpu_aa'):
                d['official_cpu_aa'] = p['official_cpu_aa']
                d['AA_overlap_and_reproducibility_require_offline_review'] = True
            if 'shared_lazy_windows' in p:
                command.extend(['--expect-shared-lazy-windows', str(p['shared_lazy_windows'])])
            command.extend(['--expected-chain', p['baseline_data']['chain']])
            d['performance_command'] = command
            save()
            with (performance_out / 'console.txt').open('w') as log:
                if p.get('official_cpu_aa'):
                    capture = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        require(capture.wait() == 0, 'official A/A capture incomplete')
                    finally:
                        if capture.poll() is None:
                            capture.terminate()  # helper cooperatively closes its owned tools before save2/CAS
                            try:
                                capture.wait(timeout=70)
                            except subprocess.TimeoutExpired:
                                capture.kill()
                                capture.wait(timeout=10)
                                raise
                else:
                    subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
            tools_result = read(performance_out / 'official-cpu-capture.json')
            require(tools_result.get('complete') is True, 'official performance windows incomplete')
            require(tools_result.get('expected_shader_chain') == p['baseline_data']['chain'],
                    'official performance expected chain differs from pinned baseline')
            require(pid() == own, 'PID changed during independent performance windows')
            d['official_performance_complete'] = True
            d['cpu_validity'] = True
            d['performance_stable_target60_PSO_variants_all_windows'] = tools_result[
                'stable_target60_PSO_variants_all_windows']
            save()
            # All Simpleperf/Perfetto windows and owned cleanup finish before
            # the optional all-stop LLDB snapshot below. Never mix their scopes.
            read_state(dev, out, 'after-tools')
        if p.get('official_gpu_tools') and not p.get('_official_gpu_disabled'):
            require(pid() == own, 'PID changed before official GPU window')
            tool = module('source_pinned_official_sdp_capture', p['official_gpu_tools']['helper']['path'])
            try:
                d['official_gpu'] = tool.capture(p['official_gpu_config'], own, out / 'official-gpu',
                    p['pool_window_bytes'], p['driver']['sha256'],
                    pinned(p['official_gpu_tools']['scheduler_helper']))
                d['official_gpu_complete'] = d['official_gpu']['complete']
                d['gpu_validity'] = d['official_gpu']['gpu_validity']
            except Exception as e:
                # Optional tool failure is not a game/CPU/memory failure. Preserve
                # its evidence, close the exact owned tools, then keep original
                # LLDB/meminfo/save2/restore. Never invent zero-bandwidth data.
                d['official_gpu_complete'] = False
                d['gpu_validity'] = False
                d['official_gpu_error'] = repr(e)
                p['_official_gpu_disabled'] = True
                d['official_gpu_early_cleanup_errors'] = tool.close(p['official_gpu_config'])
                p['_official_gpu_unclosed'] = bool(d['official_gpu_early_cleanup_errors'])
            save()
        if p.get('counter_snapshot'):
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
        if p.get('readonly_memory_snapshot'):
            require(pid() == own, 'PID changed before read-only memory snapshot')
            snapshot = p['readonly_memory_snapshot']
            snapshot_out = out / 'readonly-memory-snapshot'
            snapshot_out.mkdir()
            command = [sys.executable, str(pinned(snapshot['helper'])), own, str(snapshot_out),
                       '--config', snapshot['config']['path'], '--config-sha256',
                       snapshot['config']['sha256']]
            pinned(snapshot['config'])
            d['readonly_memory_snapshot_command'] = command
            save()
            # The pinned helper owns bounded LLDB detach/server/forward cleanup.
            with (snapshot_out / 'console.txt').open('w') as log:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
            snapshot_result = read(snapshot_out / 'debugger-result.json')
            readonly_memory_snapshot_result(snapshot_result)
            d['readonly_memory_snapshot'] = snapshot_result
            if p.get('startup_allocation_capture'):
                require(d['startup_allocation_capture']['PID']==snapshot_result['PID']==int(own),
                        'startup returns and final partition are not same owned PID')
                d['startup_final_partition_same_PID']=True
                capture_helper=pinned(p['startup_allocation_capture']['helper'])
                early_config=read(pinned(p['startup_allocation_capture']['config']))
                early_inputs=read(pinned({'path':str(capture_helper.parent/'tool-inputs.json'),
                                          'sha256':early_config['tool_inputs_sha256']}))
                for source_row in early_inputs['files'].values():
                    pinned(source_row)
                corr=module('owned_startup_partition_correlation',capture_helper.parent/'correlate.py')
                d['startup_final_partition_correlation']=corr.correlate(
                    d['startup_allocation_capture']['snapshot'],snapshot_result['snapshot'])
            save()
        require(pid() == own, 'PID changed before residency snapshot')
        (out / 'official-dumpsys-meminfo.txt').write_text(dev.adb('shell', 'dumpsys', 'meminfo', own))
        if p.get('official_memory_map'):
            # One system-tool snapshot AFTER performance windows. No app code,
            # sampling loop, debugger, page touching or custom smaps reader.
            text = dev.adb('shell', 'run-as', PKG, '/system/bin/showmap',
                           '-a', '-v', '-o', 'json', own)
            (out / 'official-showmap.json').write_text(text)
            maps = json.loads(text)
            require(isinstance(maps, list) and maps and
                    all({'object', 'RSS', 'PSS', 'start addr', 'end addr'} <= set(row)
                        for row in maps), 'official showmap output incomplete')
            require(pid() == own, 'PID changed during system memory snapshot')
            d['official_showmap_complete'] = True
            save()
        m.screenshot_collect(dev, own, d, report, out, 'HUD-after-route')
        require(current['valid'] and current['world_input_ready'], 'original world not input-ready')
        require(current['scale_3d'] == .60 and current['game_output'] == current['surface_output']
                == {'width': 2750, 'height': 1260}, 'fixed resolution mismatch')
        require(current['pool_windows']['window_bytes'] == p['pool_window_bytes']
                and current['guest_heap']['shared'] is True, 'actual shared-window mismatch')
        if 'driver_span_reference' in p:
            final_policy_state = read_state(dev, out, 'after-span-policy')
            driver_span_policy(p, final_policy_state, 'final')
            d['driver_span_reference_observed'] = final_policy_state['driver_constants']['span_reference']
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
        d['scope'] = ('Original-game B2T correctness successor, original threewalks and system residency; no CPU/GPU timing or sustained-play acceptance'
                      if p.get('native_functional_validation') else
                      'Native-only shortcandidate functional/officialCPU validation; not matchedpair or thermalproof'
                      if p.get('native_candidate_validation') else
                      'Native-only sustained original-input route, sparse official Perfetto; '
                      'no combat or whole-game acceptance implied' if p.get('sustained_play') else
                      'Functional original-game HUD/route and one system residency snapshot; not GPU sampling or FPS/heat acceptance')
        if p.get('_asset_validation'):
            d['qualified_native_asset_validation'] = p['_asset_validation']
            d['scope'] = p['_asset_validation']['scope']
        save()
    finally:
        if early_process is not None and early_process.poll() is None:
            early_process.terminate()  # existing wrapper catches TERM and detaches
            early_process.wait(timeout=35)
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
        now = pid()
        if now:
            require(not ambiguous and own and now == own, 'unknown/replacement PID; preserve transaction')
            if p.get('startup_allocation_capture'):
                status=dev.app('cat','/proc/'+own+'/status')
                require(re.search(r'^TracerPid:\s+0$',status,re.M), 'startup owned PID remains traced; preserve it')
            if p.get('startup_allocation_capture') and not launch_completed:
                native_log=dev.app('cat','files/runtime/native.log')
                if 'Android native start pid='+own+'\n' not in native_log:
                    status=dev.app('cat','/proc/'+own+'/status')
                    require(re.search(r'^TracerPid:\s+0$',status,re.M), 'early failed owned PID is still traced')
                    dev.adb('shell','am','force-stop',PKG)
                    require(not pid(), 'owned Java-wait process still alive')
                    d['early_no_native_start_stopped_without_save_claim']=True;save()
                    now=None
            if now:
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
    require(out == Path(p['output']).resolve(), 'report parent must equal the declared inputs.output directory')
    plan = {'offline': True, 'controller_sources': p['controller_sources'],
            'inputs_sha256': args.inputs_sha256, 'runtime': p['runtime_commit'],
            'phases': ['standard-ASTC4', 'native-ASTC4'], 'scale_3d': .60,
            'output': '2750x1260', 'pool_window_bytes': p['pool_window_bytes'],
            'profiling': False, 'recursive_RomFS_copy': False,
            'scope': 'Functional and system residency only, not sustained thermal/FPS proof'}
    if p.get('sustained_play'):
        plan.update(phases=['native-ASTC4'], profiling=True,
            scope='1800s uninterrupted original-input route plus four official8s Perfetto windows; not combat proof',
            sustained_play=p['sustained_play'], real_game_progress_saved=True,
            restart_or_thermal_pause_cannot_be_combined=True)
    if p.get('native_candidate_validation'):
        plan.update(phases=['native-ASTC4'],
            scope='Shortcandidate originalgame threewalks plus existing officialCPU windows; prior6f pair referenced, no newpaired claim',
            native_candidate_validation=p['native_candidate_validation'])
    if p.get('performance_tools'):
        plan['profiling'] = True
        plan['scope'] = ('Originalthreewalks and seven independent official8s CPU windows, including four PMU stat windows; no newpaired or thermal claim'
                         if p.get('native_candidate_validation') else
                         'Seven independent official8s stat/record/PMU/scheduler-Surface windows before LLDB; not yet measured netthroughput or thermal benefit')
        plan['official_performance'] = {'stat_seconds': 8, 'record_user_Hz': 99,
            'record_seconds': 8, 'scheduler_frame_seconds': 8, 'before_LLDB': True,
            'pmu_core_stat_seconds': 8, 'pmu_memory_stat_seconds': 8,
            'pmu_front_cache_stat_seconds': 8, 'pmu_front_stalls_stat_seconds': 8,
            'pmu_limit': 'Inspect actual event availability and multiplex/scaling; not per-frame instructions or DRAM traffic',
            'same_window_CPU_and_Surface_events': 'Perfetto only; stat/record are separate windows',
            'game_Surface_actual_frame_timeline_may_be_absent': True}
        if p.get('official_l1i_record'):
            plan['official_performance']['additional_record'] = {'event': 'raw-l1i-cache-refill:u',
                'period': 1000000, 'seconds': 8, 'DWARF_bytes': 32768,
                'independent_window_and_overhead': True, 'cpu_clock99_DWARF16384_unchanged': True,
                'scope': 'Refill PC samples, not all frontend stalls or per-frame work; preserve failed unwinds'}
            plan['scope'] += '; one additional independent raw-L1I refill record window'
        if p.get('official_cpu_aa'):
            plan['official_performance'] = dict(p['official_cpu_aa'],
                Perfetto_seconds=8, Perfetto_only_bookends=2, concurrent_record_traces=3,
                overlap='Requires offline official sample MONOTONIC and Perfetto clock_snapshot mapping',
                scope='Samebinary A/A and measurement disturbance; no new optimization or FPS gain claim')
            plan['scope'] = 'Same runtime originalgame/PID/camp:3x30s officialrecord and5x8s Perfetto; original save2/CAS'
        plan['controller_sources'].update({v['path']: v['sha256'] for v in p['performance_tools'].values()})
    if p.get('readonly_memory_snapshot'):
        plan['readonly_memory_snapshot'] = p['readonly_memory_snapshot']
        plan['scope'] += '; pinned read-only LLDB snapshot after route/tools, before system residency'
    if 'driver_span_reference' in p:
        plan['driver_span_reference_requested'] = driver_span_policy(p)
        plan['scope'] += '; cold span-only OFF/ON with identical APK/shader assets; shader deployment is external'
    if 'shared_image_raw_observe' in p:
        plan['shared_image_raw_observe_requested'] = bool(p['shared_image_raw_observe'])
        plan['scope'] += '; cold shared-image-raw-observe OFF/ON; separate launches, no hot switch'
    if 'shared_lazy_windows' in p:
        plan['shared_lazy_windows_requested'] = bool(p['shared_lazy_windows'])
        plan['scope'] += '; cold shared-lazy-windows OFF/ON source experiment; separate launches, no hot switch or old74 control relabelling'
    if p.get('official_gpu_tools'):
        plan['official_gpu'] = {'optional': True, 'tool': 'SDP2026.9 Realtime GUI',
            'official_seconds': 12, 'Perfetto_subset_seconds': 8, 'before_LLDB': True,
            'default': 'OFF unless source-pinned official_gpu_tools is supplied',
            'scope': 'Global hardware metrics, no exclusive draw/pass attribution'}
        plan['controller_sources'].update({v['path']: v['sha256'] for v in p['official_gpu_tools'].values()})
    if p.get('_asset_validation'):
        plan.update(phases=[f"native6217-with{p['_asset_validation']['replacements']}-qualified-ASTC6-replacements"],
                    qualified_native_asset_validation=p['_asset_validation'],
                    scope=p['_asset_validation']['scope'])
    if p['runtime_commit'] == '179c82dfa412153b6564974e29fc35036e0b3507':
        plan['combined_CPU_validation'] = {'runtime': p['runtime_commit'],
            'components': ['consumer-schema-admission', 'coherent-native-cache-walk', 'recording-Pool-pin-borrow'],
            'full2838_assets_independently_qualified': True, 'single_component_CPU_AB': False,
            'scope': 'Combined runtime plus full2838 resources; prior d148 full-resource window is a qualified comparison, not isolated component benefit'}
        plan['scope'] += '; combined CPU179c82d; independent additional raw-L1I window retained; no individual component benefit claim'
    if p.get('native_compiler_experiment'):
        plan['native_compiler_experiment'] = {**p['native_compiler_experiment'],
            'runtime_source_semantics_changed': False, 'compiler_only_comparison': True,
            'root_review': p['native_candidate_validation']['root_review']}
    if p.get('native_host_experiment'):
        plan['native_host_experiment'] = {**p['native_host_experiment'],
            'game_logic_unchanged': True, 'compiler_policy': 'original179 O3 / no LTO',
            'root_review': p['native_candidate_validation']['root_review']}
    if p.get('native_functional_validation'):
        plan['scope'] = 'Original-game B2T correctness successor, original threewalks and system residency; no CPU/GPU timing or sustained-play acceptance'
        plan['native_functional_validation'] = p['native_functional_validation']
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
    if p.get('sustained_play'):
        # Cooperative cancellation: finish the bounded existing input/tool child,
        # then enter the original save2/stop/restore finally. No child SIGKILL.
        def request_stop(signum, frame):
            p['_operator_stop_requested'] = True
        signal.signal(signal.SIGTERM, request_stop)
        signal.signal(signal.SIGINT, request_stop)
    if p.get('official_cpu_aa'):
        def cancel_AA(signum, frame):
            raise KeyboardInterrupt('A/A cancelled; close owned capture before original save2/CAS')
        signal.signal(signal.SIGTERM, cancel_AA)
        signal.signal(signal.SIGINT, cancel_AA)
    try:
        dev.app('sh', '-c', 'test ! -e "$1"', 'sh', stage)
        dev.app('mkdir', '-p', stage + '/backup', stage + '/incoming')
        created = True
        owner = out / 'owner'
        owner.write_text(token + '\n')
        for phase_index, phase in enumerate(p['_execution_phases']):
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
            if phase_index == 0:
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
            if phase_index == 0:
                installer.install(dev, Path(p['apk']), p['new_apk'], out / 'install-candidate.log', require, PKG)
            dev.identity(p['new'])
            d.setdefault('phase_reports', []).append(str(out / name / 'phase.json'))
            write()
            phase_result = phase_game(dev, out / name, phase, p, token)
            d['phases'].append(phase_result)
            write()
            require(phase_result.get('original_HUD_reached') and phase_result.get('fresh_save_then_stopped'),
                    'phase did not reach HUD / fresh save2')
            if p.get('sustained_play'):
                require(phase_result.get('sustained_play', {}).get('continuous_pass') is True,
                        '30min sustained route did not complete')
        owner.unlink()
        d['stage'] = ('qualified-native-ASTC6-original-game-resource-validation-complete'
                      if p.get('_asset_validation') else
                      'native-original-game-short-candidate-validation-complete'
                      if p.get('native_candidate_validation') else
                      'native-original-game-uninterrupted-sustained-route-complete'
                      if p.get('sustained_play') else
                      'matched-resource-original-game-functional-pair-complete')
        write()
    except BaseException as e:
        d['error'] = repr(e)
        write()
    finally:
        if p.get('official_gpu_tools'):
            try:
                tool = module('source_pinned_official_sdp_close', p['official_gpu_tools']['helper']['path'])
                d['cleanup_errors'].extend(tool.close(p['official_gpu_config']))
                d['official_gpu_tools_cleanup_complete'] = not d['cleanup_errors']
            except BaseException as e:
                d['cleanup_errors'].append('official SDP tools: ' + repr(e))
            write()
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
            d['cleanup_complete'] = True
            write()
        except BaseException as e:
            d['cleanup_errors'].append(repr(e))
            write()
    print(json.dumps({k: d.get(k) for k in ['stage', 'error', 'cleanup_errors', 'original_restored']}, indent=2))
    require(not d.get('error') and not d['cleanup_errors'] and d.get('cleanup_complete'),
            'original-game pair incomplete; preserve journal')


if __name__ == '__main__':
    main()
