#!/usr/bin/env python3
"""核对原上游规则与 Android 产物；同时检查默认 Apple 产物逐字节回归。
Audit upstream rules against Android outputs, and compare the default Apple
output byte-for-byte with the pinned upstream rewriter.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bundle', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
directory = args.bundle.resolve()
bundle = json.loads((directory / 'rewrite-manifest.json').read_text())
options = bundle['options']
source = Path(bundle['modules']['main']['recipe']['input']['path']).parent
subprocess.run([sys.executable, str(ROOT / 'scripts/dev/rewrite-native.py'), '--source', str(source),
                '--output', str(directory), '--abi', options['DM_HOST_ABI'], '--rules', options['DM_RULESET'],
                '--private-gates', options['DM_PRIVATE_GATES'], '--verify-only'], check=True)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def rows(path):
    return [line.split('\t') for line in path.read_text().splitlines() if line and not line.startswith('#')]


def patch_offset(path):
    return int(next(line.split('\t')[1] for line in path.read_text().splitlines()
                    if line.startswith('# PATCH_OFF\t')), 0)


def branch_target(raw, offset):
    if len(raw) != 8:
        return None
    word = int.from_bytes(bytes.fromhex(raw), 'little')
    if word & 0xfc000000 != 0x14000000:
        return None
    immediate = word & 0x3ffffff
    if immediate & 0x2000000:
        immediate -= 0x4000000
    return offset + immediate * 4


def appended_branch(raw, offset, patch):
    target = branch_target(raw, offset)
    return target is not None and target >= patch


def same_rule_body(baseline, android):
    if baseline[3:] != android[3:]:
        return False
    left, right = bytes.fromhex(baseline[1]), bytes.fromhex(android[1])
    if len(left) != len(right) or len(left) % 4:
        return False
    a, b = int(baseline[0], 0), int(android[0], 0)
    for off in range(0, len(left), 4):
        x, y = left[off:off + 4], right[off:off + 4]
        if x == y:
            continue
        tx, ty = branch_target(x.hex(), a + off), branch_target(y.hex(), b + off)
        if tx is None or ty is None:
            return False
        if tx == ty:
            continue
        if a <= tx < a + len(left) and b <= ty < b + len(right) and tx - a == ty - b:
            continue
        return False
    return True


report = {'upstream': bundle['upstream'], 'android_bundle_sha256': sha(directory / 'rewrite-manifest.json'),
          'modules': {}, 'game_rules_match_upstream': False}
environment = os.environ.copy()
environment.update(options)
environment['DM_HOST_ABI'] = 'darwin'
environment.pop('DM_NATIVE_TOUCH_AIM', None)
with tempfile.TemporaryDirectory(prefix='d2r-upstream-audit-') as temporary:
    scratch = Path(temporary)
    script = scratch / 'dm/tools/patch_nso.py'
    script.parent.mkdir(parents=True)
    script.write_bytes(subprocess.check_output(['git', '-C', str(ROOT), 'show',
                       bundle['upstream']['revision'] + ':dm/tools/patch_nso.py']))
    data = scratch / 'dm/data'
    data.mkdir()
    for module, record in bundle['modules'].items():
        build_id = record['recipe']['input']['build_id']
        path = ROOT / 'dm/data' / f'bootlist_{build_id}.tsv'
        if path.is_file():
            shutil.copyfile(path, data / path.name)
        original = scratch / 'upstream' / module
        apple = scratch / 'current-apple' / module
        original.mkdir(parents=True); apple.mkdir(parents=True)
        for generator, output in ((script, original), (ROOT / 'dm/tools/patch_nso.py', apple)):
            with (output / 'rewrite.log').open('w') as log:
                subprocess.run([sys.executable, str(generator), str(source / module), str(output), module],
                               env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        base_image, apple_image = original / f'{module}.patched.bin', apple / f'{module}.patched.bin'
        if sha(base_image) != sha(apple_image):
            raise SystemExit(f'{module}: default Apple binary diverges from pinned upstream')
        base_patch = patch_offset(original / f'{module}.fixups.tsv')
        android_patch = patch_offset(directory / f'{module}.fixups.tsv')
        original_cuts = rows(original / f'{module}.cuts.tsv')
        android_cuts = rows(directory / f'{module}.cuts.tsv')
        if len(original_cuts) != len(android_cuts):
            raise SystemExit(f'{module}: game cut count differs from upstream')
        relocated = 0
        for baseline, android in zip(original_cuts, android_cuts):
            if baseline[:2] != android[:2] or baseline[3:] != android[3:]:
                raise SystemExit(f'{module}: cut source/offset/scope/reason differs from upstream')
            if baseline[2] != android[2]:
                offset = int(baseline[0], 0)
                if not (appended_branch(baseline[2], offset, base_patch) and
                        appended_branch(android[2], offset, android_patch)):
                    raise SystemExit(f'{module}: game-cut bytes changed beyond platform branch relocation')
                relocated += 1
        for suffix in ('obs.tsv', 'params.tsv', 'sites.tsv'):
            if rows(original / f'{module}.{suffix}') != rows(directory / f'{module}.{suffix}'):
                raise SystemExit(f'{module}: {suffix} differs from upstream')
        base_gates = rows(original / f'{module}.gates.tsv')
        android_gates = rows(directory / f'{module}.gates.tsv')
        if [row[1:] for row in base_gates] != [row[1:] for row in android_gates]:
            raise SystemExit(f'{module}: gate semantics differ from upstream')
        base_rules = original / f'{module}.rules.tsv'
        android_rules = directory / f'{module}.rules.tsv'
        if base_rules.exists() != android_rules.exists():
            raise SystemExit(f'{module}: original rule domain differs')
        if base_rules.exists():
            left, right = rows(base_rules), rows(android_rules)
            if len(left) != len(right) or not all(same_rule_body(a, b) for a, b in zip(left, right)):
                raise SystemExit(f'{module}: original rule instructions/branch targets/reasons differ')
        report['modules'][module] = {'default_apple_byte_identical': True,
                'apple_image_sha256': sha(apple_image), 'cuts': len(android_cuts),
                'platform_branch_relocations': relocated, 'gates': len(android_gates),
                'observers': len(rows(directory / f'{module}.obs.tsv')),
                'game_rules_match_upstream': True}
        print(f'{module}: original game rules match; Apple output is byte-identical', flush=True)
report['game_rules_match_upstream'] = True
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, indent=2) + '\n')
print(f'Upstream audit passed: {args.report}')
