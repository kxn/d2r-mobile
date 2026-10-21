// Exercise the same metadata index used by image alias guards. The oracle is
// the old full-list predicate, including uint64 end overflow and live flags.
#include "../nvn_vk_alias_interval.h"
#include "../nvn_vk_alias_negative.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define COUNT 1024
static unsigned long checks;
#define CHECK(v) do {checks++;if(!(v)) {fprintf(stderr,"alias index check %s:%d: %s\n",__FILE__,__LINE__,#v);abort();}}while(0)
typedef struct item {alias_range_node node;uint64_t start,bytes,serial;int live,initialized,astc;} item;
static item items[COUNT];static alias_range_node *root;
static alias_negative_cache negative={.epoch=1};
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
  for(int shared=0;shared<2;shared++) {
    int reference=original(a,b,shared);CHECK(reference==indexed(a,b,shared));
    int cached=!b?0:b>UINT64_MAX-a?1:
        alias_negative_overlap(&negative,root,a,a+b,eligible,shared);
    CHECK(reference==cached);
  }
}
static unsigned verify(const alias_range_node *n,const alias_range_node *lo,
                       const alias_range_node *hi,uint64_t *max_end) {
  if(!n) {*max_end=0;return 0;}
  CHECK(!lo || alias_range_compare(lo,n)<0);CHECK(!hi || alias_range_compare(n,hi)<0);
  item *i=n->owner;CHECK(i->live);CHECK(n==&i->node);CHECK(n->start==i->start);
  CHECK(n->end==i->start+i->bytes);CHECK(n->serial==i->serial);
  uint64_t a,b;unsigned left=verify(n->left,lo,n,&a),right=verify(n->right,n,hi,&b);
  CHECK(abs((int)left-(int)right)<=1);CHECK(n->height==1+(left>right?left:right));
  *max_end=n->end;if(a>*max_end)*max_end=a;if(b>*max_end)*max_end=b;
  CHECK(n->max_end==*max_end);return n->height;
}
static void integrity(void) {uint64_t e;unsigned height=verify(root,NULL,NULL,&e);CHECK(height<=16);}
static void add(unsigned k,uint64_t start,uint64_t bytes,uint64_t serial) {
  item *i=&items[k];CHECK(!i->live);*i=(item){.start=start,.bytes=bytes,.serial=serial,
      .live=1,.initialized=1,.astc=1};
  i->node=(alias_range_node){.start=start,.end=start+bytes,.max_end=start+bytes,
      .serial=serial,.owner=i,.height=1};
  int inserted=0;root=alias_range_insert(root,&i->node,&inserted);CHECK(inserted);
  alias_negative_changed(&negative);
}
static void remove_item(unsigned k) {
  item *i=&items[k];CHECK(i->live);int removed=0;root=alias_range_remove(root,&i->node,&removed);
  CHECK(removed);alias_negative_changed(&negative);i->live=0;memset(&i->node,0,sizeof i->node);
}
int main(void) {
  _Static_assert(sizeof(alias_range_node)==64,"ARM64 and x86_64 node cost");
  _Static_assert(sizeof(alias_negative_cache)==800,"global negative metadata cost");
  query(0,0);query(UINT64_MAX,1);query(UINT64_MAX,0);
  add(0,4096,64,1);query(8192,64);
  CHECK(alias_negative_matches(&negative,8192,8256)); // actual cached empty geometry
  add(1,8192,64,2);CHECK(!alias_negative_matches(&negative,8192,8256));query(8192,64);
  remove_item(1);query(8192,64);CHECK(alias_negative_matches(&negative,8192,8256));
  add(1,8192,64,3);CHECK(!alias_negative_matches(&negative,8192,8256));query(8192,64);
  remove_item(1);query(8192,64);CHECK(alias_negative_matches(&negative,8192,8256));
  // Ineligible overlaps MUST NOT cache absence: initialized/encoding flip
  // without a tree change must immediately become a conflict.
  items[0].initialized=0;query(4096,64);CHECK(!alias_negative_matches(&negative,4096,4160));
  items[0].initialized=1;items[0].astc=0;query(4096,64);items[0].astc=1;query(4096,64);
  query(4080,16);query(4080,17);query(4160,16); // growth/partial/endpoints
  uint64_t collide=8192+512;CHECK(alias_negative_slot(8192,8256)==alias_negative_slot(collide,collide+64));
  query(8192,64);CHECK(alias_negative_gap_matches(&negative,8192,8256));
  query(collide,64);CHECK(alias_negative_gap_matches(&negative,collide,collide+64));
  // Same empty domain, distinct/colliding ranges: gap hits do not rewrite the
  // original exact entry. Both answers still match the original list oracle.
  CHECK(alias_negative_matches(&negative,8192,8256));query(8192,64);
  negative.epoch=UINT64_MAX;alias_negative_changed(&negative);CHECK(!negative.epoch);
  query(8192,64);CHECK(!alias_negative_matches(&negative,8192,8256));
  alias_negative_changed(&negative);CHECK(!negative.epoch); // never ABA-reenable
  negative=(alias_negative_cache){.epoch=1};remove_item(0);

  // Nested/overlapping nodes bound the complement by the PREFIX maximum end,
  // not merely the preceding start or a single predecessor's end.
  add(0,1024,1024,1);add(1,1536,64,2);add(2,4096,128,3);add(3,4096,256,4);
  query(2048,2048);CHECK(negative.gap.start==2048 && negative.gap.end==4096);
  CHECK(alias_negative_gap_matches(&negative,2048,4096));
  CHECK(!alias_negative_gap_matches(&negative,2047,4096));
  CHECK(!alias_negative_gap_matches(&negative,2048,4097));
  query(2304,16);query(4080,16); // both endpoints inside same empty domain
  add(4,3000,32,5);CHECK(!alias_negative_gap_matches(&negative,2304,2320));
  query(3000,1);query(3032,64);CHECK(negative.gap.start==3032 && negative.gap.end==4096);
  remove_item(4);query(2048,2048);CHECK(negative.gap.start==2048 && negative.gap.end==4096);
  for(unsigned k=0;k<4;k++)remove_item(k);
  // Even wrapped stored intervals follow the old full-list predicate. Do not
  // reinterpret them as saturated ranges when proving a complement gap.
  add(0,UINT64_MAX-31,64,6);query(0,16);query(64,64);query(0,UINT64_MAX);
  remove_item(0);

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
  printf("{\"test\":\"exact-image-alias-AVL\",\"checks\":%lu,\"node_bytes\":%zu,\"random_operations\":24000,\"result\":\"passed\"}\n",checks,sizeof(alias_range_node));
}
