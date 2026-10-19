#!/usr/bin/env python3
"""Vendor verified official libopus1.5.2 sources for the optional shadow HLE.

No game files are read. Large optional neural-model/DNN code is omitted;
DEEP_PLC/DRED/OSCE are disabled in the declared build. Included files are
byte-for-byte upstream and individually fingerprinted.
"""
from pathlib import Path
import argparse
import hashlib
import json
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[2]
URL='https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz'
SHA='65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--archive',type=Path,default=ROOT/'.local/downloads/opus-1.5.2.tar.gz')
a=p.parse_args()
if not a.archive.exists():
    a.archive.parent.mkdir(parents=True,exist_ok=True)
    urllib.request.urlretrieve(URL,a.archive)
assert hashlib.sha256(a.archive.read_bytes()).hexdigest()==SHA,'Opus upstream archive hash mismatch'
target=ROOT/'dm/third_party/opus';target.mkdir(parents=True,exist_ok=True)
files={}
with tarfile.open(a.archive) as archive:
    for member in archive:
        rel=Path(member.name).relative_to('opus-1.5.2')
        if not member.isfile() or '..' in rel.parts or rel.is_absolute():continue
        if rel.parts[0]=='dnn' and rel.suffix!='.h':continue
        data=archive.extractfile(member).read();path=target/rel
        if path.exists() and path.read_bytes()!=data:raise ValueError('modified vendor file: '+str(path))
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        files[rel.as_posix()]=hashlib.sha256(data).hexdigest()
(target/'SHA256SUMS').write_text(''.join(digest+'  '+name+'\n'for name,digest in sorted(files.items())))
(target/'UPSTREAM.json').write_text(json.dumps({'version':'1.5.2','source':URL,'archive_sha256':SHA,
    'official_release':'https://opus-codec.org/release/stable/2024/04/12/libopus-1_5_2.html',
    'selection':'All regular files except dnn non-header files; DNN features strictly disabled',
    'upstream_files':len(files),'license':'COPYING (BSD-3-Clause)'},indent=2)+'\n')
print(target,len(files),'verified unmodified source files')
