#!/usr/bin/env python3
"""原始 NSO 一次生成原生镜像，并验证输入、规则、工具和所有产物的来源。
Generate native images once from original NSOs, with verified input, rule,
toolchain and artifact provenance. Never consume a previously rewritten image.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
REWRITER = ROOT / 'dm/tools/patch_nso.py'
MODULES = ('main', 'sdk', 'subsdk0')
MANDATORY = ('patched.bin', 'fixups.tsv', 'gates.tsv', 'sites.tsv', 'cuts.tsv', 'params.tsv', 'obs.tsv')
OPTIONAL = ('aim.tsv', 'rules.tsv')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    temporary.replace(path)


def verify_artifacts(directory, artifacts):
    return bool(artifacts) and all(
        Path(name).name == name and (directory / name).is_file() and
        (directory / name).stat().st_size == item['bytes'] and sha(directory / name) == item['sha256']
        for name, item in artifacts.items())


def inventory(directory, names):
    return {name: {'bytes': (directory / name).stat().st_size, 'sha256': sha(directory / name)}
            for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--abi', choices=('elf', 'elfdesc', 'elfdesc-gate'), required=True)
    parser.add_argument('--x18-group-size', type=int, choices=(1, 2, 3),
                        help='ABI3 native x18 instruction group bound; 1 keeps the single-instruction reference, default 3 for ABI3 and 1 otherwise')
    parser.add_argument('--rules', choices=('native', 'touch-run-v4'), default='native')
    parser.add_argument('--private-gates', choices=('0', '1'), default=os.environ.get('DM_PRIVATE_GATES', '1'))
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    group_size = args.x18_group_size if args.x18_group_size is not None else (3 if args.abi == 'elfdesc-gate' else 1)
    if args.abi != 'elfdesc-gate' and group_size != 1:
        parser.error('--x18-group-size above 1 requires --abi elfdesc-gate')
    if args.verify_only and args.force:
        parser.error('--verify-only and --force cannot be combined')
    source, output = args.source.resolve(), args.output.resolve()
    if source == output or source in output.parents or output in source.parents:
        parser.error('original input and generated output must be separate directory trees')
    output.mkdir(parents=True, exist_ok=True)
    options = {'DM_HOST_ABI': args.abi, 'DM_RULESET': args.rules, 'DM_PRIVATE_GATES': args.private_gates,
               'DM_X18_GROUP_SIZE': str(group_size)}
    environment = os.environ.copy()
    environment.update(options)
    # 只由显式声明的规则控制；旧 Android 专属开关不再参与任何生成。
    # Only declared original rules control generation; remove the obsolete
    # Android-specific switch from subprocess environments.
    environment.pop('DM_NATIVE_TOUCH_AIM', None)
    pin = json.loads((ROOT / 'scripts/dev/upstream-source.json').read_text())
    upstream = {'remote': pin['remote'], 'revision': pin['revision']}
    original_rewriter = subprocess.check_output(['git', '-C', str(ROOT), 'show',
                                                 pin['revision'] + ':dm/tools/patch_nso.py'])
    if hashlib.sha256(original_rewriter).hexdigest() != pin['rewriter_sha256']:
        raise SystemExit('pinned upstream rewriter fingerprint differs')
    tools = {'python': sys.version, 'capstone': importlib.metadata.version('capstone'),
             'lz4': importlib.metadata.version('lz4'),
             'rewriter_sha256': sha(REWRITER), 'pipeline_sha256': sha(Path(__file__)),
             'upstream_rewriter_sha256': hashlib.sha256(original_rewriter).hexdigest()}
    records = {}
    bundle_path = output / 'rewrite-manifest.json'
    try:
        previous_bundle = json.loads(bundle_path.read_text())
    except (OSError, ValueError):
        previous_bundle = {}
    shared = previous_bundle.get('artifacts', {})
    required_shared = {'build_src.sha256', *[f'{module}.provenance.json' for module in MODULES]}
    shared_valid = required_shared <= shared.keys() and verify_artifacts(output, shared)
    for module in MODULES:
        original = source / module
        with original.open('rb') as stream:
            header = stream.read(128)
        if len(header) < 128 or header[:4] != b'NSO0':
            parser.error(f'{module}: expected an original NSO0, not a rewritten image')
        build_id = header[0x40:0x50].hex()
        bootlist = ROOT / 'dm/data' / f'bootlist_{build_id}.tsv'
        recipe = {'module': module, 'input': {'path': str(original), 'sha256': sha(original),
                   'bytes': original.stat().st_size, 'build_id': build_id},
                  'options': options, 'tools': tools, 'upstream': upstream,
                  'checkout_revision': git('rev-parse', 'HEAD'),
                  'bootlist_sha256': sha(bootlist) if bootlist.is_file() else None}
        fingerprint = hashlib.sha256(json.dumps(recipe, sort_keys=True).encode()).hexdigest()
        record_path = output / f'{module}.provenance.json'
        try:
            cached = json.loads(record_path.read_text())
        except (OSError, ValueError):
            cached = {}
        artifacts = cached.get('artifacts', {})
        mandatory = {f'{module}.{suffix}' for suffix in MANDATORY}
        valid = (shared_valid and cached.get('fingerprint') == fingerprint and mandatory <= artifacts.keys() and
                 verify_artifacts(output, artifacts))
        if args.verify_only and not valid:
            raise SystemExit(f'{module}: provenance/input/tool/artifact verification failed; rebuild from original NSO')
        if valid and not args.force:
            records[module] = cached
            print(f'{module}: verified cache {artifacts[module + ".patched.bin"]["sha256"]}')
            continue
        bundle_path.unlink(missing_ok=True)
        with tempfile.TemporaryDirectory(prefix=f'.{module}-rewrite-', dir=output.parent) as temporary:
            stage = Path(temporary)
            subprocess.run([sys.executable, str(REWRITER), str(original), str(stage), module],
                           env=environment, check=True)
            names = [f'{module}.{suffix}' for suffix in MANDATORY]
            names += [f'{module}.{suffix}' for suffix in OPTIONAL if (stage / f'{module}.{suffix}').is_file()]
            artifacts = inventory(stage, names)
            for suffix in (*MANDATORY, *OPTIONAL):
                name = f'{module}.{suffix}'
                if name in artifacts:
                    (stage / name).replace(output / name)
                else:
                    (output / name).unlink(missing_ok=True)
            for name in ('build_src.sha256', 'bootlist.tsv', 'bootlist.sha256'):
                if (stage / name).is_file():
                    shutil.copyfile(stage / name, output / name)
                elif module == 'main' and name != 'build_src.sha256':
                    (output / name).unlink(missing_ok=True)
        (output / f'{module}.native.sha256').unlink(missing_ok=True)
        record = {'format': 1, 'fingerprint': fingerprint, 'recipe': recipe, 'artifacts': artifacts}
        write_json(record_path, record)
        records[module] = record
        print(f'{module}: rebuilt from original NSO {artifacts[module + ".patched.bin"]["sha256"]}')
    names = [f'{module}.provenance.json' for module in MODULES]
    names += [name for name in ('build_src.sha256', 'bootlist.tsv', 'bootlist.sha256') if (output / name).is_file()]
    shared = inventory(output, names)
    bundle = {'format': 1, 'pipeline': 'original NSO -> original patch_nso.py -> native image; no chained patches',
              'upstream': upstream, 'options': options, 'tools': tools, 'modules': records, 'artifacts': shared}
    if args.verify_only:
        try:
            expected = json.loads(bundle_path.read_text())
        except (OSError, ValueError) as error:
            raise SystemExit(f'missing or invalid bundle provenance: {error}')
        if expected != bundle or not verify_artifacts(output, expected['artifacts']):
            raise SystemExit('bundle provenance or shared artifacts differ; rebuild from original NSOs')
    else:
        write_json(bundle_path, bundle)
    print(f'Verified rewrite bundle: {bundle_path}')


if __name__ == '__main__':
    main()
