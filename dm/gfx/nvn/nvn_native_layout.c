#include "nvn_native_layout.h"
#include "mbedtls/sha256.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static _Atomic(const nvn_native_table *) selected;
static int pow2(uint64_t n) {return n && !(n&(n-1));}
static int valid_recipe(const nvn_native_entry *e,const nvn_native_identity *identity) {
  const nvn_native_recipe *r=&e->recipe; const nvn_texdesc *d=&e->description;
  // Exact supported platform contract, never a generic optimal layout claim.
  uint32_t f=d->format==0x79?157:d->format==0x87?158:
             d->format==0x7d?165:d->format==0x8b?166:0;
  if(e->reserved || !f || d->target!=1 || d->flags || d->samples>1 || d->samples<0 ||
     d->depth!=1 || d->stride || d->pkg_block_h || !d->width || !d->height ||
     d->width>4096 || d->height>4096 || !d->levels || d->levels>NVN_NATIVE_LEVELS || memcmp(r->magic,"DMNRES1",8) ||
     r->version!=1 || r->header_bytes!=sizeof *r || r->format!=f ||
     r->width!=d->width || r->height!=d->height || r->levels!=d->levels ||
     r->depth!=1 || r->layers!=1 || r->image_type!=1 || r->samples!=1 || r->tiling ||
     r->flags || r->sharing || r->initial_layout || r->external_handle!=0x80 ||
     r->producer_layout!=1 || r->producer_family!=0xfffffffd ||
     // SAMPLED | HOST_TRANSFER | optional TRANSFER_SRC/DST. Driver creation
     // and actual per-format capabilities must still validate this recipe.
     !(r->usage&0x400000) || (r->usage&~0x400007u) || !(r->usage&4) ||
     !r->image_size || r->image_size>(16u<<20) || !pow2(r->image_alignment) ||
     r->image_alignment>65536 || r->image_alignment<4096 ||
     memcmp(r->layout_uuid,identity->layout,16) ||
     memcmp(r->device_uuid,identity->device,16) || memcmp(r->driver_uuid,identity->driver,16) ||
     memcmp(r->producer_source_sha256,identity->producer_sha256,32))return 0;
  unsigned max=1;for(uint32_t s=d->width>d->height?d->width:d->height;s>1;s>>=1)max++;
  if(d->levels>max)return 0;
  nvn_native_recipe copy=*r;uint8_t sum[32];memset(copy.digest,0,32);
  mbedtls_sha256((const void *)&copy,sizeof copy,sum,0);
  if(memcmp(sum,r->digest,32))return 0;
  uint64_t end=0;
  for(unsigned n=0;n<NVN_NATIVE_LEVELS;n++) {
    const nvn_native_mip *m=&r->mips[n];
    if(n>=r->levels) {const nvn_native_mip zero={0};if(memcmp(m,&zero,sizeof zero))return 0;continue;}
    if(!m->size || (!n && m->offset) || m->offset<end || m->offset>r->image_size ||
       m->size>r->image_size-m->offset)return 0;
    end=m->offset+m->size;
  }
  return 1;
}
int nvn_native_table_install(const void *data,size_t bytes,const uint8_t pack[32],
                             const nvn_native_identity *identity) {
  if(!data || !pack || !identity || bytes<sizeof(nvn_native_table) ||
     atomic_load_explicit(&selected,memory_order_acquire))return 0;
  // Own an immutable snapshot before validating; never retain caller storage.
  if(bytes>sizeof(nvn_native_table)+(size_t)NVN_NATIVE_MAX_ENTRIES*sizeof(nvn_native_entry))return 0;
  nvn_native_table *t=malloc(bytes);if(!t)return 0;memcpy(t,data,bytes);
  if(memcmp(t->magic,"DMNTBL1",8) || t->version!=1 || t->header_bytes!=sizeof *t ||
     !t->count || t->count>NVN_NATIVE_MAX_ENTRIES || t->entry_bytes!=sizeof(nvn_native_entry) ||
     t->file_bytes!=bytes || bytes!=sizeof *t+(size_t)t->count*sizeof(nvn_native_entry) ||
     memcmp(t->pack_sha256,pack,32) || memcmp(t->driver_sha256,identity->driver_sha256,32) ||
     memcmp(t->producer_sha256,identity->producer_sha256,32))goto fail;
  uint8_t expected[32],sum[32];memcpy(expected,t->digest,32);memset(t->digest,0,32);
  mbedtls_sha256((const void *)t,bytes,sum,0);memcpy(t->digest,expected,32);
  if(memcmp(sum,expected,32))goto fail;
  const nvn_native_entry *entries=(const void *)(t+1);
  for(uint32_t n=0;n<t->count;n++)if(entries[n].id!=n+1 || !valid_recipe(entries+n,identity))goto fail;
  const nvn_native_table *none=NULL;
  if(!atomic_compare_exchange_strong_explicit(&selected,&none,t,memory_order_release,memory_order_relaxed))goto fail;
  return 1;
fail:free(t);return 0;
}
const nvn_native_entry *nvn_native_layout_resolve(const uint8_t p[8],const nvn_texdesc *d) {
  if(!nvn_native_layout_is_marker(p) || p[3]!=1 || !d)return NULL;
  const nvn_native_table *t=atomic_load_explicit(&selected,memory_order_acquire);if(!t)return NULL;
  uint32_t id=(uint32_t)p[4]|(uint32_t)p[5]<<8|(uint32_t)p[6]<<16|(uint32_t)p[7]<<24;
  if(!id || id>t->count)return NULL;
  const nvn_native_entry *e=(const void *)(t+1);e+=id-1;
  nvn_texdesc normalized=*d;normalized.samples=normalized.samples?normalized.samples:1;
  nvn_texdesc expected=e->description;expected.samples=expected.samples?expected.samples:1;
  return memcmp(&normalized,&expected,sizeof expected)?NULL:e;
}
int nvn_native_layout_storage(const uint8_t p[8],const nvn_texdesc *d,nvn_texlayout *out) {
  const nvn_native_entry *e=nvn_native_layout_resolve(p,d);if(!e || !out)return 0;
  *out=(nvn_texlayout){.size=e->recipe.image_size,.layer_size=e->recipe.image_size,
    .align=(uint32_t)e->recipe.image_alignment,.layers=1,.bpb=16};return 1;
}
