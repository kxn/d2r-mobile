#!/usr/bin/env python3
"""Compare existing official profiler reductions; never acquire or sample a device."""
import argparse
import csv
import hashlib
import json
import math
import re
import sys
import subprocess
import runpy
from pathlib import Path

SCHEMA = 1
# Explicit monotonic counters only. Gauges (live_bytes, sizes, current objects,
# descriptor_history.bytes, etc.) must not acquire a fabricated per-frame rate.
COUNTERS = {
    'pipeline_prepare': 'reuses lookups checks',
    'descriptor_allocation': 'calls sets',
    'descriptor_history': 'hits misses',
    'descriptor_reuse': 'hits pool_resets',
    'descriptor_contents': 'hits misses',
    'sampler_lookup': 'hits misses',
    'texture_bindings': 'views prepares misses',
    'pool_lookup': 'hits ranges slow',
    'image_buffer_split': 'builds hits skipped_items expansions expanded_items invalidations',
    'attachment_raster_run': 'captures hot_records scatters expansions',
    'driver_constants': 'last_hits content_hits misses',
    'store_descriptors': 'hits misses',
    'store_shader_slots': 'stages skipped_bindings',
    'store_read_events': 'recorded duplicates_skipped',
    'texture_dirty_proofs': 'hits checks',
    'pool_uploads': 'full_bytes used_bytes protected_bytes overwrite_skipped_pages',
    'buffer_sync': 'guest_submits internal_submits readback_ranges readback_bytes internal_readback_bytes cpu_reads cpu_submit_waits cpu_pending_waits proof_reads proof_pending upload_waits',
    'readback_memory': 'invalidations',
    'storage_alignment': 'direct copies bytes',
    'cpu_known_copies': 'snapshots published fallbacks',
    'query_pool_cache': 'reuses creates',
    'texel_view_cache': 'hits creates',
    'stream_submits': 'splits boundaries draws',
    'stage_barriers': 'relaxed fallback',
    'image_layouts': 'transitions safe_uses fallbacks',
    'sample_liveness': 'live_draws overwritten_before_report elided_draws',
}


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def finite(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def number(s):
    try:
        v = float(s)
        return v if math.isfinite(v) else None
    except (ValueError, TypeError):
        return None


def sections(path):
    """Perfetto CLI CSV contains multiple header/result sets, not one CSV table."""
    out, header = {}, None
    if not path.exists():
        return out
    with path.open(newline='') as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0] == 'section':
                header = row
            elif header:
                if len(row) != len(header):
                    raise ValueError(f'{path}: malformed CSV row {row!r}')
                out.setdefault(row[0], []).append(dict(zip(header[1:], row[1:])))
    return out


def metric(value=None, status=None, **extra):
    return {'status': status or ('available' if value is not None else 'missing'), 'value': value, **extra}


def delta(a, b):
    if not finite(a) or not finite(b):
        return {'reference': a, 'candidate': b, 'delta': None, 'percent': None}
    return {'reference': a, 'candidate': b, 'delta': b - a,
            'percent': (b / a - 1) * 100 if a else None}


def dotted(obj, path):
    for key in path.split('.'):
        if not isinstance(obj, dict) or key not in obj:
            return None
        obj = obj[key]
    return obj


def scheduler_sql(pid, tid, lo, hi, frames):
    """Use trace_processor thread_state semantics; BOOTTIME values only.

    Window functions run BEFORE clipping: waker_utid belongs to the following
    Runnable row, not to the Sleeping row. See the original worker-handoff SQL.
    """
    if any(isinstance(x, bool) or not isinstance(x, int) for x in (pid, tid, lo, hi, frames)):
        raise ValueError('scheduler PID/TID/BOOTTIME bounds/frame count must be integers')
    if pid <= 0 or tid <= 0 or lo < 0 or hi <= lo or frames <= 0:
        raise ValueError('invalid scheduler owner/window/frame count')
    return f'''-- Official Perfetto thread_state reduction, same complete Surface BOOTTIME window.
-- Lead BEFORE clip; sleep waker is on the following contiguous Runnable row.
CREATE PERFETTO TABLE comparison_owner AS
SELECT t.utid,t.upid,t.tid,t.name,p.pid FROM thread t JOIN process p USING(upid)
WHERE t.tid={tid} AND p.pid={pid}
AND (t.start_ts IS NULL OR t.start_ts<{hi}) AND (t.end_ts IS NULL OR t.end_ts>{lo});
SELECT 'scheduler_identity' section,utid,upid,tid,pid,name FROM comparison_owner;
CREATE PERFETTO TABLE comparison_owner_states AS
SELECT s.*,lead(s.waker_utid) OVER(PARTITION BY s.utid ORDER BY s.ts,s.id) next_waker,
lead(s.state) OVER(PARTITION BY s.utid ORDER BY s.ts,s.id) next_state,
lead(s.ts) OVER(PARTITION BY s.utid ORDER BY s.ts,s.id) next_ts
FROM thread_state s JOIN comparison_owner o USING(utid);
CREATE PERFETTO TABLE comparison_clipped_states AS
-- dur=-1 is an incomplete record, not evidence of a state through trace_end.
-- Match the existing official SQL's dur>0 rule; coverage gaps stay unknown.
SELECT *,max(ts,{lo}) a,min(ts+dur,{hi}) b
FROM comparison_owner_states WHERE dur>0 AND ts<{hi} AND ts+dur>{lo};
SELECT 'scheduler_state' section,state,sum(b-a) duration_ns,sum(b-a)/1e6/{frames} ms_per_Surface,
count(*) intervals,max(b-a) max_clip_ns FROM comparison_clipped_states WHERE b>a GROUP BY state;
SELECT 'scheduler_integrity' section,sum(CASE WHEN previous_max_end>a THEN 1 ELSE 0 END) overlapping_intervals
FROM (SELECT a,b,max(b) OVER(ORDER BY a,b ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) previous_max_end
FROM comparison_clipped_states WHERE b>a);
CREATE PERFETTO TABLE comparison_sleep_wakes AS
SELECT s.*,w.tid waker_tid,w.name waker_name,w.upid waker_upid,p.pid waker_pid,
CASE WHEN s.next_state NOT IN ('R','R+') OR s.next_state IS NULL OR s.next_ts!=s.ts+s.dur THEN 'unknown_no_contiguous_runnable'
 WHEN s.next_waker IS NULL OR w.utid IS NULL THEN 'unknown_waker'
 WHEN w.utid=o.utid THEN 'self'
 WHEN w.upid=o.upid THEN 'same_process_other_thread'
 WHEN w.upid IS NULL THEN 'unknown_process'
 ELSE 'other_process' END waker_category
FROM comparison_clipped_states s JOIN comparison_owner o USING(utid)
LEFT JOIN thread w ON w.utid=s.next_waker LEFT JOIN process p ON p.upid=w.upid
WHERE s.b>s.a AND (s.state='S' OR s.state GLOB 'D*');
SELECT 'scheduler_waker' section,waker_category,next_waker waker_utid,waker_tid,waker_pid,waker_name,
sum(b-a) attributed_sleep_ns,sum(b-a)/1e6/{frames} attributed_sleep_ms_per_Surface,count(*) sleeps
FROM comparison_sleep_wakes GROUP BY waker_category,next_waker,waker_tid,waker_pid,waker_name
ORDER BY attributed_sleep_ns DESC;
'''


def scheduler_summary(tables, expected_running, window, frames):
    identity = tables.get('scheduler_identity', [])
    if len(identity) != 1:
        return metric(None, status='missing' if not identity else 'invalid', reason='expected exactly one owner PID/TID/lifetime match')
    rows = tables.get('scheduler_state', [])
    if not rows:
        return metric(None, reason='trace has no owner thread_state track in this window')
    integrity = tables.get('scheduler_integrity', [])
    if len(integrity) != 1 or number(integrity[0].get('overlapping_intervals')) != 0:
        return metric(None, status='invalid', reason='owner state intervals overlap or official overlap check missing')
    states = {}
    for row in rows:
        key = row['state']
        ns = number(row.get('duration_ns'))
        if key in states or ns is None or ns < 0:
            return metric(None, status='invalid', reason='invalid/duplicate thread_state aggregate')
        states[key] = {'duration_ns': ns, 'ms_per_Surface': ns / 1e6 / frames,
                       'intervals': number(row.get('intervals')), 'max_clip_ns': number(row.get('max_clip_ns'))}
    coverage = sum(r['duration_ns'] for r in states.values())
    if coverage > window[1] - window[0] + 1:
        return metric(None, status='invalid', reason='overlapping thread_state coverage exceeds complete Surface window')
    running = states.get('Running', {}).get('ms_per_Surface', 0)
    if expected_running is not None and not math.isclose(running, expected_running, rel_tol=1e-5, abs_tol=1e-5):
        return metric(None, status='invalid', reason=f'official thread_state Running {running} disagrees with analysis.csv {expected_running}')
    categories, sources = {}, []
    for row in tables.get('scheduler_waker', []):
        ns = number(row.get('attributed_sleep_ns'))
        if ns is None or ns < 0:
            return metric(None, status='invalid', reason='invalid waker sleep attribution')
        key = row['waker_category']
        categories[key] = categories.get(key, 0) + ns / 1e6 / frames
        sources.append({**row, 'attributed_sleep_ms_per_Surface': ns / 1e6 / frames})
    sleep = sum(r['ms_per_Surface'] for k, r in states.items() if k == 'S' or k.startswith('D'))
    if not math.isclose(sum(categories.values()), sleep, rel_tol=1e-6, abs_tol=1e-6):
        return metric(None, status='invalid', reason='waker source categories do not partition all S/D sleep')
    return metric({'owner': identity[0], 'states': states, 'running_ms_per_Surface': running,
                   'runnable_ms_per_Surface': sum(states.get(k, {}).get('ms_per_Surface', 0) for k in ('R', 'R+')),
                   'sleep_S_ms_per_Surface': states.get('S', {}).get('ms_per_Surface', 0),
                   'blocked_D_ms_per_Surface': sum(r['ms_per_Surface'] for k, r in states.items() if k.startswith('D')),
                   'untracked_ms_per_Surface': max(0, window[1] - window[0] - coverage) / 1e6 / frames,
                   'waker_categories_ms_per_Surface': categories, 'waker_sources': sources,
                   'running_matches_analysis': expected_running is not None,
                   'note': 'S/D duration is attributed to the following contiguous R/R+ waker. Waker identity does not prove a lock, GPU wait, or cause of the entire sleep.'},
                  scope='official trace_processor thread_state, mutually exclusive clipped BOOTTIME states / same complete Surface interval count')


def run_scheduler(arm, tool, output):
    if tool is None or not Path(tool).is_file():
        return metric(None, reason='official trace_processor unavailable; supply --trace-processor')
    trace = arm.path / 'trace.pftrace'
    if not trace.exists():
        return metric(None, reason='original trace.pftrace absent')
    f = arm.first('corrected_complete_Surface_intervals')
    bounds = arm.first('corrected_Surface_time_bounds')
    pid = arm.capture.get('pid') or arm.budget.get('PID')
    tid = arm.budget.get('TID') or arm.receipt.get('owner_TID')
    if not tid:
        owners = [r for r in arm.csv.get('thread_CPU', []) if r.get('has_replay') == '1']
        tid = int(owners[0]['tid']) if len(owners) == 1 else None
    try:
        lo, hi = int(bounds['a']), int(bounds['b'])
        n = int(f['intervals'])
        query = scheduler_sql(pid, tid, lo, hi, n)
    except (ValueError, TypeError, KeyError) as e:
        return metric(None, status='invalid', reason=f'owner or complete Surface BOOTTIME window missing/invalid: {e}')
    receipt_bounds = arm.counter_receipt.get('Surface_BOOTTIME_bounds_ns')
    if receipt_bounds is not None and receipt_bounds != [lo, hi]:
        return metric(None, status='invalid', reason='Surface BOOTTIME receipt differs from official analysis.csv; never substitute MONOTONIC bounds')
    trace_hash = sha(trace)
    if arm.capture.get('trace_sha256') and arm.capture['trace_sha256'] != trace_hash:
        return metric(None, status='invalid', reason='scheduler trace hash differs from capture receipt')
    output.mkdir(parents=True, exist_ok=True)
    sql, csv_path, err = output / 'owner-scheduler.sql', output / 'owner-scheduler.csv', output / 'owner-scheduler.stderr'
    query_hash = hashlib.sha256(query.encode()).hexdigest()
    old_receipt = output / 'receipt.json'
    if old_receipt.exists() and csv_path.exists() and sql.exists():
        cached = json.loads(old_receipt.read_text())
        if cached.get('returncode') == 0 and cached.get('tool_sha256') == sha(Path(tool)) and cached.get('trace_sha256') == trace_hash and cached.get('sql_sha256') == query_hash and sha(sql) == query_hash and cached.get('csv_sha256') == sha(csv_path):
            owner_row = next((r for r in arm.csv.get('thread_CPU', []) if r.get('tid') == str(tid)), {})
            answer = scheduler_summary(sections(csv_path), number(owner_row.get('running_ms_per_Surface_interval')), [lo, hi], n)
            answer['provenance'] = {**cached, 'reused_official_reduction': True}
            return answer
    sql.write_text(query)
    command = [str(Path(tool).resolve()), 'query', str(trace), '-f', str(sql)]
    with csv_path.open('w') as stdout, err.open('w') as stderr:
        try:
            result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=120, check=False)
            returncode = result.returncode
        except (subprocess.TimeoutExpired, OSError) as e:
            returncode = str(e)
    version = subprocess.run([str(Path(tool).resolve()), '--version'], capture_output=True, text=True, timeout=15, check=False)
    provenance = {'argv': command, 'returncode': returncode, 'tool_path': str(Path(tool).resolve()), 'tool_sha256': sha(Path(tool)),
                  'official_version': (version.stdout + version.stderr).strip(), 'trace_sha256': trace_hash,
                  'Surface_BOOTTIME_bounds_ns': [lo, hi], 'Surface_intervals': n, 'PID': pid, 'TID': tid,
                  'sql_sha256': sha(sql), 'csv_sha256': sha(csv_path)}
    (output / 'receipt.json').write_text(json.dumps(provenance, indent=2) + '\n')
    if returncode != 0:
        return metric(None, status='invalid', reason='official trace_processor query failed; inspect original stderr', provenance=provenance)
    owner_row = next((r for r in arm.csv.get('thread_CPU', []) if r.get('tid') == str(tid)), {})
    answer = scheduler_summary(sections(csv_path), number(owner_row.get('running_ms_per_Surface_interval')), [lo, hi], n)
    answer['provenance'] = provenance
    return answer




class Arm:
    def __init__(self, directory, label, config=None, metadata=None, gpu=None, gpu_arm=None):
        self.path, self.label = Path(directory).resolve(), label
        self.config, self.sources, self.issues = config or {}, [], []
        self.scheduler = None
        if not self.path.is_dir():
            raise ValueError(f'arm directory does not exist: {self.path}')
        self.base = self.path.name.removesuffix('-BASE')
        self.capture = self.load(self.path / 'capture.json')
        self.before = self.load(self.path / 'before-state.json')
        self.after = self.load(self.path / 'after-state.json')
        self.receipt = self.load(self.path.parent / (self.base + '-receipt.json'))
        self.counter_receipt = self.load(self.path.parent / (self.path.name + '-counter-receipt.json'))
        self.launch = self.load(self.path.parent / (self.base + '-launch.json'))
        self.meta = self.load(Path(metadata)) if metadata else {}
        self.budget_path = self.path.parent / 'offline-analysis' / self.path.name / 'budget.json'
        if self.meta.get('budget_path'):
            self.budget_path = Path(self.meta['budget_path']).resolve()
        self.budget = self.load(self.budget_path)
        self.csv = sections(self.path / 'analysis.csv')
        if (self.path / 'analysis.csv').exists():
            self.remember(self.path / 'analysis.csv')
        self.gpu = self.load(Path(gpu)) if gpu else self.load(self.path / 'sdp-metrics.json')
        self.gpu_arm = gpu_arm

    def remember(self, path):
        self.sources.append({'path': str(path.resolve()), 'sha256': sha(path)})

    def load(self, path):
        if not path.exists():
            return {}
        self.remember(path)
        with path.open() as f:
            v = json.load(f)
        if not isinstance(v, dict):
            raise ValueError(f'{path}: expected JSON object')
        return v

    def first(self, key):
        rows = self.csv.get(key, [])
        return rows[0] if rows else {}

    def qualify_budget(self):
        if not self.budget:
            return False
        errors = []
        frame = number(self.first('corrected_complete_Surface_intervals').get('intervals'))
        if frame is None or frame <= 0 or frame != self.budget.get('Surface_intervals'):
            errors.append('budget Surface count does not match official analysis.csv')
        mono = self.counter_receipt.get('Surface_MONOTONIC_bounds_ns')
        if mono is None or mono != self.budget.get('MONOTONIC_bounds_ns'):
            errors.append('budget MONOTONIC bounds not matched by counter receipt')
        if self.counter_receipt.get('completed') is not True:
            errors.append('counter receipt is incomplete')
        if self.budget.get('TID') != self.counter_receipt.get('TID') or self.budget.get('PID') != self.capture.get('pid'):
            errors.append('budget owner PID/TID does not match capture/counter receipt')
        checks = self.budget.get('inputs_sha256', {})
        if self.capture.get('trace_sha256') != checks.get('trace.pftrace'):
            errors.append('capture trace hash differs from budget input')
        for event, bounds in self.budget.get('exact_owner_counter_brackets', {}).items():
            values = [bounds.get(k) for k in ('window_lower', 'window_upper', 'per_Surface_interval_lower', 'per_Surface_interval_upper')]
            if not all(finite(x) and x >= 0 for x in values) or values[0] > values[1] or values[2] > values[3]:
                errors.append(f'invalid exact counter bracket: {event}')
            elif frame and any(not math.isclose(values[i] / frame, values[i + 2], rel_tol=1e-9, abs_tol=1e-6) for i in range(2)):
                errors.append(f'exact counter denominator mismatch: {event}')
        for key, p in [('trace.pftrace', self.path / 'trace.pftrace'),
                       ('counter.data', self.path / 'counter.data'),
                       ('receipt', self.path.parent / (self.path.name + '-counter-receipt.json'))]:
            if key not in checks or not p.exists():
                errors.append(f'budget input linkage missing: {key}')
            elif sha(p) != checks[key]:
                errors.append(f'budget input hash mismatch: {key}')
        host = dotted(self.receipt, 'APK.host_library_sha256')
        if checks.get('host_ELF') is None or host is None or checks['host_ELF'] != host:
            errors.append('budget ELF identity not linked to run receipt')
        self.issues.extend(errors)
        return not errors

    def result(self):
        budget_ok = self.qualify_budget()
        if self.scheduler and self.scheduler.get('status') == 'invalid':
            self.issues.append('official scheduler query invalid: ' + self.scheduler.get('reason', 'see scheduler provenance'))
        f = self.first('corrected_complete_Surface_intervals')
        frame = number(f.get('intervals'))
        frames = int(frame) if frame is not None and frame > 0 else None
        mean = number(f.get('mean_ms'))
        whole = self.first('whole_app_CPU')
        tid = self.budget.get('TID') or self.receipt.get('owner_TID')
        threads = []
        freqs = {r.get('tid'): r for r in self.csv.get('thread_CPU_frequency', [])}
        for row in self.csv.get('thread_CPU', []):
            t = {**row, 'owner': str(row.get('tid')) == str(tid)}
            for k in ('running_ms', 'running_ms_per_Surface_interval', 'sched_intervals'):
                t[k] = number(row.get(k))
            t['weighted_mhz_while_running'] = number(freqs.get(row.get('tid'), {}).get('weighted_mhz'))
            t['frequency_covered_ms'] = number(freqs.get(row.get('tid'), {}).get('frequency_covered_ms'))
            threads.append(t)
        threads.sort(key=lambda r: r['running_ms'] if r['running_ms'] is not None else -1, reverse=True)
        owner = next((t for t in threads if t['owner']), None)
        before_id = {k: self.before.get(k) for k in ('area', 'x', 'y', 'mode', 'scale_3d', 'game_output', 'surface_output', 'frame_pacing')}
        after_id = {k: self.after.get(k) for k in before_id}
        scene_changed = {k: {'before': before_id[k], 'after': after_id[k]} for k in before_id if before_id[k] != after_id[k]}
        if scene_changed:
            self.issues.append('scene/output changed during arm; inspect identity.scene_changes')
        switches = {k: v for k, v in self.launch.items()
                    if k.startswith(('requested_', 'actual_')) and isinstance(v, (bool, str, int, float, type(None)))}
        apk = self.receipt.get('APK', {})
        identity = {'APK_sha256': self.capture.get('APK_sha256') or apk.get('apk_sha256'),
                    'host_ELF_sha256': apk.get('host_library_sha256'),
                    'source_revision': self.receipt.get('source') or apk.get('source_revision'),
                    'package': self.capture.get('package'), 'device_record_meta': self.budget.get('record_meta', {}),
                    'scene_before': before_id, 'scene_after': after_id, 'scene_changes': scene_changed,
                    'switches': switches, 'performance_baseline_eligible': apk.get('performance_baseline_eligible')}
        if self.capture.get('APK_sha256') and apk.get('apk_sha256') and self.capture['APK_sha256'] != apk['apk_sha256']:
            self.issues.append('capture APK and run receipt APK mismatch')
        record_stat = dotted(self.budget, 'record_meta.record_stat') or ''
        for field, value in re.findall(r'(\w*(?:lost|truncated)\w*)=(\d+)', record_stat):
            if int(value):
                self.issues.append(f'official sample loss/truncation: {field}={value}')
        duties = self.phase_table(budget_ok)
        counters = self.state_counters()
        gpu = self.gpu_result()
        # Scheduler state/waker tables are optional reductions, never inferred
        # from whole-frame minus Running (other threads/GPU may overlap).
        scheduler = {k: rows for k, rows in self.csv.items() if re.search(r'(sleep|runnable|waker|thread_state)', k, re.I)}
        return {'label': self.label, 'directory': str(self.path), 'identity': identity,
                'frames': metric({'count': frames, 'mean_ms': mean, 'p50_ms': number(f.get('p50_ms')),
                                  'p95_ms': number(f.get('p95_ms')), 'max_ms': number(f.get('max_ms')),
                                  'FPS': 1000 / mean if mean and mean > 0 else None}, status='available' if frames else 'missing',
                                 denominator='complete Surface intervals', bounds_BOOTTIME_ns=self.counter_receipt.get('Surface_BOOTTIME_bounds_ns')),
                'CPU': {'whole_app_running_ms_per_Surface': number(whole.get('running_ms_per_Surface_interval')),
                        'whole_app_running_ms': number(whole.get('running_ms')),
                        'whole_app_weighted_mhz_while_running': number(self.first('whole_app_CPU_frequency').get('weighted_mhz')),
                        'owner': owner, 'threads_sorted_by_running': threads,
                        'scheduler_states_and_wakers': self.scheduler if self.scheduler is not None else metric(scheduler or None),
                        'note': 'owner is a measured thread, not the entire translation workload; thread IDs differ between runs'},
                'exact_owner_counters': metric(self.budget.get('exact_owner_counter_brackets') if budget_ok else None,
                                               status='available' if budget_ok and self.budget.get('exact_owner_counter_brackets') else ('invalid' if self.budget else 'missing'),
                                               scope='retired user-space owner counters, reused reducer edge brackets; not per-function counters; bounds are not confidence intervals'),
                'sampled_phase_duties': duties,
                'sampled_representative_chains': self.budget.get('actual_representative_chains') if budget_ok else None,
                'sampled_leaf_modules_by_duty': self.budget.get('leaf_modules_by_duty') if budget_ok else None,
                'sampled_top_self': metric(self.budget.get('top_self_by_duty') if budget_ok else None,
                                          scope='sampled self periods only; truncated top list, absent entries are unknown, not zero'),
                'sample_health': {'samples': self.budget.get('samples'), 'record_stat': dotted(self.budget, 'record_meta.record_stat'),
                                  'uncertainty': self.budget.get('uncertainty', [])},
                'state_counters': counters, 'GPU': gpu, 'issues': self.issues, 'sources': self.sources}

    def phase_table(self, valid):
        rows = self.budget.get('disjoint_actual_phase_duties')
        if not valid or rows is None:
            return metric(None, status='invalid' if self.budget else 'missing')
        total = self.budget.get('sampled_instruction_period_sum')
        keys = [tuple(r['key']) for r in rows]
        if len(set(keys)) != len(keys) or not finite(total) or total <= 0 or any(not finite(r.get('sampled_instruction_period')) or r['sampled_instruction_period'] < 0 for r in rows) or sum(r['sampled_instruction_period'] for r in rows) != total:
            self.issues.append('phase duties are not a complete mutually exclusive partition')
            return metric(None, status='invalid')
        n = self.budget.get('Surface_intervals')
        if not n or any(not math.isclose(r['sampled_instruction_period'] / n / 1e6, r['sampled_Minstructions_per_Surface'], rel_tol=1e-9, abs_tol=1e-9) for r in rows):
            self.issues.append('phase allocation denominator mismatch')
            return metric(None, status='invalid')
        groups = self.config.get('groups', [])
        merged = {}
        for r in rows:
            phase, duty = r['key']
            group = None
            for g in groups:
                if re.search(g.get('phase', '.*'), phase) and re.search(g.get('duty', '.*'), duty):
                    group = g['label']; break
            # Unmatched keys retain their complete identity. No black-hole other.
            key = (group,) if group else (phase, duty)
            row = merged.setdefault(key, {'key': list(key), 'sampled_instruction_period': 0,
                                          'sampled_Minstructions_per_Surface': 0, 'members': []})
            row['sampled_instruction_period'] += r['sampled_instruction_period']
            row['sampled_Minstructions_per_Surface'] += r['sampled_Minstructions_per_Surface']
            row['members'].append(r['key'])
        return metric(list(merged.values()), complete_disjoint_partition=True,
                      scope='sample-period allocation by actual phase/ancestor; estimated instructions, not exact time or per-function PMU counts')

    def state_counters(self):
        paths = [f'{group}.{field}' for group, fields in COUNTERS.items() for field in fields.split()]
        paths += self.config.get('counter_paths', [])
        bt, at = self.before.get('time_ns'), self.after.get('time_ns')
        elapsed = (at - bt) / 1e9 if finite(bt) and finite(at) and at > bt else None
        before_n, after_n = dotted(self.before, 'buffer_sync.guest_submits'), dotted(self.after, 'buffer_sync.guest_submits')
        submits = after_n - before_n if finite(before_n) and finite(after_n) and after_n > before_n else None
        bounds = self.counter_receipt.get('Surface_MONOTONIC_bounds_ns')
        bracket = bool(elapsed and bounds and bt <= bounds[0] < bounds[1] <= at)
        rows = []
        for path in dict.fromkeys(paths):
            b, a = dotted(self.before, path), dotted(self.after, path)
            if not finite(b) or not finite(a):
                rows.append({'key': path, 'status': 'missing', 'before': b, 'after': a, 'delta': None, 'per_guest_submit': None}); continue
            d = a - b
            status = 'available' if d >= 0 and elapsed else 'reset_or_invalid_window'
            rows.append({'key': path, 'status': status, 'before': b, 'after': a, 'delta': d if status == 'available' else None,
                         'per_guest_submit': d / submits if submits and status == 'available' else None})
        return {'status': 'available' if elapsed else 'missing', 'bounds_MONOTONIC_ns': [bt, at],
                'duration_seconds': elapsed, 'guest_submit_delta': submits, 'brackets_Surface_window': bracket,
                'leading_extra_ns': bounds[0] - bt if bracket else None,
                'trailing_extra_ns': at - bounds[1] if bracket else None,
                'denominator': 'guest submits measured between the SAME before/after state snapshots; NOT clipped Surface frame count',
                'rows': rows}

    def gpu_result(self):
        if not self.gpu:
            return metric(None, scope='global/system GPU hardware counters', reason='no SDP metrics supplied; no GPU bound/idle conclusion')
        selected = self.gpu
        if isinstance(selected.get('arms'), dict):
            if not self.gpu_arm or self.gpu_arm not in selected['arms']:
                self.issues.append('GPU comparison JSON requires an existing --*-gpu-arm selection')
                return metric(None, status='invalid', scope='global/system GPU hardware counters')
            selected = selected['arms'][self.gpu_arm]
        raw = selected.get('metrics')
        if isinstance(raw, dict):
            metrics = [{'name': name, **row} for name, row in raw.items()]
        elif isinstance(raw, list):
            metrics = raw
        else:
            self.issues.append('GPU supplied object has no metrics list/dict')
            return metric(None, status='invalid', scope='global/system GPU hardware counters')
        # Accept existing official SDP reducers, including the clipped integral
        # schema. Do not reconstruct rates from samples or query hardware here.
        trace_hash = selected.get('trace_sha256')
        same_trace = bool(trace_hash and self.capture.get('trace_sha256') == trace_hash)
        bounds = selected.get('Surface_first_last_BOOTTIME_ns')
        csv_bounds = self.first('corrected_Surface_time_bounds')
        expected_bounds = [number(csv_bounds.get('a')), number(csv_bounds.get('b'))]
        same_bounds = bool(bounds and expected_bounds == bounds)
        alignment = 'same trace and complete Surface bounds' if same_trace and same_bounds else 'unverified: GPU and CPU windows not proven identical'
        rows = []
        for row in metrics:
            if not isinstance(row, dict) or not isinstance(row.get('name'), str):
                self.issues.append('invalid GPU metric row'); continue
            rows.append({**row, 'reported_mean': row.get('time_weighted_mean', row.get('mean'))})
        return metric({'tool': self.gpu.get('tool'), 'metrics': rows,
                       'window_us': selected.get('window_us'), 'limitation': self.gpu.get('limitation'),
                       'declared_scope': self.gpu.get('scope'), 'session_sha256': selected.get('session_sha256'),
                       'trace_sha256': trace_hash, 'Surface_bounds_BOOTTIME_ns': bounds,
                       'clocks_note': 'Clocks/s is clock-count rate, not proof of instantaneous GPU MHz or fixed frequency',
                       'stall_note': 'percent counters overlap: do not sum or multiply by frame length to create exclusive stall milliseconds'},
                      scope='global/system GPU hardware counters; never process-exclusive', alignment=alignment)


def compare_rows(a, b, value_key, complete=False):
    aa = {tuple(r['key']) if isinstance(r['key'], list) else r['key']: r for r in a or []}
    bb = {tuple(r['key']) if isinstance(r['key'], list) else r['key']: r for r in b or []}
    rows = []
    for k in sorted(aa.keys() | bb.keys()):
        av = aa[k].get(value_key) if k in aa else (0 if complete else None)
        bv = bb[k].get(value_key) if k in bb else (0 if complete else None)
        rows.append({'key': list(k) if isinstance(k, tuple) else k, **delta(av, bv),
                     'reference_members': aa.get(k, {}).get('members'), 'candidate_members': bb.get(k, {}).get('members')})
    return sorted(rows, key=lambda r: abs(r['delta']) if r['delta'] is not None else -1, reverse=True)


def compare(reference, candidate):
    a, b = reference, candidate
    ids = {}
    for key in ('APK_sha256', 'host_ELF_sha256', 'package'):
        av, bv = a['identity'].get(key), b['identity'].get(key)
        ids[key] = {'reference': av, 'candidate': bv, 'status': 'missing' if av is None or bv is None else ('same' if av == bv else 'different')}
    same = [ids[k]['status'] for k in ('APK_sha256', 'host_ELF_sha256')]
    same_binary = 'same' if all(x == 'same' for x in same) else ('different' if 'different' in same else 'unverified')
    device_keys = ('android_build_fingerprint', 'product_props', 'kernel_version')
    devices = {k: {'reference': a['identity']['device_record_meta'].get(k), 'candidate': b['identity']['device_record_meta'].get(k)} for k in device_keys}
    same_device = None if any(v['reference'] is None or v['candidate'] is None for v in devices.values()) else all(v['reference'] == v['candidate'] for v in devices.values())
    eligibility = [a['identity'].get('performance_baseline_eligible'), b['identity'].get('performance_baseline_eligible')]
    baseline = False if False in eligibility else (True if eligibility == [True, True] else None)
    switches = {k: {'reference': a['identity']['switches'].get(k), 'candidate': b['identity']['switches'].get(k)}
                for k in sorted(a['identity']['switches'].keys() | b['identity']['switches'].keys())
                if a['identity']['switches'].get(k) != b['identity']['switches'].get(k)}
    fps = {k: delta((a['frames']['value'] or {}).get(k), (b['frames']['value'] or {}).get(k)) for k in ('FPS', 'mean_ms', 'p95_ms', 'max_ms', 'count')}
    cpu = {k: delta(a['CPU'].get(k), b['CPU'].get(k)) for k in ('whole_app_running_ms_per_Surface', 'whole_app_weighted_mhz_while_running')}
    for key in ('running_ms_per_Surface_interval', 'weighted_mhz_while_running'):
        cpu['owner_' + key] = delta((a['CPU']['owner'] or {}).get(key), (b['CPU']['owner'] or {}).get(key))
    sched_a = a['CPU']['scheduler_states_and_wakers']['value'] or {}
    sched_b = b['CPU']['scheduler_states_and_wakers']['value'] or {}
    sched_complete = all(isinstance(v.get('states'), dict) for v in (sched_a, sched_b))
    for key in ('sleep_S_ms_per_Surface', 'blocked_D_ms_per_Surface', 'runnable_ms_per_Surface', 'untracked_ms_per_Surface'):
        cpu['owner_' + key] = delta(sched_a.get(key), sched_b.get(key))
    state_changes, waker_changes = [], []
    if sched_complete:
        sa = [{'key': k, 'ms': v['ms_per_Surface']} for k, v in sched_a['states'].items()]
        sb = [{'key': k, 'ms': v['ms_per_Surface']} for k, v in sched_b['states'].items()]
        state_changes = compare_rows(sa, sb, 'ms', complete=True)
        wa = [{'key': k, 'ms': v} for k, v in sched_a['waker_categories_ms_per_Surface'].items()]
        wb = [{'key': k, 'ms': v} for k, v in sched_b['waker_categories_ms_per_Surface'].items()]
        waker_changes = compare_rows(wa, wb, 'ms', complete=True)
    exact = {}
    ca, cb = a['exact_owner_counters']['value'] or {}, b['exact_owner_counters']['value'] or {}
    for event in sorted(ca.keys() | cb.keys()):
        ar, br = ca.get(event, {}), cb.get(event, {})
        al, ah = ar.get('per_Surface_interval_lower'), ar.get('per_Surface_interval_upper')
        bl, bh = br.get('per_Surface_interval_lower'), br.get('per_Surface_interval_upper')
        available = all(finite(x) for x in (al, ah, bl, bh))
        exact[event] = {'reference_interval': [al, ah], 'candidate_interval': [bl, bh],
                        'delta_interval': [bl - ah, bh - al] if available else None,
                        'percent_interval': [(bl / ah - 1) * 100, (bh / al - 1) * 100] if available and al > 0 and ah > 0 else None}
    pa, pb = a['sampled_phase_duties'], b['sampled_phase_duties']
    phase_ok = pa.get('complete_disjoint_partition') and pb.get('complete_disjoint_partition')
    duties = compare_rows(pa['value'], pb['value'], 'sampled_Minstructions_per_Surface', complete=bool(phase_ok)) if phase_ok else []
    state_rows = compare_rows(a['state_counters']['rows'], b['state_counters']['rows'], 'per_guest_submit')
    selfs = compare_rows(a['sampled_top_self']['value'], b['sampled_top_self']['value'], 'sampled_Minstructions_per_Surface')
    for row in selfs:
        row['reference_status'] = 'available' if row['reference'] is not None else 'not_ranked_or_missing'
        row['candidate_status'] = 'available' if row['candidate'] is not None else 'not_ranked_or_missing'
    gpu_a, gpu_b = a['GPU']['value'], b['GPU']['value']
    gpu_rows = []
    if gpu_a and gpu_b:
        ga = [{'key': r['name'], **r} for r in gpu_a['metrics'] if 'name' in r]
        gb = [{'key': r['name'], **r} for r in gpu_b['metrics'] if 'name' in r]
        gpu_rows = compare_rows(ga, gb, 'reported_mean')
    negatives = [r for r in duties if r['delta'] is not None and r['delta'] < 0]
    positives = [r for r in duties if r['delta'] is not None and r['delta'] > 0]
    unknown = [r for r in duties if any(re.search(r'unknown|missing|outside|other', str(k), re.I)
                for k in [r['key'], r.get('reference_members'), r.get('candidate_members')])]
    if any('capture APK and run receipt APK mismatch' in arm['issues'] for arm in (a, b)):
        same_binary = 'invalid'
    gpu_integrals = compare_rows(ga, gb, 'integral_per_complete_Surface') if gpu_a and gpu_b else []
    return {'schema': SCHEMA, 'method': 'Organize existing official tool outputs and existing reducers; no runtime timing, sampling, or new hardware-counter reduction.',
            'arms': {'reference': a, 'candidate': b},
            'qualification': {'same_binary': same_binary, 'identities': ids, 'changed_switches': switches,
                              'same_device_record_metadata': same_device, 'device_metadata': devices,
                              'both_performance_baseline_eligible': baseline,
                              'same_scene_before': a['identity']['scene_before'] == b['identity']['scene_before'] if a['identity']['scene_before'].get('area') is not None and b['identity']['scene_before'].get('area') is not None else None,
                              'note': 'same binary is not a causal proof; inspect scene, device, output, warmup, frequency, sample loss and changed switches'},
            'comparison': {'frames': fps, 'CPU': cpu,
                           'owner_scheduler_states_ms_per_Surface': state_changes,
                           'owner_waker_categories_ms_per_Surface': waker_changes, 'exact_owner_counters_per_Surface': exact,
                           'all_disjoint_phase_duties_Minstructions_per_Surface': duties,
                           'top_new_sampled_costs': positives, 'top_self_changes_not_complete': selfs,
                           'state_counters_per_guest_submit_snapshot_window': state_rows,
                           'global_GPU_metric_means_check_alignment': gpu_rows,
                           'global_GPU_integrals_per_Surface_check_alignment': gpu_integrals},
            'net_conclusion': {'less_work_sampled': negatives, 'new_work_sampled': positives,
                               'migration': 'unproven: phase deltas alone do not establish work migration; inspect matching actual ancestor chains',
                               'unexplained_or_broad_buckets': unknown,
                               'limits': ['sampled phase allocation is not exact per-function PMU counting or CPU time',
                                          'unknown/missing ancestry and broad other buckets are not a root-cause explanation',
                                          'whole owner CPU includes guest/HLE/recording outside translation',
                                          'state snapshot counters use their own guest-submit window, not the Surface denominator',
                                          'one pair is not a thermal/frequency-controlled repeatability estimate',
                                          'GPU bound/idle conclusions require aligned original GPU data; missing GPU is not zero GPU cost']}}


def fmt(v):
    return 'missing' if v is None else f'{v:.6g}' if isinstance(v, (int, float)) else str(v)


def table(lines, title, rows, unit=''):
    lines.extend(['', f'## {title}', '', f'|项目{unit}|Reference|Candidate|Δ|Δ%|', '|---|---:|---:|---:|---:|'])
    for r in rows:
        key = ' / '.join(r['key']) if isinstance(r['key'], list) else r['key']
        key = key.replace('|', '\\|')
        lines.append('|'+ '|'.join([key, fmt(r.get('reference')), fmt(r.get('candidate')), fmt(r.get('delta')), fmt(r.get('percent'))])+'|')
    if not rows:
        lines.append('|missing|missing|missing|missing|missing|')


def markdown(report):
    q, c, arms = report['qualification'], report['comparison'], report['arms']
    lines = ['# 整轮 Profile 对照', '',
             f"{arms['reference']['label']} → {arms['candidate']['label']}；同binary：**{q['same_binary']}**；同窗口前场景：{q['same_scene_before']}；performance baseline资格：{q['both_performance_baseline_eligible']}；同设备metadata：{q['same_device_record_metadata']}。", '',
             'Δ = Candidate − Reference；missing不补0。完整身份、所有线程/计数与输入哈希见同目录comparison.json。']
    core = [{'key': k, **v} for k, v in c['frames'].items()]
    core += [{'key': k, **v} for k, v in c['CPU'].items()]
    table(lines, '核心净变化', core)
    table(lines, 'owner睡眠对应的后续Runnable waker来源', c['owner_waker_categories_ms_per_Surface'], ' (attributed S/D ms / Surface)')
    lines += ['', '|Exact owner用户态计数 / Surface|Reference区间|Candidate区间|Δ区间|Δ%区间|', '|---|---:|---:|---:|---:|']
    interval = lambda v: 'missing' if not v or any(x is None for x in v) else '[' + ', '.join(fmt(x) for x in v) + ']'
    for event, v in c['exact_owner_counters_per_Surface'].items():
        lines.append('|'+ '|'.join([event, interval(v['reference_interval']), interval(v['candidate_interval']), interval(v['delta_interval']), interval(v['percent_interval'])])+'|')
    if not c['exact_owner_counters_per_Surface']:
        lines.append('|missing|missing|missing|missing|missing|')
    for role, arm in arms.items():
        scheduler = arm['CPU']['scheduler_states_and_wakers']
        lines += ['', f"{role} scheduler：" + (scheduler.get('reason', scheduler['status']) if scheduler['value'] is None else '官方同trace / 同Surface窗口；Running对原analysis校验通过'),
                  f"{role} GPU：" + (arm['GPU'].get('reason', arm['GPU']['status']) if arm['GPU']['value'] is None else arm['GPU']['alignment'])]
    if c['global_GPU_metric_means_check_alignment']:
        table(lines, '原GPU均值（global，须核对各臂窗口资格）', c['global_GPU_metric_means_check_alignment'])
        table(lines, '原GPU积分 / Surface', c['global_GPU_integrals_per_Surface_check_alignment'])
        lines += ['', 'Clocks/s为clock-count rate，不证明瞬时MHz/锁频；percent可能重叠，不相加、不转独占stall ms。']
    lines += ['', '## 简明资格', '']
    for role, arm in arms.items():
        i = arm['identity']; scene = i['scene_before']
        lines.append(f"- {role}: `{arm['directory']}`；source `{i.get('source_revision')}`；area/x/y={scene.get('area')}/{scene.get('x')}/{scene.get('y')}；game={scene.get('game_output')}；surface={scene.get('surface_output')}；scale={scene.get('scale_3d')}。")
        if arm['issues']:
            lines.append('  资格问题：' + '; '.join(arm['issues']))
    lines += ['', '不同开关：' + ('; '.join(f"{k}: {v['reference']} → {v['candidate']}" for k, v in q['changed_switches'].items()) or 'none / 未提供'), '',
              'Exact区间来自原硬件counter reducer的采样边缘界，不是统计置信区间。owner包含guest/HLE及录制，不等于纯翻译。']
    table(lines, '互斥阶段/职责完整变化', c['all_disjoint_phase_duties_Minstructions_per_Surface'], ' (M instructions / Surface; sampled)')
    table(lines, '最大新增成本（前5）', c['top_new_sampled_costs'][:5], ' (M instructions / Surface; sampled)')
    table(lines, '最大减少（前5）', report['net_conclusion']['less_work_sampled'][:5], ' (M instructions / Surface; sampled)')
    self_rows = c['top_self_changes_not_complete']
    table(lines, '具体SELF新增/减少（前12；阶段/职责/函数）', [r for r in self_rows if r['delta'] is not None][:12], ' (sampled M instructions / Surface; 不与父预算相加)')
    unranked = [r for r in self_rows if r['delta'] is None]
    if unranked:
        table(lines, 'SELF缺对照（not_ranked，不推算Δ；前4）', unranked[:4], ' (sampled M instructions / Surface)')
    table(lines, 'owner互斥调度状态完整表', c['owner_scheduler_states_ms_per_Surface'], ' (ms / Surface)')
    lines += ['', '## 线程重点', '', '|Arm|TID|线程|owner|Running ms/Surface|运行期MHz|', '|---|---:|---|---|---:|---:|']
    for role, arm in arms.items():
        for t in arm['CPU']['threads_sorted_by_running'][:8]:
            lines.append(f"|{role}|{t['tid']}|{t.get('comm')}|{t['owner']}|{fmt(t['running_ms_per_Surface_interval'])}|{fmt(t['weighted_mhz_while_running'])}|")
    lines += ['', '## 资源计数重点（独立snapshot窗口）', '']
    for role, arm in arms.items():
        w = arm['state_counters']
        lines.append(f"- {role}: {fmt(w['duration_seconds'])}s，guest submits={fmt(w['guest_submit_delta'])}；括住Surface窗口={w['brackets_Surface_window']}；两边额外{fmt(w['leading_extra_ns'])}/{fmt(w['trailing_extra_ns'])}ns。")
    lines += ['', '**资源计数除数是同snapshot窗口的guest submits，绝不换成Surface帧数。** 未声明为单调counter的内存/对象数量等gauge不计算速率。']
    allrows = c['state_counters_per_guest_submit_snapshot_window']
    focus = [r for r in allrows if r['key'] in ('buffer_sync.guest_submits', 'buffer_sync.internal_submits', 'buffer_sync.readback_bytes', 'pool_uploads.full_bytes', 'pool_uploads.used_bytes', 'descriptor_allocation.calls', 'texture_bindings.prepares', 'image_buffer_split.builds')]
    additions = [r for r in allrows if r not in focus and r['delta'] is not None and r['delta'] != 0][:10]
    table(lines, '资源增减（全部原始值与missing项在JSON）', focus + additions, ' / guest submit')
    lines += ['', '## 净结论与未解释部分', '']
    f = c['frames']['FPS']; owner = c['CPU']['owner_running_ms_per_Surface_interval']
    lines.append(f"观测：FPS {fmt(f['reference'])} → {fmt(f['candidate'])}（{fmt(f['percent'])}%）；owner Running {fmt(owner['reference'])} → {fmt(owner['candidate'])}ms/Surface。")
    lines += ['', '少做与新增：上表报告整个互斥分区，不只目标函数。函数self的原top列表可能截断，保留在JSON且缺项不补0。', '',
              '机制：工具数据能证明计数/采样工作量变化；仅凭本对照不能将FPS平坦归因于某一个锁、GPU或相位。', '',
              '迁移：' + report['net_conclusion']['migration'], '',
              '未解释：' + ('; '.join(' / '.join(r['key']) for r in report['net_conclusion']['unexplained_or_broad_buckets']) or 'missing / 无完整分区'), '',
              '这些unknown/缺祖先/宽泛other变化不得凑成根因。缺GPU时不宣称GPU bound；即使有global GPU也不能归到进程独占耗时。', '']
    for limit in report['net_conclusion']['limits']:
        lines.append('- ' + limit)
    return '\n'.join(lines) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path); p.add_argument('candidate', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--reference-label'); p.add_argument('--candidate-label')
    p.add_argument('--trace-processor', type=Path, help='official trace_processor; defaults to repository .local/tools/perfetto/trace_processor when available')
    p.add_argument('--no-scheduler', action='store_true', help='explicitly disable optional official same-trace scheduler query')
    p.add_argument('--groups', type=Path, help='JSON groups regex list and optional explicit monotonic counter_paths')
    for role in ('reference', 'candidate'):
        p.add_argument('--' + role + '-metadata', type=Path, help='optional JSON with budget_path override')
        p.add_argument('--' + role + '-gpu', type=Path, help='existing official SDP metrics JSON; always global')
        p.add_argument('--' + role + '-gpu-arm', help='select arm from an existing GPU reducer comparison.json')
    args = p.parse_args(argv)
    config = json.loads(args.groups.read_text()) if args.groups else {}
    try:
        arms = []
        default_tool = Path(__file__).resolve().parents[2] / '.local/tools/perfetto/trace_processor'
        tool = args.trace_processor or (default_tool if default_tool.exists() else None)
        launcher_provenance = None
        if not args.no_scheduler and tool and Path(tool).resolve() == default_tool.resolve():
            # Reuse the official launcher's pinned prebuilt resolver, not a
            # second downloader/parser. Hash the actual executed binary too.
            official = runpy.run_path(str(default_tool))
            launcher_provenance = {'path': str(default_tool), 'sha256': sha(default_tool)}
            tool = Path(official['get_perfetto_prebuilt'](official['TRACE_PROCESSOR_SHELL_MANIFEST']))
        for role in ('reference', 'candidate'):
            arm = Arm(getattr(args, role), getattr(args, role + '_label') or getattr(args, role).name,
                      config, getattr(args, role + '_metadata'), getattr(args, role + '_gpu'), getattr(args, role + '_gpu_arm'))
            arm.scheduler = metric(None, reason='official scheduler query explicitly disabled') if args.no_scheduler else run_scheduler(arm, tool, args.output / 'scheduler' / role)
            if launcher_provenance and 'provenance' in arm.scheduler:
                arm.scheduler['provenance']['official_launcher'] = launcher_provenance
            arms.append(arm.result())
        report = compare(*arms)
    except (ValueError, KeyError, TypeError, re.error) as e:
        p.error(str(e))
    report['generator'] = {'script': str(Path(__file__).resolve()), 'sha256': sha(Path(__file__)),
                           'argv': list(argv) if argv is not None else sys.argv[1:],
                           'group_configuration': config,
                           'group_file': {'path': str(args.groups.resolve()), 'sha256': sha(args.groups)} if args.groups else None}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + '\n')
    (args.output / 'comparison.md').write_text(markdown(report))
    print(json.dumps({'output': str(args.output.resolve()), 'same_binary': report['qualification']['same_binary'],
                      'issues': {k: a['issues'] for k, a in report['arms'].items()}}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
