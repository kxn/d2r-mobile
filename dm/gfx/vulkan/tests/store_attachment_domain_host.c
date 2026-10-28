// Host semantic oracle around the production planner/Store implementation.
// Descriptor fixtures supply metadata only; no GPU/game/performance claim.
#ifdef DM_STORE_DOMAIN_PLAN_SOURCE
#include DM_STORE_DOMAIN_PLAN_SOURCE
#else
#include "../nvn_vk_plan.c"
#endif
vk_context V;vk_state S;
static tex_t textures[8];static uint8_t memory[8192];static uint64_t test_epoch=1;
static vk_shader shader;static int read_slot;
int vk_tbdr_enabled(void){return 1;}
int vk_replay_preserves_ops(void){return 1;}
void vk_stream_begin(void){} void vk_stream_end(void){}
int vk_ghost_replaying(void){return 0;} int vk_mip_clear_pending(void){return 0;}
int vk_pixel_local_replay_active(void){return 0;} int vk_pool_operation_active(void){return 0;}
int vk_shader_operation_active(void){return 0;} int vk_texture_binding_scope_active(void){return 0;}
int vk_texture_shader_collecting(void){return 0;}
uint64_t vk_texture_descriptor_epoch(void){return test_epoch;}
int vk_texture_descriptor(int id,tex_t *out){if(id<0||id>=8||!textures[id].serial)return 0;*out=textures[id];return 1;}
int vk_texture_descriptor_footprint(int id,vk_texture_footprint *out){tex_t t;if(!vk_texture_descriptor(id,&t))return 0;*out=(vk_texture_footprint){t.serial,(uintptr_t)t.mem,t.L.size};return 1;}
void vk_program_shaders(const prog_t *p,vk_shader *out[6]){(void)p;memset(out,0,6*sizeof *out);out[1]=&shader;}
void vk_state_op(uint32_t op,const void *data){(void)op;(void)data;}
static unsigned checks,scenarios;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"Store attachment domain host line %d: %s\n",__LINE__,#x);abort();}}while(0)
typedef struct {uint64_t words[2048];unsigned used;} stream;
static void op(stream *s,unsigned code,const void *p,unsigned n){forward_test_append((void *)s->words,&s->used,code,p,n);}
static void rt(stream *s,unsigned id){p_set_rt t={.n=1,.color={&textures[id]}};op(s,OP_SET_RT,&t,sizeof t);}
static void draw(stream *s){p_draw d={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};op(s,OP_DRAW,&d,sizeof d);}
static void read_tex(stream *s,unsigned id){p_bind_handles b={.stage=1,.n=1,.h={id}};op(s,OP_BIND_TEX,&b,offsetof(p_bind_handles,h)+8);}
static void clear(stream *s){p_clear_color c={.index=0,.mask=15};op(s,OP_CLEAR_COLOR,&c,sizeof c);}
static void end(stream *s){op(s,OP_END,NULL,0);}
static void reset(int known_shader){memset(&S,0,sizeof S);S.targets=(p_set_rt){.n=1,.color={&textures[1]}};static prog_t program;S.program=known_shader?&program:NULL;shader=(vk_shader){.stage=1,.image_read_count=1,.image_read_slots=&read_slot};read_slot=0;}
static void build(stream *s){uint64_t h=(uintptr_t)s->words;unsigned old=store_walk_comparisons;submission_index_build(&h,1);CK(valid&&index_valid);CK(store_walk_comparisons==old+1);scenarios++;}
static void scenario_set(void){
 // Empty target domain still requires the cold known-footprint proof. Unknown
 // zero-base/zero-size resources retain actual IX_READ events, not SKIP.
 for(unsigned unknown=6;unknown<=7;unknown++) {
  reset(0);S.targets=(p_set_rt){0};stream empty={0};
  p_bind_handles bind={.stage=5,.n=1,.h={unknown}};p_dispatch dispatch={1,1,1,0};
  op(&empty,OP_BIND_TEX,&bind,offsetof(p_bind_handles,h)+8);op(&empty,OP_DISPATCH,&dispatch,sizeof dispatch);end(&empty);build(&empty);
  CK(domain_count==0);CK(domain_skipped==0&&event_count==1);
 }
 // A complete known negative in the empty domain may hit repeatedly, then
 // must revisit target0 when the first SET_RT enlarges that domain.
 reset(0);S.targets=(p_set_rt){0};stream empty_grow={0};
 p_bind_handles compute_read={.stage=5,.n=1,.h={2}};p_dispatch dispatch={1,1,1,0};
 op(&empty_grow,OP_BIND_TEX,&compute_read,offsetof(p_bind_handles,h)+8);
 op(&empty_grow,OP_DISPATCH,&dispatch,sizeof dispatch);op(&empty_grow,OP_DISPATCH,&dispatch,sizeof dispatch);
 rt(&empty_grow,3);read_tex(&empty_grow,2);draw(&empty_grow);rt(&empty_grow,1);draw(&empty_grow);rt(&empty_grow,3);clear(&empty_grow);end(&empty_grow);build(&empty_grow);
 CK(domain_skipped==2&&domain_comparisons>0);
 reset(0);S.targets=(p_set_rt){0};stream empty_unknown_id={0};compute_read.h[0]=63;
 op(&empty_unknown_id,OP_BIND_TEX,&compute_read,offsetof(p_bind_handles,h)+8);op(&empty_unknown_id,OP_DISPATCH,&dispatch,sizeof dispatch);end(&empty_unknown_id);build(&empty_unknown_id);
 CK(domain_considered==0&&global_count==1);

 // R(id2) aliases future B(id3), but not initial A(id1). Its repeated first
 // reads must skip; after B is enrolled the same cached descriptor must keep.
 for(unsigned known=0;known<2;known++) {
  reset(known);stream s={0};read_tex(&s,2);draw(&s);draw(&s);rt(&s,3);read_tex(&s,5);draw(&s);rt(&s,1);read_tex(&s,2);draw(&s);rt(&s,3);clear(&s);end(&s);build(&s);
  CK(domain_skipped>=2&&domain_negative_rechecks>=2);CK(domain_comparisons>=2);
 }
 // Unknown-stage known_zero must mean literal zero handles, not an empty
 // relevant set: target growth changes relevance without another TEX bind.
 reset(0);stream nz={0};read_tex(&nz,2);draw(&nz);rt(&nz,3);draw(&nz);rt(&nz,1);draw(&nz);rt(&nz,3);clear(&nz);end(&nz);build(&nz);
 // Switching to B does NOT close old A. The B draw reads an alias of A;
 // a later full A clear must never move in front of that read's Store proof.
 reset(1);stream old={0};draw(&old);rt(&old,3);read_tex(&old,4);draw(&old);rt(&old,1);clear(&old);end(&old);build(&old);CK(domain_skipped==0);
 // Initial target and read-only/unwritten siblings remain target members.
 reset(1);S.targets.n=2;S.targets.color[1]=&textures[3];stream initial={0};read_tex(&initial,2);draw(&initial);clear(&initial);end(&initial);build(&initial);CK(domain_skipped==0&&domain_count==2);
 // Nested CALL inherits target changes. A child END must not reset the domain.
 reset(1);stream child={0},root={0};rt(&child,3);draw(&child);end(&child);uint64_t call=(uintptr_t)child.words;
 op(&root,OP_CALL,&call,8);read_tex(&root,2);draw(&root);rt(&root,1);read_tex(&root,4);draw(&root);clear(&root);end(&root);build(&root);CK(domain_count==2&&domain_skipped==0);
 // Complete clear before/after actual reads, same serial and overlapping
 // distinct serial; exact ordinals remain separate, not merged across draws.
 reset(1);stream cl={0};read_tex(&cl,4);draw(&cl);clear(&cl);draw(&cl);rt(&cl,3);clear(&cl);read_tex(&cl,2);draw(&cl);end(&cl);build(&cl);CK(domain_skipped==0);
 // Unknown descriptor remains a global requirement, never filtered.
 reset(1);stream unknown={0};read_tex(&unknown,63);draw(&unknown);end(&unknown);build(&unknown);CK(global_count>0&&domain_skipped==0);
 // Copy/clear-tex source events remain conservative even outside RT domain.
 reset(1);stream copies={0};p_copy_t2t copy={.src=&textures[2],.dst=&textures[5]};op(&copies,OP_COPY_T2T,&copy,sizeof copy);end(&copies);build(&copies);CK(event_count==2&&domain_considered==0);
 // No active pass can cross a graphics submission; a saved ghost state may
 // target any earlier logical RT. The oracle queries all prefix targets even
 // after switching away, which includes this saved-target query superset.
 // New submission must also erase all prior positive/negative proofs.
 reset(1);stream fresh={0};read_tex(&fresh,2);draw(&fresh);end(&fresh);build(&fresh);CK(domain_count==1&&domain_skipped==1);
 // Descriptor epoch change invalidates the derived index. Exercise the real
 // vk_pass_store_needed fallback, not just a cache-field mirror assertion.
 reset(1);stream epoch={0};read_tex(&epoch,4);draw(&epoch);rt(&epoch,1);clear(&epoch);end(&epoch);build(&epoch);
 replay=2;done=0;S.tex[1][0]=4;uint64_t before=atomic_load(&index_stats.fallbacks);test_epoch++;
 (void)vk_pass_store_needed(&textures[1],NULL,VK_IMAGE_ASPECT_COLOR_BIT);CK(atomic_load(&index_stats.fallbacks)==before+1);test_epoch--;
}
int main(int argc,char **argv){
 if(argc>1){domain_requested=-1;int expected=atoi(argv[1]);CK(attachment_domain_requested()==expected);setenv("DM_VULKAN_STORE_ATTACHMENT_DOMAIN","invalid-after-freeze",1);CK(attachment_domain_requested()==expected);puts("PASS immutable cold option");return 0;}
 for(unsigned i=1;i<8;i++){textures[i].serial=100+i;textures[i].mem=memory+1024*i;textures[i].L.size=256;textures[i].d.width=textures[i].d.height=8;}
 textures[2].mem=memory+3072+64;textures[3].mem=memory+3072;textures[4].mem=memory+1024+32;
 textures[6].mem=NULL;textures[7].L.size=0;
 V.options.state_segments=1;sparse_requested=0;V.options.store_check=1;domain_requested=0;
 checks+=vk_pass_test_forward_reuse();checks+=vk_pass_test_store_walk_begin();
 // The production table oracle above remains OFF/exact. All following builds
 // use the independent valid-prefix-target query oracle against frozen full.
 domain_requested=1;scenario_set();
 CK(scenarios>=10);CK(atomic_load(&domain_stats.skipped)>0);printf("PASS Store attachment domain production oracle: checks=%u scenarios=%u decisions=%u\n",checks,scenarios,store_walk_checks);
 free(plan);free(events);free(sorted_events);free(globals);free(aliases);return 0;
}
