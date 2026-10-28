"""Exact Float32/BitVec source-expression queries using existing Z3, not runtime sampling."""
from pathlib import Path
import argparse,sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'.local/tools/ubo-bitproof-python'));import z3 as z
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True);args=parser.parse_args();args.report.parent.mkdir(parents=True,exist_ok=True)
out={'solver':z.get_version_string(),'scope':'source expression only, no GPU performance/native compile; conversion-invalid NaN/Inf domain separately retained','queries':[]}
def solve(name,formula,symbols):
 s=z.Solver();s.set(timeout=30000);s.add(formula);r=s.check();d={'name':name,'result':str(r)}
 if r==z.sat:d['witness']={str(x):str(s.model().eval(x,model_completion=True))for x in symbols}
 elif r==z.unknown:d['unknown_reason']=s.reason_unknown()
 out['queries'].append(d);args.report.write_text(json.dumps(out,indent=2)+'\n');print(name,r,flush=True)
f=z.Float32();x=z.FP('x',f);zero=z.FPVal(0,f);one=z.FPVal(1,f);fourteen=z.FPVal(14,f);finite=z.And(z.Not(z.fpIsNaN(x)),z.Not(z.fpIsInf(x)))
for rm in [z.RNE(),z.RTZ()]:
 floor=z.fpRoundToIntegral(z.RTN(),x);fract=z.fpSub(rm,x,floor);scaled=z.fpMul(rm,fract,fourteen);cell=z.fpRoundToIntegral(z.RTN(),scaled);v=z.fpToSBV(z.RTZ(),cell,z.BitVecSort(32))
 solve('finite-fract-range-'+str(rm),z.And(finite,z.Or(z.fpIsNaN(fract),z.fpIsInf(fract),z.fpLT(fract,zero),z.fpGT(fract,one))),[x,fract])
 solve('finite-cell-range-'+str(rm),z.And(finite,z.Or(z.fpIsNaN(cell),z.fpIsInf(cell),v<0,v>14)),[x,cell,v])
 solve('ideal-cell0to13-counterexample-'+str(rm),z.And(finite,v==14),[x,fract,cell,v])
a,b=z.BitVecs('a b',32);idx=a*z.BitVecVal(14,32)+b;bucket=z.LShR((idx>>6)<<4,4) # value below add handled separately
source=z.LShR(((idx>>6)<<4)+112,4)
solve('int-cell0to14-four-buckets',z.And(a>=0,a<=14,b>=0,b<=14,z.Or(z.ULT(source,7),z.UGT(source,10))),[a,b,idx,source])
y=z.BitVec('y',32);address=z.LShR((y<<4)+112,4);fold=(y+7)&z.BitVecVal(0x0fffffff,32)
solve('full32-address-tail-mask-equality',address!=fold,[y,address,fold])
solve('unconstrained32-index-four-bucket-counterexample',z.Or(z.ULT(source,7),z.UGT(source,10)),[a,b,idx,source])
