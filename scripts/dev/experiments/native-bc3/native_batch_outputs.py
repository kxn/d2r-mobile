"""Standard tar extraction guards and original batch receipt hash checks, no decoding."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile

MAX_NATIVE=16<<20
MAX_METADATA=64<<10

def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def output_limits(entries):
    assert 0<len(entries)<=128
    limits={'resource-batch-complete.json':4096,'resource-batch-receipts.jsonl':4096*len(entries)}
    for row in entries:
        assert re.fullmatch('[0-9a-f]{64}',row['id'])
        for leaf,size in [('native-resource.bin',MAX_NATIVE),('native-resource-layout.bin',992),('native-resource.json',MAX_METADATA)]:
            name='items/'+row['id']+'/'+leaf
            assert name not in limits
            limits[name]=size
    return limits

def extract_outputs(archive,destination,entries):
    """Use stdlib tarfile, exact names only, all headers checked before writes."""
    limits=output_limits(entries)
    assert Path(archive).stat().st_size<=sum(limits.values())+len(limits)*1024+10240
    destination=Path(destination)
    assert not destination.exists(),'Never overwrite earlier batch output'
    with tarfile.open(archive,'r:') as tar:
        members=[];seen=set()
        for m in tar:
            assert len(members)<len(limits) and m.name in limits and m.name not in seen,'Output archive whitelist differs'
            members.append(m);seen.add(m.name)
            assert m.type in (tarfile.REGTYPE,tarfile.AREGTYPE) and not m.issparse() and not m.pax_headers,'Non-regular/link/sparse/extended entry'
            assert 0<m.size<=limits[m.name],'Output member exceeds original bounded contract'
            if m.name.endswith('/native-resource-layout.bin'):assert m.size==992
        assert seen==set(limits),'Missing output archive members'
        destination.mkdir(parents=True)
        for m in members:
            path=destination/m.name
            path.parent.mkdir(parents=True,exist_ok=True)
            stream=tar.extractfile(m)
            assert stream is not None
            with path.open('xb') as target:shutil.copyfileobj(stream,target,length=1<<20)
            assert path.stat().st_size==m.size
    return {name:sha(destination/name) for name in limits}

def validate_outputs(destination,entries,manifest_sha,identity,producer_sha):
    root=Path(destination)
    completed=json.loads((root/'resource-batch-complete.json').read_text())
    assert completed['complete'] and completed['entries']==len(entries)
    assert completed['manifest_sha256']==manifest_sha and completed['GPU_sampling_executed'] is False
    assert completed['game_modules_executed'] is False and completed['producer_source_sha256']==producer_sha
    receipts=[json.loads(line) for line in (root/'resource-batch-receipts.jsonl').read_text().splitlines()]
    assert len(receipts)==len(entries)
    total=0
    for n,(row,receipt) in enumerate(zip(entries,receipts)):
        assert receipt['complete'] and receipt['ordinal']==n and receipt['entry_id']==row['id']
        assert receipt['input_sha256']==row['input']['sha256'] and receipt['declaration_sha256']==row['source']['sha256']
        dest=root/'items'/row['id'];metadata=json.loads((dest/'native-resource.json').read_text())
        assert metadata['complete'] and metadata['contract']=='DM native image producer v1' and metadata['driverID']==18
        assert metadata['original_driver_roundtrip_compressed_blocks_exact'] is True
        assert metadata['GPU_sampling_executed'] is False and metadata['game_modules_executed'] is False
        assert receipt['original_driver_roundtrip_compressed_blocks_exact'] is True
        assert all(metadata[k]==identity[k]==receipt[k] for k in ('layout_UUID','device_UUID','driver_UUID'))
        for leaf,key in [('native-resource.bin','native_payload_sha256'),('native-resource.json','native_metadata_sha256'),('native-resource-layout.bin','layout_record_sha256')]:
            assert sha(dest/leaf)==receipt[key]
        assert metadata['native_payload_sha256']==receipt['native_payload_sha256']
        assert metadata['input_sha256']==receipt['input_sha256'] and metadata['declaration_sha256']==receipt['declaration_sha256']
        assert metadata['producer_source_sha256']==receipt['producer_source_sha256']==producer_sha
        assert metadata['physical_image_bytes']==receipt['physical_image_bytes']==(dest/'native-resource.bin').stat().st_size
        assert metadata['alignment']==receipt['alignment']
        assert all(metadata['recipe'][field]==receipt[key] for field,key in [('VkFormat','format'),('width','width'),('height','height'),('mipLevels','levels')])
        total+=metadata['physical_image_bytes']
    assert total==completed['physical_image_bytes']
    return completed
