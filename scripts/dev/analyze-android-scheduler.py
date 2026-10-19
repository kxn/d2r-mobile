#!/usr/bin/env python3
"""Summarize captured native queue/dependency annotations and vendor GPU limits.

Reads an existing trace only. Queue-to-observed-fence spans are not GPU execution
times; this report does not infer game FPS from profiler event counts.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import statistics
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--processor',type=Path,default=Path('.local/tools/perfetto/trace_processor'))
    args=parser.parse_args()
    if args.report.exists():parser.error('use a new report path')
    metadata=json.loads(args.trace.with_suffix('.json').read_text())
    digest=hashlib.sha256(args.trace.read_bytes()).hexdigest()
    if metadata['trace_sha256']!=digest:raise SystemExit('trace does not match capture metadata')
    pid=int(metadata['pid'])

    def query(sql):
        result=subprocess.run([str(args.processor),'query',str(args.trace),sql],
                              text=True,capture_output=True,check=True)
        return list(csv.DictReader(io.StringIO(result.stdout)))

    # Native names are fixed literals in nvn_vk_system_trace.h callers. Keep
    # complete slices only; report the original capture's boundary limitations.
    names=['NVN command replay','NVN submission planning','NVN attachment store index',
           'Vulkan queue submit','Vulkan CPU publish','Vulkan submit to observed fence']
    conditions=','.join("'"+name+"'" for name in names)
    rows=query(f'SELECT s.ts,s.dur,s.name FROM slice s '
               'LEFT JOIN thread_track tt ON tt.id=s.track_id LEFT JOIN thread t ON t.utid=tt.utid '
               'LEFT JOIN process_track pt ON pt.id=s.track_id '
               f'WHERE s.name IN ({conditions}) AND s.dur>0 AND '
               f'coalesce(t.upid,pt.upid) IN (SELECT upid FROM process WHERE pid={pid}) ORDER BY s.ts')
    groups={name:[] for name in names}
    spans={name:[] for name in names}
    for row in rows:
        start,duration=int(row['ts']),int(row['dur'])
        groups[row['name']].append(duration/1e6)
        spans[row['name']].append((start,start+duration))
    summary={name:{'count':len(values),'mean_ms':statistics.mean(values),
                   'median_ms':statistics.median(values),'max_ms':max(values)}
             for name,values in groups.items() if values}
    replay=spans['NVN command replay'];submits=spans['Vulkan queue submit']
    first_offsets=[];replay_tail=[];overlap=[]
    for i,(start,end) in enumerate(replay):
        limit=replay[i+1][0] if i+1<len(replay) else end
        first=next((s for s,e in submits if start<=s<limit),None)
        if first is None:continue
        first_offsets.append((first-start)/1e6)
        replay_tail.append(max(0,end-first)/1e6)
        merged=[]
        for s,e in spans['Vulkan submit to observed fence']:
            lo,hi=max(start,s),min(end,e)
            if lo>=hi:continue
            if merged and lo<=merged[-1][1]:merged[-1][1]=max(merged[-1][1],hi)
            else:merged.append([lo,hi])
        overlap.append(sum(e-s for s,e in merged)/1e6)

    end_ns=int(query('SELECT end_ts FROM trace_bounds')[0]['end_ts'])
    counters=query("SELECT ct.id,ct.name,c.ts,c.value FROM counter c JOIN counter_track ct ON ct.id=c.track_id "
                   "WHERE ct.name IN ('gpu_freq','gpu_freq_max','gpufreq','gpu_loading','target_fps','tmp_q_fps') "
                   "ORDER BY ct.id,c.ts")
    tracks={}
    for row in counters:tracks.setdefault(int(row['id']),[]).append(row)
    counter_summary=[]
    for track,values in tracks.items():
        durations={}
        for i,row in enumerate(values):
            stop=int(values[i+1]['ts']) if i+1<len(values) else end_ns
            value=float(row['value']);durations[value]=durations.get(value,0)+max(0,stop-int(row['ts']))
        total=sum(durations.values())
        name=values[0]['name']
        units={'gpufreq':'kHz','gpu_freq':'GED MHz (may be batch averaged)',
               'gpu_freq_max':'MHz','gpu_loading':'percent',
               'target_fps':'vendor requested FPS','tmp_q_fps':'vendor FPS estimate, not measured presents'}
        counter_summary.append({'track_id':track,'name':name,'unit':units[name],'sample_count':len(values),
            'min':min(durations),'max':max(durations),'covered_ms':total/1e6,
            'time_weighted_mean':sum(v*d for v,d in durations.items())/total if total else None,
            'residency_ms':{str(v):d/1e6 for v,d in sorted(durations.items())}})
    stats=query("SELECT name,idx,value FROM stats WHERE (name GLOB '*overrun*' OR name GLOB '*lost*' "
                "OR name GLOB '*parse_failure*') AND value!=0")
    raw_prefixes=query("SELECT substr(extract_arg(arg_set_id,'buf'),1,2) AS prefix,count(*) AS count "
                       "FROM ftrace_event WHERE name='print' GROUP BY prefix ORDER BY count DESC")
    result={'pid':pid,'apk_sha256':metadata['apk_sha256'],'shader_chain':metadata['shader_chain'],
            'trace_sha256':digest,'analysis_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'native_slices':summary,'matched_replay_count':len(first_offsets),
            'first_submit_offset_mean_ms':statistics.mean(first_offsets) if first_offsets else None,
            'replay_after_first_submit_mean_ms':statistics.mean(replay_tail) if replay_tail else None,
            'replay_with_any_observed_pending_queue_mean_ms':statistics.mean(overlap) if overlap else None,
            'vendor_counters':counter_summary,'nonzero_trace_health_stats':stats,'raw_marker_prefixes':raw_prefixes,
            'scope':'CPU wall spans and observed pending queue intervals, not exclusive GPU busy time or game FPS. '
                    'Counters cover first observed value to trace end; no value is inferred before the first sample. '
                    'Inspect parser failures, raw prefixes and capture mode before using results.',
            'ged_gpu_freq_max_reference':'https://github.com/OnePlusOSS/android_kernel_5.10_oneplus_mt6983/blob/'
                    'oneplus/mt6983_t_13.0_oneplus_ace_2v/drivers/gpu/mediatek/ged/src/ged_dvfs.c',
            'ged_interpretation':'Reference vendor source emits current effective GPU ceiling in MHz as gpu_freq_max '
                    'with hardcoded synthetic PID 5566. It does not identify the limiting policy. '
                    'This source family is not asserted to be the exact installed firmware.'}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:result[key] for key in ['pid','native_slices','first_submit_offset_mean_ms',
          'replay_after_first_submit_mean_ms','nonzero_trace_health_stats']},indent=2))


if __name__=='__main__':main()
