from pathlib import Path
import subprocess,json,hashlib,argparse
parser=argparse.ArgumentParser();parser.add_argument('--work-dir',type=Path,default=Path('/home/dl/d2r-mobile/.local/build/turnip-native-astc-whole-linear-v1'));args=parser.parse_args();B=args.work_dir.resolve();S=B/'mesa-26.2.5';OLD=B.parent/'turnip-host-image-v1/mesa-26.2.5'
def body(p):
 s=p.read_text();a=s.index('   {\n      blake3_hasher blake3_ctx;',s.index('props->identicalMemoryTypeRequirements'));z=s.index('\n   /* VK_KHR_acceleration_structure */',a);return s[a:z]
a=body(OLD/'src/freedreno/vulkan/tu_device.cc');b=body(S/'src/freedreno/vulkan/tu_device.cc');prefix=r'''#include "util/mesa-blake3.h"
#include <vulkan/vulkan_core.h>
#include <cstdio>
#include <cstring>
#include <cassert>
struct tu_physical_device {struct {unsigned highest_bank_bit,bank_swizzle_levels,macrotile_mode;}ubwc_config;};
struct props_type {unsigned char optimalTilingLayoutUUID[16];};
'''
code=prefix+'void original(const tu_physical_device*pdevice,props_type*props)\n{'+a+'}\nvoid candidate(const tu_physical_device*pdevice,props_type*props)\n{'+b+'}\n'+r'''
int main(){unsigned matches=0;unsigned char known[16]={0x45,0x29,0xbb,0xb8,0x70,0xcd,0xbd,0xd0,0x2d,0xb2,0x14,0x74,0x2b,0x3e,0x41,0x70};for(unsigned h=0;h<32;++h)for(unsigned s=0;s<=8;++s)for(unsigned m=0;m<2;++m){tu_physical_device p={{h,s,m}};props_type old,now;original(&p,&old);candidate(&p,&now);assert(memcmp(old.optimalTilingLayoutUUID,now.optimalTilingLayoutUUID,16));if(!memcmp(old.optimalTilingLayoutUUID,known,16)){++matches;printf("{\"original_AA_actual_layout_UUID_match\":true,\"highest_bank_bit\":%u,\"bank_swizzle_levels\":%u,\"macrotile_mode\":%u,\"predicted_new_layout_UUID\":\"",h,s,m);for(auto c:now.optimalTilingLayoutUUID)printf("%02x",c);puts("\",\"hardware_query_verified\":false,\"old_UUID_rejected_by_exact_match\":true}");}}assert(matches==1);fprintf(stderr,"PASS 576 config identities differ; unique existing Q AA UUID reconstructed; new predicted UUID still requires actual driver query\n");}
''';target=B/'layout-identity-source-test.cc';target.write_text(code);cmd=['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(S/'src'),'-I'+str(S/'include'),str(target),str(B/'host-fdl/src/util/blake3/libblake3.a'),'-o',str(B/'layout-identity-source-test')]
with(B/'layout-identity-source-build.txt').open('w')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
with(B/'layout-identity-source-result.jsonl').open('w')as f,(B/'layout-identity-source-stderr.txt').open('w')as err:p=subprocess.run([str(B/'layout-identity-source-test')],stdout=f,stderr=err)
assert p.returncode==0;(B/'layout-identity-source-provenance.json').write_text(json.dumps({'source_functions_extracted_unchanged':True,'old_source_sha256':hashlib.sha256((OLD/'src/freedreno/vulkan/tu_device.cc').read_bytes()).hexdigest(),'new_source_sha256':hashlib.sha256((S/'src/freedreno/vulkan/tu_device.cc').read_bytes()).hexdigest(),'actual_old_UUID':'4529bbb870cdbdd02db214742b3e4170','actual_new_query_required':True,'compile_argv':cmd},indent=2)+'\n');print((B/'layout-identity-source-result.jsonl').read_text())
