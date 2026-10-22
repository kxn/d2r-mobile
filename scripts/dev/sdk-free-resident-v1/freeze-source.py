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
    'run-debugger.py': 'd7c06bdc0bf1c35691b4a1e0e2f504fa2c87c617a630d4cefabb0c34e15c0ecb',
    'sdk_free_resident.py': 'ec94bac5e5b978b294b8699cce723d9f954f8e4802f2c3ce7f5f5f6348e0418b',
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
    for name, expected in CAPTURE_FILES.items():
        path = HERE / name
        row = record(path)
        original = subprocess.check_output(['git', '-C', str(source), 'show', commit + ':' + str(path.relative_to(source))])
        if row['sha256'] != expected or hashlib.sha256(original).hexdigest() != expected:
            parser.error('capture source differs from reviewed v3: ' + name)
        source_records[name] = row
    tools = [record(path) for path in TOOLS]  # Hash files only; no tool/device is executed.
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir(exist_ok=False)
    files = {}
    for name, expected in CAPTURE_FILES.items():
        shutil.copy2(HERE / name, output / name)
        files[name] = record(output / name)
        if files[name]['sha256'] != expected:
            raise RuntimeError('source copy changed: ' + name)
    manifest = {
        'schema': 1, 'source_pinned': True,
        'scope': 'Official owned LLDB SDK metadata + two existing kernel pagemap views; read-only exact SDK decoder gap, not sampler/timer/allocator/decommit.',
        'source_commit': commit, 'source_root': str(source), 'source_files': source_records,
        'files': files, 'tools': tools,
        'source_provenance': {
            'accepted_owned_cleanup_revision': '4984509f629b9ec71b3a3b6366fee7895f83bd02',
            'accepted_owned_cleanup_sha256': '86eb7fae482a52da56de766d068cfc5c7c049c45302b21d5049cc4657cb2b10c',
            'reviewed_capture_revision': 'local SDK capture v3; source bytes preserved exactly',
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
