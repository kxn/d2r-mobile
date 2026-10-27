#!/usr/bin/env python3
"""Bind the unchanged bulk transaction and compact inputs to a fresh tiled driver."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4];BASE=Path(__file__).with_name('run_bulk.py');s=BASE.read_text();build=ROOT/'.local/build/turnip-native-bc3-tiled-v1/build.json';d=json.loads(build.read_text());DS='3e9376ed3759af2cd015cf5608274f661ba3b33df6e95cb6fa2542666f34ee50';assert d['complete']and d['driver']['sha256']==DS==hashlib.sha256(Path(d['driver']['path']).read_bytes()).hexdigest()
before="assert sha(a.driver)=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'";assert s.count(before)==1;s=s.replace(before,"assert sha(a.driver)=='"+DS+"'")
before="assert ready['native_libraries']==m['native_library_sha256'] and ready['driver']['sha256']==sha(a.driver)";assert s.count(before)==1
s=s.replace(before,f'''assert ready['native_libraries']==m['native_library_sha256']
# Keep the immutable carrier's original driver provenance, and separately bind
# this new experiment's driver build. Never relabel its old exported payloads.
assert ready['driver']['sha256']=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
new_build=R/{str(build.relative_to(ROOT))!r}
assert sha(new_build)=={hashlib.sha256(build.read_bytes()).hexdigest()!r}
new_driver=json.loads(new_build.read_text());assert new_driver['complete'] and new_driver['driver']['sha256']==sha(a.driver)''')
before="assert expected_query['producer_source_sha256']==PRODUCER_SHA";assert s.count(before)==1;s=s.replace(before,before+"\nassert expected_query['layout_UUID']=='4504211f903049f5ae3acd7eda74b7bb'")
s=s.replace("'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),","'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),'new_driver_build_sha256':sha(new_build),'carrier_prior_driver_sha256':ready['driver']['sha256'],")
s=s.replace('ONE installation, explicit finite cold original BC3 UNORM/sRGB batches','New exact native-tiled driver, fresh opaque payloads. ONE installation, explicit finite cold original BC3 UNORM/sRGB batches')
path=Path(__file__).with_name('run_tiled_bulk.py');path.write_text(s)
Path(__file__).with_name('tiled-bulk-controller-source.json').write_text(json.dumps({'base':str(BASE),'base_sha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'controller_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'driver_build_sha256':hashlib.sha256(build.read_bytes()).hexdigest(),'driver_sha256':DS,'scope':'Same passed bulk transaction, APK/919 producer and original compact6217 inputs. Fresh new-driver queries/opaque output only; no old payload identity rewritten.'},indent=2)+'\n');print(path)
