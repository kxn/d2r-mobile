#!/usr/bin/env python3
"""Exercise the actual bounded C manifest/input validator, without Vulkan init."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('batch',Path(__file__).with_name('prepare-native-image-batch.py'))
batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

def test(a):
    cases=[]
    base={'format':95,'type':2,'depth':1,'layoutType':3,'width':256,'height':256,'levels':9,'layoutData':3,'thumbnailMip':4,'thumbnailBase64':'present'}
    logical='data/hd/env/texture/act1/town/texture.texture'
    assert batch.selected(logical,base,set())
    assert batch.selected('data/hd/character/player/texture.texture',base,set())
    for field,value in [('format',99),('type',3),('depth',2),('layoutType',0),('levels',17),('layoutData',6),('thumbnailMip',0),('thumbnailMip',9)]:
        d=copy.deepcopy(base);d[field]=value;assert not batch.selected(logical,d,set()),field
    assert not batch.selected(logical,base,{logical})
    assert not batch.selected('data/hd/ui/texture.texture',base,set())
    cases.append('strict-static-ASTC4-family-shape-thumbnail-array-exclusion')
    with tempfile.TemporaryDirectory(prefix='native-batch-actual-validator-') as tmp:
        root=Path(tmp);(root/'items').mkdir()
        rows=[]
        for n in range(129):
            identity=f'{n:064x}';directory=root/'items'/identity;directory.mkdir()
            for leaf in ('resource-input.bin','resource-source.json'):
                (directory/leaf).write_bytes((a.input/leaf).read_bytes())
            rows.append({'id':identity,'input':{'sha256':sha(directory/'resource-input.bin')},'source':{'sha256':sha(directory/'resource-source.json')}})
        def invoke(text,passed):
            (root/'resource-batch.tsv').write_bytes(text)
            result=subprocess.run([str(a.validator),'--verify-resource-batch',str(root)],text=True,capture_output=True)
            assert (result.returncode==0)==passed,(passed,result.returncode,result.stdout,result.stderr)
            if passed:
                terminal=json.loads(result.stdout.strip().splitlines()[-1]);assert terminal['result']=='passed' and terminal['GPU_executed'] is False
            assert not (root/'resource-batch-receipts.jsonl').exists()
            assert not (root/'resource-batch-complete.json').exists()
        for count in (1,2,128):invoke(batch.batch_text(rows[:count]),True)
        cases.append('actual-C-1-2-128entries-original-input-allchecks-noGPU')
        good=batch.batch_text(rows[:1])
        bad=[b'',b'DM_RESOURCE_BATCH_V1\n',b'WRONG\n'+good.split(b'\n',1)[1],good+b'x',good.replace(b'\t',b' ',1),good.replace(b'000000',b'../../',1),good.replace(b'000000',b'AAAAAA',1),good+good.split(b'\n',1)[1],b'DM_RESOURCE_BATCH_V1\n'+b''.join(batch.batch_text([row]).split(b'\n',1)[1] for row in rows)]
        for text in bad:invoke(text,False)
        cases.append('actual-C-empty-version-tail-separators-path-uppercase-duplicate-overcapacity-reject')
        for field in ('input','source'):
            wrong=copy.deepcopy(rows[:1]);wrong[0][field]['sha256']='f'*64
            invoke(batch.batch_text(wrong),False)
        cases.append('actual-C-pinned-input-and-declaration-digests-reject')
        file=root/'items'/rows[0]['id']/'resource-input.bin';data=bytearray(file.read_bytes());data[-1]^=1;file.write_bytes(data)
        invoke(good,False)
        cases.append('actual-C-corrupt-compressed-block-original-digest-reject')
    for count in (0,129):
        try:batch.batch_text(rows[:count]);raise AssertionError('accepted bad bound')
        except ValueError:pass
    try:batch.batch_text([rows[0],rows[0]]);raise AssertionError('accepted duplicate')
    except ValueError:pass
    cases.append('host-manifest-same-bounds-duplicates-reject')
    report={'passed':True,'cases':cases,'validator':{'path':str(a.validator),'sha256':sha(a.validator)},'input_sha256':sha(a.input/'resource-input.bin'),'scope':'Original production C batch parser plus original input validator; no Vulkan initialization, driver execution, sampling, timing or device.'}
    if a.report:a.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--validator',type=Path,required=True);p.add_argument('--input',type=Path,required=True);p.add_argument('--report',type=Path)
    test(p.parse_args())
