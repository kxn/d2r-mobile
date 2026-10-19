// Original SDK ARM64 conformance, no nnMain/module constructors/world/GPU.
// Owned host Opus packets are wrapped by the actual SDK packet setter, then
// the actual SDK reference and HW adapter execute the same original wrapper.
#include "hle.h"
#include "opus_shadow.h"
#include <opus.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>
extern void guest_thread_init(void),crash_thread_init(void);
extern uint32_t svc_thread_main_init(void);
static uint64_t comparisons,successes,rejections,scalars,different;
static unsigned max_abs;
static void *required(const char *name){void *p=guest_sym(name);if(!p)die("Opus SDK oracle missing %s",name);return p;}
static void run0(const char *name){((void (*)(void))required(name))();}
static void sdk_start(const char *directory) {
    const char *names[]={"main","sdk","subsdk0"};
    crash_thread_init();load_modules(directory,names,3);guest_thread_init();
    uint32_t main_handle=svc_thread_main_init();run0("_init_libc0");
    ((void (*)(uint64_t,uint64_t))required("nnosInitialize"))(main_handle,0);
    run0("_init_libc1");run0("nninitInitializeSdkModule");run0("nninitStartup");run0("_init_libc2");
    // Deliberately no run_module_inits, nnMain, host UI or Vulkan call.
}
typedef struct {void *self,*work_base,*work;uint64_t work_size;} sdk_object;
static void *slot(sdk_object *object,unsigned index){return (*(void ***)object->self)[index];}
static sdk_object construct(int candidate) {
    sdk_object object={0};
    object.self=((void *(*)(uint64_t))required("_Znwm"))(candidate==1?64:48);
    if(!object.self)die("Opus SDK oracle guest operator new");
    if(!candidate)((void (*)(void *))required("_ZN2nn5codec11OpusDecoderC1Ev"))(object.self);
    else {
        extern const unsigned char __start_swforce[],__stop_swforce[];
        void *ctor=NULL;
        const char *name=candidate==1?"_ZN2nn5codec19HardwareOpusDecoderC1Ei":"_ZN2nn5codec11OpusDecoderC1Ev";
        for(const hle_force_ent *e=(const void *)__start_swforce;e<(const hle_force_ent *)(const void *)__stop_swforce;e++)
          if(!strcmp(e->name,name))ctor=e->fn;
        if(!ctor)die("Opus SDK oracle named ctor adapter missing %s",name);
        if(candidate==1)((void (*)(void *,int))ctor)(object.self,0);
        else ((void (*)(void *))ctor)(object.self);
    }
    return object;
}
static void initialize(sdk_object *object,int rate,int channels) {
    object->work_size=((uint64_t (*)(void *,int,int))slot(object,2))(object->self,rate,channels);
    if(!object->work_size || object->work_size>1024*1024)die("Opus SDK oracle work-size bounds");
    if(posix_memalign(&object->work_base,64,(size_t)object->work_size+128))die("Opus SDK oracle work allocation");
    memset(object->work_base,0xa5,(size_t)object->work_size+128);object->work=(char *)object->work_base+64;
    uint32_t result=((uint32_t (*)(void *,int,int,void *,uint64_t))slot(object,4))(object->self,rate,channels,object->work,object->work_size);
    if(result)die("Opus SDK oracle init result%u rate%d channels%d",result,rate,channels);
    if(!((int (*)(const void *))slot(object,3))(object->self) ||
       ((int (*)(const void *))slot(object,6))(object->self)!=rate ||
       ((int (*)(const void *))slot(object,7))(object->self)!=channels)die("Opus SDK oracle getters");
}
static void check_work(const sdk_object *o) {
    const unsigned char *p=o->work_base;
    for(unsigned i=0;i<64;i++)if(p[i]!=0xa5 || p[64+o->work_size+i]!=0xa5)die("Opus SDK oracle work guard");
}
static void reset(sdk_object *o){((void (*)(void *))slot(o,8))(o->self);}
static void rejected_reinitialize(sdk_object *reference,sdk_object *candidate,int rate,int channels,int old_rate,int old_channels) {
    sdk_object *objects[]={reference,candidate};uint32_t result[2];
    for(unsigned i=0;i<2;i++) {
        sdk_object *o=objects[i];
        result[i]=((uint32_t (*)(void *,int,int,void *,uint64_t))slot(o,4))(o->self,rate,channels,o->work,o->work_size);
        if(!((int (*)(const void *))slot(o,3))(o->self) ||
           ((int (*)(const void *))slot(o,6))(o->self)!=old_rate ||
           ((int (*)(const void *))slot(o,7))(o->self)!=old_channels)die("Opus SDK failed re-Init changed getters");
        check_work(o);
    }
    if(result[0]!=8 || result[1]!=result[0])die("Opus SDK failed re-Init result %u/%u",result[0],result[1]);
}
static void destroy(sdk_object *o) {
    ((void (*)(void *))slot(o,5))(o->self);check_work(o);
    // Original D0 alone frees the original SDK operator-new allocation.
    ((void (*)(void *))slot(o,1))(o->self);o->self=NULL;
    free(o->work_base);o->work=o->work_base=NULL;
}
typedef struct {uint32_t payload_bytes,opaque_zero;} sdk_packet_header;
static void packet_header(unsigned char *packet,unsigned bytes) {
    sdk_packet_header in={bytes,0},out={UINT32_MAX,UINT32_MAX};
    ((void (*)(unsigned char *,const sdk_packet_header *))required("_ZN2nn5codec6detail18OpusPacketInternal17SetHeaderInPacketEPhRKNS2_6HeaderE"))(packet,&in);
    ((void (*)(sdk_packet_header *,const unsigned char *))required("_ZN2nn5codec6detail18OpusPacketInternal19GetHeaderFromPacketEPNS2_6HeaderEPKh"))(&out,packet);
    if(memcmp(&in,&out,sizeof in))die("Opus SDK oracle header roundtrip");
}
static void compare(sdk_object *ref,sdk_object *candidate,const unsigned char *packet,uint64_t bytes,uint64_t capacity,int channels) {
    enum { VALUES=5760*2+16 };
    int16_t *a=malloc(VALUES*2),*b=malloc(VALUES*2);if(!a||!b)die("Opus SDK oracle PCM allocation");
    for(unsigned i=0;i<VALUES;i++)a[i]=b[i]=(int16_t)0x6543;
    uint64_t consumed_a=UINT64_MAX,consumed_b=consumed_a;int count_a=-999,count_b=-999;
    uint32_t (*fa)(void *,uint64_t *,int *,int16_t *,uint64_t,const void *,uint64_t)=slot(ref,9);
    uint32_t (*fb)(void *,uint64_t *,int *,int16_t *,uint64_t,const void *,uint64_t)=slot(candidate,9);
    uint32_t ra=fa(ref->self,&consumed_a,&count_a,a+8,capacity,packet,bytes);
    uint32_t rb=fb(candidate->self,&consumed_b,&count_b,b+8,capacity,packet,bytes);
    comparisons++;
    if(ra!=rb || consumed_a!=consumed_b || count_a!=count_b)die("Opus SDK oracle result/count/consumed mismatch case%llu result%u/%u count%d/%d used%llu/%llu",comparisons,ra,rb,count_a,count_b,consumed_a,consumed_b);
    if(!ra) {
        if(count_a<=0 || count_a>5760 || (uint64_t)count_a*channels*2>capacity)die("Opus SDK oracle output count bounds");
        successes++;
        for(unsigned i=0;i<(unsigned)count_a*channels;i++) {
            int d=(int)a[8+i]-b[8+i];if(d<0)d=-d;
            if(d){different++;if((unsigned)d>max_abs)max_abs=(unsigned)d;}
            if(d>1)die("Opus SDK oracle PCM >1LSB case%llu scalar%u got%d/%d",comparisons,i,a[8+i],b[8+i]);
        }
        scalars+=(uint64_t)count_a*channels;
    } else {rejections++;if(memcmp(a,b,VALUES*2))die("Opus SDK oracle error output differs");}
    // Independent whole-buffer guards, including all unwritten output values.
    unsigned written=ra?0:(unsigned)count_a*channels;
    for(unsigned i=0;i<VALUES;i++)if((i<8 || i>=8+written) && (a[i]!=(int16_t)0x6543 || b[i]!=(int16_t)0x6543))die("Opus SDK oracle PCM guard case%llu",comparisons);
    check_work(ref);check_work(candidate);free(a);free(b);
}
__attribute__((visibility("default"))) int main(int argc,char **argv) {
    if(argc<2 || argc>3 || (argc==3 && strcmp(argv[2],"sdk") && strcmp(argv[2],"native"))){fprintf(stderr,"usage: native_opus_sdk_test VERIFIED_MODULE_DIR [sdk|native]\n");return 2;}
    int use_native=argc==2 || !strcmp(argv[2],"native");
    if(setenv("DM_ANDROID_OPUS_NATIVE",use_native?"1":"0",1) || setenv("DM_ANDROID_OPUS_SHADOW","0",1))die("Opus SDK oracle env");
    sdk_start(argv[1]);
    const int rates[]={8000,16000,48000};const int divisors[]={400,200,100,50};
    for(unsigned constructor=1;constructor<=2;constructor++)for(unsigned rate_index=0;rate_index<3;rate_index++)for(int channels=1;channels<=2;channels++) {
        int rate=rates[rate_index],error;
        sdk_object reference=construct(0),candidate=construct((int)constructor);
        dm_opus_shadow_stats attached;dm_opus_shadow_snapshot(&attached);
        if(use_native) {
            if(attached.live!=1 || attached.attached!=(constructor-1)*6+rate_index*2+channels)die("Opus SDK oracle constructor missing/double attachment");
        } else if(attached.live || attached.attached || *(void ***)candidate.self!=(void **)((char *)required("_ZTVN2nn5codec11OpusDecoderE")+16))die("Opus SDK default constructor altered vptr/attached");
        initialize(&reference,rate,channels);initialize(&candidate,rate,channels);
        OpusEncoder *encoder=opus_encoder_create(rate,channels,OPUS_APPLICATION_AUDIO,&error);if(!encoder||error)die("Opus SDK oracle encoder");
        unsigned char packet[4096];int16_t input[5760*2];
        for(unsigned duration=0;duration<6;duration++) {
            int frames=duration<4?rate/divisors[duration]:rate*(duration==4?40:60)/1000;
            for(unsigned n=0;n<(unsigned)frames*channels;n++)input[n]=(int16_t)(((n+duration*17)*149)%14000-7000);
            int payload=opus_encode(encoder,input,frames,packet+8,sizeof packet-8);if(payload<=0)die("Opus SDK oracle owned encode");packet_header(packet,(unsigned)payload);
            compare(&reference,&candidate,packet,(uint64_t)payload+8,5760*2*2,channels);
            if(!duration) {
                // Advance both histories before failure. Do not Reset until
                // both failed re-Inits have been followed by a direct decode;
                // resetting first would hide a stale SDK-core fallback.
                for(unsigned warm=0;warm<2;warm++)compare(&reference,&candidate,packet,(uint64_t)payload+8,5760*2*2,channels);
                rejected_reinitialize(&reference,&candidate,44100,channels,rate,channels);
                compare(&reference,&candidate,packet,(uint64_t)payload+8,5760*2*2,channels);
                rejected_reinitialize(&reference,&candidate,rate,3,rate,channels);
                compare(&reference,&candidate,packet,(uint64_t)payload+8,5760*2*2,channels);
                reset(&reference);reset(&candidate);
                compare(&reference,&candidate,packet,(uint64_t)payload+8,5760*2*2,channels);
            }
            compare(&reference,&candidate,packet,7,5760*2*2,channels); // original short header
            compare(&reference,&candidate,packet,(uint64_t)payload+7,5760*2*2,channels); // original declared payload bound
            compare(&reference,&candidate,packet,(uint64_t)payload+8,1,channels); // original PCM capacity rejection
            reset(&reference);reset(&candidate);
            compare(&reference,&candidate,packet,(uint64_t)payload+8,5760*2*2,channels);
        }
        // Invalid Opus payload uses the original SDK packet format and mapping.
        packet[8]=0xff;packet_header(packet,1);
        compare(&reference,&candidate,packet,9,5760*2*2,channels);
        reset(&reference);reset(&candidate);
        opus_encoder_destroy(encoder);destroy(&reference);destroy(&candidate);
    }
    dm_opus_shadow_stats s;dm_opus_shadow_snapshot(&s);
    if((use_native?(s.native_selected!=12 || s.attached!=12 || s.native_packets!=successes):(s.attached || s.native_selected || s.native_packets || different)) || s.native_api_rejects || s.init_fallbacks || s.stack_rejects || s.live)die("Opus SDK oracle native/default path/lifetime evidence");
    printf("{\"backend\":\"native-arm64\",\"mode\":\"%s\",\"constructor_paths\":2,\"oracle\":\"actual SDK original wrapper\",\"packet_source\":\"owned libopus encoder + actualSDK header setter\",\"compared_cases\":%llu,\"successes\":%llu,\"errors\":%llu,\"pcm_scalar_samples\":%llu,\"different_scalars\":%llu,\"max_abs\":%u,\"native_packets\":%llu,\"native_selected\":%llu,\"nnMain\":false,\"module_constructors\":false,\"result\":\"passed (bounded, not full compatibility)\"}\n",use_native?"native":"sdk-default",comparisons,successes,rejections,scalars,different,max_abs,s.native_packets,s.native_selected);
    fflush(stdout);_exit(0);
}
