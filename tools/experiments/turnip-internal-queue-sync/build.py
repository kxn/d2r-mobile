#!/usr/bin/env python3
"""Single Android whitelist opt-in for standard internally synchronized queues; exact 0e53 baseline."""
import argparse
import difflib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BASE = ROOT / '.local/build/turnip-native-bc3-v1'
RECORD_SHA = 'a71ee27bb8019cdec4d10a12fb14cadf76eef8c4e0681e9d8eec860cb2e085cc'
DRIVER_SHA = '0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
HELPER = ROOT / 'tools/experiments/turnip-native-bc3/build.py'
HELPER_SHA = '3fb441cd0db7cf4072461a4e445106ad505c7197db2877accff9f06068818491'
IMAGE = 'src/freedreno/vulkan/tu_image.cc'
POLICY = 'src/vulkan/util/vk_extensions.py'
OLD = '    # on android16-tests-release\n'
NEW = ('    # App-local single-extension experiment, not Android CTS certification.\n'
       '    # Preserve the frozen API34 build floor; this queue feature uses no newer Android ABI.\n'
       '    "VK_KHR_internally_synchronized_queues": 34,\n\n' + OLD)

def load_helper():
    import hashlib
    if hashlib.sha256(HELPER.read_bytes()).hexdigest() != HELPER_SHA:
        raise RuntimeError('Original BC3 reproducible-builder helper changed')
    spec = importlib.util.spec_from_file_location('native_bc3_baseline', HELPER)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


def baseline(h):
    h.require(h.sha(BASE / 'build.json') == RECORD_SHA, '0e53 record changed')
    record = json.loads((BASE / 'build.json').read_text())
    h.require(record['complete'] and record['source_reverified'], '0e53 incomplete')
    h.require(record['driver']['sha256'] == DRIVER_SHA and
              h.sha(Path(record['driver']['path'])) == DRIVER_SHA, '0e53 driver changed')
    _, original = h.check_baseline()  # Full ASTC ledger + declared B3 changes.
    expected = dict(original)
    ledger_path = BASE / 'candidate-source-ledger.json'
    h.require(h.sha(ledger_path) == record['candidate_source_ledger_sha256'],
              '0e53 full ledger changed')
    frozen = json.loads(ledger_path.read_text())
    expected[IMAGE] = frozen[IMAGE]
    h.require(frozen == expected, '0e53 must differ from B3 in only its BC3 whitelist')
    source = BASE / 'mesa-26.2.5'
    h.require(h.manifest(source) == frozen, '0e53 full source changed')
    h.require(h.sha(source / IMAGE) == record['source_changes'][IMAGE]['after'],
              '0e53 image source changed')
    h.require((source / IMAGE).read_text() == h.patched((h.BASE / source.name / IMAGE).read_text()),
              '0e53 is not exactly the original BC3 whitelist expansion')
    h.require(h.sha(BASE / 'android-aarch64.ini') == record['cross_file_sha256'],
              '0e53 cross file changed')
    for name, digest in record['experiment_inputs'].items():
        h.require(h.sha(HELPER.parent / name) == digest, 'BC3 experiment source changed: ' + name)
    for name, tool in record['tools'].items():
        h.require(h.sha(Path(tool.get('path', name))) == tool['sha256'],
                  'Frozen tool changed: ' + name)
    return record, frozen


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path)
    p.add_argument('--verify-only', action='store_true')
    a = p.parse_args()
    h = load_helper()
    previous, ledger = baseline(h)
    old_source = BASE / 'mesa-26.2.5'
    before = (old_source / POLICY).read_text()
    h.require(before.count(OLD) == 1, 'Android policy anchor not unique')
    h.require('"VK_KHR_internally_synchronized_queues"' not in before,
              'Extension unexpectedly already appears in Android policy')
    after = before.replace(OLD, NEW)
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                        fromfile='a/' + POLICY, tofile='b/' + POLICY))
    h.require(patch == (HERE / 'single-android-whitelist.patch').read_text(), 'Review patch mismatch')
    if a.verify_only:
        print(json.dumps({'baseline_verified': True, 'members': len(ledger),
                          'baseline_driver_sha256': DRIVER_SHA, 'changed': [POLICY]}))
        return
    h.require(a.output is not None, '--output required')
    out = a.output.resolve()
    h.require(not out.exists(), 'New output only; never overwrite prior driver/source')
    out.mkdir(parents=True)
    source = out / old_source.name
    shutil.copytree(old_source, source, symlinks=True,
                    ignore=shutil.ignore_patterns('.git', '__pycache__'))
    (source / POLICY).write_text(after)
    candidate = h.manifest(source)
    differences = [k for k in sorted(set(candidate) | set(ledger)) if candidate.get(k) != ledger.get(k)]
    h.require(differences == [POLICY],
              'Candidate changes more than the single Android extension policy')
    h.write(out / 'baseline-source-ledger.json', ledger)
    h.write(out / 'candidate-source-ledger.json', candidate)
    shutil.copy2(HERE / 'single-android-whitelist.patch', out / 'single-android-whitelist.patch')
    cross = out / 'android-aarch64.ini'
    shutil.copy2(BASE / cross.name, cross)
    build = out / 'mesa-kgsl-build'
    argv = [x.replace(str(BASE / 'mesa-kgsl-build'), str(build))
             .replace(str(old_source), str(source))
             .replace(str(BASE / cross.name), str(cross)) for x in previous['setup_argv']]
    command = ['ninja', '-C', str(build), '-j', '4', 'src/freedreno/vulkan/libvulkan_freedreno.so']
    env = dict(os.environ, **previous['declared_environment'])
    forbidden = {'CC','CXX','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','PYTHONPATH',
                 'PKG_CONFIG_PATH','PKG_CONFIG_LIBDIR','TU_BUILD_ID_OVERRIDE'}
    h.require(not (forbidden & set(env)), 'Undeclared build environment')
    record = {'complete': False, 'baseline_build': {'path': str(BASE / 'build.json'), 'sha256': RECORD_SHA},
              'baseline_driver_sha256': DRIVER_SHA,
              'baseline_source_ledger_sha256': h.sha(out / 'baseline-source-ledger.json'),
              'candidate_source_ledger_sha256': h.sha(out / 'candidate-source-ledger.json'),
              'source_revision': subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'], text=True).strip(),
              'source_status': subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'], text=True),
              'experiment_inputs': {f.name: h.sha(f) for f in sorted(HERE.iterdir()) if f.is_file()},
              'verified_baseline_builder': {'path': str(HELPER), 'sha256': HELPER_SHA},
              'cross_file_sha256': h.sha(cross), 'setup_argv': argv, 'build_argv': command,
              'declared_environment': previous['declared_environment'], 'tools': previous['tools'],
              'source_changes': {k: {'before': h.sha(old_source / k), 'after': h.sha(source / k)}
                                 for k in [POLICY]},
              'scope': 'Single Android whitelist entry permits the existing public KHR queue feature. '
                       'Frozen API34 setup and all other extension entries unchanged. No image layout, '
                       'UUID algorithm/profile salt, shared memory, WSI, or driver dispatch change. '
                       'Requires actual advertised feature, flagged queue creation and device tests.',
              'android_policy_min_build_api': 34,
              'intended_test_device_api': 36,
              'android_policy_boundary': 'This app-local experimental whitelist is not an Android CTS '
                       'compliance claim. API36-only runtime qualification belongs to the isolated APK. '
                       'No runtime API36 dependency is added to the API34-built driver.'}
    h.write(out / 'build.json', record)
    try:
        for name, cmd in [('setup.log', argv), ('compile.log', command)]:
            with (out / name).open('x') as log:
                subprocess.run(cmd, env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
        generated = build / 'src/vulkan/util/vk_extensions.c'
        original_generated = BASE / 'mesa-kgsl-build/src/vulkan/util/vk_extensions.c'
        generated_before = original_generated.read_text()
        generated_after = generated.read_text()
        expected_generated = generated_before.replace(
            '   .KHR_internally_synchronized_queues = false,',
            '   .KHR_internally_synchronized_queues = ANDROID_API_LEVEL >= 34,')
        h.require(expected_generated != generated_before and generated_after == expected_generated,
                  'Generated extension table differs beyond the single approved Android policy')
        record['generated_extension_table'] = {'before_sha256': h.sha(original_generated),
            'after_sha256': h.sha(generated), 'single_policy_delta_verified': True,
            'before': '.KHR_internally_synchronized_queues = false',
            'after': '.KHR_internally_synchronized_queues = ANDROID_API_LEVEL >= 34'}
        h.require(h.manifest(source) == candidate, 'Candidate source changed during compilation')
        driver = build / 'src/freedreno/vulkan/libvulkan_freedreno.so'
        readelf = next(Path(k).with_name('llvm-readelf') for k in previous['tools'] if k.endswith('/llvm-ar'))
        notes = subprocess.check_output([str(readelf), '-n', str(driver)], text=True)
        (out / 'elf-notes.txt').write_text(notes)
        record.update(complete=True, source_reverified=True,
                      driver={'path': str(driver), 'bytes': driver.stat().st_size, 'sha256': h.sha(driver),
                              'elf_build_id': re.search(r'Build ID: (\w+)', notes).group(1)},
                      device_driver_uuid=None, device_layout_uuid=None, device_pipeline_cache_uuid=None)
    except Exception as exc:
        record['failure'] = str(exc)
        h.write(out / 'build.json', record)
        raise
    h.write(out / 'build.json', record)
    print(json.dumps(record['driver']), flush=True)


if __name__ == '__main__':
    main()
