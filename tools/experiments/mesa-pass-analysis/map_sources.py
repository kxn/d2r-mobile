#!/usr/bin/env python3
"""Exact current native-code/NIR-source/SPIR-V identity map using standard BLAKE3."""
import argparse
from collections import defaultdict
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys

NIR=re.compile(r'NIR \(final form\) for (\w+) shader')
SOURCE=re.compile(r'source_blake3: \{([^}]+)\}')
NATIVE=re.compile(r'Native code(?: \(overridden\))? for unnamed (\w+) shader .*? with blake3 ([0-9a-f]{64}):')

def decode_source_blake3(words):
    if len(words)!=8 or any(not 0<=word<=0xffffffff for word in words):
        raise ValueError('source_blake3 must contain exactly eight uint32 values')
    return struct.pack('<8I',*words).hex()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--link',type=Path,required=True);p.add_argument('--ir3',type=Path,required=True);p.add_argument('--shader-root',type=Path,required=True);p.add_argument('--blake3-python-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    sys.path.insert(0,str(a.blake3_python_root.resolve()));import blake3
    link=json.loads(a.link.read_text());wanted={(s['stage'],s['actual_native_hash']) for g in link['groups'] if g['render_pass_ordinal'] in (4,7,10) for s in g['shaders']};native=defaultdict(list);nir=None
    for n,line in enumerate(a.ir3.open(),1):
        m=NIR.search(line)
        if m:nir={'stage':m[1],'NIR_start_line':n,'source_blake3':None}
        m=SOURCE.search(line)
        if m and nir:
            words=[int(x.strip(),16) for x in m[1].split(',')];
            nir['source_blake3']=decode_source_blake3(words)
        m=NATIVE.search(line)
        if m and (m[1],m[2]) in wanted:
            native[(m[1],m[2])].append({'native_start_line':n,'NIR':dict(nir) if nir and nir['stage']==m[1] else None})
    source_hashes={r['NIR']['source_blake3'] for records in native.values() for r in records if r['NIR'] and r['NIR']['source_blake3']};matches=defaultdict(list)
    for file in sorted(a.shader_root.iterdir()):
        if not file.is_file() or file.suffix not in ('.spv','.spec','.pred'):continue
        data=file.read_bytes();payload=data;offset=0
        if file.suffix=='.spec':offset=60;payload=data[offset:]
        elif file.suffix=='.pred':offset=164;payload=data[offset:]
        if len(payload)<20 or payload[:4]!=b'\x03\x02\x23\x07':continue
        h=blake3.blake3(payload).hexdigest()
        if h in source_hashes:matches[h].append({'path':str(file.resolve()),'SPIRV_offset':offset,'SPIRV_bytes':len(payload),'SPIRV_sha256':hashlib.sha256(payload).hexdigest(),'file_sha256':hashlib.sha256(data).hexdigest(),'source_blake3':h})
    entries=[]
    for (stage,h),records in sorted(native.items()):
        hashes=sorted({r['NIR']['source_blake3'] for r in records if r['NIR'] and r['NIR']['source_blake3']})
        entries.append({'stage':stage,'actual_native_hash':h,'actual_NIR_records':records,'source_blake3_values':hashes,'exact_original_modules':[m for source in hashes for m in matches[source]],'all_actual_sources_resolved':bool(hashes) and all(matches[source] for source in hashes)})
    result={'scope':'Exact current native identity association, no timing or GPU performance inference','method':'Mesa spirv_to_nir.c source_blake3 is BLAKE3(original SPIR-V words); dump uint32 values decoded little-endian. Native stage/hash is separately matched to actual draw trace.',
        'entries':entries,'standard_blake3_package':{'version':blake3.__version__,'module':str(Path(blake3.__file__).resolve())},'inputs':{str(f.resolve()):hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.link,a.ir3,a.shader_root/'_vk_manifest.json',Path(__file__)]}}
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n');print(json.dumps({'native_hashes':len(entries),'all_resolved':sum(e['all_actual_sources_resolved'] for e in entries),'matched_original_payloads':sum(len(e['exact_original_modules']) for e in entries)}))
if __name__=='__main__':main()
