#!/usr/bin/env python3
"""用 Android Perfetto 记录当前游戏线程的执行/等待，不重启或注入游戏。
Record running/waiting threads with Android Perfetto, without restart or injection.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = 'com.d2rmobile.nativeport'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
    p.add_argument('--seconds', type=int, default=8)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    output = args.output.resolve()
    if not 5 <= args.seconds <= 30 or output.exists() or output.suffix != '.pftrace':
        p.error('use 5..30 seconds and a new .pftrace output')
    tp = ROOT / '.local/tools/perfetto/trace_processor'
    if not tp.is_file():
        raise SystemExit('install the official https://get.perfetto.dev/trace_processor first')
    output.parent.mkdir(parents=True, exist_ok=True)
    base = ['adb', '-s', args.serial]

    def adb(*parts):
        return subprocess.check_output([*base, *parts], text=True, timeout=20).strip()

    def state():
        return json.loads(adb('exec-out', 'run-as', PACKAGE, 'tail', '-n', '1',
                              'files/runtime/game-state.jsonl'))

    def threads(pid, leaf):
        raw = adb('shell', 'ps', '-T', '-p', pid)
        output.with_suffix('.' + leaf + '.txt').write_text(raw + '\n')
        lines = raw.splitlines()
        tid_column = lines[0].split().index('TID')
        return {int(line.split()[tid_column]) for line in lines[1:] if line.strip()}

    pid = adb('shell', 'pidof', PACKAGE)
    if not pid.isdecimal():
        raise SystemExit('one running native game is required')
    log = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/runtime/native.log')
    if (f'Android native start pid={pid}\n' not in log or
            'external profiling: custom CPU/GPU timers disabled' not in log):
        raise SystemExit('start the current game with --external-profile first')
    before = state()
    tids_before = threads(pid, 'threads-before')
    if not before['world_input_ready']:
        raise SystemExit('original world/input observations are not ready')
    config = f'''
buffers {{ size_kb: 32768 fill_policy: RING_BUFFER }}
duration_ms: {args.seconds * 1000}
data_sources {{ config {{ name: "linux.ftrace" ftrace_config {{
  ftrace_events: "sched/sched_switch"
  ftrace_events: "sched/sched_waking"
  ftrace_events: "sched/sched_process_exit"
  ftrace_events: "power/cpu_frequency"
  ftrace_events: "power/cpu_idle"
  ftrace_events: "dma_fence/dma_fence_wait_start"
  ftrace_events: "dma_fence/dma_fence_wait_end"
  ftrace_events: "dma_fence/dma_fence_signaled"
  atrace_categories: "gfx"
  atrace_categories: "view"
  atrace_apps: "{PACKAGE}"
  compact_sched {{ enabled: true }}
}} }} }}
data_sources {{ config {{ name: "linux.process_stats" process_stats_config {{
  scan_all_processes_on_start: true
}} }} }}
data_sources {{ config {{ name: "android.surfaceflinger.frametimeline" }} }}
data_sources {{ config {{ name: "gpu.renderstages" }} }}
'''
    output.with_suffix('.pbtxt').write_text(config)
    remote = '/data/misc/perfetto-traces/d2r-' + uuid.uuid4().hex + '.pftrace'
    with output.with_suffix('.capture.log').open('w') as log_file:
        subprocess.run([*base, 'shell', 'perfetto', '--txt', '-c', '-', '-o', remote],
                       input=config, text=True, stdout=log_file, stderr=subprocess.STDOUT,
                       check=True, timeout=args.seconds + 30)
    subprocess.run([*base, 'pull', remote, str(output)], check=True, timeout=45,
                   stdout=subprocess.DEVNULL)
    if output.stat().st_size < 1024:
        raise SystemExit('empty trace; remote artifact retained')
    adb('shell', 'rm', remote)
    after = state()
    tids_after = threads(pid, 'threads-after')
    if adb('shell', 'pidof', PACKAGE) != pid:
        raise SystemExit('process changed; trace retained but comparison rejected')
    for key in ['area', 'x', 'y', 'hp', 'scale_3d', 'game_output', 'surface_output']:
        if before.get(key) != after.get(key):
            raise SystemExit(f'{key} changed; trace retained but stable-scene comparison rejected')
    if not after['world_input_ready'] or after['time_ns'] <= before['time_ns']:
        raise SystemExit('fresh world observations stopped; trace retained')

    def query(name, sql):
        result = subprocess.run([str(tp), 'query', str(output), sql], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                check=True, timeout=60)
        output.with_suffix('.' + name + '.csv').write_text(result.stdout)
        output.with_suffix('.' + name + '.sql').write_text(sql + '\n')
        return list(csv.DictReader(io.StringIO(result.stdout)))

    # 跟踪中的 atrace 元数据可能把线程误标成进程；按实际线程归属筛选，避免漏计。
    # Atrace metadata can misidentify threads as processes. Use actual task ownership.
    tids = ','.join(str(tid) for tid in sorted(tids_before & tids_after))
    if not tids:
        raise SystemExit('no stable Linux task IDs')
    states = query('thread-states', f'''
SELECT t.tid,t.name,s.state,s.io_wait,s.blocked_function,
       ROUND(SUM(s.dur)/1e6,3) AS duration_ms,COUNT(*) AS intervals
FROM thread_state s JOIN thread t USING(utid)
WHERE t.tid IN ({tids}) AND s.dur>0
GROUP BY t.tid,t.name,s.state,s.io_wait,s.blocked_function ORDER BY duration_ms DESC''')
    if not states:
        raise SystemExit('no game scheduler events; cannot diagnose CPU running/waiting')
    gpu = query('gpu-tracks', '''
SELECT t.name,COUNT(*) AS events,ROUND(SUM(s.dur)/1e6,3) AS duration_ms
FROM gpu_slice s JOIN track t ON s.track_id=t.id GROUP BY t.id''')
    warnings = query('trace-errors', '''
SELECT name,severity,value FROM stats WHERE value>0 AND severity IN ('error','data_loss')''')
    bounds = query('bounds', 'SELECT start_ts,end_ts,(end_ts-start_ts)/1e6 AS duration_ms FROM trace_bounds')
    associations = query('thread-associations', f'''
SELECT t.tid,t.utid,t.name,p.pid AS trace_tgid FROM thread t LEFT JOIN process p USING(upid)
WHERE t.tid IN ({tids}) ORDER BY t.tid,t.utid''')
    wakeups = query('wakeups', f'''
SELECT t.tid AS target_tid,w.tid AS waker_tid,w.name AS waker_name,COUNT(*) AS wakeups
FROM thread_state s JOIN thread t USING(utid) JOIN thread w ON s.waker_utid=w.utid
WHERE t.tid IN ({tids}) AND s.state='R'
GROUP BY t.tid,w.tid,w.name ORDER BY wakeups DESC''')
    report = {'tool': 'Android Perfetto + official trace_processor', 'pid': int(pid),
              'serial': args.serial, 'requested_seconds': args.seconds,
              'before': before, 'after': after, 'thread_states': states,
              'gpu_tracks': gpu, 'trace_warnings': warnings, 'bounds': bounds,
              'stable_linux_tids': sorted(tids_before & tids_after),
              'thread_associations': associations,
              'wakeups': wakeups,
              'trace_processor_version': subprocess.check_output([str(tp), '--version'], text=True, timeout=15).strip(),
              'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
              'limitations': 'Scheduler states are kernel events, not CPU stack samples. Sleeping does not '
                             'identify GPU waits by itself. Missing GPU tracks do not mean GPU idle. '
                             'Capture overhead and resolution comparisons need separate measurement.'}
    output.with_suffix('.capture.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'trace': str(output), 'pid': int(pid), 'thread_state_groups': len(states),
                      'gpu_track_count': len(gpu), 'trace_warning_count': len(warnings)}))


if __name__ == '__main__':
    main()
