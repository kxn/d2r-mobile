"""Derive owned metadata admission fixtures from the existing owned GPU fixture."""
from pathlib import Path
import argparse,hashlib,json,struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--source-fixture",type=Path,required=True)
p.add_argument("--output",type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
records=[]
def emit(key,meta,spv):
 for suffix,data in (("meta",meta),("spv",spv)):
  dest=a.output/(key+"."+suffix);dest.write_bytes(data)
  records.append({"path":str(dest),"sha256":hashlib.sha256(data).hexdigest()})
def raster_meta(raw,flags):
 h=list(struct.unpack("<8I",raw[:32]));assert h[0]==0x314b5644
 h[0]=0x324b5644
 return struct.pack("<12I",*h,0xffffffff,flags,15,1)+raw[32:]
for i in range(2):
 key=hashlib.sha256(bytes([i])*0x880).hexdigest()[:16]
 raw=(a.source_fixture/(key+".meta")).read_bytes()
 spv=(a.source_fixture/(key+".spv")).read_bytes()
 emit(key,raster_meta(raw,8) if i else raw,spv)
 if i==0:emit("000000000000f001",raster_meta(raw,8),spv)
 else:
  emit("000000000000f002",raster_meta(raw,16),spv)
  emit("000000000000f003",raster_meta(raw,0x80000000),spv)
(a.output/"manifest.json").write_text(json.dumps({"scope":"Owned fixture metadata contracts; no transformed shader algorithm proof", "source":str(a.source_fixture),"files":records},indent=2)+"\n")
