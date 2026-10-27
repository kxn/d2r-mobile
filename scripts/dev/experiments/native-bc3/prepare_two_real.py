#!/usr/bin/env python3
"""Prepare two original real BC3 sources with the production explicit reader; no GPU."""
import argparse,hashlib,importlib.util,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--producer',type=Path,required=True);p.add_argument('--romfs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if a.output.exists():raise ValueError('New output required')
ready=json.loads((a.producer/'source-ready.json').read_text());folder=a.producer/'source/scripts/dev/native-image-payload'
for name,digest in ready['CMake_inputs'].items():
 if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=digest:raise ValueError('Producer source changed')
spec=importlib.util.spec_from_file_location('original_bc3_prepare',ROOT/'scripts/dev/prepare-native-image-resource.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
a.original_bc3=True;a.pack_index=None;a.data_dir=None;a.pack_manifest=[];a.composition_report=None;a.producer_sha256=ready['producer_source_sha256']
context=m.OriginalBC3Context(a)
output=a.output.resolve();output.mkdir(parents=True);results={}
for label,logical in [('bc3-unorm','data/hd/character/common/status_mask_tint.texture'),('bc3-srgb','data/hd/character/common/common_dirt_threshold.texture')]:
 a.output=output/label;a.logical=logical;results[label]=m.prepare(a,context)
result={'schema':1,'complete':True,'no_device_execution':True,'no_encoder_or_tiler_added':True,'producer_source_sha256':ready['producer_source_sha256'],'producer_source_ready_sha256':m.file_sha(a.producer/'source-ready.json'),'prepare_tool_sha256':m.file_sha(ROOT/'scripts/dev/prepare-native-image-resource.py'),'script_sha256':m.file_sha(Path(__file__)),'original_RomFS':str(a.romfs.resolve()),'resources':results}
(output/'prepare-two.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
