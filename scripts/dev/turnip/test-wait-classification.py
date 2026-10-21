#!/usr/bin/env python3
"""Test the exact f830 KGSL wait function without a device or linked driver.

Reads an existing Mesa producer tree, applies the archived patch to one copied
TU, and extracts its actual wait functions into the scripted-ioctl host test.
The source tree is read-only. Optional production compilation emits only one
relocatable TU object; no shared library, APK or deployment is performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
PATCH = HERE / 'patches/mesa-26.2.5/0001-kgsl-classify-timestamp-wait-errors.patch'
TEST = HERE / 'tests/wait-classification-test.cc'
BASE_SHA256 = '691fcd1f5b90643da3165c3329fd939a86d662646a7802845a1578c1a5ed48c7'
PATCH_SHA256 = '6cc3ce6166fb95689ab2abf938dd958fa6ef48ceac037da0096625d924be2f24'
PATCHED_SHA256 = 'f50908760fe3152038422fcb8a85d6933425ea921b331631edec87f3202ff9fb'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(argv, log, *, cwd=None, env=None):
    result = subprocess.run(argv, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(result.stdout)
    if result.returncode:
        raise SystemExit(f'command failed ({result.returncode}); see {log}')
    return result.stdout


def write(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def extract(source, output):
    text = source.read_text()
    first = text.index('static int\nget_relative_ms(')
    last = text.index('static VkResult\nkgsl_queue_wait_fence(', first)
    output.write_text(text[first:last])


def compile_production_tu(commands, source, output):
    entries = json.loads(commands.read_text())
    found = [entry for entry in entries
             if entry['file'].endswith('/tu_knl_kgsl.cc')]
    if len(found) != 1:
        raise SystemExit('expected one tu_knl_kgsl.cc compile command')
    entry = found[0]
    argv = list(entry.get('arguments') or shlex.split(entry['command']))
    replacements = {
        '-o': str(output / 'tu_knl_kgsl.classification.o'),
        '-MF': str(output / 'tu_knl_kgsl.classification.o.d'),
        '-MQ': str(output / 'tu_knl_kgsl.classification.o'),
        '-MT': str(output / 'tu_knl_kgsl.classification.o'),
        '-c': str(source),
    }
    seen = set()
    index = 0
    while index < len(argv):
        flag = argv[index]
        if flag in replacements:
            if flag in seen or index + 1 == len(argv):
                raise SystemExit(f'unsupported repeated/incomplete flag: {flag}')
            seen.add(flag)
            argv[index + 1] = replacements[flag]
            index += 2
        else:
            index += 1
    if not {'-c', '-o', '-MF'} <= seen:
        raise SystemExit('expected explicit source, object and dependency flags')
    record = {'original_entry': entry, 'argv': argv,
              'cwd': entry['directory'], 'source_sha256': sha(source),
              'scope': 'One TU object only; no link or deployment.'}
    write(output / 'production-compile-command.json', record)
    run(argv, output / 'production-compile.txt', cwd=entry['directory'])
    dependency_file = output / 'tu_knl_kgsl.classification.o.d'
    dependency_text = dependency_file.read_text().replace('\\\n', ' ')
    dependencies = shlex.split(dependency_text.split(':', 1)[1])
    inputs = []
    for dependency in sorted(set(dependencies)):
        path = Path(dependency)
        if not path.is_absolute():
            path = Path(entry['directory']) / path
        path = path.resolve()
        inputs.append({'path': str(path), 'sha256': sha(path),
                       'bytes': path.stat().st_size})
    write(output / 'production-compiler-inputs.json', inputs)
    return {'object_sha256': sha(output / 'tu_knl_kgsl.classification.o'),
            'actual_dependency_count': len(inputs),
            'command': str(output / 'production-compile-command.json'),
            'inputs': str(output / 'production-compiler-inputs.json')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mesa-source', required=True, type=Path,
                        help='existing exact f830 producer Mesa 26.2.5 tree')
    parser.add_argument('--compile-commands', type=Path,
                        help='optional original f830 NDK compile_commands.json')
    parser.add_argument('--out', required=True, type=Path,
                        help='new empty directory for copied TU and host tests')
    parser.add_argument('--cxx', default='g++', help='host GCC with ASan/UBSan')
    args = parser.parse_args()
    mesa = args.mesa_source.resolve()
    source = mesa / 'src/freedreno/vulkan/tu_knl_kgsl.cc'
    if not source.is_file() or sha(source) != BASE_SHA256:
        parser.error('source differs from qualified f830 TU; no fuzzy port allowed')
    if sha(PATCH) != PATCH_SHA256:
        parser.error('archived patch checksum differs')
    compiler = shutil.which(args.cxx)
    if compiler is None:
        parser.error('host compiler was not found')
    output = args.out.resolve()
    if output == mesa or mesa in output.parents:
        parser.error('--out must be outside the read-only Mesa source tree')
    if output.exists() and any(output.iterdir()):
        parser.error('--out must be new or empty; preserve earlier evidence')
    output.mkdir(parents=True, exist_ok=True)
    patched = output / 'tu_knl_kgsl.cc'
    run(['patch', '--batch', '--forward', '--fuzz=0', '--output', str(patched),
         str(source), str(PATCH)], output / 'patch.txt')
    if sha(patched) != PATCHED_SHA256 or sha(source) != BASE_SHA256:
        raise SystemExit('patched identity or read-only source invariant failed')
    for mode, path in [('original', source), ('patched', patched)]:
        extract(path, output / f'{mode}-wait-path.inc')
    results = []
    for mode in ['patched', 'original']:
        binary = output / f'{mode}-asan-ubsan'
        argv = [compiler, '-std=c++20', '-O1', '-g', '-DNDEBUG',
                '-Wno-narrowing', '-fno-omit-frame-pointer',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-I' + str(mesa / 'include'),
                '-I' + str(mesa / 'src/freedreno/vulkan'),
                '-DWAIT_SOURCE_PATH="' + str(output / f'{mode}-wait-path.inc') + '"']
        if mode == 'original':
            argv.append('-DTEST_ORIGINAL_REFERENCE=1')
        argv += [str(TEST), '-o', str(binary)]
        run(argv, output / f'{mode}-compile.txt')
        environment = os.environ.copy()
        environment.update(ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        text = run([str(binary)], output / f'{mode}-result.txt', env=environment)
        result = json.loads(text)
        results.append({'mode': mode, 'argv': argv, 'result': result,
                        'binary_sha256': sha(binary)})
    production = None
    if args.compile_commands:
        production = compile_production_tu(args.compile_commands.resolve(),
                                           patched, output)
    if sha(source) != BASE_SHA256:
        raise SystemExit('read-only original source changed')
    record = {'scope': 'Offline function tests; no device or linked driver.',
              'base_TU_sha256': BASE_SHA256, 'patch_sha256': sha(PATCH),
              'patched_TU_sha256': sha(patched), 'test_sha256': sha(TEST),
              'runner_sha256': sha(Path(__file__).resolve()),
              'tests': results, 'production_TU': production,
              'boundary': 'Correct error classification only, not a GPU hang fix. '
                          'The scripted ioctl and fake clock test error/retry '
                          'behavior, not hardware recovery or kernel execution.'}
    write(output / 'result.json', record)
    print(output / 'result.json')


if __name__ == '__main__':
    main()
