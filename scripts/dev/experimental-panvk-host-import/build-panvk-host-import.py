#!/usr/bin/env python3
"""Verify fixed sources, then invoke the original PanVK Android builder."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
LOCK = json.loads((HERE / 'source-lock.json').read_text())

def sha(path):
    with pathlib.Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def run(argv, **kwargs):
    return subprocess.check_output([str(item) for item in argv], **kwargs)

def git(root, *args):
    return run(['git', '-C', root, *args])

def info(path):
    path = pathlib.Path(path).absolute()
    return {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size}

def require(condition, message):
    if not condition:
        raise SystemExit(message)

def verify_mesa(mesa):
    require(git(mesa, 'rev-parse', 'HEAD').decode().strip() == LOCK['mesa_commit'], 'Mesa commit mismatch')
    require(hashlib.sha256(git(mesa, 'diff', '--binary', 'HEAD')).hexdigest() == LOCK['mesa_tracked_diff_sha256'], 'Mesa tracked source differs from fixed base + original patches + host import patch')
    others = {}
    for raw in git(mesa, 'ls-files', '--others', '--exclude-standard', '-z').split(b'\0'):
        if raw:
            name = os.fsdecode(raw)
            if (mesa / name).is_file():
                others[name] = sha(mesa / name)
    require(others == LOCK['mesa_untracked_files'], 'Mesa overlay set/hash mismatch')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--checkout', type=pathlib.Path, required=True)
parser.add_argument('--mesa', type=pathlib.Path, required=True)
parser.add_argument('--host-tools', type=pathlib.Path, required=True)
parser.add_argument('--ndk', type=pathlib.Path, required=True)
parser.add_argument('--build-dir', type=pathlib.Path, required=True)
parser.add_argument('--dist-dir', type=pathlib.Path, required=True)
parser.add_argument('--prepare', action='store_true', help='Apply original stack and extension patch to a clean pinned Mesa checkout')
parser.add_argument('--check-only', action='store_true')
args = parser.parse_args()
for key, value in vars(args).items():
    if isinstance(value, pathlib.Path):
        setattr(args, key, value.resolve())
require(sha(HERE / '0001-csf-host-import-ext.patch') == LOCK['host_import_patch_sha256'], 'Extension patch hash mismatch')
require(sha(HERE / '0000-original-builder-jobs.patch') == LOCK['builder_jobs_patch_sha256'], 'Builder patch hash mismatch')
require(git(args.checkout, 'rev-parse', 'HEAD').decode().strip() == LOCK['patch_layer_commit'], 'Patch-layer commit mismatch')
require(git(args.checkout, 'diff', '--binary', 'HEAD') == (HERE / '0000-original-builder-jobs.patch').read_bytes(), 'Only declared original-builder -j2 changes are permitted')
series = run([args.checkout / 'scripts/compute-patch-series-id.py', '--profile', LOCK['profile'], '--bare']).decode().strip()
require(series == LOCK['baseline_patch_series_sha256'], 'Original patch stack hash mismatch')
require('Pkg.Revision = ' + LOCK['target_ndk'] in (args.ndk / 'source.properties').read_text(), 'NDK version mismatch')
if args.prepare:
    require(git(args.mesa, 'rev-parse', 'HEAD').decode().strip() == LOCK['mesa_commit'], 'Clean Mesa revision mismatch')
    require(not git(args.mesa, 'status', '--porcelain').strip(), 'Refusing to apply patches to a modified Mesa checkout')
    subprocess.run([str(args.checkout / 'scripts/apply-patches.sh'), '--profile', LOCK['profile'], '--mesa', str(args.mesa)], check=True)
    subprocess.run(['git', '-C', str(args.mesa), 'apply', '--check', str(HERE / '0001-csf-host-import-ext.patch')], check=True)
    subprocess.run(['git', '-C', str(args.mesa), 'apply', str(HERE / '0001-csf-host-import-ext.patch')], check=True)
verify_mesa(args.mesa)
if args.check_only:
    print('SOURCE-LOCK: PASS; no driver loaded, no GPU used')
    raise SystemExit(0)
env = os.environ.copy()
env.update(MESA=str(args.mesa), BDIR=str(args.build_dir), DDIR=str(args.dist_dir), HOST_TOOLS=str(args.host_tools))
subprocess.run([str(args.checkout / 'scripts/build-android.sh'), '--profile', LOCK['profile'], '--api', str(LOCK['target_api']), '--ndk', str(args.ndk)], env=env, check=True)
verify_mesa(args.mesa)
so = args.dist_dir / 'libvulkan_panfrost.so'
subprocess.run([str(args.checkout / 'scripts/validate-binary.sh'), '--abi', 'android', str(so)], env=env, check=True)
llvm = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
elf = run([llvm / 'llvm-readelf', '-h', '-d', '-n', '--dyn-syms', so]).decode()
require('AArch64' in elf, 'Wrong architecture')
for symbol in ['vk_icdGetInstanceProcAddr', 'vk_icdGetPhysicalDeviceProcAddr', 'vk_icdNegotiateLoaderICDInterfaceVersion', 'HMI']:
    require(symbol in elf, 'Missing ICD symbol: ' + symbol)
require('libc.so.6' not in elf and 'libX11.so' not in elf, 'Non-Android dependency')
(args.dist_dir / 'elf.txt').write_text(elf)
deps_text = run(['ninja', '-C', args.build_dir, '-t', 'deps']).decode()
(args.dist_dir / 'ninja-deps.txt').write_text(deps_text)
inputs = {}
for line in deps_text.splitlines():
    if line.startswith('    '):
        path = pathlib.Path(line.strip())
        if not path.is_absolute():
            path = args.build_dir / path
        require(path.is_file(), 'Missing observed compiler input: ' + str(path))
        inputs[str(path.absolute())] = info(path)
inputs[str(args.build_dir / 'build.ninja')] = info(args.build_dir / 'build.ninja')
(args.dist_dir / 'observed-inputs.json').write_text(json.dumps(inputs, indent=2, sort_keys=True) + '\n')
(args.dist_dir / 'compile-commands.json').write_bytes(run(['ninja', '-C', args.build_dir, '-t', 'compdb']))
(args.dist_dir / 'meson-options.json').write_bytes(run(['meson', 'introspect', '--buildoptions', args.build_dir]))
for source, frozen_name in [(HERE / 'source-lock.json', 'source-lock.json'),
                            (HERE / '0001-csf-host-import-ext.patch', 'extension.patch'),
                            (HERE / '0000-original-builder-jobs.patch', 'builder.patch')]:
    shutil.copy2(source, args.dist_dir / frozen_name)
record = {'status': 'NDK built and ELF validated; no hardware/import/game/performance result implied', 'source_lock': info(args.dist_dir / 'source-lock.json'), 'extension_patch': info(args.dist_dir / 'extension.patch'), 'builder_patch': info(args.dist_dir / 'builder.patch'), 'original_builder': info(args.checkout / 'scripts/build-android.sh'), 'api': LOCK['target_api'], 'parallelism': 2, 'profile': LOCK['profile'], 'ndk': info(args.ndk / 'source.properties'), 'compiler': info(llvm / 'clang'), 'compiler_version': run([llvm / 'aarch64-linux-android29-clang', '--version']).decode(), 'host_tools': [info(args.host_tools / name) for name in ['mesa_clc', 'vtn_bindgen2', 'panfrost_compile']], 'driver': info(so), 'build_id': re.findall(r'Build ID: ([0-9a-f]+)', elf), 'needed': re.findall(r'Shared library: \[([^\]]+)\]', elf), 'observed_inputs': info(args.dist_dir / 'observed-inputs.json'), 'options': info(args.dist_dir / 'meson-options.json'), 'default_enabled': False, 'runtime_opt_in': LOCK['runtime_env']}
(args.dist_dir / 'build.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
print(json.dumps({'driver': record['driver'], 'observed_input_count': len(inputs), 'report': str(args.dist_dir / 'build.json')}))
