// Owned encoder/replay oracle for the incremental extra-state candidate.
// Fixed-state cache remains ON in both arms. No GPU, game module or timing.
#include "nvn_obj.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
static size_t compare_bytes,compare_calls,total_compare_bytes,total_compare_calls;
static int observe_compare(const void *a,const void *b,size_t n) {
    compare_bytes+=n;compare_calls++;total_compare_bytes+=n;total_compare_calls++;return memcmp(a,b,n);
}
// Observe the actual size passed by production shadow comparisons, test-only.
#define memcmp observe_compare
#include "nvn_record_state.h"
#undef memcmp
static void die(const char *message,...) {fprintf(stderr,"%s\n",message);abort();}
#define LOG(...) do {} while(0)
#define NVN(name,result,...) static result h_##name(__VA_ARGS__)
static void x_cpu_exposure(uint64_t address,uint64_t bytes) {(void)address;(void)bytes;}
static int fail_cache_allocation;
static void *fixture_calloc(size_t n,size_t size) {
    if(fail_cache_allocation) {fail_cache_allocation=0;return NULL;}return calloc(n,size);
}
#define calloc fixture_calloc
#include "recording-under-test.inc"
#undef calloc
uint64_t dm_nvn_frame_count(void) {return 0;}
static struct {va_st attr[16];vs_st stream[16];int stencil_value[2],stencil_mask[2],stencil_ref[2];} S;
static void bounds(int first,int n,int limit) {assert(first>=0&&n>=0&&first<=limit&&n<=limit-first);}
static void pipeline_set(void *dst,const void *src,size_t bytes) {memcpy(dst,src,bytes);}
static void apply_extra(uint32_t op,const void *data) {
    switch(op) {
#include "replay-extra-under-test.inc"
    default:abort();
    }
}
static int enabled;
static unsigned op_counts[OP_COUNT_];
static unsigned long long semantic_hash;
static void observe(const void *bytes,size_t n) {
    const unsigned char *p=bytes;for(size_t i=0;i<n;i++)semantic_hash=(semantic_hash^p[i])*1099511628211ull;
}
static void walk(NVNcommandHandle handle,unsigned depth,int apply) {
    assert(depth<32);const uint8_t *p=(const void *)(uintptr_t)handle;
    for(unsigned guard=0;guard<10000;guard++) {
        op_hdr h;memcpy(&h,p,8);assert(h.size>=8&&!(h.size&7)&&h.op<OP_COUNT_);
        const void *a=p+8;uint64_t next;op_counts[h.op]++;
        switch(h.op) {
        case OP_END:return;
        case OP_BEGIN:break;
        case OP_JUMP:memcpy(&next,a,8);p=(const void *)(uintptr_t)next;continue;
        case OP_CALL:memcpy(&next,a,8);walk(next,depth+1,apply);break;
        case OP_BIND_VATTR:case OP_BIND_VSTREAM:case OP_STENCIL:
            if(apply)apply_extra(h.op,a);break;
        case OP_DRAW:if(apply) {observe(&S,sizeof S);observe(a,sizeof(p_draw));}break;
        case OP_BARRIER:case OP_REPORT_COUNTER:case OP_RESET_COUNTER:case OP_UPDATE_UB:
            if(apply)observe(a,h.size-8);break;
        default:die("unexpected extra fixture opcode");
        }
        p+=h.size;
    }
    die("fixture command stream did not terminate");
}
static unsigned count(NVNcommandHandle h,uint32_t op) {
    memset(op_counts,0,sizeof op_counts);walk(h,0,0);return op_counts[op];
}
static void replay_case(const char *name,NVNcommandHandle h) {
    // Sentinel tails make accidental full-array reset distinguishable from
    // the real prefix-only state application.
    memset(&S,0,sizeof S);for(unsigned i=0;i<16;i++) {S.attr[i].offset=900+i;S.stream[i].stride=700+i;S.stream[i].divisor=600+i;}
    memset(op_counts,0,sizeof op_counts);semantic_hash=1469598103934665603ull;walk(h,0,1);
    printf("OBS %s %llx draws=%u call=%u query=%u/%u update=%u barrier=%u\n",name,semantic_hash,op_counts[OP_DRAW],op_counts[OP_CALL],op_counts[OP_REPORT_COUNTER],op_counts[OP_RESET_COUNTER],op_counts[OP_UPDATE_UB],op_counts[OP_BARRIER]);
}
typedef struct {NVNcommandBuffer command;pool_t pool;uint8_t memory[32768];size_t used;} fixture;
static void supply(fixture *f,size_t bytes) {
    assert(f->used+bytes<=sizeof f->memory);h_nvnCommandBufferAddCommandMemory(&f->command,(NVNmemoryPool *)&f->pool,(ptrdiff_t)f->used,bytes);f->used+=bytes;
}
static void init(fixture *f,int failure) {
    memset(f,0,sizeof *f);f->pool.mem=f->memory;f->pool.size=sizeof f->memory;fail_cache_allocation=failure;
    assert(h_nvnCommandBufferInitialize(&f->command,NULL));
    assert(!!CB(&f->command)->record_state==!failure);
    if(!failure)assert(CB(&f->command)->record_state->extra_enabled==enabled);
    supply(f,8192);
}
static void finish(fixture *f) {h_nvnCommandBufferFinalize(&f->command);assert(!CB(&f->command)->record_state);}
static void draw(fixture *f,int id) {h_nvnCommandBufferDrawArrays(&f->command,NVN_DRAW_PRIMITIVE_TRIANGLES,id,3);}
static NVNcommandHandle end(fixture *f) {return h_nvnCommandBufferEndRecording(&f->command);}
static void stencil(fixture *f,unsigned which,NVNface face,int value) {
    switch(which) {
    case 0:h_nvnCommandBufferSetStencilValueMask(&f->command,face,value);break;
    case 1:h_nvnCommandBufferSetStencilMask(&f->command,face,value);break;
    case 2:h_nvnCommandBufferSetStencilRef(&f->command,face,value);break;
    default:abort();
    }
}
static void layout(fixture *f,int stream,int n,const void *value) {
    if(stream)h_nvnCommandBufferBindVertexStreamState(&f->command,n,value);
    else h_nvnCommandBufferBindVertexAttribState(&f->command,n,value);
}
static void fill_layout(int stream,unsigned n,void *value,unsigned seed) {
    if(stream)for(unsigned i=0;i<n;i++)((vs_st *)value)[i]=(vs_st){seed+i,seed+30+i};
    else for(unsigned i=0;i<n;i++) {va_st *a=value;a[i]=(va_st){0};a[i].fmt=seed%5+1;a[i].offset=seed+4*i;a[i].stream=i%3;}
}
static void check_compare(size_t calls,size_t bytes) {assert(compare_calls==(enabled?calls:0)&&compare_bytes==(enabled?bytes:0));}
static void stencil_values(void) {
    fixture f;init(&f,0);h_nvnCommandBufferBeginRecording(&f.command);compare_bytes=compare_calls=0;
    // Interleaved independent which values may HIT; the full face must match.
    for(unsigned which=0;which<3;which++)stencil(&f,which,NVN_FACE_FRONT_AND_BACK,7+which);
    for(unsigned which=0;which<3;which++)stencil(&f,which,NVN_FACE_FRONT_AND_BACK,7+which);
    check_compare(3,24);draw(&f,1);
    for(unsigned which=0;which<3;which++) {
        stencil(&f,which,NVN_FACE_FRONT,11+which);stencil(&f,which,NVN_FACE_BACK,13+which);
        stencil(&f,which,NVN_FACE_FRONT,11+which); // must record after BACK
    }
    draw(&f,2);NVNcommandHandle h=end(&f);assert(count(h,OP_STENCIL)==(enabled?12:15));replay_case("stencil-which-and-face",h);
    assert(S.stencil_value[0]==11&&S.stencil_value[1]==13&&S.stencil_mask[0]==12&&S.stencil_mask[1]==14&&S.stencil_ref[0]==13&&S.stencil_ref[1]==15);
    // Changing another which does not invalidate a valid full-face reference.
    h_nvnCommandBufferBeginRecording(&f.command);stencil(&f,2,NVN_FACE_FRONT,4);stencil(&f,0,NVN_FACE_BACK,9);stencil(&f,2,NVN_FACE_FRONT,4);draw(&f,3);h=end(&f);
    assert(count(h,OP_STENCIL)==(enabled?2:3));replay_case("independent-stencil-which",h);finish(&f);
}
static void prefix_sizes(void) {
    for(int stream=0;stream<2;stream++)for(unsigned n=1;n<=16;n++) {
        fixture f;init(&f,0);size_t size=n*(stream?8u:4u);void *data=malloc(size);assert(data);fill_layout(stream,n,data,10);
        h_nvnCommandBufferBeginRecording(&f.command);layout(&f,stream,(int)n,data);compare_bytes=compare_calls=0;
        layout(&f,stream,(int)n,data);check_compare(1,size);draw(&f,10);
        fill_layout(stream,n,data,20);layout(&f,stream,(int)n,data);draw(&f,11);
        fill_layout(stream,n,data,10);layout(&f,stream,(int)n,data);layout(&f,stream,(int)n,data);draw(&f,12);
        check_compare(4,4*size);NVNcommandHandle h=end(&f);assert(count(h,stream?OP_BIND_VSTREAM:OP_BIND_VATTR)==(enabled?3:5));
        char name[64];snprintf(name,sizeof name,"%s-exact-n%u",stream?"stream":"attribute",n);replay_case(name,h);
        if(n<16)assert(stream?S.stream[n].stride==700+n:S.attr[n].offset==900+n);
        free(data);finish(&f);
    }
}
static void count_and_empty(void) {
    for(int stream=0;stream<2;stream++) {
        fixture f;init(&f,0);uint64_t values[16]={0};fill_layout(stream,8,values,30);
        h_nvnCommandBufferBeginRecording(&f.command);layout(&f,stream,4,values);layout(&f,stream,2,values);layout(&f,stream,4,values);layout(&f,stream,4,values);
        layout(&f,stream,0,NULL);layout(&f,stream,0,NULL);layout(&f,stream,4,values);
        uint8_t *before=CB(&f.command)->cur;layout(&f,stream,-1,NULL);layout(&f,stream,17,NULL);assert(before==CB(&f.command)->cur);
        layout(&f,stream,4,values); // invalid input did not change logical state
        // Bytes beyond the declared prefix are not compared or captured.
        memset((uint8_t *)values+4*(stream?8:4),0xff,4*(stream?8:4));layout(&f,stream,4,values);draw(&f,20);
        NVNcommandHandle h=end(&f);assert(count(h,stream?OP_BIND_VSTREAM:OP_BIND_VATTR)==(enabled?6:9));replay_case(stream?"stream-count-empty-invalid-tail":"attribute-count-empty-invalid-tail",h);finish(&f);
    }
}
static void set_all(fixture *f,unsigned seed) {
    va_st a[2];vs_st s[2];fill_layout(0,2,a,seed);fill_layout(1,2,s,seed);
    layout(f,0,2,a);layout(f,1,2,s);for(unsigned k=0;k<3;k++)stencil(f,k,NVN_FACE_FRONT_AND_BACK,(int)seed+(int)k);
}
static void lifetime_and_calls(void) {
    fixture f,child,other;init(&f,0);init(&child,0);init(&other,0);
    h_nvnCommandBufferBeginRecording(&child.command);set_all(&child,20);draw(&child,30);NVNcommandHandle hchild=end(&child);
    h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);draw(&f,31);h_nvnCommandBufferCopyCommands(&f.command,1,&hchild);set_all(&f,10);draw(&f,32);NVNcommandHandle h=end(&f);
    assert(count(h,OP_STENCIL)==9&&count(h,OP_BIND_VATTR)==3&&count(h,OP_BIND_VSTREAM)==3);replay_case("CALL-invalidates-all-extra",h);assert(S.attr[0].offset==10&&S.stream[0].stride==10&&S.stencil_ref[1]==12);
    h_nvnCommandBufferBeginRecording(&other.command);set_all(&other,10);draw(&other,33);NVNcommandHandle o=end(&other);assert(count(o,OP_STENCIL)==3);replay_case("separate-command-object",o);
    nvn_record_state_cache *allocation=CB(&f.command)->record_state;
    h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);draw(&f,34);h=end(&f);assert(CB(&f.command)->record_state==allocation);assert(count(h,OP_BIND_VATTR)==1);replay_case("Begin-does-not-inherit-extra",h);
    // Recycle command memory after the first handle has finished being used.
    f.used=0;supply(&f,8192);h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);draw(&f,35);h=end(&f);assert(count(h,OP_STENCIL)==3);finish(&f);replay_case("recycled-memory-Finalize-keeps-payload",h);
    finish(&child);finish(&other);
    // New object lifetime at exactly the same address has no old proof.
    init(&f,0);h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);draw(&f,36);h=end(&f);assert(count(h,OP_STENCIL)==3);replay_case("same-address-new-lifetime",h);finish(&f);
    init(&f,1);h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);set_all(&f,10);draw(&f,37);h=end(&f);assert(count(h,OP_STENCIL)==6&&count(h,OP_BIND_VATTR)==2);replay_case("allocation-failure-original-extra-encoding",h);finish(&f);
}
static fixture *callback_fixture;
static int callback_mode,callback_count;
static NVNcommandHandle callback_call;
static void *callback_data;
static int callback_stream;
static void memory_callback(NVNcommandBuffer *c,NVNcommandBufferMemoryEvent event,size_t bytes,void *unused) {
    (void)c;(void)unused;assert(event==NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY&&bytes<2048);callback_count++;supply(callback_fixture,2048);
    if(callback_mode==1)set_all(callback_fixture,20);
    if(callback_mode==2)h_nvnCommandBufferCopyCommands(&callback_fixture->command,1,&callback_call);
    if(callback_mode==3)fill_layout(callback_stream,2,callback_data,30);
}
static void force_callback(fixture *f) {CB(&f->command)->end=CB(&f->command)->cur+OP_RESERVE;}
static void callbacks(void) {
    fixture f,child;init(&f,0);init(&child,0);callback_fixture=&f;callback_count=0;h_nvnCommandBufferSetMemoryCallback(&f.command,memory_callback);
    callback_mode=1;force_callback(&f);h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,20);draw(&f,40);NVNcommandHandle h=end(&f);assert(count(h,OP_STENCIL)==3&&count(h,OP_BIND_VATTR)==1);replay_case("callback-before-BEGIN",h);
    h_nvnCommandBufferBeginRecording(&child.command);set_all(&child,20);draw(&child,41);callback_call=end(&child);
    // Reentry while allocating each new category must discard every old extra
    // proof, then capture only the final outer command after the callback.
    for(unsigned category=0;category<3;category++) {
        callback_mode=2;h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);force_callback(&f);
        va_st a[2];vs_st s[2];fill_layout(0,2,a,30);fill_layout(1,2,s,30);
        if(category==0)layout(&f,0,2,a);else if(category==1)layout(&f,1,2,s);else stencil(&f,2,NVN_FACE_FRONT,99);
        set_all(&f,10);draw(&f,42+(int)category);h=end(&f);assert(count(h,OP_CALL)==1);replay_case(category==0?"attribute-MISS-callback-CALL":category==1?"stream-MISS-callback-CALL":"stencil-MISS-callback-CALL",h);
        assert(S.attr[0].offset==10&&S.stream[0].stride==10&&S.stencil_ref[0]==12&&S.stencil_ref[1]==12);
    }
    for(int stream=0;stream<2;stream++) {
        uint64_t data[2]={0};callback_stream=stream;callback_data=data;callback_mode=3;
        h_nvnCommandBufferBeginRecording(&f.command);fill_layout(stream,2,data,10);layout(&f,stream,2,data);
        fill_layout(stream,2,data,20);force_callback(&f);layout(&f,stream,2,data);draw(&f,50);
        fill_layout(stream,2,data,20);layout(&f,stream,2,data);draw(&f,51);h=end(&f);assert(count(h,stream?OP_BIND_VSTREAM:OP_BIND_VATTR)==3);replay_case(stream?"stream-callback-mutates-source":"attribute-callback-mutates-source",h);
        assert(stream?S.stream[0].stride==20:S.attr[0].offset==20);
    }
    assert(callback_count==6);finish(&f);finish(&child);
}
static void continuous_jump_and_effects(void) {
    fixture f;init(&f,0);h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);supply(&f,1024);set_all(&f,10);
    h_nvnCommandBufferResetCounter(&f.command,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    h_nvnCommandBufferReportCounter(&f.command,NVN_COUNTER_TYPE_SAMPLES_PASSED,0x20000);
    uint64_t bytes[2]={123,456};h_nvnCommandBufferUpdateUniformBuffer(&f.command,0x30000,128,0,16,bytes);memset(bytes,0xff,sizeof bytes);
    h_nvnCommandBufferBarrier(&f.command,17);set_all(&f,10);draw(&f,60);NVNcommandHandle h=end(&f);
    assert(count(h,OP_JUMP)==1&&count(h,OP_STENCIL)==(enabled?3:9));replay_case("continuous-JUMP-query-update-barrier",h);
    assert(op_counts[OP_REPORT_COUNTER]==1&&op_counts[OP_RESET_COUNTER]==1&&op_counts[OP_UPDATE_UB]==1&&op_counts[OP_BARRIER]==1);finish(&f);
}
int main(int argc,char **argv) {
    if(argc>1) {
        fixture f;memset(&f,0,sizeof f);f.pool.mem=f.memory;f.pool.size=sizeof f.memory;
        assert(h_nvnCommandBufferInitialize(&f.command,NULL));supply(&f,8192);
        if(!strcmp(argv[1],"parent-disabled"))assert(!CB(&f.command)->record_state);
        else {assert(!strcmp(argv[1],"extra-default"));assert(CB(&f.command)->record_state&&!CB(&f.command)->record_state->extra_enabled);}
        h_nvnCommandBufferBeginRecording(&f.command);set_all(&f,10);set_all(&f,10);draw(&f,70);
        NVNcommandHandle h=end(&f);assert(count(h,OP_STENCIL)==6&&count(h,OP_BIND_VATTR)==2&&count(h,OP_BIND_VSTREAM)==2);finish(&f);
        printf("PASS cold gate %s keeps original extra encoding\n",argv[1]);return 0;
    }
    assert(!strcmp(getenv("DM_NVN_RECORD_STATE_CACHE"),"1"));enabled=!strcmp(getenv("DM_NVN_RECORD_STATE_EXTRA_CACHE"),"1");
    stencil_values();prefix_sizes();count_and_empty();lifetime_and_calls();callbacks();continuous_jump_and_effects();
    // Once initialized, the policy remains cold even for later command objects.
    setenv("DM_NVN_RECORD_STATE_EXTRA_CACHE",enabled?"0":"1",1);fixture f;init(&f,0);finish(&f);
    printf("COST shadow_bytes=%zu old_shadow_bytes=151 extra_shadow_bytes=%zu original_field_prefix_bytes=%zu command_bytes=%zu compare_stencil=8 compare_attribute_max=64 compare_stream_max=128 requested_compare_calls=%zu requested_compare_bytes=%zu enabled=%d\n",sizeof(nvn_record_state_cache),sizeof(nvn_record_state_cache)-151,offsetof(nvn_record_state_cache,extra_valid),sizeof(cmd_t),total_compare_calls,total_compare_bytes,enabled);
    printf("PASS incremental extra state encoder/replay; fixed cache remains ON\n");return 0;
}
