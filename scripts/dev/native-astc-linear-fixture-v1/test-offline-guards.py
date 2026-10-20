#!/usr/bin/env python3
"""No device/GPU: rejection guards for actual-row contract and owned installer UI."""
import ast,copy,json,runpy,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
source=(HERE/'run-owned.py').read_text();tree=ast.parse(source);selected=[]
for n in tree.body:
 if isinstance(n,ast.FunctionDef)and n.name in ('require','validate_rows'):selected.append(n)
ns={};exec(compile(ast.Module(selected,type_ignores=[]),str(HERE/'run-owned.py'),'exec'),ns)
ns.update(LAYOUT='ab91c540847448360a44496b50045159',FORMATS={157,158,165,166},SHAPES=[[1,1,1],[5,7,3],[63,65,7],[128,256,9]])
def rows(index=3):
 w,h,levels=ns['SHAPES'][index];out=[dict(native_linear_matrix_start=True,uid=10123,pid=1234,shape_selector=index,real_resource_bake=False,game_modules_executed=False),dict(native_device_identity=True,layout_UUID=ns['LAYOUT'],driverID=18,vendorID=0x5143,device_UUID='1'*32,driver_UUID='2'*32),dict(native_linear_shape_begin=True,index=index,width=w,height=h,levels=levels)]
 for fmt in [157,165,158,166]:
  for version in [0,1]:out.append(dict(native_plain_hostcopy_roundtrip=True,format=fmt,pattern=version,levels=levels,every_mip_compressed_blocks_exact=True))
  out.append(dict(native_old_AA_opaque_identity_refused=True,format=fmt,production_table_rejection_proven=False))
  for lifecycle in [0,1]:
   for pattern in [0,1]:
    sample=dict(native_all_base_mips_sampled=True,format=fmt,width=w,height=h,levels=levels,base_views=levels,allpixel_assertions_retained=True)
    def filtered(phase):
     return [dict(native_filter_capability=True,format=fmt,phase=phase,linear_filter_supported=True),dict(native_filter_GPU_reference=True,format=fmt,width=w,height=h,levels=levels,base_views=levels,dispatches=levels*(levels+1),compared_pixels=sum(2*(n+1)*(max(1,w>>n)+2)*(max(1,h>>n)+2)for n in range(levels)),every_mip_pixel_plus_edges=True,phase=phase,bilinear_and_trilinear=True,fractional_UV_and_LOD=True,private_optimal_reference=True,same_sampler=True,RGBA_float_bits_exact=True,unused_output_guard=True,CPU_filter_or_decode=False)]
    out += [sample.copy()]+filtered('opaque-before-native-B2T')+[dict(native_all_mip_buffer_copy=True,format=fmt,width=w,height=h,levels=levels,B2T_T2B_blocks_exact=True),sample.copy()]+filtered('after-native-B2T')+[dict(native_import_sampled=True,format=fmt,lifecycle=lifecycle,pattern=pattern,memory_flags=15,extra_image_allocation=False)]
 out+=[dict(native_linear_shape_complete=True,index=index,width=w,height=h,levels=levels,formats=4),dict(native_linear_matrix_terminal=True,uid=10123,pid=1234,rc=0,completed_shapes=1,real_resource_bake=False,GPU_sampling_requested=True,game_modules_executed=False)];return out
count=0
for n in range(4):ns['validate_rows'](rows(n),n);count+=1
for name,mutator in [
 ('missing-terminal',lambda r:r.pop()),('wrong-device-UUID-layout',lambda r:r[1].update(layout_UUID='4529bbb870cdbdd02db214742b3e4170')),
 ('CPU-device-vendor',lambda r:r[1].update(vendorID=0)),('wrong-PID',lambda r:r[-1].update(pid=4321)),
 ('false-allpixel',lambda r:next(x for x in r if x.get('native_all_base_mips_sampled')).update(allpixel_assertions_retained=False)),
 ('base-view-gap',lambda r:next(x for x in r if x.get('native_all_base_mips_sampled')).update(base_views=1)),
 ('missing-buffer-copy',lambda r:r.remove(next(x for x in r if x.get('native_all_mip_buffer_copy')))),
 ('false-buffer-bytes',lambda r:next(x for x in r if x.get('native_all_mip_buffer_copy')).update(B2T_T2B_blocks_exact=False)),
 ('missing-initial-sample',lambda r:r.remove(next(x for x in r if x.get('native_all_base_mips_sampled')))),
 ('upload-before-first-sample',lambda r:reorder(r)),
 ('wrong-lifecycle',lambda r:next(x for x in r if x.get('native_import_sampled')).update(lifecycle=1)),
 ('extra-image-memory',lambda r:next(x for x in r if x.get('native_import_sampled')).update(extra_image_allocation=True)),
 ('missing-hostcopy',lambda r:r.remove(next(x for x in r if x.get('native_plain_hostcopy_roundtrip')))),
 ('false-hostcopy',lambda r:next(x for x in r if x.get('native_plain_hostcopy_roundtrip')).update(every_mip_compressed_blocks_exact=False)),
 ('wrong-shape',lambda r:r[2].update(width=129)),('wrong-four-formats',lambda r:r[-2].update(formats=3))]:
 def reorder(r):
  a=next(i for i,x in enumerate(r)if x.get('native_all_base_mips_sampled'));b=next(i for i,x in enumerate(r)if x.get('native_all_mip_buffer_copy'));r[a],r[b]=r[b],r[a]
 r=rows()
 try:mutator(r);ns['validate_rows'](r,3)
 except (AssertionError,KeyError):count+=1
 else:raise AssertionError('accepted negative '+name)
sys.path.insert(0,str(HERE));import owned_installer
xml='<hierarchy><node package="com.android.packageinstaller" text="D2R ASTC Filter GPU Fixture"/><node package="com.android.packageinstaller" resource-id="android:id/button1" text="继续安装" enabled="true" bounds="[0,0][100,100]"/></hierarchy>'
assert len(owned_installer.actions(xml))==1;count+=1
for bad in [xml.replace('D2R ASTC Filter GPU Fixture','Other'),xml.replace('com.android.packageinstaller','other'),xml.replace('[0,0][100,100]','[1,1][0,0]'),'<not xml']:
 assert not owned_installer.actions(bad);count+=1
print(json.dumps({'passed':True,'positive_4_shapes_and_negative_guards':count,'GPU_used':False,'device_access':False,'scope':'controller/installer refusal guards, synthetic rows are not runtime or GPU evidence'}))
