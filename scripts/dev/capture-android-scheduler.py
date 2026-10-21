#!/usr/bin/env python3
"""Record scheduler/frequency events for the current original Android game.

CPU/system tracing complements vendor GPU counters; it is not a GPU render-stage
capture. Unsupported frequency events must not be inferred from other counters.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import uuid

PACKAGE='com.d2rmobile.nativeport'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--seconds',type=int,default=10)
    parser.add_argument('--buffer-mb',type=int,choices=(32,64,128),default=32,
                        help='official Perfetto ring buffer capacity; increase for current-capture overwrite/loss, not service-lifetime discarded-chunk totals')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--app-markers',action='store_true',help='enable native replay/queue/observed-fence CPU trace annotations')
    parser.add_argument('--clean-atrace-print',action='store_true',
                        help='official print_filter: retain all B/E and async phases, app counters only; discard vendor free-text and wrong-TGID counters')
    parser.add_argument('--frame-timeline',action='store_true',help='request official SurfaceFlinger frame/timeline sources; verify packet counts before interpreting')
    parser.add_argument('--thermal-events',action='store_true',help='request the Android thermal tracing category; verify which events the device actually emits')
    args=parser.parse_args()
    sidecars=[args.output,args.output.with_suffix('.json'),args.output.with_suffix('.pbtxt'),args.output.with_suffix('.capture.log')]
    if not 1<=args.seconds<=30 or args.output.suffix!='.pftrace' or any(p.exists() for p in sidecars):
        parser.error('use a new .pftrace output and duration from 1 to 30 seconds')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    base=['adb','-s',args.serial]

    def adb(*parts):
        return subprocess.check_output([*base,*parts],text=True,timeout=20)

    def state():
        return json.loads(adb('exec-out','run-as',PACKAGE,'tail','-n','1','files/runtime/game-state.jsonl'))

    pid=adb('shell','pidof',PACKAGE).strip()
    if not pid.isdecimal():raise SystemExit('one running original game process is required')
    native_log=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log')
    if f'Android native start pid={pid}\n' not in native_log or 'external profiling: custom CPU/GPU timers disabled' not in native_log:
        raise SystemExit('use the original game in --external-profile mode')
    apk=adb('shell','pm','path',PACKAGE).strip().removeprefix('package:')
    if not apk.startswith('/') or '\n' in apk:raise SystemExit('one APK path is required')
    apk_sha=adb('shell',shlex.join(['sha256sum',apk])).split()[0]
    chain=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/shaders/.chain').strip()
    # APP markers do not require the global gfx category. Enabling gfx also
    # activates driver/framework instrumentation and changes the measured load.
    app_markers=('  atrace_apps: "'+PACKAGE+'"\n') if args.app_markers else ''
    if args.thermal_events:app_markers+='  atrace_categories: "thermal"\n'
    print_filter=''
    if args.clean_atrace_print:
        # Preserve both sides of every nesting/async pair, including bare E.
        # Some vendor C records put a thread ID in the process-ID field, which
        # makes Trace Processor reparent real app threads to false processes.
        prefixes=['B|','E','S|','F|','I|','N|','G|','H|','C|'+pid+'|']
        print_filter='  print_filter {\n'+''.join(
            '    rules { prefix: '+json.dumps(prefix)+' allow: true }\n' for prefix in prefixes)+\
            '    rules { prefix: "" allow: false }\n  }\n'
    config='''buffers { size_kb: %d fill_policy: RING_BUFFER }
data_sources { config { name: "linux.ftrace" ftrace_config {
  ftrace_events: "sched/sched_switch"
  ftrace_events: "sched/sched_waking"
  ftrace_events: "power/cpu_frequency"
  ftrace_events: "power/cpu_frequency_limits"
  ftrace_events: "power/cpu_idle"
  ftrace_events: "power/gpu_frequency"
%s%s  compact_sched { enabled: true }
} } }
data_sources { config { name: "linux.process_stats" process_stats_config {
  scan_all_processes_on_start: true
} } }
duration_ms: %d
'''%(args.buffer_mb*1024,app_markers,print_filter,args.seconds*1000)
    if args.frame_timeline:
        config+='data_sources { config { name: "android.surfaceflinger.frame" } }\n'
        config+='data_sources { config { name: "android.surfaceflinger.frametimeline" } }\n'
    args.output.with_suffix('.pbtxt').write_text(config)
    # Temperature is displayed through the app's public Android API only.
    # Do not add shell temperature reports to the measured gameplay path.
    before=state();thermal_before='not collected; app public API only'
    remote='/data/misc/perfetto-traces/d2r-scheduler-'+uuid.uuid4().hex+'.pftrace'
    with args.output.with_suffix('.capture.log').open('w') as log:
        subprocess.run([*base,'shell','perfetto','--txt','-c','-','-o',remote],input=config,text=True,
                       stdout=log,stderr=subprocess.STDOUT,timeout=args.seconds+30,check=True)
    after=state();thermal_after='not collected; app public API only'
    if adb('shell','pidof',PACKAGE).strip()!=pid or adb('exec-out','run-as',PACKAGE,'cat','files/runtime/shaders/.chain').strip()!=chain:
        raise SystemExit('game process or shader chain changed; no capture accepted')
    subprocess.run([*base,'pull',remote,str(args.output)],check=True,stdout=subprocess.DEVNULL)
    if args.output.stat().st_size<1024:raise SystemExit('empty scheduler trace')
    adb('shell','rm',remote)
    report={'serial':args.serial,'pid':int(pid),'seconds_requested':args.seconds,'apk_sha256':apk_sha,
            'buffer_mb':args.buffer_mb,
            'clean_atrace_print_requested':args.clean_atrace_print,
            'app_markers_requested':args.app_markers,
            'thermal_events_requested':args.thermal_events,'frame_timeline_requested':args.frame_timeline,
            'shader_chain':chain,'before':before,'after':after,'thermal_before':thermal_before,
            'thermal_after':thermal_after,'trace_sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),
            'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'scope':'CPU scheduler/frequency system trace, optional native replay/submit/publish annotations. Submit-to-observed-fence includes queueing/driver/GPU/CPU wake latency, not pure GPU execution. Verify event counts; GPU frequency may be unavailable.'}
    args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k in ('pid','trace_sha256','scope')}))


if __name__=='__main__':main()
