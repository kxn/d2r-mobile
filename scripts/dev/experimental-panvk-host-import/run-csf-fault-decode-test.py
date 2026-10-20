#!/usr/bin/env python3
"""Run existing Mesa CS binary decoder against bounded owned host mappings.

Requires an original Meson host build of the decoder/util libraries. No GPU,
guest code, CPU interpreter, timestamps or sampling are used.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-build', required=True, type=pathlib.Path)
p.add_argument('--output', required=True, type=pathlib.Path)
p.add_argument('--control-decoder', type=pathlib.Path)
a = p.parse_args()
b = a.host_build.resolve()
o = a.output.resolve()
o.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).with_name('t_csf_fault_decode.c')
entry = next(x for x in json.loads((b/'compile_commands.json').read_text())
             if x['file'].endswith('decode_csf.c') and 'arch-v10' in x['command'])
base = shlex.split(entry['command'])

def command(argv, log):
    (o/(log+'.argv.json')).write_text(json.dumps(argv, indent=2)+'\n')
    with (o/(log+'.log')).open('w') as out:
        subprocess.run(argv, cwd=b, stdout=out, stderr=subprocess.STDOUT, check=True)

argv = base.copy()
argv[argv.index('-o')+1] = str(o/'probe.o')
argv[argv.index('-c')+1] = str(source)
command(argv, 'compile')
libs = ['src/panfrost/genxml/libpanfrost_decode.a',
        'src/panfrost/genxml/libpandecode-arch-v10.a',
        'src/util/libmesa_util.a', 'src/c11/impl/libmesa_util_c11.a']
link = ['c++', '-Wl,--gc-sections', '-o', str(o/'probe'), str(o/'probe.o'),
        '-Wl,--start-group', *[str(b/x) for x in libs], '-Wl,--end-group',
        '-lm', '-pthread', '-lz', '-ldl']
command(link, 'link')
with (o/'probe.log').open('w') as out:
    result = subprocess.run([str(o/'probe')], cwd=o, stdout=out,
                            stderr=subprocess.STDOUT,
                            env={**os.environ, 'PANDECODE_DUMP_FILE': str(o/'decoded')})
assert result.returncode == 0, result.returncode
text = ''.join(x.read_text() for x in o.glob('decoded.*'))
assert text.count('Cannot decode CS range') == 5
assert 'MOVE32 r42, #0xDEADBEEF' in text and 'CALL d0, r2' in text
assert 'main_cs@5efe08bff8' in text and 'main_cs@5efe07c000' in text
report = {'returncode': 0, 'actual_Mesa_decoder': True, 'checks': 24,
          'owned_mapping_rejections': 5, 'GPU_or_guest_execution': False,
          'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
if a.control_decoder:
    argv = base.copy()
    argv[argv.index('-o')+1] = str(o/'old-decoder.o')
    argv[argv.index('-c')+1] = str(a.control_decoder.resolve())
    command(argv, 'control-compile')
    argv = link.copy()
    argv[argv.index('-o')+1] = str(o/'control')
    argv.insert(argv.index('-Wl,--start-group'), str(o/'old-decoder.o'))
    command(argv, 'control-link')
    with (o/'control.log').open('w') as out:
        rc = subprocess.run([str(o/'control')], cwd=o, stdout=out,
                            stderr=subprocess.STDOUT,
                            env={**os.environ, 'PANDECODE_DUMP_FILE': str(o/'control-decoded')}).returncode
    assert rc == -11, rc
    report['control_original_decoder_SIGSEGV'] = rc
    report['control_sha256'] = hashlib.sha256(a.control_decoder.read_bytes()).hexdigest()
(o/'result.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report))
