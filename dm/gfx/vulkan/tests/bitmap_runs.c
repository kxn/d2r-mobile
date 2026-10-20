// Functional interval-sequence differential oracle, not a benchmark/timer.
// The reference loop is the previous production sync_used_pools enumeration.
#include "../nvn_vk_bitmap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint64_t first,end; } span;
typedef struct { span intervals[512];unsigned count,iterations; } result;
static void emit(result *out,uint64_t first,uint64_t end) {
  assert(out->count<512 && first<end);
  out->intervals[out->count++]=(span){first,end};
}
static result enumerate(const uint64_t *words,unsigned count,int candidate) {
  result out={0};uint64_t first=0,end=0;
  for(unsigned word=0;word<count;word++) {
    uint64_t bits=words[word];
    while(bits) {
      unsigned low,length=1;
      if(candidate)length=vk_bitmap_take_run(&bits,&low);
      else {low=(unsigned)__builtin_ctzll(bits);bits&=bits-1;}
      uint64_t page=(uint64_t)word*64+low;
      out.iterations++;
      if(end&&page!=end) {emit(&out,first,end);end=0;}
      if(!end)first=page;
      end=page+length;
    }
  }
  if(end)emit(&out,first,end);
  return out;
}
static unsigned checks;
static void check(const uint64_t *words,unsigned count) {
  uint64_t original[16];assert(count<=16);memcpy(original,words,count*sizeof *words);
  result old=enumerate(words,count,0),now=enumerate(words,count,1);
  assert(old.count==now.count);
  assert(!memcmp(old.intervals,now.intervals,old.count*sizeof *old.intervals));
  assert(now.iterations<=old.iterations);
  assert(!memcmp(words,original,count*sizeof *words));
  checks++;
}
static uint64_t random_word(uint64_t *seed) {
  *seed^=*seed<<13;*seed^=*seed>>7;*seed^=*seed<<17;return *seed;
}
int main(void) {
  uint64_t words[16]={0};check(words,16);
  for(unsigned bit=0;bit<64;bit++) {
    words[0]=UINT64_C(1)<<bit;check(words,1);
    for(unsigned length=1;length<=64-bit;length++) {
      words[0]=(UINT64_MAX>>(64-length))<<bit;check(words,1);
    }
  }
  for(unsigned base=0;base<3;base++)for(unsigned pattern=0;pattern<65536;pattern++) {
    unsigned shift=(unsigned[]){0,31,48}[base];
    words[0]=(uint64_t)pattern<<shift;
    words[1]=~words[0];words[2]=(uint64_t)pattern<<((shift+7)%49);
    check(words,3);
  }
  // Cross-word runs, alternating holes, bit63->next bit0, empty words,
  // incomplete final pool word and upload/GPU-owned-page masking.
  uint64_t seed=UINT64_C(0x56e4dd2327261159);
  for(unsigned trial=0;trial<200000;trial++) {
    unsigned count=1+(unsigned)(random_word(&seed)%16);
    for(unsigned i=0;i<count;i++) {
      uint64_t used=random_word(&seed),overwritten=random_word(&seed);
      words[i]=(trial%3==0?used:trial%3==1?~used:UINT64_MAX)&~overwritten;
    }
    unsigned tail=1+(unsigned)(random_word(&seed)%64);
    words[count-1]&=UINT64_MAX>>(64-tail);
    check(words,count);
  }
  for(unsigned i=0;i<16;i++)words[i]=UINT64_MAX;
  check(words,16);
  result dense_old=enumerate(words,16,0),dense_new=enumerate(words,16,1);
  assert(dense_old.count==1 && dense_new.count==1);
  assert(dense_old.iterations==1024 && dense_new.iterations==16);
  for(unsigned i=0;i<16;i++)words[i]=UINT64_C(0xaaaaaaaaaaaaaaaa);
  check(words,16);
  result sparse=enumerate(words,16,1);assert(sparse.iterations==512);
  printf("bitmap run equivalence: %u cases; dense1024page iterations %u->%u, same1interval; alternating512runs unchanged\n",
    checks,dense_old.iterations,dense_new.iterations);
  return 0;
}
