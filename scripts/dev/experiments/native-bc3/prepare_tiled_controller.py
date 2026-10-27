#!/usr/bin/env python3
"""Bind the passed NPOT/reference carrier to a new exact tiled-driver candidate."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4];BASE=Path(__file__).with_name('run_reference_probe.py');s=BASE.read_text();driver=ROOT/'.local/build/turnip-native-bc3-tiled-v1/build.json';d=json.loads(driver.read_text());SHA='3e9376ed3759af2cd015cf5608274f661ba3b33df6e95cb6fa2542666f34ee50'
assert d['complete'] and d['driver']['sha256']==SHA and hashlib.sha256(Path(d['driver']['path']).read_bytes()).hexdigest()==SHA
before="assert sha(a.driver)=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'";assert s.count(before)==1;s=s.replace(before,"assert sha(a.driver)=='"+SHA+"'")
before="assert ready['native_libraries']==m['native_library_sha256'] and ready['driver']['sha256']==sha(a.driver)";assert s.count(before)==1
after=f'''assert ready['native_libraries']==m['native_library_sha256']
# The APK's original 0e53 provenance remains intact. Only this independent run
# binds a new driver, new real query and new opaque exports.
assert ready['driver']['sha256']=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
tiled_build=R/{str(driver.relative_to(ROOT))!r}
assert sha(tiled_build)=={hashlib.sha256(driver.read_bytes()).hexdigest()!r}
new_build=json.loads(tiled_build.read_text());assert new_build['complete'] and new_build['driver']['sha256']==sha(a.driver)'''
s=s.replace(before,after)
before="assert driver_build['complete'] and driver_build['driver']['sha256']==sha(a.driver)";assert s.count(before)==1;s=s.replace(before,"assert driver_build['complete'] and driver_build['driver']['sha256']==ready['driver']['sha256']")
s=s.replace("'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),","'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),'new_driver_build_sha256':sha(tiled_build),'immutable_carrier_original_driver_sha256':ready['driver']['sha256'],")
s=s.replace('BC3-only13x9/4mip with midtone565 sRGB CPU oracle and exact ordinary nonexternal VkImage GPU sample-byte reference. Original NVN real-resource sampling NOT covered.','New exact native-tiled driver candidate. BC3-only13x9/4mip NPOT/reference may use linear fallback; NOT proof of TILE3. Fresh real256x256/9mip opaque bakes; original NVN per-baseMip GPU acceptance remains separate.')
path=Path(__file__).with_name('run_tiled_probe.py');path.write_text(s)
Path(__file__).with_name('tiled-controller-source.json').write_text(json.dumps({'base':str(BASE),'base_sha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'controller_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'new_driver_build':str(driver),'new_driver_build_sha256':hashlib.sha256(driver.read_bytes()).hexdigest(),'driver_sha256':SHA,'scope':'Same immutable APK and compact source bytes; new exact driver query/bake. No old opaque payload, UUID or previous driver receipt relabeled.'},indent=2)+'\n');print(path)
