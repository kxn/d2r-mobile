#!/usr/bin/env python3
"""仅部署已验证来源的完整模块包，手机回读全部哈希后才切换目录。
Deploy only a provenance-verified complete bundle; switch module directories
only after verifying every staged artifact on the phone.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bundle', type=Path, required=True)
parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
directory = args.bundle.resolve()
manifest = directory / 'rewrite-manifest.json'
bundle = json.loads(manifest.read_text())
options = bundle['options']
source = Path(bundle['modules']['main']['recipe']['input']['path']).parent
subprocess.run([sys.executable, str(ROOT / 'scripts/dev/rewrite-native.py'), '--source', str(source),
                '--output', str(directory), '--abi', options['DM_HOST_ABI'], '--rules', options['DM_RULESET'],
                '--x18-group-size', options.get('DM_X18_GROUP_SIZE', '3' if options['DM_HOST_ABI'] == 'elfdesc-gate' else '1'),
                '--private-gates', options['DM_PRIVATE_GATES'], '--verify-only'], check=True)
package = 'com.d2rmobile.nativeport'
base = ['adb', '-s', args.serial]


def adb(*parts, check=True):
    return subprocess.run([*base, *parts], capture_output=True, text=True, check=check)


def stopped():
    if adb('shell', 'pidof', package, check=False).stdout.strip():
        raise SystemExit('game process is running; finish original background saving and stop it before deployment')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


stopped()
artifacts = dict(bundle['artifacts'])
for module in bundle['modules'].values():
    artifacts.update(module['artifacts'])
artifacts['rewrite-manifest.json'] = {'sha256': sha(manifest), 'bytes': manifest.stat().st_size}
if any(Path(name).name != name for name in artifacts):
    raise SystemExit('invalid artifact filename')
token = uuid.uuid4().hex
remote = '/data/local/tmp/d2r-modules-' + token
incoming = 'files/runtime/.modules-' + token
previous = 'files/runtime/.modules-previous'
target = 'files/runtime/modules'
published = False
try:
    adb('shell', 'mkdir', '-p', remote)
    adb('push', *[str(directory / name) for name in artifacts], remote + '/')
    adb('shell', 'run-as', package, 'mkdir', '-p', incoming)
    for name in artifacts:
        adb('shell', 'run-as', package, 'cp', remote + '/' + name, incoming + '/' + name)
    def verify(path):
        lines = adb('exec-out', 'run-as', package, 'sha256sum',
                    *[path + '/' + name for name in artifacts]).stdout.splitlines()
        actual = {Path(line.split(None, 1)[1].strip()).name: line.split(None, 1)[0] for line in lines}
        if actual != {name: item['sha256'] for name, item in artifacts.items()}:
            raise SystemExit('phone module/metadata hash verification failed')
    verify(incoming)
    adb('shell', 'run-as', package, 'touch', incoming + '/.d2r-deploy-managed')
    stopped()
    # 仅轮换工具管理的派生模块目录；存档、RomFS 与密钥不参与部署。
    # Rotate only generated module directories managed by this tool. Saves,
    # RomFS and keys are outside the deployment transaction.
    exists = adb('shell', 'run-as', package, 'test', '-d', previous, check=False).returncode == 0
    if exists:
        if adb('shell', 'run-as', package, 'test', '-f', previous + '/.d2r-deploy-managed', check=False).returncode:
            raise SystemExit('previous directory is not managed by this provenance tool')
        adb('shell', 'run-as', package, 'rm', '-r', previous)
    had_modules = adb('shell', 'run-as', package, 'test', '-d', target, check=False).returncode == 0
    if had_modules:
        adb('shell', 'run-as', package, 'touch', target + '/.d2r-deploy-managed')
        adb('shell', 'run-as', package, 'mv', target, previous)
    try:
        adb('shell', 'run-as', package, 'mv', incoming, target)
    except subprocess.CalledProcessError:
        if had_modules:
            adb('shell', 'run-as', package, 'mv', previous, target)
        raise
    published = True
    try:
        verify(target)
    except BaseException:
        adb('shell', 'run-as', package, 'mv', target, incoming)
        if had_modules:
            adb('shell', 'run-as', package, 'mv', previous, target)
        published = False
        raise
    report = {'serial': args.serial, 'package': package, 'host_abi': options['DM_HOST_ABI'],
              'rules': options['DM_RULESET'], 'bundle_sha256': artifacts['rewrite-manifest.json']['sha256'],
              'artifacts': artifacts, 'verified_on_device': True, 'previous_directory': previous if had_modules else None}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(f'Verified {len(artifacts)} artifacts on {args.serial}; report: {args.report}')
finally:
    adb('shell', 'rm', '-r', remote, check=False)
    if not published:
        adb('shell', 'run-as', package, 'rm', '-r', incoming, check=False)
