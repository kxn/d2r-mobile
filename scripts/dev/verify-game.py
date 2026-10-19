#!/usr/bin/env python3
"""Verify extracted 3.3.94012 content without loading keys or using a GPU."""
import argparse
import hashlib
import json
from pathlib import Path

EXPECTED = {
    'main': '55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e',
    'sdk': '1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017',
    'subsdk0': '67a38fcf89cd13e040a021eb3d157072f38ebf6680a907874c35c7b231b5d4e2',
}
ARCHIVES = ([f'00-{i:08d}' for i in range(1, 7)] +
            [f'{group:02d}-{i:08d}' for group in [1, 2, 3, 4, 5, 17] for i in [1, 2]])


def verify(root):
    errors, hashes = [], {}
    for name, expected in EXPECTED.items():
        file = root / 'ExeFS' / name
        if not file.is_file():
            errors.append(f'missing ExeFS/{name}')
            continue
        with file.open('rb') as stream:
            hashes[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
        if hashes[name] != expected:
            errors.append(f'ExeFS/{name} does not match 3.3.94012')
    resources = root / 'RomFS/data'
    required = ['build_manifest.psv'] + [name + '.data' for name in ARCHIVES]
    for name in required:
        if not (resources / name).is_file() or not (resources / name).stat().st_size:
            errors.append(f'missing or empty RomFS/data/{name}')
    files = [p for p in (root / 'RomFS').rglob('*') if p.is_file()]
    return {'status': 'verified' if not errors else 'failed',
            'exefs_sha256': hashes, 'required_archives': len(ARCHIVES),
            'romfs_files': len(files), 'romfs_bytes': sum(p.stat().st_size for p in files),
            'errors': errors}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = verify(args.directory)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end='')
    raise SystemExit(bool(result['errors']))
