// Driven by tools/tests/test_nvn_record_state.py. Actual production recording
// and Vulkan's fixed-state replay case are included from generated fragments.
#include "nvn_obj.h"
#include "nvn_record_state.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static void die(const char *message, ...) { fprintf(stderr,"%s\n",message);abort(); }
#define LOG(...) do {} while (0)
#define NVN(name, result, ...) static result h_##name(__VA_ARGS__)
static void x_cpu_exposure(uint64_t address,uint64_t bytes) {(void)address;(void)bytes;}
static int fail_cache_allocation;
static void *fixture_calloc(size_t n,size_t size) {
    if(fail_cache_allocation) {fail_cache_allocation=0;return NULL;}
    return calloc(n,size);
}
#define calloc fixture_calloc
#include "recording-under-test.inc"
#undef calloc
uint64_t dm_nvn_frame_count(void) {return 0;}

static struct {blend_st blend[8];color_st color;chmask_st chmask;ms_st ms;poly_st poly;ds_st ds;} S;
static void pipeline_set(void *dst,const void *src,size_t bytes) {memcpy(dst,src,bytes);}
static void apply_state(const void *data) {
    switch(OP_BIND_STATE) {
#include "replay-state-under-test.inc"
    default:abort();
    }
}
static unsigned draws, clears, uniforms, barriers, stencils, calls, jumps, states, reports, resets, updates;
static int enabled;
static unsigned long long semantic_hash=1469598103934665603ull;
static void observe(const void *bytes,size_t n) {
    const unsigned char *p=bytes;
    for(size_t k=0;k<n;k++)semantic_hash=(semantic_hash^p[k])*1099511628211ull;
}
static void replay(NVNcommandHandle handle,unsigned depth) {
    assert(depth<32);const uint8_t *p=(const void *)(uintptr_t)handle;
    for(unsigned guard=0;guard<10000;guard++) {
        op_hdr h;memcpy(&h,p,8);assert(h.size>=8 && !(h.size&7));
        const void *a=p+8;uint64_t next;
        switch(h.op) {
        case OP_END:return;
        case OP_BEGIN:break;
        case OP_JUMP:memcpy(&next,a,8);p=(const void *)(uintptr_t)next;jumps++;continue;
        case OP_CALL:memcpy(&next,a,8);calls++;replay(next,depth+1);break;
        case OP_BIND_STATE:states++;apply_state(a);break;
        case OP_DRAW:draws++;observe(&S,sizeof S);observe(a,sizeof(p_draw));break;
        case OP_CLEAR_COLOR:clears++;observe(a,sizeof(p_clear_color));break;
        case OP_BIND_UBO:uniforms++;observe(a,sizeof(p_bind_buf));break;
        case OP_BARRIER:barriers++;observe(a,8);break;
        case OP_STENCIL:stencils++;observe(a,sizeof(p_stencil));break;
        case OP_REPORT_COUNTER:reports++;observe(a,16);break;
        case OP_RESET_COUNTER:resets++;observe(a,8);break;
        case OP_UPDATE_UB:updates++;observe(a,h.size-8);break;
        default:die("unexpected fixture opcode");
        }
        p+=h.size;
    }
    die("fixture replay did not terminate");
}
static void replay_case(const char *name,NVNcommandHandle handle) {
    draws=clears=uniforms=barriers=stencils=calls=jumps=states=reports=resets=updates=0;
    memset(&S,0,sizeof S);semantic_hash=1469598103934665603ull;
    replay(handle,0);
    printf("OBS %s %llx draws=%u clear=%u ubo=%u barrier=%u stencil=%u calls=%u query=%u/%u update=%u\n",
           name,semantic_hash,draws,clears,uniforms,barriers,stencils,calls,reports,resets,updates);
}
static unsigned count_bindings(NVNcommandHandle handle,unsigned depth) {
    assert(depth<32);const uint8_t *p=(const void *)(uintptr_t)handle;unsigned count=0;
    for(unsigned guard=0;guard<10000;guard++) {
        op_hdr h;memcpy(&h,p,8);const void *a=p+8;uint64_t next;
        if(h.op==OP_END)return count;
        if(h.op==OP_JUMP) {memcpy(&next,a,8);p=(const void *)(uintptr_t)next;continue;}
        if(h.op==OP_CALL) {memcpy(&next,a,8);count+=count_bindings(next,depth+1);}
        if(h.op==OP_BIND_STATE)count++;
        p+=h.size;
    }
    abort();
}
// Each object owns disjoint fixture memory. The real AddCommandMemory path
// inserts the real JUMP record rather than a test-only command representation.
typedef struct {NVNcommandBuffer command;pool_t pool;uint8_t memory[32768];size_t used;} command_fixture;
static void supply(command_fixture *f,size_t bytes) {
    assert(f->used+bytes<=sizeof f->memory);
    h_nvnCommandBufferAddCommandMemory(&f->command,(NVNmemoryPool *)&f->pool,(ptrdiff_t)f->used,bytes);
    f->used+=bytes;
}
static void init(command_fixture *f) {
    memset(f,0,sizeof *f);f->pool.mem=f->memory;f->pool.size=sizeof f->memory;
    assert(h_nvnCommandBufferInitialize(&f->command,NULL));
    assert(!!CB(&f->command)->record_state==enabled);supply(f,4096);
}
static void finish(command_fixture *f) {
    h_nvnCommandBufferFinalize(&f->command);
    assert(!CB(&f->command)->record_state && !CB(&f->command)->recording);
}
static void bind_kind(NVNcommandBuffer *c,unsigned kind,const void *value) {
    switch(kind) {
    case 0:h_nvnCommandBufferBindBlendState(c,value);break;
    case 1:h_nvnCommandBufferBindColorState(c,value);break;
    case 2:h_nvnCommandBufferBindChannelMaskState(c,value);break;
    case 3:h_nvnCommandBufferBindMultisampleState(c,value);break;
    case 4:h_nvnCommandBufferBindPolygonState(c,value);break;
    case 5:h_nvnCommandBufferBindDepthStencilState(c,value);break;
    default:abort();
    }
}
static void draw(NVNcommandBuffer *c,int id) {h_nvnCommandBufferDrawArrays(c,NVN_DRAW_PRIMITIVE_TRIANGLES,id,3);}
static const size_t sizes[6]={sizeof(blend_st),sizeof(color_st),sizeof(chmask_st),sizeof(ms_st),sizeof(poly_st),sizeof(ds_st)};
static void value_for(unsigned kind,unsigned value,uint8_t out[24]) {
    memset(out,0,24);
    switch(kind) {
    case 0:{blend_st b={0};b.target=0;b.src_rgb=value;memcpy(out,&b,sizeof b);break;}
    case 1:{color_st b={0};b.blend_en=value;memcpy(out,&b,sizeof b);break;}
    case 2:{chmask_st b={.mask=value};memcpy(out,&b,sizeof b);break;}
    case 3:{ms_st b={.samples=(uint8_t)value};memcpy(out,&b,sizeof b);break;}
    case 4:{poly_st b={0};b.cull=value;memcpy(out,&b,sizeof b);break;}
    case 5:{ds_st b={0};b.df=value;memcpy(out,&b,sizeof b);break;}
    }
}
static void simple_values(void) {
    command_fixture f;init(&f);h_nvnCommandBufferBeginRecording(&f.command);
    uint8_t values[6][24];
    for(unsigned kind=0;kind<6;kind++) {
        value_for(kind,1,values[kind]);bind_kind(&f.command,kind,values[kind]);
        // Different source address with exactly equal value must also HIT.
        uint8_t other[24];memcpy(other,values[kind],24);bind_kind(&f.command,kind,other);
        draw(&f.command,(int)kind);
    }
    // Other commands remain present and do not change these six state slots.
    float rgba[4]={1,.5f,.25f,1};
    h_nvnCommandBufferClearColor(&f.command,0,rgba,15);
    h_nvnCommandBufferBindUniformBuffer(&f.command,0,0,0x123400,256);
    h_nvnCommandBufferBarrier(&f.command,17);
    h_nvnCommandBufferSetStencilRef(&f.command,NVN_FACE_FRONT_AND_BACK,7);
    h_nvnCommandBufferResetCounter(&f.command,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    h_nvnCommandBufferReportCounter(&f.command,NVN_COUNTER_TYPE_SAMPLES_PASSED,0x123800);
    uint64_t update[2]={0x1122334455667788ull,0x8877665544332211ull};
    h_nvnCommandBufferUpdateUniformBuffer(&f.command,0x123400,256,32,sizeof update,update);
    memset(update,0xff,sizeof update); // original encoder must snapshot bytes
    for(unsigned kind=0;kind<6;kind++)bind_kind(&f.command,kind,values[kind]);
    draw(&f.command,10);
    // Same original address now contains a changed value: never pointer cache.
    for(unsigned kind=0;kind<6;kind++) {value_for(kind,2,values[kind]);bind_kind(&f.command,kind,values[kind]);}
    draw(&f.command,11);
    NVNcommandHandle h=h_nvnCommandBufferEndRecording(&f.command);
    assert(count_bindings(h,0)==(enabled?12:24));
    memset(values,0xff,sizeof values); // encoded payload must remain immutable
    replay_case("all-six-and-side-effects",h);
    assert(draws==8 && clears==1 && uniforms==1 && barriers==1 && stencils==1 && reports==1 && resets==1 && updates==1);
    replay_case("same-handle-reused",h);
    // A new recording starts unknown even when inheriting the old CPU cache.
    nvn_record_state_cache *allocation=CB(&f.command)->record_state;
    h_nvnCommandBufferBeginRecording(&f.command);uint8_t v[24];value_for(1,2,v);
    bind_kind(&f.command,1,v);draw(&f.command,12);
    NVNcommandHandle h2=h_nvnCommandBufferEndRecording(&f.command);
    assert(CB(&f.command)->record_state==allocation);assert(count_bindings(h2,0)==1);
    replay_case("begin-is-unknown",h2);assert(S.color.blend_en==2);finish(&f);
}
static void call_and_targets(void) {
    command_fixture child,parent,other;init(&child);init(&parent);init(&other);
    uint8_t a[24],b[24];value_for(1,1,a);value_for(1,2,b);
    h_nvnCommandBufferBeginRecording(&child.command);bind_kind(&child.command,1,b);draw(&child.command,20);
    NVNcommandHandle h=h_nvnCommandBufferEndRecording(&child.command);
    h_nvnCommandBufferBeginRecording(&parent.command);bind_kind(&parent.command,1,a);draw(&parent.command,21);
    NVNcommandHandle twice[2]={h,h};h_nvnCommandBufferCopyCommands(&parent.command,2,twice);
    bind_kind(&parent.command,1,a);draw(&parent.command,22);
    NVNcommandHandle p=h_nvnCommandBufferEndRecording(&parent.command);
    replay_case("call-inherits-and-restores",p);assert(S.color.blend_en==1 && calls==2 && draws==4);
    assert(count_bindings(p,0)==4);
    h_nvnCommandBufferBeginRecording(&other.command);bind_kind(&other.command,1,a);draw(&other.command,23);
    NVNcommandHandle o=h_nvnCommandBufferEndRecording(&other.command);
    assert(count_bindings(o,0)==1);replay_case("different-command-object",o);
    // Blend slot target is part of the complete value, including returning to
    // a former target after touching another target with identical factors.
    h_nvnCommandBufferBeginRecording(&parent.command);blend_st blend={0};blend.src_rgb=3;
    bind_kind(&parent.command,0,&blend);blend.target=1;bind_kind(&parent.command,0,&blend);
    blend.target=0;bind_kind(&parent.command,0,&blend);draw(&parent.command,24);
    p=h_nvnCommandBufferEndRecording(&parent.command);assert(count_bindings(p,0)==3);
    replay_case("blend-target-is-part-of-value",p);assert(S.blend[0].src_rgb==3 && S.blend[1].src_rgb==3);
    finish(&child);finish(&parent);finish(&other);
}
static void manual_jump(void) {
    command_fixture f;init(&f);uint8_t v[24];value_for(1,3,v);
    h_nvnCommandBufferBeginRecording(&f.command);bind_kind(&f.command,1,v);
    supply(&f,128);bind_kind(&f.command,1,v);draw(&f.command,30);
    NVNcommandHandle h=h_nvnCommandBufferEndRecording(&f.command);
    assert(count_bindings(h,0)==(enabled?1:2));replay_case("continuous-jump",h);assert(jumps==1);finish(&f);
}
static command_fixture *callback_fixture;
static int callback_mode,callback_count;
static NVNcommandHandle callback_call;
static uint8_t *callback_mutable;
static void memory_callback(NVNcommandBuffer *c,NVNcommandBufferMemoryEvent event,size_t bytes,void *unused) {
    (void)unused;assert(event==NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY);assert(bytes<1024);
    callback_count++;supply(callback_fixture,1024);
    if(callback_mode==1) {uint8_t v[24];value_for(1,2,v);bind_kind(c,1,v);}
    if(callback_mode==2)h_nvnCommandBufferCopyCommands(c,1,&callback_call);
    if(callback_mode==3)value_for(1,3,callback_mutable);
}
static void force_callback(command_fixture *f) {CB(&f->command)->end=CB(&f->command)->cur+OP_RESERVE;}
static void callbacks(void) {
    command_fixture f,child;init(&f);init(&child);callback_fixture=&f;callback_count=0;
    h_nvnCommandBufferSetMemoryCallback(&f.command,memory_callback);
    uint8_t a[24],b[24],p[24];value_for(1,1,a);value_for(1,2,b);value_for(4,1,p);
    // The callback binds B before the actual BEGIN header; that command is
    // outside the new handle. The first in-handle B must never be eliminated.
    callback_mode=1;force_callback(&f);h_nvnCommandBufferBeginRecording(&f.command);
    bind_kind(&f.command,1,b);draw(&f.command,40);NVNcommandHandle h=h_nvnCommandBufferEndRecording(&f.command);
    assert(count_bindings(h,0)==1);replay_case("begin-allocation-reentry",h);assert(S.color.blend_en==2);
    h_nvnCommandBufferBeginRecording(&child.command);bind_kind(&child.command,1,b);draw(&child.command,41);
    callback_call=h_nvnCommandBufferEndRecording(&child.command);
    // MISS allocation inserts CALL changing a different kind. Only the final
    // outer polygon payload can become known after that arbitrary guest code.
    callback_mode=2;h_nvnCommandBufferBeginRecording(&f.command);bind_kind(&f.command,1,a);
    force_callback(&f);bind_kind(&f.command,4,p);bind_kind(&f.command,1,a);draw(&f.command,42);
    h=h_nvnCommandBufferEndRecording(&f.command);replay_case("miss-allocation-call-reentry",h);
    assert(S.color.blend_en==1 && S.poly.cull==1 && calls==1);
    // The original recorder reads st AFTER a memory callback. If a shadow
    // captured the earlier value, a later real B bind would be skipped wrong.
    callback_mode=3;h_nvnCommandBufferBeginRecording(&f.command);bind_kind(&f.command,1,a);
    callback_mutable=b;force_callback(&f);bind_kind(&f.command,1,b);draw(&f.command,43);
    value_for(1,2,b);bind_kind(&f.command,1,b);draw(&f.command,44);
    h=h_nvnCommandBufferEndRecording(&f.command);assert(count_bindings(h,0)==3);
    replay_case("callback-mutates-source-value",h);assert(S.color.blend_en==2);
    assert(callback_count==3);finish(&f);finish(&child);
}
static void object_reuse_and_fallback(void) {
    command_fixture f;uint8_t v[24];value_for(1,1,v);
    for(unsigned i=0;i<12;i++) {
        init(&f);h_nvnCommandBufferBeginRecording(&f.command);bind_kind(&f.command,1,v);bind_kind(&f.command,1,v);draw(&f.command,50);
        NVNcommandHandle h=h_nvnCommandBufferEndRecording(&f.command);finish(&f);
        // Finalize frees only the shadow, not command payloads or handles.
        replay_case("finalize-and-reuse-object",h);assert(S.color.blend_en==1);
    }
    // Failure is a per-object conservative fallback, not a partial-cache mode.
    memset(&f,0,sizeof f);f.pool.mem=f.memory;f.pool.size=sizeof f.memory;fail_cache_allocation=enabled;
    assert(h_nvnCommandBufferInitialize(&f.command,NULL));assert(!CB(&f.command)->record_state);supply(&f,1024);
    h_nvnCommandBufferBeginRecording(&f.command);bind_kind(&f.command,1,v);bind_kind(&f.command,1,v);draw(&f.command,51);
    NVNcommandHandle h=h_nvnCommandBufferEndRecording(&f.command);assert(count_bindings(h,0)==2);
    replay_case("allocation-failure-original-recorder",h);finish(&f);
    // Unsupported helper input must not grant a reuse proof or retain stale
    // supported values. Public six binders are compile-time <=24 bytes.
    nvn_record_state_cache cache={0};nvn_record_state_capture(&cache,1,v,sizes[1]);
    nvn_record_state_capture(&cache,6,v,4);assert(!cache.valid);
    assert(!nvn_record_state_matches(&cache,6,v,4));
    nvn_record_state_capture(&cache,1,v,25);assert(!cache.valid);
}
int main(void) {
    enabled=getenv("DM_NVN_RECORD_STATE_CACHE") && !strcmp(getenv("DM_NVN_RECORD_STATE_CACHE"),"1");
    simple_values();call_and_targets();manual_jump();callbacks();object_reuse_and_fallback();
    // Policy is read once before command objects exist, never toggled by a
    // subsequent environment edit or inherited by a different object's value.
    setenv("DM_NVN_RECORD_STATE_CACHE",enabled?"0":"1",1);command_fixture f;init(&f);finish(&f);
    printf("PASS recorder enabled=%d sizeof(cmd_t)=%zu reserved=%zu\n",enabled,sizeof(cmd_t),sizeof(NVNcommandBuffer));
    return 0;
}
