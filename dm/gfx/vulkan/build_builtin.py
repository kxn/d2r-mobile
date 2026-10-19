#!/usr/bin/env python3
"""离线编译并验证宿主内置 GLSL，生成带来源记录的 C 头。
Compile and validate host GLSL offline, emitting a C header and provenance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--glslang', required=True)
parser.add_argument('--validator', required=True)
parser.add_argument('--symbol', default='dm_bright_spv')
parser.add_argument('--define', action='append', default=[])
args = parser.parse_args()
if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.symbol):
    parser.error('invalid C symbol')
for define in args.define:
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?', define):
        parser.error('invalid shader define')
args.output.parent.mkdir(parents=True, exist_ok=True)
spv = args.output.with_suffix('.spv')
subprocess.run([args.glslang, '-V', '--target-env', 'vulkan1.1',
                *['-D'+value for value in args.define], '-o', str(spv), str(args.source)], check=True)
subprocess.run([args.validator, '--target-env', 'vulkan1.1', str(spv)], check=True)
data = spv.read_bytes()
words = struct.unpack('<' + 'I' * (len(data) // 4), data)
header = '// Generated from host GLSL; do not edit.\n#pragma once\n'
header += f'static const uint32_t {args.symbol}[] = {{\n'
header += '\n'.join('  ' + ','.join(f'0x{w:08x}' for w in words[i:i+8]) + ','
                    for i in range(0, len(words), 8)) + '\n};\n'
args.output.write_text(header)
digest = lambda b: hashlib.sha256(b).hexdigest()
record = {'target': 'vulkan1.1', 'source': args.source.name, 'symbol': args.symbol, 'defines': args.define,
          'source_sha256': digest(args.source.read_bytes()),
          'builder_sha256': digest(Path(__file__).read_bytes()),
          'spirv_sha256': digest(data), 'header_sha256': digest(header.encode()),
          'glslang': subprocess.check_output([args.glslang, '--version'], text=True),
          'validator': subprocess.check_output([args.validator, '--version'], text=True)}
args.output.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')
