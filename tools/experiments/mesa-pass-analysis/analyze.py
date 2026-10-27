#!/usr/bin/env python3
"""Organize existing Mesa u_trace CSV intervals; no capture, sampling or new timing."""
import argparse
import io
from collections import Counter,defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

PARAM=re.compile(r'([A-Za-z_][A-Za-z_0-9]*)=')
PASS_FIELDS=('width','height','attachment_count','hasDepth','ubwc','maxSamples','clearCPP','loadCPP','storeCPP','tiledRender','tilingDisableReason','drawCount')
COMPUTE_FIELDS=('cs_sha1','local_size_x','local_size_y','local_size_z','num_groups_x','num_groups_y','num_groups_z','indirect','unaligned')


def params(text):
    """Top-level key=value fields; retain comma-containing nested JSON and empty values."""
    found={};duplicates=[];unknown=[];tokens=[];start=0;depth=0;quote=None;escape=False
    for n,char in enumerate(text):
        if quote:
            if escape:escape=False
            elif char=='\\':escape=True
            elif char==quote:quote=None
        elif char in ('"',"'"):quote=char
        elif char in '[{(':depth+=1
        elif char in ']})':depth=max(0,depth-1)
        elif char==',' and depth==0:
            tokens.append(text[start:n]);start=n+1
    tokens.append(text[start:])
    # A plain textual value may itself contain a comma. Preserve continuation.
    last=None
    for token in tokens:
        token=token.strip()
        if not token:continue
        match=PARAM.match(token)
        if not match:
            if last:found[last]+=','+token
            else:unknown.append(token)
            continue
        key=match[1];value=token[match.end():]
        if key in found:duplicates.append(key)
        found[key]=value;last=key
    return found,{'duplicate_keys':duplicates,'unparsed_fields':unknown,'unclosed_quote_or_container':bool(quote or depth)}


def number(value):
    try:return int(value)
    except (ValueError,TypeError):return None


def distribution(values):
    if not values:return {'count':0,'sum_ns':0,'mean_ms':None,'p50_ms':None,'p95_ms':None,'min_ms':None,'max_ms':None}
    ordered=sorted(values)
    return {'count':len(values),'sum_ns':sum(values),'mean_ms':statistics.mean(values)/1e6,
            'p50_ms':statistics.median(values)/1e6,'p95_ms':ordered[max(0,math.ceil(.95*len(ordered))-1)]/1e6,'min_ms':ordered[0]/1e6,'max_ms':ordered[-1]/1e6}


def union_ns(ranges):
    end=None;total=0
    for a,b in sorted(ranges):
        if b<a:continue
        total+=b-max(a,end) if end is not None and b>max(a,end) else (b-a if end is None else 0)
        end=b if end is None else max(end,b)
    return total


def histogram(rows):return [{'value':list(key),'count':count} for key,count in rows.most_common()]


def parse(lines):
    batches={};ignored_headers=0;invalid=[];parameter_issues=[];event_rows=0
    for line_no,line in enumerate(lines,1):
        if line.startswith('frame,batch,time_ns,event'):ignored_headers+=1;continue
        if not line.strip():continue
        parts=line.rstrip('\r\n').split(',',4)
        if len(parts)!=5 or any(not re.fullmatch(r'\d+',x) for x in parts[:3]):
            invalid.append({'line':line_no,'reason':'invalid CSV prefix','raw':line.rstrip()});continue
        frame,batch,ts=map(int,parts[:3]);event=parts[3];pa,issues=params(parts[4]);event_rows+=1
        if any(issues.values()):parameter_issues.append({'line':line_no,**issues})
        row={'line':line_no,'frame_field':frame,'ts':ts,'event':event,'params':pa}
        b=batches.setdefault(batch,{'id':batch,'events':[],'intervals':[],'missing_start':[],'missing_end':[],'negative':[],'timestamp_backsteps':[],'same_kind_nested_starts':[]})
        if b['events'] and ts<b['events'][-1]['ts']:b['timestamp_backsteps'].append({'line':line_no,'previous':b['events'][-1]['ts'],'current':ts})
        b['events'].append(row)
    all_intervals=[]
    for b in batches.values():
        stacks=defaultdict(list)
        for row in b['events']:
            event=row['event']
            if event.startswith('start_'):
                if stacks[event[6:]]:b['same_kind_nested_starts'].append({'line':row['line'],'ts':row['ts'],'kind':event[6:]})
                stacks[event[6:]].append(row)
            elif event.startswith('end_'):
                name=event[4:]
                if not stacks[name]:b['missing_start'].append(row);continue
                first=stacks[name].pop()
                if row['ts']<first['ts']:b['negative'].append({'start':first,'end':row});continue
                interval={'id':len(all_intervals),'batch':b['id'],'kind':name,'start_ns':first['ts'],'end_ns':row['ts'],'duration_ns':row['ts']-first['ts'],
                          'start_line':first['line'],'end_line':row['line'],'start_params':first['params'],'end_params':row['params'],'frame_fields':[first['frame_field'],row['frame_field']]}
                b['intervals'].append(interval);all_intervals.append(interval)
        for values in stacks.values():b['missing_end'].extend(values)
        b['event_rows']=len(b['events']);del b['events']
    return batches,all_intervals,{'event_rows':event_rows,'ignored_headers':ignored_headers,'invalid_rows':invalid,'parameter_issues':parameter_issues}


def analyze(lines):
    batches,intervals,quality=parse(lines);commands=[];unowned=[]
    # Ownership uses actual complete command-buffer envelopes, not frame/batch IDs.
    for b in batches.values():
        cbs=[i for i in b['intervals'] if i['kind']=='cmd_buffer']
        for cb in cbs:
            children=[]
            for i in b['intervals']:
                if i is cb or i['kind']=='cmd_buffer':continue
                owners=[c for c in cbs if c['start_ns']<=i['start_ns'] and i['end_ns']<=c['end_ns']]
                if owners and min(owners,key=lambda c:(c['duration_ns'],c['id'])) is cb:children.append(i);i['owner_cb_id']=cb['id']
            passes=[i for i in children if i['kind']=='render_pass'];computes=[i for i in children if i['kind']=='compute']
            for ordinal,i in enumerate(sorted(passes,key=lambda i:(i['start_ns'],i['end_ns'],i['id']))):i['render_pass_ordinal_in_CB']=ordinal
            for ordinal,i in enumerate(sorted(computes,key=lambda i:(i['start_ns'],i['end_ns'],i['id']))):i['compute_ordinal_in_CB']=ordinal
            child_scope_complete=not any(cb['start_ns']<=x['ts']<=cb['end_ns'] for x in b['missing_start']+b['missing_end']+b['same_kind_nested_starts'])
            observed={'renderpasses':len(passes),'dispatches':len(computes),'draw_count_sum':sum(number(i['end_params'].get('drawCount')) or 0 for i in passes)}
            reported={k:number(cb['end_params'].get(k)) for k in ('renderpasses','dispatches')}
            shape=tuple((i['start_params'].get('width'),i['start_params'].get('height'),i['start_params'].get('attachment_count'),i['start_params'].get('hasDepth')) for i in passes)
            signature=(reported['renderpasses'],reported['dispatches'],observed['renderpasses'],observed['dispatches'],cb['start_params'].get('appName'),cb['start_params'].get('oneTimeSubmit'),cb['start_params'].get('simultaneousUse'),shape)
            class_id=hashlib.sha256(json.dumps(signature,separators=(',',':')).encode()).hexdigest()[:16]
            commands.append({'id':cb['id'],'batch':b['id'],'class_id':class_id,'start_ns':cb['start_ns'],'end_ns':cb['end_ns'],'duration_ns':cb['duration_ns'],
                'start_metadata':cb['start_params'],'end_metadata':cb['end_params'],'reported':reported,'observed':observed,'count_matches':{k:(reported[k]==observed[k] if reported[k] is not None else None) for k in reported},
                'child_interval_ids':[i['id'] for i in children],'child_union_ns':union_ns([(i['start_ns'],i['end_ns']) for i in children]),'nested_children_are_not_additive':True,'enabled_child_pairs_unambiguous_and_complete':child_scope_complete})
        unowned.extend(i['id'] for i in b['intervals'] if i['kind']!='cmd_buffer' and 'owner_cb_id' not in i)
    by_id={i['id']:i for i in intervals};owned_intervals=defaultdict(list)
    for i in intervals:
        if 'owner_cb_id' in i:owned_intervals[i['owner_cb_id']].append(i)
    classes=defaultdict(list)
    for c in commands:classes[c['class_id']].append(c)
    summaries=[];total_draws=sum(c['observed']['draw_count_sum'] for c in commands)
    for cid,cb_list in classes.items():
        pass_groups=defaultdict(list);compute_groups=defaultdict(list);phases=defaultdict(list)
        for cb in cb_list:
            for iid in cb['child_interval_ids']:
                i=by_id[iid];phases[i['kind']].append(i['duration_ns']);merged=dict(i['start_params'],**i['end_params'])
                if i['kind']=='render_pass':pass_groups[tuple(merged.get(k) for k in PASS_FIELDS)].append(i)
                elif i['kind']=='compute':compute_groups[tuple(merged.get(k) for k in COMPUTE_FIELDS)].append(i)
        groups=[]
        for key,items in pass_groups.items():
            sub=defaultdict(list);formats=Counter()
            for rp in items:
                for i in owned_intervals[rp['owner_cb_id']]:
                    if i['id']==rp['id']:continue
                    if rp['start_ns']<=i['start_ns'] and i['end_ns']<=rp['end_ns']:
                        sub[i['kind']].append(i['duration_ns'])
                        if i['start_params'].get('format'):formats[(i['kind'],i['start_params']['format'])]+=1
            groups.append({'key':dict(zip(PASS_FIELDS,key)),**distribution([i['duration_ns'] for i in items]),'inclusive_ms_per_class_CB':sum(i['duration_ns'] for i in items)/len(cb_list)/1e6,
                'nested_phase_statistics_inclusive_not_additive':{kind:distribution(v) for kind,v in sub.items()},'nested_format_histogram':histogram(formats),'render_pass_ordinal_histogram':histogram(Counter((i['render_pass_ordinal_in_CB'],) for i in items))})
        groups.sort(key=lambda g:g['sum_ns'],reverse=True)
        cg=[{'key':dict(zip(COMPUTE_FIELDS,key)),**distribution([i['duration_ns'] for i in items]),'inclusive_ms_per_class_CB':sum(i['duration_ns'] for i in items)/len(cb_list)/1e6} for key,items in compute_groups.items()];cg.sort(key=lambda g:g['sum_ns'],reverse=True)
        starts=sorted(c['start_ns'] for c in cb_list);draws=sum(c['observed']['draw_count_sum'] for c in cb_list)
        summaries.append({'class_id':cid,'complete_CB_count':len(cb_list),'qualified_complete_scope_CB_count':sum(c['enabled_child_pairs_unambiguous_and_complete'] for c in cb_list),'batch_ids':sorted(set(c['batch'] for c in cb_list)),'complete_CB_ids':[c['id'] for c in cb_list],
            'metadata_histogram':histogram(Counter((c['reported']['renderpasses'],c['reported']['dispatches'],c['observed']['renderpasses'],c['observed']['dispatches'],c['observed']['draw_count_sum']) for c in cb_list)),
            'start_metadata_example':cb_list[0]['start_metadata'],'duration':distribution([c['duration_ns'] for c in cb_list]),'CB_begin_intervals':distribution([b-a for a,b in zip(starts,starts[1:])]),
            'aggregate_reported_draws':draws,'draw_workload_share':draws/total_draws if total_draws else None,'pass_groups':groups,'compute_groups':cg,
            'inclusive_phase_statistics_do_not_sum':{k:distribution(v) for k,v in phases.items()},'child_union_statistics':distribution([c['child_union_ns'] for c in cb_list])})
    summaries.sort(key=lambda c:(c['aggregate_reported_draws'],c['complete_CB_count']),reverse=True)
    cb_batch_counts=Counter(c['batch'] for c in commands)
    dominant=summaries[0] if summaries and summaries[0]['qualified_complete_scope_CB_count']>=2 and (summaries[0]['draw_workload_share'] or 0)>=.5 else None
    return {'schema':1,'scope':'Organization of existing driver GPU intervals only, no capture/new measurement/FPS claim',
        'time_domain':{'source':'Mesa CSV time_ns (driver GPU ticks scaled to integers)','host_BOOTTIME_mapping':None,'Surface_frame_mapping':None,'batch_is_frame':False,'frame_field_is_used_for_selection':False,'nested_intervals_additive':False},
        'quality':quality,'batch_completeness':[{'batch':b['id'],'event_rows':b['event_rows'],'complete_CB_count':cb_batch_counts[b['id']],'paired_intervals':len(b['intervals']),'missing_start':b['missing_start'],'missing_end':b['missing_end'],'negative_ranges':b['negative'],'timestamp_backsteps':b['timestamp_backsteps'],'same_kind_nested_starts':b['same_kind_nested_starts'],'all_enabled_pairs_complete':not(b['missing_start'] or b['missing_end'] or b['negative'])} for b in batches.values()],
        'complete_CB_count':len(commands),'complete_CB_means':'Paired start/end envelope only; each CB separately reports child scope completeness and ambiguity.','CB_metadata_histogram':histogram(Counter((c['reported']['renderpasses'],c['reported']['dispatches'],c['observed']['renderpasses'],c['observed']['dispatches']) for c in commands)),
        'dominant_draw_workload_class':dominant['class_id'] if dominant else None,'class_selection_basis':'Recurrent complete CB structural shape and aggregate reported draw footprint; never longest duration. This is a workload candidate, not an HDR or frame identity.',
        'command_buffers':commands,'CB_classes':summaries,'intervals':intervals,'unowned_complete_interval_ids':unowned,
        'required_for_cross_tool_join':['actual joint GPU/host clock calibration or official clock snapshot','verified current Surface layer and workload boundaries; batch/frame field alone is insufficient'],
        'measurement_limits':['Intervals include dependencies and boundary work; not pure shader time','Instrumented interval union is not a hardware GPU busy counter','Different phase kinds may nest/overlap and must not be added','Pairing uses a per-batch per-kind stack; same-kind nested starts are retained as ambiguous, not silently certified','Metadata counts may differ from enabled tracepoint counts; preserve mismatch, do not repair or force fixed pass count','No implicit warmup/tail trimming; all complete observed CBs retained']}


def checkpoint_suffix(after,before):
    if not after.startswith(before):raise ValueError('CSV checkpoint is not an exact byte prefix of later CSV')
    return after[len(before):],{'selection':'Only newly published CSV bytes after the explicit earlier checkpoint; complete envelopes only, edge fragments retained unknown.',
        'before_sha256':hashlib.sha256(before).hexdigest(),'after_sha256':hashlib.sha256(after).hexdigest(),'before_bytes':len(before),'after_bytes':len(after),
        'first_suffix_row_may_be_fragment':bool(before and not before.endswith(b'\n')),'line_numbers_relative_to_suffix':True,'source_line_offset':before.count(b'\n'),
        'host_window_mapping':None,'batch_id_is_not_a_frame':True}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('csv',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--before-checkpoint',type=Path,help='explicit earlier byte-prefix CSV; analyze only later published bytes, without host-clock mapping');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    raw=a.csv.read_bytes()
    if a.before_checkpoint:
        suffix,checkpoint=checkpoint_suffix(raw,a.before_checkpoint.read_bytes());result=analyze(io.StringIO(suffix.decode()));checkpoint['before_path']=str(a.before_checkpoint.resolve());result['checkpoint_selection']=checkpoint
    else:
        result=analyze(io.StringIO(raw.decode()))
    intervals=result.pop('intervals')
    with (a.output/'intervals.jsonl').open('w') as f:
        for i in intervals:f.write(json.dumps(i,sort_keys=True)+'\n')
    result['input']={'path':str(a.csv.resolve()),'sha256':hashlib.sha256(raw).hexdigest(),'bytes':a.csv.stat().st_size}
    result['tool_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest();result['intervals_file']='intervals.jsonl'
    (a.output/'analysis.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    text=['Existing Mesa u_trace CSV analysis; GPU clock only, no Surface/frame join.','',f'Complete CBs: {result["complete_CB_count"]}; structural draw-workload candidate: {result["dominant_draw_workload_class"]}.','Nested phase intervals are inclusive and not additive.','']
    for c in result['CB_classes']:
        text.append(f'CB class {c["class_id"]}: count={c["complete_CB_count"]}, inclusive mean={c["duration"]["mean_ms"]:.6f}ms, draw footprint={c["draw_workload_share"]}, metadata={c["metadata_histogram"][:3]}')
        for g in c['pass_groups'][:8]:text.append(f'  Render group {g["key"]}: {g["inclusive_ms_per_class_CB"]:.6f} inclusive ms/class CB, count={g["count"]}')
        for g in c['compute_groups'][:5]:text.append(f'  Compute group {g["key"]}: {g["inclusive_ms_per_class_CB"]:.6f} inclusive ms/class CB, count={g["count"]}')
    (a.output/'SUMMARY.txt').write_text('\n'.join(text)+'\n');print(json.dumps({'complete_CBs':result['complete_CB_count'],'classes':len(result['CB_classes']),'dominant_draw_workload_class':result['dominant_draw_workload_class'],'output':str(a.output)}))
if __name__=='__main__':main()
