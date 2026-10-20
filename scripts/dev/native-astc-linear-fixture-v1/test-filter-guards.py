#!/usr/bin/env python3
"""Source-extracted guards under host mocks; no GPU or device execution."""
import argparse,ast,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--matrix-result',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();here=Path(__file__).resolve().parent;root=here.parents[2];source=here/'run-owned.py';tree=ast.parse(source.read_text());functions=[n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name in('require','validate_rows')];assert len(functions)==2
scope={'SHAPES':[[1,1,1],[5,7,3],[63,65,7],[128,256,9]],'FORMATS':{157,158,165,166},'LAYOUT':'ab91c540847448360a44496b50045159'};exec(compile(ast.Module(body=functions,type_ignores=[]),str(source),'exec'),scope)
# Use prior real nearest-only rows solely as host-test input; added filter rows
# are explicitly synthetic and can never count as real GPU acceptance.
old=json.loads(a.matrix_result.read_text())['actual_rows'];rows=[];selected=False;mapping={0:0,1:1,2:2,6:3}
for row in old:
 if row.get('native_linear_matrix_start')or row.get('native_linear_matrix_terminal'):
  row=copy.deepcopy(row)
  if row.get('native_linear_matrix_terminal'):row['completed_shapes']=4
  rows.append(row);continue
 if row.get('native_linear_shape_begin'):
  selected=row['index']in mapping
  if selected:row=copy.deepcopy(row);row['index']=mapping[row['index']]
 if not selected:continue
 if row.get('native_linear_shape_complete'):row=copy.deepcopy(row);row['index']=mapping[row['index']]
 rows.append(row)
 if row.get('native_all_base_mips_sampled'):
  fmt=row['format'];level=row['levels'];prior=sum(x.get('native_filter_GPU_reference')is True and x.get('format')==fmt for x in rows)
  # Count resets by shape through explicit pre/post position within each import.
  phase='opaque-before-native-B2T'if prior%2==0 else'after-native-B2T'
  rows.append({'native_filter_capability':True,'format':fmt,'phase':phase,'linear_filter_supported':True})
  rows.append(dict(native_filter_GPU_reference=True,format=fmt,width=row['width'],height=row['height'],levels=level,base_views=level,dispatches=level*(level+1),compared_pixels=sum(2*(n+1)*(max(1,row['width']>>n)+2)*(max(1,row['height']>>n)+2)for n in range(level)),every_mip_pixel_plus_edges=True,phase=phase,bilinear_and_trilinear=True,fractional_UV_and_LOD=True,private_optimal_reference=True,same_sampler=True,RGBA_float_bits_exact=True,unused_output_guard=True,CPU_filter_or_decode=False))
proof=scope['validate_rows'](rows,-1);assert proof['linear_trilinear_GPU_reference_floatbits_proven'];checks=['synthetic-full-coverage-positive']
def reject(name,edit):
 bad=copy.deepcopy(rows);edit(bad)
 try:scope['validate_rows'](bad,-1)
 except (AssertionError,KeyError):checks.append(name);return
 raise AssertionError('invalid rows accepted: '+name)
def first(rs,key):return next(x for x in rs if x.get(key))
for key in ('bilinear_and_trilinear','fractional_UV_and_LOD','private_optimal_reference','same_sampler','RGBA_float_bits_exact','unused_output_guard'):
 reject('reject-'+key,lambda rs,k=key:first(rs,'native_filter_GPU_reference').update({k:False}))
reject('reject-CPU-reference',lambda rs:first(rs,'native_filter_GPU_reference').update(CPU_filter_or_decode=True))
reject('reject-unsupported-linear',lambda rs:first(rs,'native_filter_capability').update(linear_filter_supported=False))
reject('reject-missing-mip',lambda rs:first(rs,'native_filter_GPU_reference').update(dispatches=0))
reject('reject-missing-baseMip',lambda rs:first(rs,'native_filter_GPU_reference').update(base_views=0))
reject('reject-no-pre-upload',lambda rs:first(rs,'native_filter_GPU_reference').update(phase='after-native-B2T'))
reject('reject-filter-row-missing',lambda rs:rs.remove(first(rs,'native_filter_GPU_reference')))
reject('reject-original-nearest-row-missing',lambda rs:rs.remove(first(rs,'native_all_base_mips_sampled')))
# The actual production comparison function, not a mirror implementation.
s=(root/'scripts/dev/native-image-payload/native_filter_reference.inc').read_text();start=s.index('static void filter_compare_words(');end=s.index('static void native_filter_reference(',start);function=s[start:end]
with tempfile.TemporaryDirectory(prefix='astc-filter-guard-')as td:
 c=Path(td)/'check.c';binary=Path(td)/'check';c.write_text('#include <stdint.h>\n#include <string.h>\n#include <stdlib.h>\n#define CHECK(x) do{if(!(x))exit(73);}while(0)\n'+function+'\nint main(int argc,char**argv){uint32_t w[9+32];memset(w,0xcc,sizeof w);for(int i=0;i<4;i++)w[i]=w[i+4]=0x3f000000u+i;w[8]=0xf17e0000u;if(argc>1){int n=atoi(argv[1]);w[n]^=1;}filter_compare_words(w,1,41);return 0;}\n')
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 assert subprocess.run([str(binary)]).returncode==0;checks.append('actual-C-compare-positive-ASan-UBSan')
 for n in (0,1,2,3,4,5,6,7,8,9,40):
  assert subprocess.run([str(binary),str(n)]).returncode==73;checks.append('actual-C-reject-word-'+str(n))
original=Path('/home/dl/d2r-mobile/.local/worktrees/native-astc-linear-gpu-fixture-v1/scripts/dev/native-image-payload');new=root/'scripts/dev/native-image-payload'
assert (original/'sample.comp').read_bytes()==(new/'sample.comp').read_bytes();checks.append('original-NEAREST-shader-byte-exact')
def body(text,start,end):return text[text.index(start):text.index(end,text.index(start))]
assert body((original/'fixture.c').read_text(),'static void sample_from_base(','static void sample_all(')==body((new/'fixture.c').read_text(),'static void sample_from_base(','static void sample_all(');checks.append('original-everypixel-NEAREST-checks-byte-exact')
imp=(new/'native_import.inc').read_text();assert imp.index('"opaque-before-native-B2T"')<imp.index('native_linear_transfer_roundtrip(&i,format)')<imp.index('"after-native-B2T"');checks.append('actual-native-callorder-first-filter-before-B2T')
shader=(new/'paired_filter.comp').read_text();assert 'floatBitsToUint(textureLod(native_image' in shader and 'floatBitsToUint(textureLod(private_image' in shader and '.375'not in shader;checks.append('shader-two-real-textureLod-floatbit-outputs')
report={'status':'passed','checks':checks,'check_count':len(checks),'GPU_used':False,'ADB_used':False,'synthetic_rows_are_not_GPU_acceptance':True,'source':str(source),'comparison_source':str(new/'native_filter_reference.inc')};a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
