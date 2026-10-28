#!/usr/bin/env python3
"""Replay the archived three-file policy through the existing Mesa/NDK builder.

Default is a three-file/source-ledger archive check, without compilation or a
full source scan. --output prepares a new isolated tree; --build also executes
the original Meson/Ninja commands. Never installs or accesses a device.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile

D = Path(__file__).resolve().parent
R = D.parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def ledger_entry(path):
    return {'kind': 'file', 'sha256': sha(path), 'bytes': path.stat().st_size}


def patch(tree, path):
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(path)],
                   cwd=tree, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT)


def verify():
    a = read(D / 'archive.json')
    for name, expected in a['archived_files'].items():
        assert sha(D / name) == expected, name
    assert sha(D / 'memory-critical-schedule.patch') == a['combined_patch_sha256']
    assert sha(R / a['helper']['path']) == a['helper']['sha256']
    b = a['baseline']
    assert sha(b['build_record_path']) == b['build_record_sha256']
    assert sha(b['source_ledger_path']) == b['source_ledger_sha256']
    ledger = read(b['source_ledger_path'])
    assert len(ledger) == b['source_entries']
    # Baseline's helper serialized sort_keys=True, whereas the three actual
    # stage builders serialized the manifest's insertion order. Reconstruct
    # that original helper order without rereading the 18,218 source files.
    ledger = {name: ({'kind': item['kind'], 'sha256': item['sha256'], 'bytes': item['bytes']}
                     if item['kind'] == 'file' else
                     {'kind': item['kind'], 'target': item['target']})
              for name, item in sorted(ledger.items(), key=lambda pair: Path(pair[0]))}
    source = Path(b['source'])
    with tempfile.TemporaryDirectory(prefix='ir3-schedule-archive-') as tmp:
        tmp = Path(tmp)
        for f, hashes in a['files'].items():
            assert sha(source / f) == hashes['before'], f
            target = tmp / f
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / f, target)
        initial = copy.deepcopy(ledger)
        for version, s in a['stages'].items():
            patch(tmp, D / 'stages' / version / 'memory-critical-schedule.patch')
            for f in a['files']:
                ledger[f] = ledger_entry(tmp / f)
            serialized = (json.dumps(ledger, indent=2) + '\n').encode()
            assert hashlib.sha256(serialized).hexdigest() == s['candidate_source_ledger_sha256'], version
            policy = read(D / 'stages' / version / 'cache-policy-key.json')
            tag = hashlib.sha256(json.dumps(policy['hash_input'], sort_keys=True,
                                          separators=(',', ':')).encode()).hexdigest()
            assert tag == policy['tu_build_id_hex'] == s['tu_build_id_hex']
        assert sorted(k for k in ledger if ledger[k] != initial[k]) == sorted(a['files'])
        for f, hashes in a['files'].items():
            assert sha(tmp / f) == hashes['after']
        # Independently apply the direct 12b -> 82c patch and compare final bytes.
        for f in a['files']:
            shutil.copyfile(source / f, tmp / f)
        patch(tmp, D / 'memory-critical-schedule.patch')
        for f, hashes in a['files'].items():
            assert sha(tmp / f) == hashes['after']
    return a, ledger


def prepare_build(a, expected, output, build):
    output = output.resolve()
    assert not output.exists(), 'Output must be new; historical outputs are immutable'
    baseline = read(a['baseline']['build_record_path'])
    assert baseline['complete'] and baseline['source_reverified']
    assert sha(baseline['driver']['path']) == a['baseline']['driver']['sha256']
    for name, tool in baseline['tools'].items():
        assert sha(tool.get('path', name)) == tool['sha256'], name
    helper_path = R / a['helper']['path']
    spec = importlib.util.spec_from_file_location('original_native_bc3_builder', helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    source = Path(a['baseline']['source'])
    assert helper.manifest(source) == read(a['baseline']['source_ledger_path'])
    output.mkdir(parents=True)
    target, binary, cross = output / 'mesa-26.2.5', output / 'mesa-kgsl-build', output / 'android-aarch64.ini'
    shutil.copytree(source, target, symlinks=True,
                    ignore=shutil.ignore_patterns('.git', '__pycache__'))
    patch(target, D / 'memory-critical-schedule.patch')
    assert helper.manifest(target) == expected
    shutil.copyfile(D / 'stages/v3/android-aarch64.ini', cross)
    final = read(D / 'stages/v3/build.json')
    old = a['stages']['v3']['original_root']
    setup = [arg.replace(old, str(output)) for arg in final['setup_argv']]
    command = ['ninja', '-C', str(binary), '-j', '4', 'src/freedreno/vulkan/libvulkan_freedreno.so']
    env = dict(os.environ, **final['declared_environment'])
    forbidden = {'CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                 'PYTHONPATH', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'TU_BUILD_ID_OVERRIDE'}
    assert not forbidden.intersection(env), forbidden.intersection(env)
    receipt = {'schema': 1, 'complete': False, 'archive_sha256': sha(D / 'archive.json'),
               'builder_sha256': sha(__file__), 'baseline': a['baseline'],
               'source_ledger_sha256': a['stages']['v3']['candidate_source_ledger_sha256'],
               'policy': read(D / 'stages/v3/cache-policy-key.json'),
               'setup_argv': setup, 'build_argv': command, 'tools': final['tools'],
               'declared_environment': final['declared_environment'],
               'historical_final_driver': a['stages']['v3']['driver'],
               'rebuilt_ELF_hash_not_assumed': True, 'device_tested': False}
    write(output / 'candidate-source-ledger.json', expected)
    write(output / 'build.json', receipt)
    if build:
        try:
            for name, argv in [('setup.log', setup), ('compile.log', command)]:
                with (output / name).open('x') as log:
                    subprocess.run(argv, env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
            assert helper.manifest(target) == expected
            assert helper.manifest(source) == read(a['baseline']['source_ledger_path'])
            elf = binary / 'src/freedreno/vulkan/libvulkan_freedreno.so'
            receipt.update(complete=True, driver={'path': str(elf), 'sha256': sha(elf), 'bytes': elf.stat().st_size})
        except BaseException as error:
            receipt['failure'] = repr(error)
            raise
        finally:
            write(output / 'build.json', receipt)
    return receipt


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path)
    p.add_argument('--build', action='store_true')
    args = p.parse_args()
    assert not args.build or args.output, '--build requires a new --output'
    archive, ledger = verify()
    result = {'archive_verified': True, 'three_file_patch_and_all_stage_ledgers_verified': True,
              'full_source_tree_scanned': False, 'compiled': False, 'device_access': False}
    if args.output:
        result = prepare_build(archive, ledger, args.output, args.build)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
