"""Official clang record-layout inspection with each real frozen compile command."""
from pathlib import Path
import json,shlex,subprocess,re,hashlib
R=Path('/home/dl/d2r-mobile');H=Path(__file__).resolve().parent;out={}
for label,root in [('v1',R/'.local/build/turnip-memory-critical-schedule-v1'),('v2',H)]:
 c=json.load(open(root/'actual-emitter-compile-command.json'))[0];args=shlex.split(c['command']);new=[];i=0
 while i<len(args):
  if args[i]in ['-o','-MF','-MT','-MQ']:i+=2;continue
  if args[i]in ['-MD','-MMD','-c']:i+=1;continue
  new.append(args[i]);i+=1
 new+=['-fsyntax-only','-Xclang','-fdump-record-layouts'];log=H/(label+'-record-layout.txt')
 with log.open('w')as f:p=subprocess.run(new,cwd=c['directory'],stdout=f,stderr=subprocess.STDOUT)
 assert p.returncode==0,p.returncode
 s=log.read_text();m=re.search(r'\*\*\* Dumping AST Record Layout\n(?:(?!\*\*\* Dumping AST Record Layout).)*?\| struct ir3_instruction\n(?:(?!\*\*\* Dumping AST Record Layout).)*?\[sizeof=(\d+), align=(\d+)\]',s,re.S);assert m,label
 out[label]={'size':int(m[1]),'align':int(m[2]),'record':m[0],'command':new,'compiler_output_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
assert out['v1']['size']==out['v2']['size'];(H/'instruction-metadata-layout.json').write_text(json.dumps(out,indent=2)+'\n');print({k:(v['size'],v['align'])for k,v in out.items()})
