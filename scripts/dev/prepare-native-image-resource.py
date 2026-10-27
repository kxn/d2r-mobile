#!/usr/bin/env python3
"""Prepare one real, hash-verified CASC ASTC resource for native driver baking.

No encoding, vendor layout implementation, game binary changes or deployment.
Original CASC reader and NVN mip/deswizzle C remain authoritative.
"""
import argparse, ctypes as C, hashlib, json, re, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'dm/tools'),str(ROOT/'dm/ios/tools')]
import astc_repack as repack
import casc_overlay as casc
import romfs_art
FORMATS={95:(0x79,157,4),81:(0x87,158,4),99:(0x7d,165,6),85:(0x8b,166,6)}
BC3_FORMATS={61:(0x44,137,4),62:(0x48,138,4)}
MAX_BYTES=16<<20

def sha(data):return hashlib.sha256(data).hexdigest()
def file_sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def record(path):
    path=Path(path).resolve();return {'path':str(path),'bytes':path.stat().st_size,'sha256':file_sha(path)}
def canonical(data):return json.dumps(data,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()+b'\n'
class SourceContext:
  """One immutable hash-verified CASC/cache context; bounded per-entry payloads."""
  def __init__(self,args):
    indexes=casc.read_prebuilt(args.pack_index)
    original=casc.read_manifest(args.romfs)
    verified={}
    for path in args.pack_manifest:
        manifest=json.loads(path.read_text());name=manifest['name']
        if Path(name).name!=name:raise ValueError('Invalid archive filename')
        data=args.data_dir/name
        if data.stat().st_size!=manifest['size'] or file_sha(data)!=manifest['sha256']:
            raise ValueError('Archive size/hash mismatch: '+str(data))
        if name in verified:raise ValueError('Duplicate archive declaration')
        verified[name]={'manifest':record(path),'archive':{'path':str(data.resolve()),'bytes':data.stat().st_size,'sha256':manifest['sha256']}}
    if args.composition_report:
        composition=json.loads(args.composition_report.read_text())
        if not composition['complete'] or file_sha(args.pack_index)!=composition['merged_index_sha256']:
            raise ValueError('Composition/index fingerprint mismatch')
        for name,digest in composition['deploy_files'].items():
            if not name.startswith('data/') or not name.endswith('.data'):continue
            leaf=Path(name).name;path=args.data_dir/leaf
            if file_sha(path)!=digest:raise ValueError('Composition archive mismatch: '+leaf)
            verified[leaf]={'composition':record(args.composition_report),'archive':{'path':str(path.resolve()),'bytes':path.stat().st_size,'sha256':digest}}
    if not verified:raise ValueError('A verified pack manifest or full composition report is required')
    def read(logical):
        key=casc.norm_path(logical)
        if key not in indexes:raise ValueError('Resource absent from original pack index: '+key)
        ekey,encoded,decoded=indexes[key]
        if casc.data_filename(ekey[9],ekey[10]) not in verified:
            raise ValueError('Indexed archive is not hash-verified')
        if key not in original:raise ValueError('Original entry encoding unavailable')
        encoding=original[key][8]
        value=casc.read_data_file(args.data_dir,ekey,encoded,encoding=encoding)
        if len(value)!=decoded:raise ValueError('CASC decoded size differs')
        return value,{'logical':key,'ekey':ekey.hex(),'encoding':encoding,'encoded_bytes':encoded,'decoded_bytes':decoded,'sha256':sha(value)}
    self.read=read
    cache,self.cache_info=read('data/hd/texture_desc_cache.json')
    self.entries={}
    for entry in json.loads(cache):
        key=casc.norm_path(entry['filename'])
        if key in self.entries:raise ValueError('Duplicate descriptor: '+key)
        self.entries[key]=entry
    tools=[ROOT/'scripts/dev/prepare-native-image-resource.py',ROOT/'scripts/dev/native-image-payload/resource_format.h',ROOT/'dm/tools/astc_repack.py',ROOT/'dm/tools/casc_overlay.py',ROOT/'dm/ios/tools/romfs_art.py',ROOT/'dm/gfx/nvn/nvn_tex.c',ROOT/'dm/gfx/nvn/nvn_tex.h',ROOT/'dm/gfx/tex/nvn_deswizzle.c',ROOT/'dm/gfx/tex/nvn_deswizzle.h']
    self.common={'original_build_manifest':record(args.romfs/'data/build_manifest.psv'),'source_index':record(args.pack_index),'verified_archives':verified,'source_cache':self.cache_info,'tool_inputs':[record(p) for p in tools],'NVN_layout_library':record(Path(romfs_art.lib()._name))}

class OriginalBC3Context:
  """Explicit original-CASC source; no processed pack or ASTC authority."""
  def __init__(self,args):
    if args.pack_index or args.data_dir or args.pack_manifest or args.composition_report:
        raise ValueError('Original BC3 cannot be mixed with processed pack inputs')
    producer=getattr(args,'producer_sha256',None)
    if not isinstance(producer,str) or not re.fullmatch('[0-9a-f]{64}',producer):
        raise ValueError('Original BC3 requires explicit new producer SHA256')
    original=casc.read_manifest(args.romfs)
    verified={};stamps={}
    def stamp(path):
        st=path.stat()
        return st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns
    manifest_path=args.romfs/'data/build_manifest.psv'
    manifest_stamp=stamp(manifest_path)
    def read(logical):
        key=casc.norm_path(logical)
        if key not in original:raise ValueError('Original resource absent from manifest')
        row=original[key];ekey=bytes.fromhex(row[6]);leaf=casc.data_filename(ekey[9],ekey[10])
        archive=args.romfs/'data'/leaf
        if leaf not in verified:
            before=stamp(archive);verified[leaf]={'archive':record(archive)}
            if stamp(archive)!=before:raise ValueError('Original archive changed while hashing')
            stamps[leaf]=before
        if stamp(archive)!=stamps[leaf]:raise ValueError('Original archive changed during source preparation')
        data=casc.read_data_file(args.romfs/'data',ekey,int(row[7]),encoding=row[8])
        if stamp(archive)!=stamps[leaf]:raise ValueError('Original archive changed while reading')
        if len(data)!=int(row[5]) or hashlib.md5(data).hexdigest()!=row[4]:
            raise ValueError('Original resource MD5/decoded size mismatch')
        return data,{'logical':key,'ekey':ekey.hex(),'encoding':row[8],
                     'encoded_bytes':int(row[7]),'decoded_bytes':len(data),'sha256':sha(data),
                     'original_manifest_md5':row[4]}
    def validate_sources():
        if stamp(manifest_path)!=manifest_stamp:raise ValueError('Original manifest changed during source preparation')
        for leaf,expected in stamps.items():
            if stamp(args.romfs/'data'/leaf)!=expected:raise ValueError('Original archive changed after reading')
    self.validate_sources=validate_sources
    self.read=read
    cache,self.cache_info=read('data/hd/texture_desc_cache.json')
    self.entries={}
    for entry in json.loads(cache):
        key=casc.norm_path(entry['filename'])
        if key in self.entries:raise ValueError('Duplicate original descriptor')
        self.entries[key]=entry
    tools=[ROOT/'scripts/dev/prepare-native-image-resource.py',ROOT/'scripts/dev/native-image-payload/resource_format.h',ROOT/'dm/tools/astc_repack.py',ROOT/'dm/tools/casc_overlay.py',ROOT/'dm/ios/tools/romfs_art.py',ROOT/'dm/gfx/nvn/nvn_tex.c',ROOT/'dm/gfx/nvn/nvn_tex.h',ROOT/'dm/gfx/tex/nvn_deswizzle.c',ROOT/'dm/gfx/tex/nvn_deswizzle.h']
    self.common={'source_profile':'original-bc3-raw-casc-v1','intended_producer_sha256':producer,
                 'original_build_manifest':record(args.romfs/'data/build_manifest.psv'),
                 'source_index':record(args.romfs/'data/build_manifest.psv'),
                 'verified_archives':verified,'source_cache':self.cache_info,
                 'tool_inputs':[record(p) for p in tools],'NVN_layout_library':record(Path(romfs_art.lib()._name))}

def prepare(args, context=None):
    if args.output.exists():raise ValueError('Use a new output directory; partial outputs are not resumed')
    original_bc3=getattr(args,'original_bc3',False)
    context=context or (OriginalBC3Context(args) if original_bc3 else SourceContext(args))
    read=context.read
    logical=casc.norm_path(args.logical)
    if logical not in context.entries:raise ValueError('Descriptor missing')
    entry=context.entries[logical]
    formats=BC3_FORMATS if original_bc3 else FORMATS
    if entry['format'] not in formats:raise ValueError('Resource format outside explicitly selected BC3 or ASTC source profile')
    nvn,vk,block=formats[entry['format']]
    data,texture_info=read(logical)
    tab,first,desc,offsets=repack.full_texture(data,entry,nvn_format=nvn)
    if desc.levels>16:raise ValueError('Too many mips')
    compact_bytes=sum(repack.block_bytes(*repack.dimensions(desc.width,desc.height,l),block) for l in range(desc.levels))
    if offsets[-1]>MAX_BYTES or 1088+compact_bytes>MAX_BYTES:
        raise ValueError('Bounded native/source input exceeds 16MiB')
    storage=C.create_string_buffer(offsets[-1])
    linear=[];mips=[]
    for level in range(desc.levels):
        raw=repack.deswizzle(desc,data[first:],level,block)
        # Original swizzle is a layout roundtrip oracle, not a replacement tiler.
        repack.swizzle(desc,storage,level,raw,block)
        w,h=repack.dimensions(desc.width,desc.height,level)
        linear.append(raw);mips.append({'level':level,'width':w,'height':h,'linear_bytes':len(raw),'sha256':sha(raw),'original_NVNmip_offset':offsets[level]})
    # Padding may differ: prove useful compressed blocks, not arbitrary old padding.
    for level,raw in enumerate(linear):
        if repack.deswizzle(desc,storage.raw,level,block)!=raw:raise ValueError('Original NVN retile roundtrip differs')
    if original_bc3 and hasattr(context,'validate_sources'):context.validate_sources()
    declaration={'schema':1,'contract':'DM native resource producer input v1','scope':'real '+('original BC3' if original_bc3 else 'ASTC')+' resource, complete 2D single-layer mip chain; no game read-only authority claimed',**context.common,'source_texture':texture_info,'source_descriptor':entry,'descriptor_sha256':sha(canonical(entry)),'game_format':entry['format'],'NVN_format':nvn,'VkFormat':vk,'block':[block,block],'width':desc.width,'height':desc.height,'levels':desc.levels,'layers':1,'depth':1,'NVN_padded_payload_bytes':offsets[-1],'linear_compressed_bytes':sum(map(len,linear)),'mips':mips,'layout_roundtrip_useful_blocks_exact':True,'decoder_or_encoder_used':False,'game_modules_executed':False,'runtime_immutability_or_publication_contract':'not inferred from file origin; pending NVN flags/Flush/fence protocol'}
    if not original_bc3:
        declaration.pop('linear_compressed_bytes')
        declaration['linear_ASTC_bytes']=sum(map(len,linear))
    manifest=canonical(declaration)
    header_size=1080;cursor=(header_size+63)&~63;payload=bytearray(cursor);rows=[]
    for raw,m in zip(linear,mips):
        rows.append(struct.pack('<IIQQ32s',m['width'],m['height'],cursor,len(raw),bytes.fromhex(m['sha256'])))
        payload+=raw;cursor+=len(raw)
    if len(payload)>MAX_BYTES:raise ValueError('Bounded input exceeds 16MiB')
    header=struct.pack('<8sIIQ8I',b'DMRSRC1\0',1,header_size,len(payload),desc.width,desc.height,desc.levels,vk,block,entry['format'],nvn,0)
    header+=bytes.fromhex(texture_info['sha256'])+bytes.fromhex(declaration['descriptor_sha256'])+hashlib.sha256(manifest).digest()
    header+=b''.join(rows)+bytes(56*(16-len(rows)))+bytes(32)
    assert len(header)==header_size
    payload[:header_size]=header
    payload[header_size-32:header_size]=hashlib.sha256(payload).digest()
    args.output.mkdir(parents=True)
    (args.output/'resource-source.json').write_bytes(manifest)
    (args.output/'resource-input.bin').write_bytes(payload)
    # Original processed texture, including the matched thumbnail/CFT payload.
    # It is packer input only, never retained as another runtime texture copy.
    (args.output/'source.texture').write_bytes(data)
    result={'complete':True,'input':record(args.output/'resource-input.bin'),'declaration':record(args.output/'resource-source.json'),'logical':logical,'VkFormat':vk,'width':desc.width,'height':desc.height,'levels':desc.levels,'linear_compressed_bytes':sum(map(len,linear)),'original_NVNPadded_bytes':offsets[-1],'no_conversion':True,'no_device_or_game_execution':True}
    if not original_bc3:
        result.pop('linear_compressed_bytes')
        result['linear_ASTC_bytes']=sum(map(len,linear))
    (args.output/'prepare-report.json').write_bytes(canonical(result));return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--romfs',type=Path,required=True);p.add_argument('--pack-index',type=Path);p.add_argument('--data-dir',type=Path);p.add_argument('--pack-manifest',type=Path,action='append',default=[]);p.add_argument('--composition-report',type=Path);p.add_argument('--logical',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--original-bc3',action='store_true',help='Explicit original unmodified CASC BC3 source; cannot use a processed pack')
    p.add_argument('--producer-sha256',help='New producer source identity for the explicit original BC3 declaration')
    args=p.parse_args()
    if not args.original_bc3 and (not args.pack_index or not args.data_dir or args.producer_sha256):p.error('ASTC profile requires pack-index/data-dir; producer-sha256 is original-BC3 only')
    print(json.dumps(prepare(args),indent=2))
