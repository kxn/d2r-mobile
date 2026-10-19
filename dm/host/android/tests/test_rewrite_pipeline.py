#!/usr/bin/env python3
"""原创 NSO 夹具验证构建可重复性与损坏产物拒绝，不代替游戏验收。
Original NSO fixtures test reproducibility and rejection/repair of corrupted
artifacts; these checks are separate from real game acceptance.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
PIPELINE = ROOT / 'scripts/dev/rewrite-native.py'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--abi', choices=('elfdesc', 'elfdesc-gate'), default='elfdesc')
parser.add_argument('--x18-group-size', type=int, choices=(1, 2, 3))
args = parser.parse_args()


def run(command, expected=0):
    result = subprocess.run(command, capture_output=True, text=True)
    if (result.returncode == 0) != (expected == 0):
        raise AssertionError(result.stdout + result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix='d2r-pipeline-test-') as temporary:
    work = Path(temporary)
    source, output = work / 'source', work / 'output'
    source.mkdir()
    run([sys.executable, str(Path(__file__).with_name('make_fixture.py')), str(source)])
    for module in ('main', 'sdk', 'subsdk0'):
        shutil.copyfile(source / 'fixture.nso', source / module)
    command = [sys.executable, str(PIPELINE), '--source', str(source), '--output', str(output),
               '--abi', args.abi, '--rules', 'native']
    if args.x18_group_size is not None:
        command += ['--x18-group-size', str(args.x18_group_size)]
    run(command)
    expected_manifest = (output / 'rewrite-manifest.json').read_bytes()
    expected_image = (output / 'main.patched.bin').read_bytes()
    run([*command, '--verify-only'])
    # 全新输出目录须逐字节相同，不能依靠上次加工后的镜像。
    # A clean output directory must match byte-for-byte, without an old image.
    clean = command.copy()
    clean[clean.index('--output') + 1] = str(work / 'clean')
    run(clean)
    assert (work / 'clean/rewrite-manifest.json').read_bytes() == expected_manifest
    if args.abi == 'elfdesc-gate':
        # The safe reference uses the same ABI and rules. Its different group
        # option must never accept a candidate's cached image or re-stamp it.
        alternate = command.copy()
        if '--x18-group-size' in alternate:
            alternate[alternate.index('--x18-group-size') + 1] = '1' if args.x18_group_size != 1 else '3'
        else:
            alternate += ['--x18-group-size', '1']
        run([*alternate, '--verify-only'], expected=1)
        assert (output / 'rewrite-manifest.json').read_bytes() == expected_manifest
        run([*command, '--verify-only'])
    (output / 'main.patched.bin').write_bytes(expected_image[:-1] + bytes([expected_image[-1] ^ 1]))
    run([*command, '--verify-only'], expected=1)
    repaired = run(command)
    assert 'main: rebuilt from original NSO' in repaired.stdout
    assert (output / 'main.patched.bin').read_bytes() == expected_image
    assert (output / 'rewrite-manifest.json').read_bytes() == expected_manifest
    # 共享清单损坏也必须拒绝/重建，不能以新哈希把坏缓存重新盖章。
    # Reject and rebuild corrupted shared metadata; never bless its new hash.
    (output / 'build_src.sha256').write_text('undocumented source\n')
    run([*command, '--verify-only'], expected=1)
    repaired = run(command)
    assert 'main: rebuilt from original NSO' in repaired.stdout
    assert (output / 'rewrite-manifest.json').read_bytes() == expected_manifest
    (output / 'sdk.gates.tsv').unlink()
    run([*command, '--verify-only'], expected=1)
    run(command)
    (output / 'main.obs.tsv').write_text('0x0\t999\t0\tundocumented\n')
    run([*command, '--verify-only'], expected=1)
    run(command)
    (source / 'main').write_bytes(expected_image)
    result = run([*command, '--verify-only'], expected=1)
    assert 'original NSO0' in result.stderr
print(json.dumps({'result': 'passed', 'host_abi': args.abi, 'clean_rebuild_identical': True,
                  'x18_group_size': args.x18_group_size, 'different_group_option_rejected': args.abi == 'elfdesc-gate',
                  'corrupt_image_rejected_and_rebuilt': True,
                  'corrupt_shared_metadata_rejected_and_rebuilt': True,
                  'missing_gates_rejected': True, 'changed_observers_rejected': True,
                  'rewritten_input_rejected': True, 'real_game_acceptance': False}))
