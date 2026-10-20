"""Bounded original-input orchestration, not a performance sampler or game bot."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
R = Path('/home/dl/d2r-mobile')
RUNTIME = '6f8188e392f21ff045ef640ad8c4f184de048806'
DRIVER = 'f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
REVIEW_STATUS = 'ACTUAL_ORIGINAL_GAME_PAIRED_FUNCTIONAL_OFFICIAL_CPU_GPU_MEMORY_PASS_CLEANUP_FOLLOWUP_CONFIRMED'
CAMP = [{'x': 5215, 'y': 6289, 'area': 1},
        {'x': 5225, 'y': 6295, 'area': 1},
        {'x': 5235, 'y': 6289, 'area': 1}]


def validate(p, pinned, read, require):
    c = p['sustained_play']
    require(set(c) == {'seconds', 'interval_seconds', 'trace_seconds', 'paired_acceptance',
                        'route', 'resume_step', 'scheduler_helper'}, 'sustained config fields')
    require((c['seconds'], c['interval_seconds'], c['trace_seconds']) == (1800, 600, 8),
            'reviewed finite 30min / 10min / official8s only')
    require(not any(k in p for k in ('counter_snapshot', 'performance_tools', 'official_gpu_tools')),
            'sustained test excludes LLDB, extra CPU windows and SDP GUI')
    require(p['runtime_commit'] == RUNTIME and p['driver']['sha256'] == DRIVER
            and p['pool_window_bytes'] == 1048576, 'accepted exact resident f830 1MiB target')
    require(p['phases'][1]['composition_data']['entry_count'] == 6217, 'same6217 only')
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


def endpoint_state(dev, pid, p, m):
    m.require(dev.adb('shell', 'pidof ' + m.PKG + ' || true') == pid, 'sustained PID changed')
    state = json.loads(dev.app('tail', '-n', '1', 'files/runtime/game-state.jsonl'))
    m.require(state['valid'] and state['world_input_ready'], 'sustained original world not ready')
    m.require(state['scale_3d'] == .6 and state['game_output'] == state['surface_output'] ==
              {'width': 2750, 'height': 1260}, 'sustained fixed resolution changed')
    m.require(state['guest_heap']['shared'] is True and
              state['pool_windows']['window_bytes'] == p['pool_window_bytes'], 'shared-window changed')
    # Preserve all existing PSO/target counters verbatim; do not compute FPS here.
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
              'requested_seconds': 1800, 'performance_measurement': 'official Perfetto only'}
    d['sustained_play'] = result
    save = lambda: m.journal(report, d)

    def snapshot(label):
        state = endpoint_state(dev, pid, p, m)
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
        after = endpoint_state(dev, pid, p, m)
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
                before = endpoint_state(dev, pid, p, m)
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
                after = endpoint_state(dev, pid, p, m)
                m.require(after['time_ns'] > before['time_ns'], 'game state stopped advancing')
                m.require(after['area'] == step.get('until_area', step['area']), 'unexpected area after input')
                index = (index + 1) % len(steps)
                result['last_walk_attempt']['after'] = after
                old_target = result.get('last_actual_state', initial).get('frame_pacing')
                if after.get('frame_pacing') != old_target:
                    result.setdefault('observed_target_changes', []).append({
                        'after_walk': number, 'game_time_ns': after['time_ns'],
                        'from': old_target, 'to': after.get('frame_pacing')})
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
