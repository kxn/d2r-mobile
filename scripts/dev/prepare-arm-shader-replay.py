#!/usr/bin/env python3
"""Create a diagnostic Arm/GFXR replay with one shader module replaced.

This edits a derived tool capture, never a game binary or deployed shader cache.
All other capture blocks must remain byte-identical. Pixel comparison and actual
game performance are separate acceptance steps; replay FPS is not game FPS.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--original-spv',type=Path,required=True)
    parser.add_argument('--replacement-spv',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.input.suffix!='.gfxr' or args.output.suffix!='.gfxr' or args.output.exists() or args.report.exists():
        parser.error('input must be a GFXR tool capture; output/report must be new')
    original=args.original_spv.read_bytes();replacement=args.replacement_spv.read_bytes()
    for path,code in [(args.original_spv,original),(args.replacement_spv,replacement)]:
        if len(code)<20 or len(code)%4 or code[:4]!=bytes.fromhex('03022307'):
            raise SystemExit('invalid SPIR-V: '+str(path))
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(path)],check=True)
    if original==replacement:raise SystemExit('shader replacement is identical')
    lz=ctypes.CDLL('liblz4.so.1')
    lz.LZ4_decompress_safe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]

    def file_header(stream):
        header=stream.read(16)
        if len(header)!=16 or header[:4]!=b'GFXR':raise ValueError('invalid GFXR header')
        options=struct.unpack_from('<I',header,12)[0]
        if options>1024:raise ValueError('too many capture options')
        data=stream.read(options*8)
        if len(data)!=options*8:raise ValueError('truncated capture options')
        return header+data

    def block(stream):
        header=stream.read(12)
        if not header:return None
        if len(header)!=12:raise ValueError('truncated block header')
        length,kind=struct.unpack('<QI',header)
        if length>1<<30:raise ValueError('unreasonable capture block')
        payload=stream.read(length)
        if len(payload)!=length:raise ValueError('truncated capture block')
        return header,kind,payload

    changes=[];blocks=0
    args.output.parent.mkdir(parents=True,exist_ok=True)
    try:
        with args.input.open('rb') as source,args.output.open('xb') as destination:
            destination.write(file_header(source))
            while (entry:=block(source)) is not None:
                blocks+=1;header,kind,payload=entry
                if kind&0x7fffffff==4 and len(payload)>=12 and struct.unpack_from('<I',payload)[0]==0x1103b:
                    data=payload[12:]
                    if kind&0x80000000:
                        length=struct.unpack_from('<Q',data)[0]
                        if length>32<<20:raise ValueError('unreasonable shader block')
                        out=ctypes.create_string_buffer(length);compressed=data[8:]
                        if lz.LZ4_decompress_safe(compressed,out,len(compressed),length)!=length:
                            raise ValueError('shader block decompression failed')
                        data=out.raw
                    length=struct.unpack_from('<Q',data,32)[0]
                    if data[60:60+length]==original:
                        if struct.unpack_from('<Q',data,52)[0]!=len(original)//4:
                            raise ValueError('shader code-array count differs')
                        if struct.unpack_from('<i',data,len(data)-4)[0]!=0:
                            raise ValueError('original shader creation failed')
                        revised=bytearray(data[:60]+replacement+data[60+length:])
                        struct.pack_into('<Q',revised,32,len(replacement))
                        struct.pack_into('<Q',revised,52,len(replacement)//4)
                        changed=payload[:12]+revised
                        destination.write(struct.pack('<QI',len(changed),kind&0x7fffffff));destination.write(changed)
                        changes.append({'block':blocks,'module':struct.unpack_from('<Q',data,len(data)-12)[0]})
                        continue
                destination.write(header);destination.write(payload)
        if len(changes)!=1:raise ValueError('expected exactly one matching shader creation')
        with args.input.open('rb') as source,args.output.open('rb') as destination:
            if file_header(source)!=file_header(destination):raise ValueError('file header changed')
            for index in range(1,blocks+1):
                a,b=block(source),block(destination)
                if a is None or b is None:raise ValueError('block count changed')
                if index!=changes[0]['block'] and a!=b:raise ValueError('an unrelated capture block changed')
            if block(source) is not None or block(destination) is not None:raise ValueError('capture suffix changed')
    except BaseException:
        args.output.unlink(missing_ok=True)
        raise
    report={'input':str(args.input),'input_sha256':sha(args.input),'output':str(args.output),
            'output_sha256':sha(args.output),'original_spv':str(args.original_spv),'original_spv_sha256':sha(args.original_spv),
            'replacement_spv':str(args.replacement_spv),'replacement_spv_sha256':sha(args.replacement_spv),
            'changed_blocks':changes,'total_blocks':blocks,'all_other_blocks_byte_identical':True,
            'production_game_or_shader_cache_modified':False,'acceptance':'diagnostic derivative; pixel validation and game FPS still required',
            'script_sha256':sha(Path(__file__))}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['changed_blocks','all_other_blocks_byte_identical','output_sha256']}))


if __name__=='__main__':main()
