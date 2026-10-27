#!/usr/bin/env python3
"""Repackage one frozen fixture ELF/DEX/assets under the original game app ID.

Uses Android SDK packaging/signing tools only. Never compiles or deploys native
code, modifies a game module/profile, or claims measured game performance.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[4]
PACKAGE = 'com.d2rmobile.nativeport'
ACTIVITY = PACKAGE + '.MainActivity'


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def record(path):
    path = Path(path).resolve()
    return {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size}


def require(value, reason):
    if not value:
        raise ValueError(reason)


def build(base, out):
    require(not out.exists(), 'New output required; never overwrite accepted/carrier artifacts')
    ready_path, manifest_path = base / 'ready.json', base / 'candidate.apk.build.json'
    ready_bytes, manifest_bytes = ready_path.read_bytes(), manifest_path.read_bytes()
    ready, source_manifest = json.loads(ready_bytes), json.loads(manifest_bytes)
    ready_sha = hashlib.sha256(ready_bytes).hexdigest()
    manifest_sha = hashlib.sha256(manifest_bytes).hexdigest()
    apk = base / 'candidate.apk'
    require(sha(apk) == ready['APK']['sha256'] == source_manifest['apk_sha256'], 'Fixture APK binding')
    require(ready['candidate_manifest']['sha256'] == sha(manifest_path), 'Fixture manifest binding')
    require(source_manifest['package'] == 'com.d2rmobile.nativefamilyfixture' and
            source_manifest['resident_lazy_requested'] is True, 'Expected explicit resident/lazy fixture source')
    libraries = source_manifest['native_library_sha256']
    require(len(libraries) == 6 and libraries == ready['native_libraries'], 'Exact six-library contract')
    source = Path(ready['source'])
    final_record = ready['final_source_ledger']
    final_ledger_path = Path(final_record['path'])
    require(sha(final_ledger_path) == final_record['sha256'], 'Compiled final source ledger changed')
    ledger = json.loads(final_ledger_path.read_text())
    require(all(sha(source / n) == value for n, value in ledger.items()), 'Compiled frozen source changed')
    baseline_jni = subprocess.check_output(['git', 'show', ready['base_revision'] + ':dm/host/android/jni.c'], cwd=ROOT, text=True)
    helper_path = Path(__file__).with_name('prepare_nvn_fixture.py')
    spec = importlib.util.spec_from_file_location('frozen_fixture_entry_adapter', helper_path)
    helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
    actual_jni = (source / 'dm/host/android/jni.c').read_text()
    require(helper.native_fixture_entry(baseline_jni, True) == actual_jni, 'JNI differs beyond declared cold fixture adaptation')
    boundary = 'JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTests('
    original_prefix, compiled_prefix = baseline_jni.split(boundary, 1)[0], actual_jni.split(boundary, 1)[0]
    require(original_prefix == compiled_prefix and 'DM_NATIVE_RESOURCE_EXPECT_' not in compiled_prefix,
            'Normal nativeStart/game path changed by fixture expectations')
    original_manifest = ROOT / 'dm/android/AndroidManifest.xml'
    original_xml = original_manifest.read_text()
    baseline_xml = subprocess.check_output(['git', 'show', ready['base_revision'] + ':dm/android/AndroidManifest.xml'], cwd=ROOT, text=True)
    require(original_xml == baseline_xml, 'Original manifest changed since compiled source snapshot')
    expected_fixture_xml = original_xml.replace('package="' + PACKAGE + '"', 'package="com.d2rmobile.nativefamilyfixture"').replace('android:name=".MainActivity"', 'android:name="' + ACTIVITY + '"')
    require(expected_fixture_xml == (source / 'dm/android/AndroidManifest.xml').read_text(), 'Unexpected compiled fixture manifest adaptation')
    profile_source = source / 'dm/android/java/com/d2rmobile/nativeport/VerifiedStartupProfile.java'
    require('boolean version6 = "6".equals(schema);' in profile_source.read_text() and
            sha(profile_source) == source_manifest['source_inputs'][str(profile_source.relative_to(source))], 'Frozen schema6 parser source binding')
    resources = source / '.local/build/android-apk/resources.zip'
    require(resources.is_file(), 'Original compiled resources missing')
    accepted = ROOT / '.local/build/record-state-extra-schema6-v1/canonical.apk'
    require(sha(accepted) == '39197b1cbc6cbde76b35ccb461b756c6c69e4e3bd4132d0f868024283837f8c3', 'Accepted restore APK binding')
    out.mkdir(parents=True)
    sdk = ROOT / '.local/android-sdk'; tools = sdk / 'build-tools/36.0.0'
    commands = []
    def run(*args):
        command = list(map(str, args)); commands.append(command)
        return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)
    unsigned, aligned, result_apk = out / 'unsigned.apk', out / 'aligned.apk', out / 'canonical.apk'
    run(tools / 'aapt2', 'link', '-I', sdk / 'platforms/android-36/android.jar',
        '--manifest', original_manifest, '-o', unsigned, resources)
    with zipfile.ZipFile(apk) as src, zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as target:
        present = set(target.namelist())
        for entry in src.infolist():
            if entry.filename in present or entry.filename.startswith('META-INF/'):
                continue
            target.writestr(entry, src.read(entry.filename))
    xml = run(tools / 'aapt2', 'dump', 'xmltree', unsigned, '--file', 'AndroidManifest.xml')
    (out / 'AndroidManifest.xmltree.txt').write_text(xml)
    badging = run(tools / 'aapt2', 'dump', 'badging', unsigned)
    (out / 'badging.txt').write_text(badging)
    require("package: name='" + PACKAGE + "'" in badging and "launchable-activity: name='" + ACTIVITY + "'" in badging,
            'Original package/activity not restored')
    require('com.d2rmobile.nativefamilyfixture' not in xml, 'Fixture package leaked into manifest')
    run(tools / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned)
    run(tools / 'apksigner', 'sign', '--ks', ROOT / '.local/build/android-apk/debug.keystore',
        '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', result_apk, aligned)
    def certificate(path, leaf):
        text = run(tools / 'apksigner', 'verify', '--print-certs', path)
        (out / leaf).write_text(text)
        values = re.findall(r'^Signer #1 certificate SHA-256 digest: ([0-9a-f]{64})$', text, re.M)
        require(len(values) == 1, 'Single signer certificate required')
        return values[0]
    signatures = {'fixture': certificate(apk, 'fixture-signature.txt'),
                  'carrier': certificate(result_apk, 'carrier-signature.txt'),
                  'accepted391': certificate(accepted, 'accepted-signature.txt')}
    require(len(set(signatures.values())) == 1, 'Signing certificate changed')
    run(tools / 'zipalign', '-c', '-P', '16', '4', result_apk)
    with zipfile.ZipFile(apk) as src, zipfile.ZipFile(result_apk) as dst:
        excluded = {'AndroidManifest.xml', 'resources.arsc'}
        original_entries = {n for n in src.namelist() if not n.startswith('META-INF/') and n not in excluded}
        new_entries = {n for n in dst.namelist() if not n.startswith('META-INF/') and n not in excluded}
        require(original_entries == new_entries, 'Payload entry set changed')
        same = {n: hashlib.sha256(dst.read(n)).hexdigest() for n in sorted(original_entries)}
        require(all(hashlib.sha256(src.read(n)).hexdigest() == value for n, value in same.items()), 'ELF/DEX/asset payload bytes changed')
        require(all(same['lib/arm64-v8a/' + n] == value for n, value in libraries.items()), 'Native ELF bytes changed')
        native = {n for n in dst.namelist() if n.startswith('lib/') and n.endswith('.so')}
        require(native == {'lib/arm64-v8a/' + n for n in libraries}, 'Unexpected native library')
        require(not any('launch-profile' in n or n.endswith('.nso') for n in dst.namelist()), 'Profile/game module embedded')
    require(sha(ready_path) == ready_sha and
            sha(manifest_path) == manifest_sha == ready['candidate_manifest']['sha256'] and
            sha(apk) == source_manifest['apk_sha256'] and
            sha(final_ledger_path) == final_record['sha256'], 'Frozen inputs changed')
    result = {'schema': 1, 'package': PACKAGE, 'activity': ACTIVITY, 'apk_sha256': sha(result_apk),
              'host_library_sha256': libraries['libdmhost_android.so'], 'native_library_sha256': libraries,
              'source_revision': ready['base_revision'], 'compiled_source_root': str(source),
              'source_identity_authority': 'Original fixture source/final ledgers and compiler dependency manifest; not accepted391 and not a new native build',
              'compiled_fixture_ready': record(ready_path), 'compiled_fixture_manifest': record(manifest_path),
              'compiled_fixture_APK': record(apk), 'compiled_final_source_ledger': final_record,
              'original_manifest': record(original_manifest), 'original_compiled_resources': record(resources),
              'packager': record(Path(__file__)), 'fixture_entry_adapter': record(helper_path),
              'JNI_cold_fixture_adaptation_only': True,
              'normal_game_JNI_prefix_sha256': hashlib.sha256(compiled_prefix.encode()).hexdigest(),
              'normal_nativeStart_game_path_bytes_unchanged': True, 'fixture_EXPECT_environment_not_set_in_game_path': True,
              'schema6_parser_source': record(profile_source), 'schema6_Java_and_DEX_unchanged': True,
              'profile_embedded_or_modified': False, 'accepted_restore_APK': record(accepted),
              'base_payload_entries_verified_identical': same, 'signer_certificate_sha256': signatures['carrier'],
              'signature_certificates_equal': signatures, 'native_recompiled': False,
              'contains_game_binary': False, 'game_modules_executed': False, 'game_validation': 'NOT_TESTED',
              'performance_conclusion': 'NONE; same-ELF carrier prepared for future controlled B3/linear/tiled game arms',
              'installed': False, 'deployed': False, 'build_commands': commands}
    write = lambda path, value: path.write_text(json.dumps(value, indent=2) + '\n')
    write(out / 'canonical.apk.build.json', result)
    ready_result = {'status': 'HOST_PACKAGED_GAME_CARRIER_NOT_GAME_VALIDATED', 'package': PACKAGE, 'activity': ACTIVITY,
                    'APK': record(result_apk), 'apk_sha256': result['apk_sha256'],
                    'host_library_sha256': result['host_library_sha256'], 'native_libraries': libraries,
                    'source_revision': ready['base_revision'], 'candidate_manifest': record(out / 'canonical.apk.build.json'),
                    'compiled_fixture_ready': record(ready_path), 'native_recompiled': False,
                    'installed': False, 'game_modules_executed': False, 'game_validation': 'NOT_TESTED',
                    'performance_conclusion': 'NONE', 'signer_certificate_sha256': signatures['carrier']}
    write(out / 'ready.json', ready_result)
    return ready_result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(build(a.base.resolve(), a.output.resolve()), indent=2))


if __name__ == '__main__':
    main()
