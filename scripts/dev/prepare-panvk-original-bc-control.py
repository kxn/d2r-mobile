#!/usr/bin/env python3
"""Prove original BC CASC identity and current full4 UI equivalence; copy only four overlay files."""
from pathlib import Path
import argparse,collections,hashlib,importlib.util,io,json,shutil,struct,tarfile
R=Path('/home/dl/d2r-mobile');P=argparse.ArgumentParser();P.add_argument('--output',type=Path,required=True);a=P.parse_args()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def rec(p):p=Path(p).resolve();return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
proof=R/'.local/logs/panvk-original-fork-ui-only-resource-proof-v2.json';assert sha(proof)=='76438192aedb73d5457901f4284581d1db6ff1d9db3aafbc3e80bd429d2ab1e5';old=json.loads(proof.read_text())
for r in old['source_tools']:assert sha(r['path'])==r['sha256']
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
c=load('original_casc_overlay',old['source_tools'][0]['path']);touch=load('original_touch_layout',old['source_tools'][1]['path'])
base=Path(old['base']);bc=Path(old['overlay']);full=R/'.local/build/android-astc-full/deploy-overlay'
promotion=R/'.local/logs/image-binding-022d625-default-promote-mali-v1/root-result.json';prod=json.loads(promotion.read_text());expected={n.removeprefix('files/runtime/romfs_overlay/'):h for n,h in prod['promoted_module_and_resource_hashes'].items()if n.startswith('files/runtime/romfs_overlay/')}
assert set(expected)=={'ctrl_touch','touch_strings.tsv','pack.json','data/.build.config','data/00-00000007.data','data/00-00000008.data'}
for n,h in expected.items():assert sha(full/n)==h,n
sourcefiles={r['relative']:r for r in old['overlay_files']};assert len(sourcefiles)==4
for n,row in sourcefiles.items():assert sha(bc/n)==row['sha256']
for n in ['ctrl_touch','touch_strings.tsv']:assert sourcefiles[n]['sha256']==expected[n]
original,original_bc,current_full=[c.load_vfs1(root)for root in [base,bc,full]]
def tables(v):
 assert v[:4]==b'TVFS'and v[8:12]==b'\0\0\0\x0a'
 po,ps,vo,vs,co,cs=struct.unpack('>IIIIII',v[12:36]);eo,es=struct.unpack('>II',v[38:46]);assert es==13 and cs%25==0
 return {'paths':(po,ps),'files':(vo,vs),'cft':(co,cs),'espec':(eo,es)}
def segment(v,t,key):o,n=t[key];return bytes(v[o:o+n])
ot,bt,ft=map(tables,[original,original_bc,current_full]);manifest=c.read_manifest(base)
def paths(v,t):o,n=t['paths'];return {name:value for _,name,value,folder,_ in c.walk_paths(v,o,o+n)if folder is None}
op,bp,fp=paths(original,ot),paths(original_bc,bt),paths(current_full,ft)
assert len(op)==132412 and len(bp)==len(fp)==133200 and all(bp[n]==x for n,x in op.items())and bp==fp
assert segment(original,ot,'files')==segment(original_bc,bt,'files')[:ot['files'][1]]
assert segment(original_bc,bt,'files')==segment(current_full,ft,'files')
assert segment(original,ot,'espec')==segment(original_bc,bt,'espec')==segment(current_full,ft,'espec')
# Reuse the documented existing set_cft_sizes 16+4+1+4 record format;
# preserve positions (not just searching the new table for matching bytes).
oc,bcft,fc=segment(original,ot,'cft'),segment(original_bc,bt,'cft'),segment(current_full,ft,'cft')
key_positions=collections.defaultdict(list)
for off in range(0,len(oc),25):key_positions[oc[off:off+16]].append(off)
selected=set()
for logical,row in manifest.items():
 if not(logical.endswith('.texture')or logical=='data/hd/texture_desc_cache.json'):continue
 positions=key_positions[bytes.fromhex(row[6])];assert len(positions)==1,logical
 off=positions[0];assert oc[off:off+25]==bcft[off:off+25],logical;selected.add(off)
assert len(selected)==31093
nontexture=[off for off in range(0,len(bcft),25)if off not in selected]
assert len(nontexture)==102107 and all(bcft[o:o+25]==fc[o:o+25]for o in nontexture)
cache=touch._read_entry(base,manifest['data/hd/texture_desc_cache.json']);assert hashlib.sha256(cache).hexdigest()==old['cache_original_sha256']and len(cache)==old['cache_original_bytes']
formats={str(k):v for k,v in sorted(collections.Counter(row['format']for row in json.loads(cache)).items())};assert formats==old['original_cache_format_counts']
a.output.mkdir(parents=True,exist_ok=False);overlay=a.output/'romfs_overlay';overlay.mkdir();files={}
for n,row in sorted(sourcefiles.items()):
 p=overlay/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(bc/n,p);assert sha(p)==row['sha256'];files[n]=row['sha256']
# A deterministic, regular-file-only four-file transfer; no RomFS links or payload traversal.
archive=a.output/'resources.tar'
with tarfile.open(archive,'w')as tar:
 for n in sorted(files):
  content=(overlay/n).read_bytes();i=tarfile.TarInfo(n);i.size=len(content);i.mode=0o600;i.uid=i.gid=i.mtime=0;tar.addfile(i,io.BytesIO(content))
report={'schema':1,'complete':True,'deploy_allowed':False,'game_acceptance':False,'scope':'original BC/native-layout semantic baseline with current identical upstream UI; not ASTC performance or shared-import proof','old_resource_proof':rec(proof),'promotion':rec(promotion),'source_tools':[rec(x['path'])for x in old['source_tools']],'preparer':rec(__file__),'source_base':str(base),'source_BC_overlay':str(bc),'current_full4_host_overlay':str(full),'current_full4_expected_files':expected,'original_manifest':rec(base/'data/build_manifest.psv'),'source_configs':[rec(root/'data/.build.config')for root in [base,bc,full]],'original_VFS_sha256':hashlib.sha256(original).hexdigest(),'BC_VFS_sha256':hashlib.sha256(original_bc).hexdigest(),'full4_VFS_sha256':hashlib.sha256(current_full).hexdigest(),'proofs':{'existing_original_paths_same_filetable_offsets':132412,'new_upstream_UI_paths':788,'original_filetable_prefix_identical':True,'BC_and_current_full4_entire_filetable_identical':True,'original_texture_and_cache_CFT_entries_identical':31093,'other_including_all_UI_CFT_entries_current_full4_identical':102107,'espec_identical':True,'ctrl_touch_and_strings_current_Mali_identical':True,'texture_reencoding':False,'recursive_RomFS_copy_or_symlink':False},'original_cache':{'bytes':len(cache),'sha256':hashlib.sha256(cache).hexdigest(),'formats':formats},'deploy_overlay':str(overlay.resolve()),'deploy_files':files,'transfer_archive':rec(archive),'inactive_ASTC_archive8_and_tool_pack_manifest_omitted':True,'candidate_file_count':4,'original_BC_payload_source':'Unchanged original CASC CFT entries/archives from source_base; overlay replaces only original upstream UI and TVFS, not texture payloads.','device_access':False}
p=a.output/'composition.json';p.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(rec(p)))
