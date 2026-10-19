#!/usr/bin/env python3
"""Apply the tested NSZ 5.0.0 overlapping-header size fix in the active venv."""
import importlib.metadata
from pathlib import Path
import subprocess

dist = importlib.metadata.distribution('nsz')
if dist.version != '5.0.0':
    raise SystemExit('This patch is only verified against NSZ 5.0.0')
source = Path(dist.locate_file('nsz/Decompressor.py'))
fixed = 'nca_size = max(INCOMPRESSIBLE_HEADER_SIZE, max(s.offset + s.size for s in sections))'
if source.read_text().count(fixed) == 2:
    print('NSZ 5.0.0 size fix already applied')
else:
    patch = Path(__file__).with_name('nsz-5.0.0-size.patch').resolve()
    subprocess.run(['patch', '--dry-run', '--forward', str(source), str(patch)], check=True)
    subprocess.run(['patch', '--forward', str(source), str(patch)], check=True)
    if source.read_text().count(fixed) != 2:
        raise SystemExit('NSZ patch verification failed')
    print('Applied NSZ 5.0.0 size fix')
