#!/usr/bin/env python3
"""Join existing Mesa draw identity hashes to existing IR3 native-code dump blocks."""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import re

HEADER=re.compile(r'Native code(?P<override> \(overridden\))? for unnamed (?P<stage>\w+) shader .*? with blake3 (?P<hash>[0-9a-f]{64}):')
STAT=re.compile(r'; (?P<stage>\w+) prog [^:]+: (?P<body>.*)')
OP=re.compile(r'^(?:\([^)]*\)\s*)*(?P<op>[a-z][a-z0-9_.]*)(?=\s|$)')

def parse_ir3(lines):
    blocks=[];current=None
    for n,line in enumerate(lines,1):
        header=HEADER.search(line)
        if header:
            current={'stage':header['stage'],'blake3':header['hash'],'overridden':bool(header['override']),'start_line':n,'statistics_lines':[],'opcode_text_histogram':Counter(),'compiler_instruction_counts':[]};blocks.append(current);continue
        if 'NIR (final form)' in line:current=None
        if not current:continue
        match=STAT.search(line)
        if match and match['stage']==current['stage']:
            current['statistics_lines'].append(match.group(0));count=re.match(r'(\d+) instr,',match['body'])
            if count:current['compiler_instruction_counts'].append(int(count[1]))
        body=re.sub(r'^.*?MESA: info: ','',line).strip()
        if not body or body.startswith((';','@')):continue
        op=OP.match(body)
        if op and not body.endswith(':'):current['opcode_text_histogram'][op['op']]+=1
    for b in blocks:b['opcode_text_histogram']=dict(b['opcode_text_histogram'])
    return blocks

def link(intervals,ir3_blocks,cb_classes=None):
    cb_classes=cb_classes or {}
    rps=defaultdict(list)
    for i in intervals:
        if i['kind']=='render_pass' and 'owner_cb_id' in i:rps[i['owner_cb_id']].append(i)
    compiler=defaultdict(list)
    for block in ir3_blocks:compiler[(block['stage'],block['blake3'])].append(block)
    grouped=defaultdict(lambda:defaultdict(lambda:{'draw_calls':0,'reported_count_argument_sum':0,'unknown_count_arguments':0}));unowned=[];draws_per_rp=Counter()
    for i in intervals:
        if i['kind']!='draw':continue
        matches=[r for r in rps[i.get('owner_cb_id')] if r['start_ns']<=i['start_ns'] and i['end_ns']<=r['end_ns']]
        if len(matches)!=1:unowned.append({'draw_interval_id':i['id'],'candidate_render_pass_ids':[r['id'] for r in matches]});continue
        r=matches[0];draws_per_rp[r['id']]+=1
        key=(cb_classes.get(r['owner_cb_id']),r['render_pass_ordinal_in_CB'],r['start_params'].get('width'),r['start_params'].get('height'),r['start_params'].get('attachment_count'))
        for stage,field in [('VERT','vs_sha1'),('FRAG','fs_sha1')]:
            sha=i['start_params'].get(field);g=grouped[key][(stage,sha)];g['draw_calls']+=1
            try:g['reported_count_argument_sum']+=int(i['start_params'].get('count'))
            except (ValueError,TypeError):g['unknown_count_arguments']+=1
    groups=[]
    for key,shaders in grouped.items():
        rows=[]
        for (stage,sha),count in shaders.items():
            blocks=compiler.get((stage,sha),[])
            rows.append({'stage':stage,'actual_native_hash':sha,**count,'exact_native_IR3_matches':blocks,'compiler_match_available':bool(blocks)})
        rows.sort(key=lambda r:(r['stage'],-r['draw_calls'],str(r['actual_native_hash'])))
        groups.append({'CB_class_id':key[0],'render_pass_ordinal':key[1],'width':key[2],'height':key[3],'attachment_count':key[4],'shaders':rows})
    checks=[]
    for cb_rps in rps.values():
        for r in cb_rps:
            try:reported=int(r['end_params'].get('drawCount'))
            except (TypeError,ValueError):reported=None
            checks.append({'render_pass_id':r['id'],'owner_cb_id':r['owner_cb_id'],'ordinal':r['render_pass_ordinal_in_CB'],'reported_draw_count':reported,'paired_draw_identities':draws_per_rp[r['id']],'count_matches':reported==draws_per_rp[r['id']] if reported is not None else None})
    return {'scope':'Actual native draw identity linkage only. No per-draw GPU cost attribution; draw/index counts are not fragment invocation or time weights.',
        'draw_timestamps_used_as_cost':False,'groups':sorted(groups,key=lambda g:(g['render_pass_ordinal'],str(g['width']),str(g['height']))),'draw_identity_completeness':checks,'unowned_or_ambiguous_draws':unowned,
        'IR3_hash_contract':'Turnip IR3 variant hash is native binary plus double_threadsize; exact hash/stage join only, no historical program-number guessing.',
        'static_opcode_limit':'Opcode text histogram includes padding and repeated-instruction syntax; compiler statistics are preserved separately. Neither is dynamic operation count.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--analysis',type=Path,required=True);p.add_argument('--ir3',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    report=json.loads((a.analysis/'analysis.json').read_text());ip=a.analysis/report['intervals_file'];intervals=[json.loads(line) for line in ip.open()];raw=a.ir3.read_bytes();result=link(intervals,parse_ir3(raw.decode(errors='replace').splitlines()),{c['id']:c['class_id'] for c in report['command_buffers']})
    result['inputs']={str(f.resolve()):hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.analysis/'analysis.json',ip,a.ir3,Path(__file__)]}
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
if __name__=='__main__':main()
