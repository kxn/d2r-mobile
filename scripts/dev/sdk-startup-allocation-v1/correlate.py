"""Same-PID captured return extents versus later original-SDK partition.
Reuses the original decoder and saved bytes; no new acquisition or lifetimes oracle.
"""
import importlib.util
import hashlib
from pathlib import Path

def require(ok,why):
    if not ok:raise ValueError(why)

def correlate(startup, snapshot):
    require(startup['PID']==snapshot['PID'] and startup['ELF_sha256']==snapshot['ELF_sha256'], 'cross-PID/ELF partition correlation rejected')
    require(snapshot.get('complete') is True and snapshot.get('partition_complete') is True,
            'incomplete original SDK partition')
    require(snapshot.get('nvn_pools',{}).get('complete') is True, 'current Pool registry is unavailable')
    spec=importlib.util.spec_from_file_location('unchanged_sdk_partition_decoder',Path(__file__).parent/'sdk_free_resident.py')
    sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
    raw={}
    for row in snapshot['raw_files']:
        p=Path(row['path']); b=p.read_bytes()
        require(len(b)==row['bytes'] and hashlib.sha256(b).hexdigest()==row['sha256'], 'saved raw bytes changed '+str(p))
        require(p.name not in raw, 'duplicate raw basename');raw[p.name]=b
    def read(address,size):
        out=bytearray()
        while size:
            base=address&-4096;n=min(size,4096-(address-base))
            page=raw.get('span-metadata-'+format(base,'x')+'.bin')
            require(page is not None and len(page)==4096, 'missing SDK metadata page')
            out.extend(page[address-base:address-base+n]);address+=n;size-=n
        return bytes(out)
    spans=sdk.decode_spans(snapshot['central'],snapshot['central_pages'],raw['span-boundary-table.bin'],read)
    pools=snapshot['nvn_pools']['pools']; rows=[]
    for event in startup['returns']:
        ptr,size=event['pointer'],event['requested_bytes'];end=ptr+size
        matches=[s for s in spans if ptr and s['begin']<=ptr and end<=s['end']]
        owner=[dict(identity=p['identity'],begin=max(ptr,p['guest']),end=min(end,p['guest']+p['size']))
               for p in pools if ptr and max(ptr,p['guest'])<min(end,p['guest']+p['size'])]
        rows.append(dict(return_stop_id=event['stop_id'],pointer=ptr,requested_bytes=size,
                         later_containing_span=matches[0] if len(matches)==1 else None,
                         current_Pool_intersections=owner,
                         limitation='Later address containment only: intervening free/reuse is not tracked; not object type or reclaimability.'))
    return dict(PID=startup['PID'],partition_stop_id=snapshot['stop_id'],same_PID=True,
                decoded_partition_span_count=len(spans),returns=rows,raw_hashes_verified=True,
                no_acquisition=True,lifetime_continuity_proven=False)
