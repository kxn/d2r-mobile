#!/usr/bin/env python3
"""Build a separate B3-derived BC3 HOST_ALLOCATION candidate; never deploy."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BASE = ROOT / '.local/build/turnip-swapchain-ubwc-v4'
BASE_RECORD_SHA = 'bfa160faf756343e1b24e05c4f5c796d0c41c25015b543506e615354695de380'
BASE_DRIVER_SHA = 'b3fce5ab29c6617bd2cf334b3a56c67951de271bd870878c9a3121ff606b1b9f'
IMAGE = 'src/freedreno/vulkan/tu_image.cc'
IMAGE_SHA = '1377292bdf2ec16c8f2298287a9f5dfd3d9e086ece94eabcb3945e849ed3050e'
OLD = ('       format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK || '
       'format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK) &&')
NEW = ('       format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK || '
       'format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK ||\n'
       '       format == VK_FORMAT_BC3_UNORM_BLOCK || '
       'format == VK_FORMAT_BC3_SRGB_BLOCK) &&')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def manifest(source):
    result = {}
    for p in sorted(source.rglob('*')):
        relative = p.relative_to(source)
        if '.git' in relative.parts or '__pycache__' in relative.parts:
            continue
        if p.is_symlink():
            result[str(relative)] = {'kind': 'symlink', 'target': os.readlink(p)}
        elif p.is_file():
            result[str(relative)] = {'kind': 'file', 'sha256': sha(p),
                                     'bytes': p.stat().st_size}
    return result


def check_baseline():
    record_path = BASE / 'build.json'
    require(sha(record_path) == BASE_RECORD_SHA, 'B3 build record changed')
    record = json.loads(record_path.read_text())
    require(record['complete'] and not record['source_dirty'], 'B3 incomplete/dirty')
    require(record['source_revision'] == 'ea5e63f7063fe6511c512992af9dd15d3516f0cf',
            'B3 source revision mismatch')
    ledger_path = Path(record['baseline_entire_source_ledger']['path'])
    require(sha(ledger_path) == record['baseline_entire_source_ledger']['sha256'],
            'ASTC source ledger changed')
    expected = json.loads(ledger_path.read_text())
    source = BASE / 'mesa-26.2.5'
    actual = manifest(source)
    additions = {
        'src/freedreno/vulkan/tu_android_usage.cc': 'policy_source_sha256',
        'src/freedreno/vulkan/tu_android_usage.h': 'policy_header_sha256',
    }
    changes = {
        'src/freedreno/vulkan/tu_formats.cc': 'candidate_formats_sha256',
        'src/freedreno/vulkan/meson.build': 'candidate_meson_sha256',
    }
    for relative, key in {**changes, **additions}.items():
        require(actual[relative]['sha256'] == record[key], relative + ' B3 mismatch')
        expected[relative] = actual[relative]
    require(actual == expected, 'B3 full source differs from frozen declared ledger')
    require(sha(source / IMAGE) == IMAGE_SHA, 'original tu_image mismatch')
    require(sha(Path(record['driver']['path'])) == BASE_DRIVER_SHA,
            'B3 driver mismatch')
    require(record['driver']['sha256'] == BASE_DRIVER_SHA, 'B3 driver record mismatch')
    require(sha(BASE / 'android-aarch64.ini') == record['cross_file_sha256'],
            'B3 cross file changed')
    require(sha(ROOT / 'tools/experiments/turnip-swapchain-ubwc/build.py') ==
            record['builder_sha256'], 'B3 builder changed')
    setup = Path(record['baseline_setup']['path'])
    require(sha(setup) == record['baseline_setup']['sha256'], 'original setup changed')
    return record, actual


def patched(text):
    require(text.count(OLD) == 1, 'host image whitelist anchor mismatch')
    return text.replace(OLD, NEW)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    base, baseline_manifest = check_baseline()
    original_source = BASE / 'mesa-26.2.5'
    before = (original_source / IMAGE).read_text()
    after = patched(before)
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                      fromfile='a/' + IMAGE, tofile='b/' + IMAGE))
    require(patch == (HERE / 'host-bc3.patch').read_text(), 'review patch mismatch')
    if args.verify_only:
        print(json.dumps({'baseline_verified': True, 'source_files': len(baseline_manifest),
                          'only_changed_source': IMAGE, 'tu_image_sha256': IMAGE_SHA}))
        return
    require(args.output is not None, '--output required for build')
    out = args.output.resolve()
    require(not out.exists(), 'refusing to overwrite an existing output')
    out.mkdir(parents=True)
    source = out / original_source.name
    shutil.copytree(original_source, source, symlinks=True,
                    ignore=shutil.ignore_patterns('.git', '__pycache__'))
    (source / IMAGE).write_text(after)
    candidate_manifest = manifest(source)
    changed = [p for p in set(candidate_manifest) | set(baseline_manifest)
               if candidate_manifest.get(p) != baseline_manifest.get(p)]
    require(changed == [IMAGE], 'candidate must change only tu_image.cc')
    write(out / 'baseline-source-ledger.json', baseline_manifest)
    write(out / 'candidate-source-ledger.json', candidate_manifest)
    shutil.copy2(HERE / 'host-bc3.patch', out / 'host-bc3.patch')
    cross = out / 'android-aarch64.ini'
    shutil.copy2(BASE / cross.name, cross)
    build = out / 'mesa-kgsl-build'
    argv = [part.replace(str(BASE / 'mesa-kgsl-build'), str(build))
                .replace(str(original_source), str(source))
                .replace(str(BASE / cross.name), str(cross)) for part in base['setup_argv']]
    build_argv = ['ninja', '-C', str(build), '-j', '4',
                  'src/freedreno/vulkan/libvulkan_freedreno.so']
    env = dict(os.environ, **base['declared_environment'])
    affected_env = {k: v for k, v in env.items() if k in
        {'CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'PYTHONPATH',
         'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'TU_BUILD_ID_OVERRIDE'}}
    require(not affected_env, 'undeclared build environment: ' + str(affected_env))
    tools = {}
    for tool in ['meson', 'ninja', 'pkg-config', 'python3']:
        path = Path(shutil.which(tool, path=env['PATH'])).resolve()
        tools[tool] = {'path': str(path), 'sha256': sha(path)}
    for executable in re.findall(r"= '([^']+)'", cross.read_text()):
        path = Path(executable)
        if path.is_file():
            tools[executable] = {'resolved': str(path.resolve()), 'sha256': sha(path)}
    record = {
        'complete': False, 'baseline_build': {'path': str(BASE / 'build.json'),
                                            'sha256': BASE_RECORD_SHA},
        'baseline_driver_sha256': BASE_DRIVER_SHA,
        'source_revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse',
                                                  'HEAD'], text=True).strip(),
        'source_status': subprocess.check_output(['git', '-C', str(ROOT), 'status',
                                                 '--porcelain'], text=True),
        'experiment_inputs': {p.name: sha(p) for p in sorted(HERE.iterdir()) if p.is_file()},
        'baseline_source_ledger_sha256': sha(out / 'baseline-source-ledger.json'),
        'candidate_source_ledger_sha256': sha(out / 'candidate-source-ledger.json'),
        'source_changes': {IMAGE: {'before': IMAGE_SHA, 'after': sha(source / IMAGE)}},
        'cross_file_sha256': sha(cross), 'setup_argv': argv, 'build_argv': build_argv,
        'declared_environment': base['declared_environment'], 'tools': tools,
        'scope': 'Driver-only BC3 host-import candidate. No deployment or GPU validation. '
                 'No encoder/tiler, ordinary image, coherency, UBWC or UUID code changes.'}
    write(out / 'build.json', record)
    try:
        for name, command in [('setup.log', argv), ('compile.log', build_argv)]:
            with (out / name).open('x') as log:
                subprocess.run(command, env=env, check=True, stdout=log,
                               stderr=subprocess.STDOUT)
        require(manifest(source) == candidate_manifest, 'candidate source changed during build')
        driver = build / 'src/freedreno/vulkan/libvulkan_freedreno.so'
        readelf = next(Path(p).with_name('llvm-readelf') for p in tools
                       if p.endswith('/llvm-ar'))
        notes = subprocess.check_output([str(readelf), '-n', str(driver)], text=True)
        (out / 'elf-notes.txt').write_text(notes)
        record.update(complete=True, source_reverified=True,
                      driver={'path': str(driver), 'bytes': driver.stat().st_size,
                              'sha256': sha(driver),
                              'elf_build_id': re.search(r'Build ID: (\w+)', notes).group(1)},
                      device_driver_uuid=None, device_pipeline_cache_uuid=None)
    except Exception as exc:
        record['failure'] = str(exc)
        write(out / 'build.json', record)
        raise
    write(out / 'build.json', record)
    print(json.dumps(record['driver']), flush=True)


if __name__ == '__main__':
    main()
