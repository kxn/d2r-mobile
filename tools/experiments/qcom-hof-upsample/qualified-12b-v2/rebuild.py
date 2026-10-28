#!/usr/bin/env python3
"""Rebuild the archived HOF input used by the independent 12b offscreen fixtures. No device or runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=ROOT / '.local/build/qcom-hof-upsample-qualified-12b-v2-rebuild')
    args = parser.parse_args()
    receipt = json.loads((HERE / 'source-receipt.json').read_text())
    for name, expected in receipt['source_files'].items():
        if sha(HERE / name) != expected:
            raise SystemExit('Archived source changed: ' + name)
    output = args.output.resolve()
    if output.is_relative_to(HERE):
        raise SystemExit('Keep generated binaries outside the source archive.')
    output.mkdir(parents=True, exist_ok=False)
    compiler = shutil.which('glslangValidator')
    validator = shutil.which('spirv-val')
    if not compiler or not validator:
        raise SystemExit('Install existing glslangValidator and spirv-val first.')
    spirv = output / 'candidate.spv'
    commands = [
        [compiler, '-V', '--target-env', 'vulkan1.2', '-S', 'frag',
         '-o', str(spirv), str(HERE / receipt['candidate_source'])],
        [validator, '--target-env', 'vulkan1.2', str(spirv)],
        [validator, '--target-env', 'vulkan1.3', str(spirv)],
    ]
    records = []
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True)
        records.append({'argv': command, 'exit': result.returncode,
                        'stdout': result.stdout, 'stderr': result.stderr})
        if result.returncode:
            break
    actual = sha(spirv) if spirv.exists() else None
    passed = len(records) == 3 and all(r['exit'] == 0 for r in records)
    passed = passed and actual == receipt['expected_SPIRV_sha256']
    report = {
        'scope': 'Host GLSL/SPIR-V reconstruction only; no device, native GPU '
                 'compilation, drawing, quality or performance result.',
        'source_receipt_sha256': sha(HERE / 'source-receipt.json'),
        'source_sha256': sha(HERE / receipt['candidate_source']),
        'expected_SPIRV_sha256': receipt['expected_SPIRV_sha256'],
        'actual_SPIRV_sha256': actual,
        'exact_HOF_input_rebuilt': passed,
        'tools': {},
        'commands': records,
    }
    for name, tool in [('glslangValidator', compiler), ('spirv-val', validator)]:
        version = subprocess.run([tool, '--version'], capture_output=True, text=True)
        report['tools'][name] = {'path': tool, 'sha256': sha(tool),
                                 'version': version.stdout + version.stderr}
    (output / 'rebuild-receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    if not passed:
        raise SystemExit('HOF reconstruction did not match; inspect retained output '
                         'and tool versions. Do not replace the archived input.')
    print('Exact HOF SPIR-V rebuilt: ' + actual)


if __name__ == '__main__':
    main()
