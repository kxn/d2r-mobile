#!/usr/bin/env python3
"""Isolated incremental-policy source build, never install or rewrite a game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BASE = ROOT / '.local/build/turnip-native-astc-whole-linear-v1'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--jobs', type=int, choices=range(1, 17), default=4)
args = parser.parse_args()
out = args.output.resolve()
if out.exists():
    parser.error('use a new output directory')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(name, value):
    (out / name).write_text(json.dumps(value, indent=2) + '\n')


source = BASE / 'mesa-26.2.5'
ledger_path = BASE / 'source-ready-freeze/entire-source-ledger.json'
ledger = json.loads(ledger_path.read_text())
actual = {str(p.relative_to(source)) for p in source.rglob('*')
          if (p.is_file() or p.is_symlink())
          and '.git' not in p.relative_to(source).parts}
extra = actual - set(ledger)
assert not (set(ledger) - actual), 'baseline source member missing'
assert all('__pycache__' in Path(p).parts and p.endswith('.pyc')
           for p in extra), 'unexpected baseline source member'
for relative, declaration in ledger.items():
    p = source / relative
    if declaration['kind'] == 'symlink':
        assert p.is_symlink() and os.readlink(p) == declaration['target'], relative
    else:
        assert not p.is_symlink() and sha(p) == declaration['sha256'], relative
baseline_driver = BASE / 'source-ready-freeze/driver/libvulkan_freedreno.so'
assert sha(baseline_driver) == 'f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'

out.mkdir(parents=True)
new_source = out / source.name
shutil.copytree(source, new_source, symlinks=True,
                ignore=shutil.ignore_patterns('.git', '__pycache__'))
addition = new_source / 'src/freedreno/vulkan/tu_android_usage.cc'
assert not addition.exists()
shutil.copy2(HERE / addition.name, addition)
header = addition.with_suffix('.h')
assert not header.exists()
shutil.copy2(HERE / header.name, header)
formats_file = new_source / 'src/freedreno/vulkan/tu_formats.cc'
old_formats = formats_file.read_text()
include = '#include "tu_image.h"'
assert old_formats.count(include) == 1
formats_text = old_formats.replace(include, include +
    '\n#ifdef VK_USE_PLATFORM_ANDROID_KHR\n#include "tu_android_usage.h"\n#endif')
anchor_usage = '         props->sampleCounts = VK_SAMPLE_COUNT_1_BIT;'
assert formats_text.count(anchor_usage) == 1
formats_file.write_text(formats_text.replace(anchor_usage, anchor_usage +
    '\n#ifdef VK_USE_PLATFORM_ANDROID_KHR\n'
    '         tu_android_apply_ubwc_usage(physicalDevice, base_info, base_props);\n#endif'))
meson_file = new_source / 'src/freedreno/vulkan/meson.build'
old_meson = meson_file.read_text()
anchor = "if tu_wsi\n  libtu_files += 'tu_wsi.cc'\nendif"
assert old_meson.count(anchor) == 1
meson_file.write_text(old_meson.replace(anchor, anchor +
    "\n\nif with_platform_android\n  libtu_files += 'tu_android_usage.cc'\nendif"))

setup_record = BASE / 'android-setup-command.json'
original = json.loads(setup_record.read_text())
env = dict(os.environ, **original['env'])
cross = out / 'android-aarch64.ini'
shutil.copy2(BASE / cross.name, cross)
build = out / 'mesa-kgsl-build'
argv = [part.replace(str(BASE / 'mesa-kgsl-build'), str(build))
            .replace(str(source), str(new_source))
            .replace(str(BASE / cross.name), str(cross))
        for part in original['argv']]
build_argv = ['ninja', '-C', str(build), '-j', str(args.jobs),
              'src/freedreno/vulkan/libvulkan_freedreno.so']
record = {
    'complete': False,
    'source_revision': subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
    'source_dirty': bool(subprocess.check_output(
        ['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip()),
    'baseline_entire_source_ledger': {'path': str(ledger_path), 'sha256': sha(ledger_path)},
    'baseline_driver_sha256': sha(baseline_driver),
    'baseline_setup': {'path': str(setup_record), 'sha256': sha(setup_record)},
    'policy_source_sha256': sha(addition),
    'policy_header_sha256': sha(header),
    'original_formats_sha256': ledger['src/freedreno/vulkan/tu_formats.cc']['sha256'],
    'candidate_formats_sha256': sha(formats_file),
    'original_meson_sha256': ledger['src/freedreno/vulkan/meson.build']['sha256'],
    'candidate_meson_sha256': sha(meson_file),
    'builder_sha256': sha(Path(__file__)),
    'cross_file_sha256': sha(cross),
    'setup_argv': argv, 'build_argv': build_argv,
    'declared_environment': original['env'],
    'scope': 'Separate source build only. No deployment, native-table rebinding, game execution or performance claim.'}
assert not record['source_dirty'], 'commit the reviewable experiment before building'
write('build.json', record)
for name, command in [('setup.log', argv), ('compile.log', build_argv)]:
    with (out / name).open('x') as log:
        subprocess.run(command, env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
driver = build / 'src/freedreno/vulkan/libvulkan_freedreno.so'
record.update(complete=True, driver={'path': str(driver), 'bytes': driver.stat().st_size,
                                    'sha256': sha(driver)})
write('build.json', record)
print(json.dumps(record['driver']), flush=True)
