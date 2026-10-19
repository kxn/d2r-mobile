#!/usr/bin/env python3
"""安装固定版本的 Arm 官方性能工具到本地忽略目录。
Install pinned official Arm profiling tools under the local ignored directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
VERSION = '2026.5'
NAME = f'Arm_Performance_Studio_{VERSION}'
ARCHIVE = NAME + '_linux_x86-64.tgz'
URL = f'https://artifacts.tools.arm.com/arm-performance-studio/{VERSION}/{ARCHIVE}'
# 官方下载服务器的 x-checksum-sha256；更新版本须重新核对官方发布。
# Published x-checksum-sha256 from Arm's download server; recheck on upgrades.
SHA256 = 'f9a776cdabdeb56a228c1d2f153eac6067a38a55d7330d6ac8de84824bcd6c3f'
OMIT_HOST_LINK = NAME + '/renderdoc_for_arm_gpus/share/renderdoc/pylibs/lib/python3.10/sitecustomize.py'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    archive = ROOT / '.local/downloads' / ARCHIVE
    install = ROOT / '.local/tools'
    target = install / NAME
    if not archive.exists():
        if args.verify_only:
            raise SystemExit('official archive missing')
        archive.parent.mkdir(parents=True, exist_ok=True)
        temporary = archive.with_suffix('.download')
        with urllib.request.urlopen(URL, timeout=60) as response, temporary.open('wb') as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if sha(temporary) != SHA256:
            raise SystemExit('Arm archive checksum mismatch; not installed')
        temporary.replace(archive)
    if sha(archive) != SHA256:
        raise SystemExit('Arm archive checksum mismatch; not installed')
    marker = install / 'arm-performance-studio-install.json'
    if not args.verify_only and not marker.exists():
        install.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as bundle:
            for member in bundle.getmembers():
                if Path(member.name).parts[0] != NAME:
                    raise SystemExit('unexpected archive root')
            # 官方包带宿主 /etc 的可选 Python 定制链接；本地安装不引用宿主定制。
            # Omit the vendor archive's optional host /etc Python customization
            # symlink. All other members retain tarfile's safe data filter.
            def safe_member(member, destination):
                if member.name == OMIT_HOST_LINK and member.issym() and member.linkname == '/etc/python3.10/sitecustomize.py':
                    return None
                return tarfile.data_filter(member, destination)
            bundle.extractall(install, filter=safe_member)
    if not (target / 'streamline/Streamline-cli').is_file():
        raise SystemExit('Streamline CLI missing; installation incomplete')
    report = {'version': VERSION, 'url': URL, 'archive_sha256': SHA256,
              'archive_bytes': archive.stat().st_size, 'install': str(target),
              'omitted_optional_host_symlink': OMIT_HOST_LINK}
    marker.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
