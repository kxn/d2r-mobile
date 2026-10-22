#!/usr/bin/env python3
"""Freeze the reviewed read-only SDK hook and actual official host tool inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = Path('/home/dl/d2r-mobile')
CAPTURE_FILES = {
    'run-debugger.py': (HERE / 'run-debugger.py', 'd2f6c1f33dededbcc4560c81e0906c3911bd9920761dfc2753ac1f354abf6678'),
    'sdk_free_resident.py': (HERE / '../sdk_free_resident.py', 'a46fe631dde9f97d3d9b8352e35d2a1bbe90e20a6d1036aae6d693e525ec1e21'),
    'sdk_nvn_pool_registry.py': (HERE / '../sdk_nvn_pool_registry.py', '5735d89be2852c0c8e4113fadab140c2cd81037c378ea17155c4cb63c64dfc17'),
}
TOOLS = [
    ROOT / '.local/tools/lldb19/usr/lib/llvm-19/bin/lldb',
    ROOT / '.local/tools/lldb19/usr/lib/x86_64-linux-gnu/liblldb-19.so.1',
    ROOT / '.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/lldb-server',
    ROOT / '.local/android-sdk/platform-tools/adb',
    Path(sys.executable).resolve(),
]


def record(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path), 'sha256': digest, 'bytes': path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path, help='New, exclusively owned output directory')
    args = parser.parse_args()
    source = Path(subprocess.check_output(['git', '-C', str(HERE), 'rev-parse', '--show-toplevel'], text=True).strip())
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no']):
        parser.error('tracked source tree is dirty; freeze an exact committed revision')
    source_records = {}
    for name, (path, expected) in CAPTURE_FILES.items():
        path = path.resolve()
        row = record(path)
        original = subprocess.check_output(['git', '-C', str(source), 'show', commit + ':' + str(path.relative_to(source))])
        if row['sha256'] != expected or hashlib.sha256(original).hexdigest() != expected:
            parser.error('capture source differs from reviewed Pool successor: ' + name)
        source_records[name] = row
    tools = [record(path) for path in TOOLS]  # Hash files only; no tool/device is executed.
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir(exist_ok=False)
    files = {}
    for name, (path, expected) in CAPTURE_FILES.items():
        shutil.copy2(path, output / name)
        files[name] = record(output / name)
        if files[name]['sha256'] != expected:
            raise RuntimeError('source copy changed: ' + name)
    manifest = {
        'schema': 1, 'source_pinned': True,
        'scope': 'Official owned LLDB SDK metadata + two existing kernel pagemap views; read-only exact SDK decoder gap, not sampler/timer/allocator/decommit.',
        'source_commit': commit, 'source_root': str(source), 'source_files': source_records,
        'freeze_producer': record(__file__),
        'files': files, 'tools': tools,
        'source_provenance': {
            'accepted_owned_cleanup_revision': '4984509f629b9ec71b3a3b6366fee7895f83bd02',
            'accepted_owned_cleanup_sha256': '86eb7fae482a52da56de766d068cfc5c7c049c45302b21d5049cc4657cb2b10c',
            'reviewed_capture_revision': '306dd9a SDK/NVN metadata plus minimal source-pinned wrapper successor; old v3 untouched',
            'readiness': 'Observe named official server socket before first LLDB connection; never consume its sole client slot with a probe.',
            'binary_transport': 'Existing Android dd status=none option suppresses transfer summaries; no guessed binary suffix removal.',
        },
    }
    path = output / 'tool-inputs.json'
    path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'output': str(output), 'source_commit': commit,
                      'tool_inputs': record(path), 'no_device_access': True}, indent=2))


if __name__ == '__main__':
    main()
