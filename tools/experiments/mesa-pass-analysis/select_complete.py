#!/usr/bin/env python3
"""Stream an existing large CSV and retain explicitly requested complete-CB candidates."""
import argparse
from collections import Counter,deque
import hashlib
import json
from pathlib import Path
import re

def select(path,reported_passes,keep):
    chosen=deque(maxlen=keep);hist=Counter();rows=[];current=None;previous=None;metadata=[];cb_starts=0;cb_ends=0;invalid=0;blocks=0;missing_cb_edges=0
    def finish():
        nonlocal blocks,missing_cb_edges
        if not rows:return
        blocks+=1
        for n in metadata:hist[n]+=1
        if cb_starts!=cb_ends:missing_cb_edges+=1
        if reported_passes in metadata and cb_starts==cb_ends and cb_starts:
            chosen.append({'batch':current,'lines':list(rows),'metadata_renderpasses':list(metadata)})
    with path.open() as stream:
        for line in stream:
            prefix=line.split(',',4)
            if len(prefix)!=5 or not prefix[0].isdigit() or not prefix[1].isdigit():
                if not line.startswith('frame,batch,time_ns,event,'):invalid+=1
                continue
            batch=int(prefix[1])
            if previous is not None and batch<previous:raise ValueError('Interleaved/decreasing batch IDs: bounded streaming selection cannot certify this file; retain original input for a different selector')
            previous=batch
            if current!=batch:
                finish();rows=[];metadata=[];cb_starts=cb_ends=0;current=batch
            rows.append(line)
            cb_starts+=prefix[3]=='start_cmd_buffer';cb_ends+=prefix[3]=='end_cmd_buffer'
            if prefix[3]=='end_cmd_buffer':
                match=re.search(r'(?:^|,\s*)renderpasses=(\d+)(?:,|\s*$)',prefix[4])
                if match:metadata.append(int(match[1]))
    finish()
    return list(chosen),{'streamed_batch_blocks':blocks,'complete_CB_renderpass_metadata_histogram':dict(hist),'invalid_or_partial_rows':invalid,'batch_blocks_with_missing_CB_edges':missing_cb_edges,'requested_reported_renderpasses':reported_passes,'retained_candidates':len(chosen),'max_candidates':keep,'selection_is_not_a_performance_window':True}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('csv',type=Path);p.add_argument('--reported-passes',type=int,required=True);p.add_argument('--keep',type=int,default=3);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.keep<1:raise ValueError('keep must be positive')
    chosen,stats=select(a.csv,a.reported_passes,a.keep)
    if not chosen:raise ValueError('No requested complete candidate; do not infer shader identity from old data')
    a.output.mkdir(parents=True,exist_ok=False);target=a.output/'selected.csv'
    with target.open('w') as stream:
        stream.write('frame,batch,time_ns,event,\n')
        for block in chosen:stream.writelines(block['lines'])
    with a.csv.open('rb') as f:source_sha=hashlib.file_digest(f,'sha256').hexdigest()
    record={'scope':'Explicit structural subset for identity analysis only; not FPS or per-draw timing','source':{'path':str(a.csv.resolve()),'sha256':source_sha,'bytes':a.csv.stat().st_size},'selected':{'path':str(target.resolve()),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()},'batches':[b['batch'] for b in chosen],'statistics':stats,'tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (a.output/'selection.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
