#!/usr/bin/env python3
"""同进程 ABBA 对照渲染开关，记录完整实际上屏与 HWUI 重画次数。
Compare rendering controls with same-process ABBA, actual presents and HWUI counts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import time

PACKAGE = 'com.d2rmobile.nativeport'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--warmup', type=float, default=15, help='seconds before preflight; keep startup compilation outside measured windows')
    parser.add_argument('--cpu-stats',action='store_true',help='read process/thread CPU ticks at window boundaries')
    parser.add_argument('--mali-ged-probe',action='store_true',help='read the audited MT6983 helper at boundaries; reject any effective GPU ceiling change across the run')
    parser.add_argument('--control', choices=['none','overlay_continuous','pipeline_byte_hash','pipeline_prepare_reference','query_per_draw_reference','graphics_stage_reference','cover_wait_reference','readback_coherent_reference','storage_bias_reference','pool_sync_reference','performance_hint_reference','image_layout_reference','stream_submit_reference','store_descriptor_reference','cpu_copy_reference','driver_constant_reference'],
                        default='overlay_continuous', help='true selects the previous implementation')
    parser.add_argument('--scale', type=float, default=.60, help='required observed 3D scale; none control only measures presents')
    args = parser.parse_args()
    script_sha=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    if args.report.exists() or not 1 <= args.rounds <= 5 or not .5 <= args.scale <= 1 or not 0 <= args.warmup <= 60:
        parser.error('use a new report, 1..5 rounds and scale from 0.5 to 1.0')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    time.sleep(args.warmup)

    def adb(*parts):
        return subprocess.check_output(['adb', '-s', args.serial, *parts], text=True, timeout=20)

    pid = adb('shell', 'pidof', PACKAGE).strip()
    if not pid:
        raise SystemExit('game is not running')
    log=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log')
    if (f'Android native start pid={pid}\n' not in log or
            'external profiling: custom CPU/GPU timers disabled' not in log):
        raise SystemExit('use a fresh --external-profile game process for performance comparisons')
    apk=adb('shell','pm','path',PACKAGE).strip().removeprefix('package:')
    apk_sha=adb('shell',shlex.join(['sha256sum',apk])).split()[0]
    shader_chain=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/shaders/.chain').strip()
    ged_helper_sha=None;ged_ceiling=None;ged_observations=[]
    if args.mali_ged_probe:
        local_helper=Path('.local/build/mali-ged-query/mali-ged-query')
        source=Path('scripts/dev/probes/mali-ged-query.c')
        if not local_helper.exists() or local_helper.with_name('source.sha256').read_text().strip()!=hashlib.sha256(source.read_bytes()).hexdigest():
            raise SystemExit('deploy the current query-mali-dvfs.py helper first')
        ged_helper_sha=hashlib.sha256(local_helper.read_bytes()).hexdigest()
        if adb('exec-out','run-as',PACKAGE,'sha256sum','files/runtime/mali-ged-query').split()[0]!=ged_helper_sha:
            raise SystemExit('deployed Mali query helper hash mismatch')

    def ged_snapshot():
        nonlocal ged_ceiling
        if not args.mali_ged_probe:return None
        values=json.loads(adb('exec-out','run-as',PACKAGE,'files/runtime/mali-ged-query'))
        ged_observations.append({'host_time_ns':time.monotonic_ns(),'values':values})
        dvfs=values.get('dvfs_query',{});ceiling=dvfs.get('effective_ceiling_khz',0)
        if dvfs.get('error')!=0 or not 0<ceiling<=values.get('hardware_max_frequency_khz',0)<2000000:
            raise SystemExit('invalid vendor GPU ceiling query')
        if ged_ceiling is None:ged_ceiling=ceiling
        if ceiling!=ged_ceiling:raise SystemExit('effective GPU ceiling changed; entire comparison rejected')
        return values
    compiled_opt=re.search(r'Android native host optimization=-O([123])\b',log)
    clock_ticks=int(adb('shell','getconf','CLK_TCK')) if args.cpu_stats else None
    translation_tid=None
    completion_tid=None

    def cpu_stats():
        if not args.cpu_stats:
            return None
        # 只在窗口边界读内核累计时间，不做逐帧采样；已退出的短命线程允许缺席。
        # Read kernel cumulative ticks only at boundaries, not per-frame samples.
        # Short-lived threads may disappear while reading the task directory.
        tracked=([translation_tid]+([completion_tid] if completion_tid else [])) if translation_tid else []
        script=(f'cat /proc/{pid}/stat '+ ' '.join(f'/proc/{pid}/task/{tid}/stat' for tid in tracked) if tracked else
                f'cat /proc/{pid}/stat; for taskfile in /proc/{pid}/task/*/stat; '
                'do cat "$taskfile" 2>/dev/null; done; true')
        raw=adb('shell',shlex.join(['run-as',PACKAGE,'sh','-c',script]))
        records=[]
        for line in raw.splitlines():
            start=line.index('(');end=line.rindex(')');fields=line[end+2:].split()
            records.append(dict(tid=int(line[:start]),name=line[start+1:end],
                                user_ticks=int(fields[11]),system_ticks=int(fields[12])))
        return dict(host_time_s=time.monotonic(),process=records[0],threads=records[1:])

    if args.cpu_stats:
        threads=cpu_stats()['threads']
        candidates=[t for t in threads if t['name']=='nvn-translate']
        if not candidates:
            raise SystemExit('translation thread missing')
        # 驱动辅助线程可继承同名；选择累计 CPU 时间最大的，保留准确 TID。
        # Driver helpers can inherit this name. Select the busiest by cumulative
        # CPU time and preserve its exact TID for subsequent boundary reads.
        translation_tid=max(candidates,key=lambda t:t['user_ticks']+t['system_ticks'])['tid']
        completion=[t for t in threads if t['name']=='dm-gpu-complete']
        if len(completion)==1:completion_tid=completion[0]['tid']
    layers = [s for s in adb('shell', 'dumpsys', 'SurfaceFlinger', '--list').splitlines()
              if PACKAGE in s and 'SurfaceView[' in s and '(BLAST)' in s]
    if len(layers) != 1:
        raise SystemExit('expected one game layer')
    layer = layers[0]
    if layer.startswith('RequestedLayerState{'):
        layer = layer.split('{', 1)[1].split(' parentId=', 1)[0]

    def presents():
        raw = adb('shell', shlex.join(['dumpsys', 'SurfaceFlinger', '--latency', layer]))
        values = []
        for line in raw.splitlines():
            words = line.split()
            if len(words) == 3 and all(s.isdecimal() for s in words):
                t = int(words[1])
                if 0 < t < 2**63 - 1:
                    values.append(t)
        return sorted(set(values)), raw

    def state():
        value = json.loads(adb('exec-out', 'run-as', PACKAGE, 'tail', '-n', '1',
                               'files/runtime/game-state.jsonl'))
        if not value.get('world_input_ready') or abs(value['scale_3d'] - args.scale) > .001:
            raise SystemExit('stable world at the requested 3D scale required')
        result={k: value[k] for k in ['time_ns', 'area', 'x', 'y', 'hp', 'scale_3d']}
        if 'pipeline_prepare' in value:
            result['pipeline_prepare']=value['pipeline_prepare']
        if 'occlusion_queries' in value:
            result['occlusion_queries']=value['occlusion_queries']
        if 'sample_liveness' in value:result['sample_liveness']=value['sample_liveness']
        if 'readback_memory' in value:result['readback_memory']=value['readback_memory']
        if 'buffer_sync' in value:result['buffer_sync']=value['buffer_sync']
        if 'stage_barriers' in value:result['stage_barriers']=value['stage_barriers']
        if 'storage_alignment' in value:result['storage_alignment']=value['storage_alignment']
        if 'pool_uploads' in value:result['pool_uploads']=value['pool_uploads']
        if 'performance_hint' in value:result['performance_hint']=value['performance_hint']
        if 'image_layouts' in value:result['image_layouts']=value['image_layouts']
        if 'stream_submits' in value:result['stream_submits']=value['stream_submits']
        if 'store_descriptors' in value:result['store_descriptors']=value['store_descriptors']
        if 'cpu_known_copies' in value:result['cpu_known_copies']=value['cpu_known_copies']
        if 'driver_constants' in value:result['driver_constants']=value['driver_constants']
        for key in ['game_output','surface_output']:
            if key in value:result[key]=value[key]
        return result

    def hwui():
        raw = adb('shell', 'dumpsys', 'gfxinfo', PACKAGE)
        match = re.search(r'Total frames rendered:\s*(\d+)', raw)
        if not match:
            raise SystemExit('missing HWUI frame count')
        return int(match[1]), raw

    results = []
    initial_scene=state()
    try:
        if args.control=='pipeline_byte_hash':
            # 哈希对照始终查表，单独排除直接复用管线的收益。
            # Isolate hashing by excluding the direct preparation reuse path.
            adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity',
                '--ez', 'pipeline_prepare_reference', 'true')
        sequence = [None] * (4 * args.rounds) if args.control=='none' else [True, False, False, True] * args.rounds
        for i, legacy in enumerate(sequence):
            if args.control!='none':
                adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity',
                    '--ez', args.control, str(legacy).lower())
            time.sleep(2)
            before = state()
            if args.control in ('cpu_copy_reference','driver_constant_reference'):
                # Confirm the observed recording policy before timing a window.
                deadline=time.monotonic()+12
                field='cpu_known_copies' if args.control=='cpu_copy_reference' else 'driver_constants'
                while before.get(field,{}).get('reference')!=legacy:
                    if time.monotonic()>deadline:
                        raise SystemExit('requested recording policy did not reach a submission boundary')
                    time.sleep(.2);before=state()
            for key in ['area','x','y','hp','scale_3d','game_output','surface_output']:
                if before.get(key)!=initial_scene.get(key):
                    raise SystemExit('scene/output changed between comparison windows')
            if args.control=='query_per_draw_reference' and before.get('occlusion_queries',{}).get('span')!= (not legacy):
                raise SystemExit('query mode did not reach a submission boundary')
            if args.control=='graphics_stage_reference' and before.get('stage_barriers',{}).get('reference')!=legacy:
                raise SystemExit('graphics stage mode did not apply')
            if args.control=='cover_wait_reference' and before.get('buffer_sync',{}).get('cover_reference')!=legacy:
                raise SystemExit('cover read mode did not apply')
            if args.control=='readback_coherent_reference' and before.get('readback_memory',{}).get('coherent_reference')!=legacy:
                raise SystemExit('readback memory mode did not reach submission boundary')
            if args.control=='storage_bias_reference' and before.get('storage_alignment',{}).get('reference')!=legacy:
                raise SystemExit('storage bias mode did not reach submission boundary')
            if args.control=='pool_sync_reference' and before.get('pool_uploads',{}).get('reference')!=legacy:
                raise SystemExit('pool upload mode did not reach submission boundary')
            if args.control=='performance_hint_reference':
                hint=before.get('performance_hint',{})
                if hint.get('reference')!=legacy or (legacy and hint.get('active',0)) or (not legacy and (hint.get('available')!=1 or hint.get('active',0)<1 or hint.get('failures',0))):
                    raise SystemExit('performance hint session did not apply or is unavailable')
            if args.control=='image_layout_reference' and before.get('image_layouts',{}).get('reference')!=legacy:
                raise SystemExit('image layout policy did not reach a submission boundary')
            if args.control=='stream_submit_reference' and before.get('stream_submits',{}).get('reference')!=legacy:
                raise SystemExit('stream submission policy did not reach an NVN submission boundary')
            if args.control=='store_descriptor_reference' and before.get('store_descriptors',{}).get('reference')!=legacy:
                raise SystemExit('store descriptor lookup policy did not reach an NVN submission boundary')
            thermal = adb('shell', 'dumpsys', 'thermalservice')
            ged_before=ged_snapshot()
            count0, gfx0 = hwui()
            cpu0=cpu_stats()
            old, raw0 = presents()
            if len(old) < 10:
                raise SystemExit('insufficient initial history')
            # 保留约 30% 环形记录余量；若期间大幅提速，溢出必须拒绝。
            # Reserve about 30% of the history ring; reject overflow on speed jumps.
            median_s = statistics.median([b-a for a,b in zip(old[-10:], old[-9:])]) / 1e9
            duration = min(7, 85 * median_s)
            started = time.monotonic()
            time.sleep(duration)
            history, raw1 = presents()
            elapsed = time.monotonic() - started
            cpu1=cpu_stats()
            count1, gfx1 = hwui()
            after = state()
            if adb('shell', 'pidof', PACKAGE).strip() != pid:
                raise SystemExit('process changed')
            if adb('exec-out','run-as',PACKAGE,'cat','files/runtime/shaders/.chain').strip()!=shader_chain:
                raise SystemExit('shader cache changed during measurement')
            if not history or history[0] > old[-1]:
                raise SystemExit('actual-present ring overflow; no result accepted')
            times = [old[-1], *[v for v in history if v > old[-1]]]
            intervals = [(b-a)/1e6 for a,b in zip(times, times[1:])]
            if len(intervals) < 20 or count1 < count0:
                raise SystemExit('too few presents or HWUI counter reset')
            for k in ['area', 'x', 'y', 'hp', 'scale_3d']:
                if before[k] != after[k]:
                    raise SystemExit('scene/scale changed')
            for k in ['game_output','surface_output']:
                if before.get(k)!=after.get(k):raise SystemExit('output resolution changed')
            if before.get('pipeline_prepare',{}).get('pipelines')!=after.get('pipeline_prepare',{}).get('pipelines'):
                raise SystemExit('pipeline compilation occurred during measurement')
            if args.control=='stream_submit_reference' and not legacy and after['stream_submits']['splits']<=before['stream_submits']['splits']:
                raise SystemExit('no actual Vulkan submission splits in the candidate window')
            if args.control=='store_descriptor_reference' and not legacy and after['store_descriptors']['hits']<=before['store_descriptors']['hits']:
                raise SystemExit('no descriptor cache hits in the candidate window')
            if args.control=='cpu_copy_reference' and not legacy and after['cpu_known_copies']['published']<=before['cpu_known_copies']['published']:
                raise SystemExit('no known CPU producer bytes in candidate window')
            if args.control=='driver_constant_reference' and not legacy and after['driver_constants']['last_hits']<=before['driver_constants']['last_hits']:
                raise SystemExit('no driver constant fast-path hits in candidate window')
            value = dict(index=i, legacy=legacy, frames=len(intervals),
                         fps=1000/statistics.mean(intervals), mean_ms=statistics.mean(intervals),
                         p95_ms=statistics.quantiles(intervals,n=100,method='inclusive')[94],
                         elapsed_s=elapsed, hwui_frames=count1-count0,
                         actual_present_ns=times, intervals_ms=intervals,
                         state_before=before, state_after=after, thermal_before=thermal,
                         thermal_after=adb('shell', 'dumpsys', 'thermalservice'))
            if args.mali_ged_probe:
                value['ged_before']=ged_before;value['ged_after']=ged_snapshot()
            if cpu0 is not None:
                def ticks(record): return record['user_ticks']+record['system_ticks']
                elapsed_cpu=cpu1['host_time_s']-cpu0['host_time_s']
                translation0=[t for t in cpu0['threads'] if t['name']=='nvn-translate']
                translation1=[t for t in cpu1['threads'] if t['name']=='nvn-translate']
                if len(translation0)!=1 or len(translation1)!=1 or translation0[0]['tid']!=translation1[0]['tid']:
                    raise SystemExit('translation thread changed or missing')
                value['cpu']=dict(before=cpu0,after=cpu1,clock_ticks_per_second=clock_ticks,
                    translation_tid=translation_tid,selection='busiest cumulative CPU among nvn-translate names at preflight',
                    elapsed_s=elapsed_cpu,process_core_seconds_per_second=(ticks(cpu1['process'])-ticks(cpu0['process']))/clock_ticks/elapsed_cpu,
                    translation_core_seconds_per_second=(ticks(translation1[0])-ticks(translation0[0]))/clock_ticks/elapsed_cpu)
                if completion_tid:
                    completed0=[t for t in cpu0['threads'] if t['tid']==completion_tid]
                    completed1=[t for t in cpu1['threads'] if t['tid']==completion_tid]
                    if len(completed0)!=1 or len(completed1)!=1:
                        raise SystemExit('GPU completion thread changed or missing')
                    value['cpu']['completion_tid']=completion_tid
                    value['cpu']['completion_core_seconds_per_second']=(ticks(completed1[0])-ticks(completed0[0]))/clock_ticks/elapsed_cpu
            results.append(value)
            args.report.with_name(args.report.stem+f'-{i}-raw.txt').write_text(
                raw0+'\nAFTER\n'+raw1+'\nHWUI BEFORE\n'+gfx0+'\nHWUI AFTER\n'+gfx1)
            print(json.dumps({k: value[k] for k in ['index','legacy','fps','hwui_frames']}),flush=True)
    finally:
        # 无论测量是否成功，都恢复默认实现。
        # Restore the default implementation even if a measurement fails.
        if args.control!='none':
            adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity',
                '--ez', args.control, 'true' if args.control in ('storage_bias_reference','performance_hint_reference','image_layout_reference','stream_submit_reference','cpu_copy_reference','driver_constant_reference') else 'false')
        if args.control=='pipeline_byte_hash':
            adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity',
                '--ez', 'pipeline_prepare_reference', 'false')
        args.report.write_text(json.dumps(dict(pid=pid, layer=layer, control=args.control,apk_sha256=apk_sha,
            measurement_script_sha256=script_sha,
            shader_chain=shader_chain,
            native_host_opt_level=int(compiled_opt.group(1)) if compiled_opt else None,
            async_submit='[vulkan] submission overlap: enabled' in log,
            async_present='[vulkan] presentation overlap: enabled' in log,
            complete=len(results)==4*args.rounds,warmup_s=args.warmup,samples=results,
            ged_helper_sha256=ged_helper_sha,ged_effective_ceiling_khz=ged_ceiling,
            ged_boundary_observations=ged_observations,
            limitation='Sequential short ABBA windows; clocks/lighting may drift. '
                       'No per-frame ADB polling; unchanged game/input/scale; not GPU-exclusive time.'),indent=2)+'\n')
    if args.control=='none':
        report=json.loads(args.report.read_text())
        intervals=[ms for sample in results for ms in sample['intervals_ms']]
        report['measurement']=dict(fps=1000/statistics.mean(intervals),mean_ms=statistics.mean(intervals),
                                   frames=len(intervals),control_changes=False)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report['measurement']))
        return
    old_mean = statistics.mean(v['mean_ms'] for v in results if v['legacy'])
    new_mean = statistics.mean(v['mean_ms'] for v in results if not v['legacy'])
    report=json.loads(args.report.read_text())
    report['comparison']=dict(old_mean_ms=old_mean,new_mean_ms=new_mean,
        mean_interval_reduction_percent=(1-new_mean/old_mean)*100,
        throughput_increase_percent=(old_mean/new_mean-1)*100)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('mean interval reduction percent:', (1-new_mean/old_mean)*100, flush=True)


if __name__ == '__main__':
    main()
