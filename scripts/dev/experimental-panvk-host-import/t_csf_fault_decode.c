/* Uses Mesa's real CS builder/decoder, never executes GPU or guest code. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "panfrost/genxml/decode.h"
#include "panfrost/genxml/cs_builder.h"
#include "panfrost/vulkan/csf/panvk_kbase_diag_ring.h"
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) {fprintf(stderr,"CHECK %u line%d: %s\n",checks,__LINE__,#x);abort();} } while (0)
static void decode(struct pandecode_context *ctx,uint64_t addr,uint32_t size) {
   simple_mtx_lock(&ctx->lock);
   pandecode_cs_binary_v10(ctx,addr,size);
   simple_mtx_unlock(&ctx->lock);
}
int main(void) {
   const uint32_t capacity=65536;struct panvk_kbase_diag_segment s[2];
   unsigned n;
   /* Two exact captured cursor cases. Both were previously decoded outside
    * the physical ring. Their bytes are not present in the original logs. */
   uint64_t base=0x5efe07c000ull;
   n=panvk_kbase_diag_ring_segments(base,capacity,1078504,1078656,s);
   CHECK(n==1 && s[0].address==0x5efe0834e8ull && s[0].size==152);
   base=0x5efa06f000ull;
   n=panvk_kbase_diag_ring_segments(base,capacity,1394344,1394496,s);
   CHECK(n==1 && s[0].address==0x5efa0736a8ull && s[0].size==152);
   n=panvk_kbase_diag_ring_segments(base,capacity,16ull*capacity+capacity-8,17ull*capacity+16,s);
   CHECK(n==2 && s[0].address==base+capacity-8 && s[0].size==8 && s[1].address==base && s[1].size==16);
   uint64_t begin;
   CHECK(panvk_kbase_diag_tail_begin(1078656,29440,capacity,&begin));
   CHECK(begin==16ull*capacity+29440 && 1078656-begin==640);
   CHECK(panvk_kbase_diag_tail_begin(17ull*capacity+16,capacity-8,capacity,&begin));
   CHECK(begin==17ull*capacity-8);
   CHECK(!panvk_kbase_diag_tail_begin(16,capacity-8,capacity,&begin));
   CHECK(!panvk_kbase_diag_tail_begin(65536,65536,capacity,&begin));
   CHECK(!panvk_kbase_diag_tail_begin(65536,0,capacity,&begin));
   CHECK(!panvk_kbase_diag_ring_segments(base,capacity,8,0,s));
   CHECK(!panvk_kbase_diag_ring_segments(base,capacity,0,capacity+8,s));
   CHECK(!panvk_kbase_diag_ring_segments(UINT64_MAX-7,capacity,0,8,s));
   CHECK(!panvk_kbase_diag_ring_segments(base,capacity,1,8,s));
   CHECK(!panvk_kbase_diag_ring_segments(base,capacity,0,7,s));
   CHECK(!panvk_kbase_diag_ring_segments(base,0,0,8,s));
   CHECK(!panvk_kbase_diag_ring_segments(base,capacity-8,0,8,s));
   /* Real decoder mapped-memory validation in release/NDEBUG mode. */
   uint8_t *data=mmap(NULL,capacity,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
   CHECK(data!=MAP_FAILED);base=0x5efe07c000ull;
   struct pandecode_context *ctx=pandecode_create_context(false);CHECK(ctx);
   pandecode_inject_mmap(ctx,base,data,capacity,"owned-test-ring");
   struct cs_builder b;
   struct cs_builder_conf conf={.nr_registers=96,.nr_kernel_registers=4};
   struct cs_buffer buffer={.cpu=(uint64_t *)data,.gpu=base,.capacity=capacity/8};
   cs_builder_init(&b,&conf,buffer);
   cs_move32_to(&b,cs_reg32(&b,42),0xdeadbeef);cs_end(&b);CHECK(cs_is_valid(&b));
   decode(ctx,base,b.root_chunk.size*8);
   CHECK(data[0]==0xef); /* decoder released its temporary read-only mapping */
   data[0]=0xef;
   /* Unknown mapping, NULL host mapping, truncated span and unaligned span
    * must each produce the actual existing decoder's readable failure. */
   decode(ctx,base+0x1074e8,152);
   decode(ctx,base+capacity-8,16);
   decode(ctx,base+1,8);
   pandecode_inject_mmap(ctx,base+2*capacity,NULL,4096,"unmapped-test");
   decode(ctx,base+2*capacity,8);
   /* Both pieces of a physically wrapped CS ring get official disassembly. */
   n=panvk_kbase_diag_ring_segments(base,capacity,17ull*capacity-8,17ull*capacity+16,s);CHECK(n==2);
   memset(data,0,16);memset(data+capacity-8,0,8);
   for(unsigned i=0;i<n;i++)decode(ctx,s[i].address,s[i].size);
   /* A CALL target absent from mappings is rejected without hiding the valid
    * caller's CS listing; no interpreter or execution path is invoked. */
   cs_builder_fini(&b);cs_builder_init(&b,&conf,buffer);
   cs_move64_to(&b,cs_reg64(&b,0),base+4*capacity);
   cs_move32_to(&b,cs_reg32(&b,2),8);
   cs_call(&b,cs_reg64(&b,0),cs_reg32(&b,2));cs_end(&b);CHECK(cs_is_valid(&b));
   decode(ctx,base,b.root_chunk.size*8);
   cs_builder_fini(&b);pandecode_destroy_context(ctx);CHECK(!munmap(data,capacity));
   printf("actual Mesa CS decoder boundary checks=%u passed; no GPU/guest execution\n",checks);
   return 0;
}
