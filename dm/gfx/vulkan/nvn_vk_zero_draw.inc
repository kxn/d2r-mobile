// Original Metal zskip_* counterpart. This one shader fact is upstream's
// existing outline-blur rule, not a generic assumption about black textures.
// Resource proofs and side-effect guards remain in the Vulkan backend.
static atomic_int zero_draw_reference=-1;
static atomic_uint_fast64_t zero_draw_skipped;
static atomic_uint_fast64_t zero_draw_attempts,zero_draw_reasons[7];
void vk_zero_draw_proof_stats(uint64_t *attempts,uint64_t reasons[7]) {
  *attempts=atomic_load(&zero_draw_attempts);
  for(unsigned k=0;k<7;k++) reasons[k]=atomic_load(&zero_draw_reasons[k]);
}
static int zero_draw_reject(unsigned reason) {
  atomic_fetch_add(&zero_draw_reasons[reason],1);return 0;
}
// Like upstream zskip_test_add: owned GPU fixtures can exercise the exact
// guards with their own zero-preserving shader. Empty during original games.
static char zero_draw_fixture_key[17];
void vk_zero_draw_test_fact(const char *key) {
  if(key && strlen(key)!=16) die("Vulkan zero-draw fixture key");
  snprintf(zero_draw_fixture_key,sizeof zero_draw_fixture_key,"%s",key?key:"");
}
void vk_zero_draw_reference(int reference) { atomic_store(&zero_draw_reference,!!reference); }
int vk_zero_draw_reference_mode(void) { return atomic_load(&zero_draw_reference)>0; }
uint64_t vk_zero_draw_skipped(void) { return atomic_load(&zero_draw_skipped); }
static int vk_zero_draw_enabled(void) {
  int reference=atomic_load(&zero_draw_reference);
  if(reference<0) {
    const char *v=getenv("DM_NVN_ZSKIP");
    reference=v && !strcmp(v,"0");
    atomic_store(&zero_draw_reference,reference);
  }
  return !reference && vk_tbdr_enabled();
}
static int vk_zero_draw_can_skip(vk_shader *const shaders[6],const void *payload) {
  vk_shader *f=shaders[1];
  if(!f || (strcmp(f->key,"4a819818b8e489c5") &&
      (!zero_draw_fixture_key[0] || strcmp(f->key,zero_draw_fixture_key)))) return 0;
  atomic_fetch_add(&zero_draw_attempts,1);
  if(!vk_zero_draw_enabled()) return zero_draw_reject(0);
  if(S.ds.dw || S.ds.st || S.color.logic_op!=NVN_LOGIC_OP_COPY ||
      S.targets.n<1 || S.targets.n>8) return zero_draw_reject(1);
  for(int k=0;k<S.targets.n;k++) if(S.targets.color[k] && (S.color.blend_en&(1u<<k)))
    return zero_draw_reject(1);
  for(unsigned c=0;c<4;c++) if(!isfinite(S.blend_color[c])) return zero_draw_reject(1);
  // Upstream fact: c4[1].x is the finite blur coefficient. A pending GPU
  // producer is not a license to wait or read a stale CPU mirror here.
  p_bind_buf u=S.ubo[1][4];float coefficient;
  if(u.size<20 || u.addr>UINT64_MAX-16 ||
      vk_pool_try_read(u.addr+16,sizeof coefficient,&coefficient)!=1 || !isfinite(coefficient)) return zero_draw_reject(2);
  unsigned inputs=0,zero_components=15;
  for(unsigned st=0;st<5;st++) if(shaders[st])
    for(unsigned k=0;k<shaders[st]->nb;k++) {
      const vk_binding *b=&shaders[st]->bindings[k];
      if(b->kind==4 || b->kind==9 || vk_binding_writable(b)) return zero_draw_reject(3);
      if(st!=1) continue;
      if(b->kind==5 || b->kind==7) return zero_draw_reject(3); // No separate-sampler or buffer-image proof.
      if(b->kind!=3 && b->kind!=6) continue;
      if(b->reg<0 || b->reg>=128 || b->count!=1) return zero_draw_reject(3);
      uint64_t h=S.tex[1][b->reg];int sid=0;
      if(b->kind==3) {
        if(b->sampler>=128) return zero_draw_reject(3);
        sid=b->sampler>=0?S.samp[1][b->sampler]>>20:h>>20;
      }
      int components=vk_texture_zero_descriptor(h&0xfffff,sid);
      if(!components) return zero_draw_reject(4);
      zero_components&=components;
      inputs++;
    }
  if(!inputs) return zero_draw_reject(3);
  for(int k=0;k<S.targets.n;k++) if(S.targets.color[k]) {
    int components=vk_texture_zero_target(S.targets.color[k],
        S.targets.has_cview[k]?&S.targets.cview[k]:NULL);
    if(!components || (components&~zero_components)) return zero_draw_reject(5);
  }
  // This final check also records the existing query-liveness elision proof.
  if(vk_pass_samples_live(payload)) return zero_draw_reject(6);
  atomic_fetch_add(&zero_draw_skipped,1);
  return 1;
}
