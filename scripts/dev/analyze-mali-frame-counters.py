#!/usr/bin/env python3
"""Normalize captured Arm SDK primary counters to actual game presents.

System GPU counts include other clients. Counter cycles are not GPU nanoseconds.
Matching sample/present windows avoid mixing FPS from another process or run.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True,help='capture output prefix')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():parser.error('use a new report')
    raw=Path(str(args.input)+'.jsonl');meta=json.loads(Path(str(args.input)+'.json').read_text())
    digest=hashlib.sha256(raw.read_bytes()).hexdigest()
    if digest!=meta['raw_sha256']:raise SystemExit('counter data hash mismatch')
    rows=[json.loads(line) for line in raw.read_text().splitlines()];samples=rows[1:]
    if not rows[0].get('metadata') or len(samples)!=40:raise SystemExit('incomplete sample sequence')
    for i,s in enumerate(samples):
        if s['sample']!=i or s['user_data']!=i or s['error'] or s['stretched'] not in (False,None):
            raise SystemExit('invalid/dropped hardware counter sample')
        if not 0<s['end_ns']-s['begin_ns']<1000000000:raise SystemExit('unreasonable sample interval')
        if i and s['begin_ns']!=samples[i-1]['end_ns']:raise SystemExit('noncontiguous samples')
    raw_hits=sum(s['raw_before_ns']<=s['end_ns']<=s['raw_after_ns'] for s in samples)
    mono_hits=sum(s['mono_before_ns']<=s['end_ns']<=s['mono_after_ns'] for s in samples)
    if raw_hits==40:
        clock='monotonic_raw'
        low=min(s['mono_before_ns']-s['raw_before_ns'] for s in samples)
        high=max(s['mono_after_ns']-s['raw_after_ns'] for s in samples)
    elif mono_hits==40:clock='monotonic';low=high=0
    else:raise SystemExit('no observed clock matches the counter samples')
    # Add a conservative 1ms boundary allowance for sample/clock calls. Retain
    # the resulting count range rather than pretending it is always exact.
    start_lo=samples[0]['begin_ns']+low-1000000
    start_hi=samples[0]['begin_ns']+high+1000000
    end_lo=samples[-1]['end_ns']+low-1000000
    end_hi=samples[-1]['end_ns']+high+1000000
    offset=(low+high)//2;start=samples[0]['begin_ns']+offset;end=samples[-1]['end_ns']+offset
    times=[]
    for line in Path(str(args.input)+'.sf-after.txt').read_text().splitlines():
        words=line.split()
        if len(words)==3 and all(word.isdecimal() for word in words):
            value=int(words[1])
            if 0<value<2**63-1:times.append(value)
    times=sorted(set(times))
    if not times or times[0]>start_lo or times[-1]<end_hi:
        raise SystemExit('actual-present history does not enclose the complete counter window')
    n=sum(start<=t<end for t in times)
    n_lo=sum(start_hi<=t<end_lo for t in times);n_hi=sum(start_lo<=t<end_hi for t in times)
    if n_lo<10:raise SystemExit('too few actual presents')
    totals=Counter();cores={}
    for s in samples:
        for b in s['blocks']:
            totals.update(b['counters'])
            if b['group']==2:
                core=cores.setdefault(str(b['index']),Counter());core.update(b['counters'])
    duration=(end-start)/1e9
    report={'pid':meta['pid'],'apk_sha256':meta['apk_sha256'],'shader_chain':meta['shader_chain'],
        'input_scope':meta.get('scope'),'raw_sha256':digest,'analysis_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'hardware':rows[0],'sample_clock':clock,'clock_offset_bounds_ns':[low,high],
        'duration_s':duration,'actual_present_count':n,'actual_present_count_bounds':[n_lo,n_hi],
        'observed_present_rate':n/duration,'counter_totals':dict(totals),
        'counters_per_present':{k:v/n for k,v in totals.items()},
        'per_core_exec_cycles':{k:v.get('G710_EXEC_CORE_ACTIVE',0) for k,v in cores.items()},
        'scope':'System GPU primary counters divided by observed target presents in the matching timestamp window. '
                'Display/producer boundary phase and other GPU clients remain. Cycle counts are not GPU nanoseconds, '
                'per-core active counts are not additive exclusive execution times, and this is not a game FPS benchmark.'}
    # The G710 reference guide uses the platform AXI width for external
    # bus beats, and a fixed 16-byte beat for shader-core L2 traffic. These
    # are byte counts, never exclusive GPU durations or uncached DRAM bytes.
    width=rows[0].get('axi_bus_width_bits')
    if isinstance(width,int) and width>0 and width%8==0:
        external={name:totals[key]*(width//8) for name,key in {
            'external_read_bytes':'G710_L2_EXT_READ_BEATS',
            'external_write_bytes':'G710_L2_EXT_WRITE_BEATS'}.items() if key in totals}
        core={name:totals[key]*16 for name,key in {
            'load_store_l2_read_bytes':'G710_BEATS_RD_LSC',
            'texture_l2_read_bytes':'G710_BEATS_RD_TEX',
            'load_store_external_read_bytes':'G710_BEATS_RD_LSC_EXT',
            'texture_external_read_bytes':'G710_BEATS_RD_TEX_EXT',
            'load_store_write_bytes':'G710_BEATS_WR_LSC_OTHER',
            'load_store_writeback_bytes':'G710_BEATS_WR_LSC_WB',
            'tile_write_bytes':'G710_BEATS_WR_TIB'}.items() if key in totals}
        byte_counts={**external,**core}
        report['memory_traffic']={
            'reference':'https://documentation-service.arm.com/static/667183a7e98d6a2ff18647c6',
            'totals_bytes':byte_counts,
            'bytes_per_actual_present':{k:v/n for k,v in byte_counts.items()},
            'bytes_per_present_bounds':{k:[v/n_hi,v/n_lo] for k,v in byte_counts.items()},
            'external_bytes_per_second':{k:v/duration for k,v in external.items()},
            'interpretation':'System GPU counters; shader-core categories overlap with the external bus totals. Do not add them together. External traffic may hit a system cache rather than DRAM. No bandwidth-saturation claim follows from bytes alone.'}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['pid','duration_s','actual_present_count','actual_present_count_bounds','observed_present_rate']}))


if __name__=='__main__':main()
