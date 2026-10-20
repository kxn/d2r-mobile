// Exercise the same metadata index used by image alias guards. The oracle is
// the old full-list predicate, including uint64 end overflow and live flags.
#include "../nvn_vk_alias_interval.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define COUNT 1024
static unsigned long checks,comparisons;
#define CHECK(v) do {checks++;if(!(v)) {fprintf(stderr,"alias index check %s:%d: %s\n",__FILE__,__LINE__,#v);abort();}}while(0)
typedef struct item {alias_range_node node;uint64_t start,bytes,serial;int live,initialized,astc;} item;
static item items[COUNT];static alias_range_node *root;
static uint64_t seed=0xf39bac1234d678e1ULL;
static uint64_t random64(void) {seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
static int eligible(const alias_range_node *n,int shared) {
  item *i=n->owner;return i->initialized && (shared || i->astc) && i->start && i->bytes;
}
static int original(uint64_t a,uint64_t b,int shared) {
  if(!b)return 0;
  if(b>UINT64_MAX-a)return 1;
  for(unsigned k=0;k<COUNT;k++) {item *i=&items[k];
    if(i->live && i->initialized && (shared || i->astc) && i->start && i->bytes &&
        a<i->start+i->bytes && a+b>i->start)return 1;
  }
  return 0;
}
static int indexed(uint64_t a,uint64_t b,int shared) {
  if(!b)return 0;
  if(b>UINT64_MAX-a)return 1;
  return alias_range_overlap(root,a,a+b,eligible,shared);
}
static void query(uint64_t a,uint64_t b) {
  for(int shared=0;shared<2;shared++) {comparisons++;CHECK(original(a,b,shared)==indexed(a,b,shared));}
}
static unsigned verify(const alias_range_node *n,const alias_range_node *lo,
                       const alias_range_node *hi,uint64_t *min_start,uint64_t *max_end) {
  if(!n) {*min_start=UINT64_MAX;*max_end=0;return 0;}
  CHECK(!lo || alias_range_compare(lo,n)<0);CHECK(!hi || alias_range_compare(n,hi)<0);
  item *i=n->owner;CHECK(i->live);CHECK(n==&i->node);CHECK(n->start==i->start);
  CHECK(n->end==i->start+i->bytes);CHECK(n->serial==i->serial);
  uint64_t a,b,amin,bmin;
  unsigned left=verify(n->left,lo,n,&amin,&a),right=verify(n->right,n,hi,&bmin,&b);
  CHECK(abs((int)left-(int)right)<=1);CHECK(n->height==1+(left>right?left:right));
  *max_end=n->end;if(a>*max_end)*max_end=a;if(b>*max_end)*max_end=b;
  *min_start=n->start;if(amin<*min_start)*min_start=amin;if(bmin<*min_start)*min_start=bmin;
  CHECK(n->min_start==*min_start);CHECK(n->max_end==*max_end);return n->height;
}
static void integrity(void) {uint64_t begin,end;unsigned height=verify(root,NULL,NULL,&begin,&end);CHECK(height<=16);}
static void add(unsigned k,uint64_t start,uint64_t bytes,uint64_t serial) {
  item *i=&items[k];CHECK(!i->live);*i=(item){.start=start,.bytes=bytes,.serial=serial,
      .live=1,.initialized=1,.astc=1};
  i->node=(alias_range_node){.start=start,.end=start+bytes,.min_start=start,.max_end=start+bytes,
      .serial=serial,.owner=i,.height=1};
  int inserted=0;root=alias_range_insert(root,&i->node,&inserted);CHECK(inserted);
}
static void remove_item(unsigned k) {
  item *i=&items[k];CHECK(i->live);int removed=0;root=alias_range_remove(root,&i->node,&removed);
  CHECK(removed);i->live=0;memset(&i->node,0,sizeof i->node);
}
int main(void) {
  _Static_assert(sizeof(alias_range_node)==72,"ARM64 and x86_64 node cost");
  query(0,0);query(UINT64_MAX,1);query(UINT64_MAX,0);
  // Four insertion orders force LL/RR/LR/RL rotations. A query wholly before
  // every start is disjoint, including exact adjacency. Remove the minimum
  // then a two-child root; bounds must describe the remaining identities.
  const unsigned orders[4][3]={{2,1,0},{0,1,2},{2,0,1},{0,2,1}};
  for(unsigned order=0;order<4;order++) {
    for(unsigned j=0;j<3;j++) {unsigned k=orders[order][j];add(k,10000+k*100,80,k);integrity();}
    CHECK(root->min_start==10000);
    query(1,1);query(9000,1000);query(9999,1);query(9999,2);
    query(10080,20);query(10100,1);query(10279,1);query(10280,1);
    // Eligibility can change without insertion/removal: never cache it in
    // the bounds, nor stop before a later eligible overlapping identity.
    items[0].initialized=0;query(10000,1);query(10000,201);
    items[1].astc=0;query(10100,1);items[0].initialized=1;query(10000,1);
    remove_item(0);integrity();CHECK(root->min_start==10100);
    add(0,10000,180,0);integrity();query(10099,2);
    while(root) {item *i=root->owner;remove_item((unsigned)(i-items));integrity();}
  }
  // Duplicate starts/serials remain distinct pointer identities; insert sorted
  // and reverse keys, then delete roots with two children and each duplicate.
  for(unsigned k=0;k<COUNT;k++)add(k,4096+(k/4)*64,32,k/8);
  integrity();for(unsigned k=0;k<COUNT;k++) {query(4096+k*16,1);query(4096+k*16,32);}
  for(unsigned k=0;k<COUNT;k+=2)remove_item(k);
  integrity();
  for(unsigned k=1;k<COUNT;k+=2)remove_item(k);
  CHECK(!root);
  for(unsigned k=0;k<COUNT;k++)add(k,UINT64_MAX-k*8,8,k);
  integrity();query(0,UINT64_MAX);query(UINT64_MAX-4,4);query(UINT64_MAX-4,5);
  for(unsigned k=0;k<COUNT;k++)remove_item(k);
  CHECK(!root);
  add(0,0,16,0);add(1,100,0,1);add(2,UINT64_MAX-8,16,2);
  add(3,4096,8192,3);add(4,4096,4096,3);add(5,8192,4096,3);
  query(0,1);query(0,UINT64_MAX);query(99,2);query(4095,1);query(12288,1);
  query(4095,2);query(8191,1);query(8192,1);query(12287,1);integrity();
  for(unsigned k=0;k<6;k++)remove_item(k);
  for(unsigned step=0;step<24000;step++) {
    unsigned k=random64()%COUNT;item *i=&items[k];
    if(!i->live) {
      uint64_t a=(random64()%2048)*16,b=random64()%1024;
      if(step%37==0)a=UINT64_MAX-(random64()%256);
      if(step%79==0)b=UINT64_MAX;
      add(k,a,b,random64()%32);
    }else if(random64()&1)remove_item(k);
    else {i->initialized=!!(random64()&1);i->astc=!!(random64()&1);}
    integrity();
    for(unsigned n=0;n<8;n++)query(random64()%32768,random64()%2048);
    query(0,UINT64_MAX);query(UINT64_MAX-16,random64()%32);
  }
  for(unsigned k=0;k<COUNT;k++)if(items[k].live)remove_item(k);
  CHECK(!root);
  printf("{\"test\":\"exact-image-alias-AVL\",\"checks\":%lu,\"oracle_comparisons\":%lu,\"node_bytes\":%zu,\"random_operations\":24000,\"result\":\"passed\"}\n",checks,comparisons,sizeof(alias_range_node));
}
