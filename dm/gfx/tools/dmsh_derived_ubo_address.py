"""Explicit verified-cache derivation: proved integer address normalization, VS only.

The complete original receipt is an input authority, not an assertion that the
new SPIR-V has the old identity. Only explicitly selected VS modules and their
original hash-bound closures may change. Verification reruns the finite rewrite
and official SPIRV-Tools; Z3 is separate qualification, never a build dependency.
"""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import dmsh_derived_predicates as base
import dmsh_driver_keys
import dmsh_pixel_local
import dmsh_pixel_local_wire

sha, artifact, canonical, require = base.sha, base.artifact, base.canonical, base.require
ROOT = Path(__file__).resolve().parents[3]
NORMALIZER = ROOT/'tools/experiments/spirv-ubo-address-normalize/normalize.py'
SCHEMA = 'dmsh-verified-cache-ubo-address-v1'
RECORD = '_derived_build.json'
INPUT = '_derived_input_receipt.json'
BASE_MANIFEST = '_derived_base_manifest.json'
SUFFIXES = ('spv', 'json', 'dkey', 'dspan', 'plocal.json', 'plocal')


def source_identity():
    sources = [Path(__file__), Path(base.__file__), Path(dmsh_driver_keys.__file__),
               Path(dmsh_pixel_local.__file__), Path(dmsh_pixel_local_wire.__file__),
               Path(__file__).with_name('dmsh_uniform_predicates.py'), NORMALIZER]
    tools = {}
    for name in ('spirv-opt', 'spirv-dis', 'spirv-as', 'spirv-val'):
        found = shutil.which(name)
        require(found is not None, 'Missing official SPIRV-Tools: '+name)
        path = Path(found).resolve()
        tools[name] = {'path': str(path), 'sha256': sha(path),
                       'version': subprocess.check_output([str(path), '--version'], text=True)}
    require(len({str(Path(v['path']).parent) for v in tools.values()}) == 1,
            'Normalization requires one pinned official tool directory')
    return {'source_files': {str(p.relative_to(ROOT)): sha(p) for p in sources},
            'official_tools': tools}


def selected_reflection(cache, key):
    require(base.KEY.fullmatch(key), 'Invalid selected shader key')
    p = cache/(key+'.json')
    require(p.is_file(), 'Selected key has no original reflection: '+key)
    meta = json.loads(p.read_text())
    require(meta.get('stage') == 'vs' and meta.get('descriptor_set') == 0,
            'Address normalization requires vertex set0: '+key)
    require(meta.get('spirv_sha256') == sha(cache/(key+'.spv')) and
            meta.get('metadata_sha256') == sha(cache/(key+'.meta')),
            'Selected original reflection identity mismatch')
    # Changing a base under an existing alternate module requires transforming
    # and regenerating its complete paired contract; this candidate rejects it.
    require(not any((cache/(key+'.'+s)).exists() for s in ('spec', 'pred', 'tssbo')),
            'Selected VS has a paired alternate module; unsupported derivation')
    require(all((cache/(key+'.'+s)).is_file() for s in SUFFIXES),
            'Selected VS is missing original hash-bound closures')
    return meta


def transform(original, tools):
    with tempfile.TemporaryDirectory(prefix='dmsh_ubo_normalize_') as temporary:
        root = Path(temporary); source = root/'original.spv'; source.write_bytes(original)
        out = root/'normalized'
        subprocess.run([sys.executable, str(NORMALIZER), '--input', str(source),
                        '--output', str(out), '--tools', str(tools)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        result = json.loads((out/'result.json').read_text())
        require(result['changed_shift_results'] > 0, 'Selected VS has no proved address rewrite')
        proof = {k: result[k] for k in ('contract', 'tool_source_sha256', 'proofs',
                 'changed_shift_results', 'rewrite_steps', 'new_float_precision',
                 'memory_operations_or_robustness_changed_by_custom_pass')}
        return (out/'candidate.spv').read_bytes(), proof


def emit_closures(folder, key, original_meta, chain, proof):
    meta = copy.deepcopy(original_meta)
    meta['chain'] = chain
    meta['spirv_sha256'] = sha(folder/(key+'.spv'))
    meta['compiler_options'] = {**meta['compiler_options'], 'ubo_address_normalize': True}
    meta['ubo_address_normalize'] = proof
    meta.update(dmsh_driver_keys.emit_with_spans(folder, key, meta))
    options = original_meta['compiler_options']
    if options.get('pixel_local_reflection'):
        meta['pixel_local'] = dmsh_pixel_local.emit(folder, key, meta)
    if options.get('pixel_local_runtime'):
        meta['pixel_local_runtime'] = dmsh_pixel_local_wire.emit(folder, key, meta)
    (folder/(key+'.json')).write_text(json.dumps(meta, ensure_ascii=False, indent=1)+'\n')
    return meta


def identity_chain(identity):
    import hashlib
    return hashlib.sha256(canonical(identity)).hexdigest()[:12]


def build(cache, receipt, expected_sha, selected, parent, report,
          dry_run=False, expected_shaders=6931):
    original, manifest, keys = base.validate_input(cache, receipt, expected_sha, expected_shaders)
    require(selected and len(set(selected)) == len(selected) and set(selected) <= keys,
            'Unique selected input keys are required')
    reflections = {key: selected_reflection(cache, key) for key in sorted(selected)}
    identity = {'schema': SCHEMA, 'input_receipt_sha256': expected_sha,
                'base_chain': manifest['chain'], 'base_manifest': manifest,
                'selection': sorted(selected), 'sources': source_identity(),
                'operation': 'proved scalar-int32 low-bit address normalization; selected VS only',
                'original_reflections': reflections}
    chain = identity_chain(identity); output = parent/('vk-'+chain)
    require(not output.exists() and not output.resolve().is_relative_to(cache.resolve()),
            'Output must be a new independent cache')
    record = {'schema': SCHEMA, 'identity': identity, 'chain': chain,
              'output': str(output.resolve()), 'status': 'plan' if dry_run else 'building',
              'selected_keys': sorted(selected), 'input_files_verified': len(original['artifacts']),
              'input_shaders_verified': len(keys), 'performance_baseline_eligible': True,
              'performance_note': 'Candidate is unmeasured until an ordinary game A/B; no instrumentation added'}
    report.parent.mkdir(parents=True, exist_ok=True)
    if dry_run:
        report.write_text(json.dumps(record, indent=2)+'\n'); return record
    output.mkdir(parents=True)
    subprocess.run(['cp', '-a', '--reflink=auto', str(cache)+'/.', str(output)], check=True)
    (output/INPUT).write_bytes(receipt.read_bytes())
    (output/BASE_MANIFEST).write_bytes((cache/'_vk_manifest.json').read_bytes())
    tools = Path(identity['sources']['official_tools']['spirv-opt']['path']).parent
    proofs = {}
    for key in sorted(selected):
        source = (cache/(key+'.spv')).read_bytes()
        (output/(key+'.ubo-input')).write_bytes(source)
        candidate, proof = transform(source, tools)
        (output/(key+'.spv')).write_bytes(candidate)
        proofs[key] = proof
        emit_closures(output, key, reflections[key], chain, proof)
        print('Derived exact VS address normalization: '+key, flush=True)
    new_manifest = {'chain': chain, 'expected': len(keys), 'validated': len(keys),
                    'target': manifest['target'], 'toolchain': 'verified-cache-derived; exact VS address normalization',
                    'base_compiler_options': manifest['compiler_options'],
                    'derived_identity': identity, 'ubo_address_proofs': proofs}
    (output/'_vk_manifest.json').write_text(json.dumps(new_manifest, indent=1)+'\n')
    (output/'.chain').write_text(chain+'\n')
    require(source_identity() == identity['sources'], 'Source/tool closure changed during derivation')
    differences = {name: {'original': original['artifacts'].get(name), 'derived': artifact(path)}
                   for name, path in sorted(base.files(output).items())
                   if name not in original['artifacts'] or artifact(path) != original['artifacts'][name]}
    record.update(status='built-not-deployed', ubo_address_proofs=proofs,
                  changed_artifacts=differences,
                  artifacts={name: artifact(path) for name, path in sorted(base.files(output).items())})
    (output/RECORD).write_text(json.dumps(record, indent=2)+'\n')
    record['independent_verification'] = verify(output, expected_sha, expected_shaders)
    report.write_text(json.dumps(record, indent=2)+'\n')
    return record


def verify(cache, expected_input_sha, expected_shaders=6931):
    record = json.loads((cache/RECORD).read_text()); identity = record['identity']
    require(record.get('schema') == identity.get('schema') == SCHEMA, 'Derived schema mismatch')
    require(identity['input_receipt_sha256'] == expected_input_sha == sha(cache/INPUT),
            'Derived input receipt identity mismatch')
    require(identity['sources'] == source_identity(), 'Derived source/tool identity mismatch')
    chain = identity_chain(identity)
    require(record['chain'] == chain == (cache/'.chain').read_text().strip() and cache.name == 'vk-'+chain,
            'Derived chain identity mismatch')
    original = json.loads((cache/INPUT).read_text())
    require(original.get('validated_game_and_builtin_shaders') == expected_shaders and
            original.get('original_proofs_unchanged') is True, 'Invalid complete input receipt')
    require(original['chain'] == identity['base_chain'] == identity['base_manifest']['chain'], 'Original chain mismatch')
    require(artifact(cache/BASE_MANIFEST) == original['artifacts']['_vk_manifest.json'] and
            json.loads((cache/BASE_MANIFEST).read_text()) == identity['base_manifest'],
            'Original manifest identity mismatch')
    selected = identity['selection']; require(selected and len(selected) == len(set(selected)), 'Invalid selection')
    require(set(selected) == set(identity['original_reflections']) == set(record['ubo_address_proofs']), 'Proof selection mismatch')
    expected = record['artifacts']; actual = base.files(cache)
    require(set(actual) == set(expected)|{RECORD}, 'Undeclared derived output')
    for name, info in expected.items(): require(artifact(cache/name) == info, 'Derived artifact mismatch: '+name)
    require(set(expected) == set(original['artifacts'])|{INPUT, BASE_MANIFEST}|{key+'.ubo-input' for key in selected},
            'Unexpected derived file mutation domain')
    mutable = {'.chain', '_vk_manifest.json'}|{key+'.'+s for key in selected for s in SUFFIXES}
    for name, info in original['artifacts'].items():
        if name not in mutable: require(artifact(cache/name) == info, 'Unrelated input artifact changed: '+name)
    keys = {p.stem for p in cache.glob('*.spv')}
    require(len(keys) == expected_shaders and keys == {Path(n).stem for n in original['artifacts'] if n.endswith('.spv')},
            'Derived base key set mismatch')
    manifest = json.loads((cache/'_vk_manifest.json').read_text())
    require(manifest['derived_identity'] == identity and manifest['chain'] == chain and
            manifest['expected'] == manifest['validated'] == expected_shaders and
            manifest['ubo_address_proofs'] == record['ubo_address_proofs'], 'Derived manifest mismatch')
    tools = Path(identity['sources']['official_tools']['spirv-opt']['path']).parent
    for key in selected:
        raw = cache/(key+'.ubo-input')
        require(artifact(raw) == original['artifacts'][key+'.spv'], 'Original selected SPIR-V identity mismatch')
        meta = identity['original_reflections'][key]
        require(meta['spirv_sha256'] == sha(raw) and meta['stage'] == 'vs' and meta['descriptor_set'] == 0,
                'Original selected reflection identity mismatch')
        # The embedded exact original JSON must match the receipt, not merely
        # present a self-consistent rewritten reflection under a new chain.
        original_json = json.dumps(meta, ensure_ascii=False, indent=1)
        original_info = original['artifacts'][key+'.json']
        require(any(len(blob) == original_info['bytes'] and __import__('hashlib').sha256(blob).hexdigest() == original_info['sha256']
                    for blob in (original_json.encode(), (original_json+'\n').encode())),
                'Original reflection serialization identity mismatch')
        candidate, proof = transform(raw.read_bytes(), tools)
        require(candidate == (cache/(key+'.spv')).read_bytes() and proof == record['ubo_address_proofs'][key],
                'Normalization regeneration mismatch: '+key)
        with tempfile.TemporaryDirectory(prefix='dmsh_ubo_closure_verify_') as temporary:
            temp = Path(temporary)
            for suffix in ('spv', 'meta'):
                shutil.copyfile(cache/(key+'.'+suffix), temp/(key+'.'+suffix))
            emit_closures(temp, key, meta, chain, proof)
            for suffix in SUFFIXES:
                require((temp/(key+'.'+suffix)).read_bytes() == (cache/(key+'.'+suffix)).read_bytes(),
                        'Original closure regeneration mismatch: '+key+'.'+suffix)
    require(source_identity() == identity['sources'], 'Source changed during independent verification')
    return {'status': 'passed-not-deployed', 'chain': chain, 'complete_shaders': len(keys),
            'selected_keys_regenerated': selected, 'output_artifacts_verified': len(expected),
            'input_receipt_sha256': expected_input_sha,
            'unchanged_original_artifacts': len(original['artifacts'])-len([n for n in mutable if n in original['artifacts']])}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__); sub = p.add_subparsers(dest='operation', required=True)
    for op in ('plan', 'build'):
        s = sub.add_parser(op); s.add_argument('--input-cache', type=Path, required=True)
        s.add_argument('--input-receipt', type=Path, required=True); s.add_argument('--input-receipt-sha256', required=True)
        s.add_argument('--keys', nargs='+', required=True); s.add_argument('--output-parent', type=Path, required=True)
        s.add_argument('--report', type=Path, required=True)
    s = sub.add_parser('verify'); s.add_argument('--cache', type=Path, required=True)
    s.add_argument('--input-receipt-sha256', required=True); s.add_argument('--report', type=Path, required=True)
    a = p.parse_args(argv)
    if a.operation == 'verify':
        result = verify(a.cache.resolve(), a.input_receipt_sha256)
        a.report.write_text(json.dumps(result, indent=2)+'\n')
    else:
        result = build(a.input_cache.resolve(), a.input_receipt.resolve(), a.input_receipt_sha256,
                       a.keys, a.output_parent.resolve(), a.report.resolve(), a.operation == 'plan')
    print(json.dumps({k: v for k, v in result.items() if k not in ('identity', 'artifacts', 'ubo_address_proofs', 'changed_artifacts')}, indent=2))

if __name__ == '__main__': main()
