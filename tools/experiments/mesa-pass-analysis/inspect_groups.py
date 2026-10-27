#!/usr/bin/env python3
"""Summarize existing parsed pass metadata, including LRZ and clear-only envelopes."""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import importlib.util

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('mesa_interval_helpers',HERE/'analyze.py');H=importlib.util.module_from_spec(spec);spec.loader.exec_module(H)
FIELDS=('lrz','lrzDisableReason','lrzDisabledAtDraw','lrzWriteDisableReason','lrzWriteDisabledAtDraw','lrzStatus','avgPerSampleBandwidth')

def inspect(directory):
    raw=(directory/'analysis.json').read_bytes();report=json.loads(raw);interval_file=directory/report['intervals_file'];ids={c['id']:c for c in report['command_buffers']};classes={c['id']:c['class_id'] for c in report['command_buffers']};rows=defaultdict(list);all_rps=defaultdict(list);zero=defaultdict(list)
    for line in interval_file.open():
        i=json.loads(line)
        if i['kind']!='render_pass' or i.get('owner_cb_id') not in ids:continue
        cid=classes[i['owner_cb_id']];rows[(cid,i['render_pass_ordinal_in_CB'])].append(i);all_rps[i['owner_cb_id']].append((i['start_ns'],i['end_ns']))
        if i['end_params'].get('drawCount')=='0':zero[i['owner_cb_id']].append((i['start_ns'],i['end_ns']))
    out=[]
    for c in report['CB_classes']:
        cbids=c['complete_CB_ids'];ordinal=[]
        for (cid,n),items in rows.items():
            if cid!=c['class_id']:continue
            attrs=Counter(tuple(dict(i['start_params'],**i['end_params']).get(k) for k in H.PASS_FIELDS) for i in items)
            lrz=Counter(tuple(i['end_params'].get(k) for k in FIELDS) for i in items)
            ordinal.append({'ordinal_in_CB':n,**H.distribution([i['duration_ns'] for i in items]),'pass_metadata_histogram':[{'key':dict(zip(H.PASS_FIELDS,key)),'count':count} for key,count in attrs.most_common()],
                'lrz_metadata_histogram':[{'key':dict(zip(FIELDS,key)),'count':count} for key,count in lrz.most_common()]})
        overlap=sum(sum(b-a for a,b in all_rps[k])-H.union_ns(all_rps[k]) for k in cbids)
        out.append({'class_id':c['class_id'],'complete_CB_count':len(cbids),'ordinal_passes_sorted_by_cost':sorted(ordinal,key=lambda x:x['sum_ns'],reverse=True),
            'sibling_render_pass_overlap_ns':overlap,'zero_draw_pass_union_ms_per_CB':sum(H.union_ns(zero[k]) for k in cbids)/len(cbids)/1e6,
            'zero_draw_passes_can_all_be_removed':None,'unknown':['CSV does not identify attachment image identity or following draw blend/depth/stencil/FS output coverage; no redundant-clear eligibility claim.','avgPerSampleBandwidth is driver model metadata, not measured DRAM bytes.','lrz valid, write-disable reason, and GPU buffer direction status have different meanings; do not equate one field with whole-pass culling.']})
    return {'scope':'Derived from existing Mesa CSV output only; no capture or new timing','input_analysis_sha256':hashlib.sha256(raw).hexdigest(),'input_intervals_sha256':hashlib.sha256(interval_file.read_bytes()).hexdigest(),'tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'time_domain':report['time_domain'],'CB_classes':out}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.write_text(json.dumps(inspect(a.directory),indent=2,sort_keys=True)+'\n')
