// Optional independent reference oracle, link the existing Arm astcenc 5.2.0 library.
#define ASTCENC_DYNAMIC_LIBRARY
#include "astcenc.h"
#include "../../tex/nvn_astc_metadata.h"
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
static void check(astcenc_context *c,const uint8_t *b,unsigned w,unsigned h,unsigned &accepted,unsigned &false_ldr,unsigned &invalid_accepted) {
 bool ours=nvn_astc_mip_is_ldr(b,16,w,h); astcenc_block_info info={};
 assert(astcenc_get_block_info(c,b,&info)==ASTCENC_SUCCESS);
 if(ours) {
  accepted++;
  if(info.is_error_block)invalid_accepted++;
  else if(info.is_constant_block) {if(b[1]&2)false_ldr++;} // documented constant info fields only
  else if(info.is_hdr_block)false_ldr++;
 }
}
int main(int argc,char **argv) {
 unsigned accepted=0,false_ldr=0,invalid_accepted=0,blocks=0;uint32_t rng=0x72616e64;
 astcenc_config cfg;assert(astcenc_config_init(ASTCENC_PRF_HDR,12,12,1,0,0,&cfg)==ASTCENC_SUCCESS);
 astcenc_context *ctx;assert(astcenc_context_alloc(&cfg,1,&ctx)==ASTCENC_SUCCESS);
 for(unsigned n=0;n<200000;n++) {
  uint8_t b[16];for(unsigned k=0;k<16;k++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;b[k]=rng;}
  check(ctx,b,12,12,accepted,false_ldr,invalid_accepted);blocks++;
 }
 astcenc_context_free(ctx);
 for(int f=1;f<argc;f++) {
  std::ifstream file(argv[f],std::ios::binary);assert(file.good());std::vector<uint8_t>d((std::istreambuf_iterator<char>(file)),{});
  assert(astcenc_config_init(ASTCENC_PRF_HDR,d[4],d[5],1,0,0,&cfg)==ASTCENC_SUCCESS);
  assert(astcenc_context_alloc(&cfg,1,&ctx)==ASTCENC_SUCCESS);
  for(size_t o=16;o<d.size();o+=16){check(ctx,d.data()+o,d[4],d[5],accepted,false_ldr,invalid_accepted);blocks++;}
  astcenc_context_free(ctx);
 }
 printf("{\"reference\":\"Arm astcenc 5.2.0 block-info\",\"blocks\":%u,\"accepted\":%u,\"false_ldr\":%u,\"invalid_accepted\":%u}\n",blocks,accepted,false_ldr,invalid_accepted);
 return false_ldr||invalid_accepted;
}
