#!/usr/bin/env python3
"""Add positive actual-resident/lazy assertions to the passed NVN transactions."""
import hashlib,json
from pathlib import Path
HERE=Path(__file__).resolve().parent
outputs={}
for base,name in [('run_nvn_fixture.py','run_resident_nvn_fixture.py'),('run_tiled_nvn_fixture.py','run_tiled_resident_nvn_fixture.py')]:
 p=HERE/base;s=p.read_text();anchor="assert len(m['native_library_sha256'])==6";assert s.count(anchor)==1
 s=s.replace(anchor,anchor+"\nassert m['apk_sha256']=='72ad51f6c8ac04fe49ec2fb391a0d61d8595ec8edb1db2e2e09d8c44aa310066'\nassert m['native_library_sha256']['libdmhost_android.so']=='7a37a24221e269c36c640c7bef56fd6b0ad0bbb9c66ec13317a3777860543497'")
 anchor="  config=[r for r in rows if r.get('native_resource_app_configuration') is True]";assert s.count(anchor)==1
 checks="""  owners=[r for r in rows if r.get('native_resource_owner_check') is True]
  assert len(owners)==1 and owners[0]['resident_expected'] is True and owners[0]['all_sampled_mips_checked'] is True
  lazy=[r for r in rows if r.get('native_resource_lazy_check') is True]
  assert len(lazy)==1 and lazy[0]['expected'] is True and lazy[0]['active'] is True and lazy[0]['window_bytes']==65536
  allocations=[r for r in rows if r.get('native_allocation_lane') is True]
  assert len(allocations)==1 and allocations[0]['window_two_unions'] is False
  assert allocations[0]['same_physical_pages_as_Buffer_imports'] is True and allocations[0]['no_payload_copy'] is True and allocations[0]['last_release_collected'] is True
"""
 s=s.replace(anchor,checks+anchor)
 s=s.replace('no window/lazy, no game/performance claim','positive per-sampled-mip owner2 and active64KiB lazy assertions; no game/performance claim')
 path=HERE/name;path.write_text(s);outputs[name]={'base':str(p),'base_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'controller_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(HERE/'resident-controller-source.json').write_text(json.dumps({'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'outputs':outputs,'scope':'Same cold ownership transaction and original NVN semantics. Exact same72ad APK for two exact drivers; require actual per-image owner2 and lazyactive/window checks, not flags alone. No rebuild/loader/timing added.'},indent=2)+'\n')
