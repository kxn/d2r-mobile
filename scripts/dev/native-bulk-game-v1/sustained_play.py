"""Bounded original-input orchestration, not a performance sampler or game bot."""
from pathlib import Path
import importlib.util
import json
import os
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
_state_spec = importlib.util.spec_from_file_location('original_state_reader', HERE.parent / 'game_state_json.py')
state_reader = importlib.util.module_from_spec(_state_spec); _state_spec.loader.exec_module(state_reader)
R = Path('/home/dl/d2r-mobile')
RUNTIME = '6f8188e392f21ff045ef640ad8c4f184de048806'
CURRENT_RUNTIME = '88620ec7c92edeb59512612cebf42906a4848509'
CURRENT_DELIVERED = '28f3f9dd9763e4b7fb8cdd8caff24c94dc69b54d7a885a198dac755adcdebdb6'
DRIVER = 'f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
REVIEW_STATUS = 'ACTUAL_ORIGINAL_GAME_PAIRED_FUNCTIONAL_OFFICIAL_CPU_GPU_MEMORY_PASS_CLEANUP_FOLLOWUP_CONFIRMED'
# Historical6f v2 completed this B<->C cycle (588 walks). Current886 must
# prove its own first loop; any miss fails instead of inventing a detour.
CAMP = [{'x': 5225, 'y': 6295, 'area': 1},
        {'x': 5235, 'y': 6289, 'area': 1}]


def current_baseline(p, delivered, pinned, read, require):
    c = p['sustained_play']
    if 'live_baseline_review' not in c:
        require(p['baseline']['sha256'] == CURRENT_DELIVERED
                and pinned(c['current_acceptance']) == pinned(p['baseline']),
                'current sustained requires delivered886 or explicit reviewed live baseline')
        return
    review = read(pinned(c['live_baseline_review']))
    require(review['status'] == 'ROOT_REVIEWED_CURRENT886_STOPPED_LIVE_BASELINE_TEMP_ONLY'
            and review['runtime'] == CURRENT_RUNTIME
            and all(review.get(k) is True for k in ('same_delivery', 'game_PID_absent',
                'immutable_identity_unchanged', 'only_temp_changed', 'original_Device_identity_passed',
                'profile_and_publication_receipt_device_hashes_match', 'controller_lock_absent')),
            'current886 stopped live baseline review incomplete')
    for name, expected in (('delivered_baseline', c['current_acceptance']), ('fresh_baseline', p['baseline'])):
        require(review[name]['sha256'] == expected['sha256']
                and pinned(review[name]) == pinned(expected), 'reviewed live baseline pin differs ' + name)
    fresh = read(pinned(p['baseline']))
    require(fresh['game_PID_absent'] is True and all(fresh[k] == delivered[k] for k in
                ('APK_sha256', 'host_ELF_SHA256', 'model', 'serial', 'chain', 'package',
                 'platform', 'profile', 'publication_receipt')), 'live baseline immutable identity differs')
    old, new = delivered['inventories'], fresh['inventories']
    temp = 'files/runtime/temp'
    require(set(old) == set(new) and len(old) == 8
            and all(old[k] == new[k] for k in old if k != temp),
            'live baseline non-temp inventory differs')
    require(set(old[temp]) == set(new[temp]), 'live baseline temp file set differs')
    changed = [n for n in old[temp] if old[temp][n] != new[temp][n]]
    allowed = (r'files/runtime/temp/(?:vulkan-pso/v1-[0-9a-f]{8}-[0-9a-f]{8}-'
               r'[0-9a-f]{8}-[0-9a-f]{32}\.bin(?:\.recipes-v1)?|Diablo II/GameLogs/D[0-9]{7}\.txt)')
    require(changed and all(re.fullmatch(allowed, n) and
                re.fullmatch('[0-9a-f]{64}', new[temp][n]) for n in changed),
            'live baseline unapproved temp hash change')
    actual_changes = [{'path': n, 'old_sha256': old[temp][n], 'new_sha256': new[temp][n]}
                      for n in changed]
    require(sorted(review['changes'], key=lambda v: v['path']) ==
            sorted(actual_changes, key=lambda v: v['path']), 'live baseline reviewed changes differ')


def current_qualification(p, pinned, read, require):
    c = p['sustained_play']
    delivered = read(pinned(c['current_acceptance']))
    require(c['current_acceptance']['sha256'] == CURRENT_DELIVERED,
            'current sustained requires the actual delivered886 qualification')
    require(p['runtime_commit'] == CURRENT_RUNTIME and delivered['game_PID_absent'] is True
            and delivered['chain'] == '9507875007c6', 'current sustained runtime/chain differs')
    current_baseline(p, delivered, pinned, read, require)
    publication = read(pinned(delivered['publication_receipt']))
    require(publication['status'] == 'ROOT_REVIEWED_Q_LOCAL_DESKTOP_DELIVERY_CANDIDATE'
            and publication['runtime'] == CURRENT_RUNTIME
            and publication['Q_local_publish_allowed'] is True
            and publication['global_default_enable'] is False
            and publication['historical_asset_receipts_unchanged'] is True
            and publication['default_cache_budget_bytes'] == 536870912,
            'current delivered runtime/source qualification incomplete')
    for key in ('canonical', 'raw'):
        require(publication[key]['sha256'] == p[key]['sha256']
                and pinned(publication[key]) == pinned(p[key]), 'delivered artifact differs ' + key)
    require(publication['provider']['sha256'] == p['driver']['sha256'] == DRIVER
            and publication['composition'] == p['phases'][1]['composition']
            and delivered['APK_sha256'] == p['new_apk']
            and delivered['host_ELF_SHA256'] == p['new_elf']
            and p['original_apk']['sha256'] == delivered['APK_sha256']
            and p['original_host_elf'] == delivered['host_ELF_SHA256']
            and publication['dynamic_HUD_settings_not_overridden'] is True,
            'delivered host/provider/assets or restore/startup policy differ')
    profile = publication['profile']
    require(profile['shared_guest_heap'] == profile['shared_lazy_windows'] == 'true'
            and profile['shared_image_raw_observe'] == 'false'
            and profile['driver_span_reference'] == 'true'
            and p.get('shared_lazy_windows') == 1 and p.get('shared_image_raw_observe') == 0
            and p.get('driver_span_reference') == 1, 'current delivered startup policy differs')
    pinned(delivered['profile'])
    require(len(delivered['desktop_rounds']) == 2 and all(
        row['no_Intent_extras'] is True and row['original_HUD'] is True
        and row['shared_native_policy_observed'] is True
        and row['lifecycle']['fresh_original_lifecycle_save'] is True
        and row['lifecycle']['save_commits'] == 2 for row in delivered['desktop_rounds']),
        'current original desktop lifecycle evidence incomplete')
    actual = read(pinned(publication['controlled_game_qualification']))
    require(actual['status'] == 'ROOT_REVIEWED_886_ACTUAL_ORIGINAL_GAME_FUNCTIONAL_QUALIFICATION_FOR_Q_LOCAL_PUBLICATION'
            and actual['runtime'] == CURRENT_RUNTIME and actual['actual_saved_stopped_original_restored'] is True
            and actual['canonical_RAW_source_review']['sha256'] == p['native_candidate_validation']['root_review']['sha256']
            and len(actual['actual_runs']) == 2, 'current original-game source qualification differs')
    pinned(actual['canonical_RAW_source_review'])
    for row in actual['actual_runs']:
        inputs = read(pinned(row['input'])); result = read(pinned(row['result'])); phase = read(pinned(row['phase']))
        pinned(row['HUD_after_route'])
        require(inputs['runtime_commit'] == CURRENT_RUNTIME
                and result['inputs_sha256'] == row['input']['sha256']
                and result['plan']['runtime'] == CURRENT_RUNTIME
                and result['phases'] == [phase]
                and result.get('original_restored') is True
                and result.get('cleanup_complete') is True and result.get('cleanup_errors') == []
                and not result.get('error') and phase.get('original_route_completed') == 3
                and all(phase.get(k) is True for k in ('original_HUD_reached', 'game_modules_executed', 'fresh_save_then_stopped')),
                'current short original-game/lifecycle evidence incomplete')
    return CURRENT_RUNTIME


def validate(p, pinned, read, require):
    c = p['sustained_play']
    current = 'current_acceptance' in c
    fields = {'seconds', 'interval_seconds', 'trace_seconds',
              'current_acceptance' if current else 'paired_acceptance',
              'route', 'resume_step', 'scheduler_helper'}
    if current and 'live_baseline_review' in c:
        fields.add('live_baseline_review')
    require(set(c) == fields, 'sustained config fields')
    require((c['seconds'], c['interval_seconds'], c['trace_seconds']) == (1800, 600, 8),
            'reviewed finite 30min / 10min / official8s only')
    require(not any(k in p for k in ('counter_snapshot', 'performance_tools', 'official_gpu_tools',
                'official_cpu_aa', 'official_l1i_record', 'startup_allocation_capture',
                'readonly_memory_snapshot', 'builtin_vma_on_suspend', 'official_memory_map',
                'native_functional_validation', 'prior_actual_game_qualification')),
            'sustained test excludes extra capture/diagnostic modes')
    require(p['driver']['sha256'] == DRIVER and p['pool_window_bytes'] == 1048576,
            'accepted exact resident f830 1MiB target')
    require(p['phases'][1]['composition_data']['entry_count'] == 6217, 'same6217 only')
    if current:
        current_qualification(p, pinned, read, require)
    else:
        require(p['runtime_commit'] == RUNTIME, 'accepted exact6f sustained runtime')
        review = read(pinned(c['paired_acceptance']))
        require(review['status'] == REVIEW_STATUS and review['runtime'] == RUNTIME
                and review['driver'] == DRIVER and review['visual_root_actually_viewed']
                and review['cleanup_followup_all_owned_not_executing'], 'paired acceptance not closed')
        for row in review['refs']:
            pinned(row)
    require(pinned(c['scheduler_helper']) == HERE / 'capture-sustained-scheduler.py',
            'source-pinned official Perfetto orchestrator required')
    route = read(pinned(c['route']))
    require(route['schema'] == 1 and route['scope'] in ('town', 'verified-route-no-combat-proof'),
            'route scope cannot claim combat')
    steps = route['steps']
    require(1 <= len(steps) <= 128 and type(c['resume_step']) is int
            and 0 <= c['resume_step'] < len(steps), 'finite valid resume step required')
    if route['kind'] == 'accepted-camp':
        require(steps == CAMP and route['scope'] == 'town', 'unknown camp coordinates')
    else:
        require(route['kind'] == 'root-verified-original-touch-route'
                and route['root_verified'] is True, 'route must first be actually explored')
        require(pinned(route['walk_tool']) == R / 'scripts/dev/android-game-walk-to.py',
                'route uses a different input tool')
        require(len(route['evidence']) == len(steps), 'one actual original-walk receipt per step')
        for step, row in zip(steps, route['evidence']):
            evidence = read(pinned(row))
            after = evidence['after']
            until = step.get('until_area')
            require(evidence['reached'] is True and evidence['before']['area'] == step['area']
                    and after['valid'] and after['world_input_ready']
                    and ((until is not None and after['area'] == until) or
                         (after['area'] == step['area'] and
                          (after['x'] - step['x']) ** 2 + (after['y'] - step['y']) ** 2 <= 16)),
                    'route receipt does not prove target/area')
    for step in steps:
        require(set(step) <= {'x', 'y', 'area', 'until_area'}
                and all(type(step[k]) is int and 0 < step[k] < 100000 for k in ('x', 'y', 'area')),
                'invalid original-input route step')
        if 'until_area' in step:
            require(type(step['until_area']) is int and 0 < step['until_area'] < 100000,
                    'invalid expected transition')
    # An area-changing cycle must close; never extrapolate an unverified exit.
    for n, step in enumerate(steps):
        require(step.get('until_area', step['area']) == steps[(n + 1) % len(steps)]['area'],
                'route area cycle not closed')
    p['_sustained_route'] = route
    return CURRENT_RUNTIME if current else None


def validate_state(state, p, require):
    require(state['valid'] and state['world_input_ready'], 'sustained original world not ready')
    if p.get('_sustained_current') is True:
        # Original adaptive levels and existing HUD manual settings remain live.
        # This is a startup .75 policy, never a fixed-resolution comparison.
        require(type(state['scale_3d']) in (int, float) and .5 <= state['scale_3d'] <= 1.,
                'current adaptive/HUD scale outside original supported policy')
        require(state.get('shared_lazy_windows', {}).get('active') is True
                and state.get('shared_image_raw_observe', {}).get('enabled_policy') is False
                and state.get('driver_constants', {}).get('span_reference') is True
                and state.get('private_image_cache', {}).get('budget_bytes') == 536870912
                and state.get('pool_copy_accounting', {}).get('enabled', False) is False,
                'current shared/lazy/raw/span/cache/accounting policy changed')
    else:
        require(state['scale_3d'] == .6, 'sustained fixed scale changed')
    require(state['game_output'] == state['surface_output'] == {'width': 2750, 'height': 1260},
            'sustained output resolution changed')
    require(state['guest_heap']['shared'] is True and
            state['pool_windows']['window_bytes'] == p['pool_window_bytes'], 'shared-window changed')


def endpoint_state(dev, pid, p, m):
    m.require(dev.adb('shell', 'pidof ' + m.PKG + ' || true') == pid, 'sustained PID changed')
    state = state_reader.latest_complete_state(state_reader.read_state_text(dev.app))
    validate_state(state, p, m.require)
    return state


def thermal_log(dev, pid, out, label, m):
    text = dev.adb('logcat', '-d', '--pid=' + pid, '-v', 'threadtime', '-s',
                   'DmThermal:I', 'AndroidRuntime:E')
    (out / (label + '-public-api-logcat.txt')).write_text(text)
    m.require('original save and background suspend' not in text,
              'public API thermal protection paused game; uninterrupted test failed')
    return {'log': str(out / (label + '-public-api-logcat.txt')),
            'API_status_changes': [int(v) for v in re.findall(r'Android API thermal status=(\d+)', text)],
            'temperature_source': 'existing battery overlay/public API only; no ADB thermal or sysfs'}


def stop_requested(out, token, m):
    path = out / 'stop-request.json'
    if not path.exists():
        return
    request = json.loads(path.read_text())
    m.require(request == {'token': token, 'action': 'save-stop'}, 'unowned or invalid stop request')
    raise RuntimeError('owned operator save-stop request; uninterrupted test not complete')


def run(dev, pid, d, report, out, initial, p, m):
    c, route = p['sustained_play'], p['_sustained_route']
    steps = route['steps']
    index = c['resume_step']
    m.require(initial['area'] == steps[index]['area'], 'route begins in different area')
    if route['kind'] == 'accepted-camp':
        m.require(abs(initial['x'] - 5225) <= 24 and abs(initial['y'] - 6289) <= 24,
                  'outside verified camp start; do not invent a route')
    env = os.environ.copy()
    env['PATH'] = str(R / '.local/android-sdk/platform-tools') + ':' + env.get('PATH', '')
    # Monotonic time bounds a user-requested duration and child scheduling only.
    # All frame/frequency/scheduling measurements come from official Perfetto.
    start = time.monotonic()
    result = {'scope': route['scope'], 'route': c['route'], 'start_step': index,
              'completed_walks': 0, 'continuous_pass': False, 'snapshots': [],
              'game_progress': 'normal gameplay progress retained and original lifecycle saved',
              'resumed_segments_cannot_be_summed_as_continuous': True,
              'requested_seconds': 1800, 'performance_measurement': 'official Perfetto only',
              'scaling_policy': 'startup .75, upstream adaptive and HUD settings live; not fixed AB'
                  if p.get('_sustained_current') else 'legacy fixed .60'}
    d['sustained_play'] = result
    save = lambda: m.journal(report, d)
    previous = initial

    def state_now():
        nonlocal previous
        state = endpoint_state(dev, pid, p, m)
        for key, field in (('scale_3d', 'observed_scale_changes'), ('frame_pacing', 'observed_target_changes')):
            if state.get(key) != previous.get(key):
                result.setdefault(field, []).append({'after_walks': result['completed_walks'],
                    'game_time_ns': state['time_ns'], 'from': previous.get(key), 'to': state.get(key)})
        previous = state
        return state

    def snapshot(label):
        state = state_now()
        (out / (label + '-state.json')).write_text(json.dumps(state, indent=2) + '\n')
        row = {'label': label, 'state': state, 'next_step': index,
               'thermal': thermal_log(dev, pid, out, label, m)}
        # Sparse heavyweight observations are outside every official trace window.
        (out / (label + '-meminfo.txt')).write_text(dev.adb('shell', 'dumpsys', 'meminfo', pid))
        m.screenshot_collect(dev, pid, d, report, out, label)
        result['snapshots'].append(row)
        save()
        return state

    def official_window(label, before):
        trace = out / (label + '.pftrace')
        cmd = [sys.executable, c['scheduler_helper']['path'], '--serial', p['serial'],
               '--seconds', '8', '--buffer-mb', '64', '--app-markers',
               '--clean-atrace-print', '--frame-timeline', '--output', str(trace)]
        row = {'command': cmd, 'complete': False}
        result.setdefault('official_windows', []).append(row)
        save()
        with (out / (label + '-console.txt')).open('x') as stream:
            subprocess.run(cmd, env=env, check=True, stdout=stream, stderr=subprocess.STDOUT, timeout=65)
        actual = json.loads(trace.with_suffix('.json').read_text())
        m.require(actual['pid'] == int(pid) and actual['seconds_requested'] == 8
                  and actual['apk_sha256'] == p['new_apk']
                  and actual['shader_chain'] == p['baseline_data']['chain']
                  and m.sha(trace) == actual['trace_sha256'], 'official trace identity differs')
        m.require(not p.get('_operator_stop_requested'), 'operator requested save-stop; test incomplete')
        stop_requested(out, d['transaction_token'], m)
        after = state_now()
        m.require(after['time_ns'] > before['time_ns'], 'game paused during official trace')
        row.update(complete=True, trace_sha256=m.sha(trace), actual_report=str(trace.with_suffix('.json')))
        thermal_log(dev, pid, out, label + '-after-tools', m)
        save()

    try:
        before = snapshot('sustained-00min-before-tools')
        official_window('sustained-00min', before)
        for segment in range(1, 4):
            deadline = start + segment * c['interval_seconds']
            while time.monotonic() < deadline:
                m.require(not p.get('_operator_stop_requested'), 'operator requested save-stop; test incomplete')
                stop_requested(out, d['transaction_token'], m)
                before = state_now()
                step = steps[index]
                m.require(before['area'] == step['area'], 'unexpected area; route not extended')
                cmd = [sys.executable, str(R / 'scripts/dev/android-game-walk-to.py'),
                       str(step['x']), str(step['y']), '--radius', '4', '--timeout', '15',
                       '--ready-timeout', '2', '--serial', p['serial']]
                if 'until_area' in step:
                    cmd += ['--until-area', str(step['until_area'])]
                number = result['completed_walks']
                log = out / ('sustained-walk-%06d.log' % number)
                result['last_walk_attempt'] = {'index': index, 'target': step,
                    'before': before, 'log': str(log), 'command': cmd}
                save()
                try:
                    with log.open('x') as stream:
                        subprocess.run(cmd, env=env, check=True, stdout=stream,
                                       stderr=subprocess.STDOUT, timeout=25)
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                    result['termination_reason'] = 'Original walk tool failed or missed target; not by itself a game crash'
                    save()
                    raise
                after = state_now()
                m.require(after['time_ns'] > before['time_ns'], 'game state stopped advancing')
                m.require(after['area'] == step.get('until_area', step['area']), 'unexpected area after input')
                index = (index + 1) % len(steps)
                result['last_walk_attempt']['after'] = after
                result.update(completed_walks=number + 1, next_step=index, last_actual_state=after)
                save()
            m.require(not p.get('_operator_stop_requested'), 'operator requested save-stop; test incomplete')
            stop_requested(out, d['transaction_token'], m)
            label = 'sustained-%02dmin' % (segment * 10)
            before = snapshot(label + '-before-tools')
            official_window(label, before)
        result['continuous_pass'] = True
        result['scope_limit'] = 'Town movement is not combat, field route, peak-memory or all-scene acceptance'
        save()
    finally:
        # A walk failure or app pause still preserves public API evidence. The
        # caller's original finally performs fresh save2/stop and full CAS restore.
        already_failing = sys.exc_info()[0] is not None
        try:
            result['final_public_API_log'] = thermal_log(dev, pid, out, 'sustained-final', m)
        except BaseException as error:
            result['continuous_pass'] = False
            result['final_observation_error'] = repr(error)
            save()
            if not already_failing:
                raise
        save()
