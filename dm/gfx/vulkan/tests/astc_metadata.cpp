// Owned negative/size-boundary checks; no game assets or GPU are required.
#include "../../tex/nvn_astc_metadata.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <fstream>
static unsigned checks;
#define CHECK(x) do {assert(x);checks++;}while(0)
static void bits(uint8_t block[16],unsigned off,unsigned n,unsigned value) {
  for(unsigned b=0;b<n;b++) {
    unsigned mask=1u<<((off+b)%8);
    block[(off+b)/8]=(block[(off+b)/8]&~mask)|(((value>>b)&1)?mask:0);
  }
}
int main(int argc,char **argv) {
  uint8_t constant[16]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,31,31,63,63,127,127,255,255};
  CHECK(nvn_astc_mip_is_ldr(constant,16,4,4));
  CHECK(nvn_astc_mip_is_ldr(constant,16,6,6));
  uint8_t bad[16];memcpy(bad,constant,16);bad[1]|=2;
  CHECK(!nvn_astc_mip_is_ldr(bad,16,4,4)); // FP16 void extent, not U16.
  memcpy(bad,constant,16);bad[1]&=~4;
  CHECK(!nvn_astc_mip_is_ldr(bad,16,4,4)); // reserved constant bit.
  memset(bad,0,16);CHECK(!nvn_astc_mip_is_ldr(bad,16,4,4));
  CHECK(!nvn_astc_mip_is_ldr(nullptr,16,4,4));
  CHECK(!nvn_astc_mip_is_ldr(constant,0,4,4));
  CHECK(!nvn_astc_mip_is_ldr(constant,15,4,4));
  CHECK(!nvn_astc_mip_is_ldr(constant,16,7,7));
  uint8_t pair[32];memcpy(pair,constant,16);memcpy(pair+16,bad,16);
  CHECK(!nvn_astc_mip_is_ldr(pair,32,4,4));
  unsigned valid=0;
  for(unsigned mode=0;mode<2048;mode++) {
    memset(bad,0,16);bits(bad,0,11,mode);
    // At least some valid nonconstant LDR blocks must survive. These are
    // metadata fixtures, not claimed artist-quality encoder output.
    if(nvn_astc_mip_is_ldr(bad,16,12,12)) {
      valid++;
      for(unsigned cem: {2u,3u,7u,11u,14u,15u}) {
        bits(bad,13,4,cem);CHECK(!nvn_astc_mip_is_ldr(bad,16,12,12));
      }
    }
    // Exercise every mode, partition count and CEM under sanitizer. A
    // deterministic byte oracle, no sampling or timing implementation.
    for(unsigned parts=0;parts<4;parts++)for(unsigned cem=0;cem<16;cem++) {
      bits(bad,11,2,parts);bits(bad,13,4,cem);
      (void)nvn_astc_mip_is_ldr(bad,16,12,12);
    }
  }
  CHECK(valid>0);
  for(int n=1;n<argc;n++) {
    std::ifstream f(argv[n],std::ios::binary);CHECK(f.good());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
    CHECK(data.size()>=32 && data[0]==0x13 && data[1]==0xab && data[2]==0xa1 && data[3]==0x5c);
    CHECK(data[6]==1);CHECK(nvn_astc_mip_is_ldr(data.data()+16,data.size()-16,data[4],data[5]));
  }
  printf("{\"test\":\"astc-metadata\",\"checks\":%u,\"nonconstant_modes\":%u,\"result\":\"passed\"}\n",checks,valid);
}
