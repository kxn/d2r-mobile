// nn::codec::HardwareOpusDecoder：真机经 hwopus 服务用硬件解码（游戏的音频与过场音轨都是 Opus，不是联网功能）。
// 宿主：构造函数改为在同一块内存上构造 SDK 的软件 OpusDecoder（sdk 自带 libopus，ARM 原生）。
// 依据：两者同为 nn::codec::IOpusDecoder 的实现，虚表 10 个槽位顺序与签名逐一相同（析构×2、GetWorkBufferSize、IsInitialized、
// Initialize、Finalize、GetSampleRate、GetChannelCount、Reset、DecodeInterleaved）；游戏按 0x40 字节分配，软件版对象 0x30 字节。
// nn::codec::HardwareOpusDecoder: the real device decodes in hardware via the hwopus service (the game's audio and cutscene tracks are all Opus; this is not an online feature).
// Host: the constructor instead constructs the SDK's software OpusDecoder in the same memory (the sdk bundles libopus, native ARM).
// Basis: both are implementations of nn::codec::IOpusDecoder, with the 10 vtable slots identical one by one in order and signature (destructor×2, GetWorkBufferSize, IsInitialized,
// Initialize, Finalize, GetSampleRate, GetChannelCount, Reset, DecodeInterleaved); the game allocates 0x40 bytes and the software object is 0x30 bytes.
#include "hle.h"
#ifdef __ANDROID__
static void opus_shadow_maybe_attach(void *self);
static void opus_original_construct(void *self);
#endif

HLE_FORCE("_ZN2nn5codec19HardwareOpusDecoderC1Ei", "hwopus 服务；换成 SDK 软件 OpusDecoder（同一 IOpusDecoder 虚接口）",
          void, h_codec_HwOpusCtor, void *self, int option) {
    (void)option;
#ifdef __ANDROID__
    opus_original_construct(self);
#else
    static void (*soft_ctor)(void *);
    if (!soft_ctor && !(soft_ctor = guest_sym("_ZN2nn5codec11OpusDecoderC1Ev"))) die("缺 OpusDecoder 构造");
    soft_ctor(self);
#endif
#ifdef __ANDROID__
    opus_shadow_maybe_attach(self);
#endif
}

#ifdef __ANDROID__
// FMOD also constructs the same SDK software interface directly. Keep both
// named constructors on the identical adapter boundary. The HW path above
// calls guest_sym's ORIGINAL constructor, never this forced entry, so one
// original guest object receives at most one attachment.
HLE_FORCE("_ZN2nn5codec11OpusDecoderC1Ev", "Opus 软件构造：复用可选同 IOpusDecoder 原生 core；默认仍 SDK",
          void, h_codec_SoftOpusCtor, void *self) {
    opus_original_construct(self);
    opus_shadow_maybe_attach(self);
}

#include "opus_shadow.h"
#include <stdatomic.h>
static void (*opus_soft_ctor)(void *);
static pthread_once_t opus_ctor_once=PTHREAD_ONCE_INIT;
static void opus_resolve_ctor(void){
    opus_soft_ctor=guest_sym("_ZN2nn5codec11OpusDecoderC1Ev");
    if(!opus_soft_ctor)die("缺 OpusDecoder 构造");
}
static void opus_original_construct(void *self){pthread_once(&opus_ctor_once,opus_resolve_ctor);opus_soft_ctor(self);}
// Backend choice is cached before Init; defaultSDK, correctness-only shadow or
// explicit native-core/SDK-wrapper. No per-packet getenv or midstream switch.
static pthread_once_t opus_shadow_once=PTHREAD_ONCE_INIT;
static int opus_backend,opus_shadow_abi;
_Atomic uintptr_t dm_opus_original_math[3] __attribute__((visibility("hidden")));
_Atomic unsigned dm_opus_core_dispatch_enabled __attribute__((visibility("hidden")));
static pthread_once_t opus_math_once=PTHREAD_ONCE_INIT;
static void opus_math_resolve(void) {
    static const char *const names[3]={"nncodecLibopus_opus_decoder_init","nncodecLibopus_opus_decoder_ctl","nncodecLibopus_opus_decode"};
    for(unsigned i=0;i<3;i++) {
        void *fn=guest_sym(names[i]);if(!fn)die("missing original codec math %s",names[i]);
        atomic_store_explicit(&dm_opus_original_math[i],(uintptr_t)fn,memory_order_release);
    }
}
static void *opus_shadow_table,*opus_shadow_slots[10];
static void opus_shadow_configure(void) {
    const char *mode=getenv("DM_ANDROID_OPUS_SHADOW");
    const char *native=getenv("DM_ANDROID_OPUS_NATIVE");
    int shadow_requested=mode && !strcmp(mode,"1"),native_requested=native && !strcmp(native,"1");
    if(shadow_requested && native_requested){LOG("Opus native/shadow are mutually exclusive; SDK retained");return;}
    opus_backend=native_requested?DM_OPUS_NATIVE:shadow_requested?DM_OPUS_SHADOW:0;
    if(!opus_backend)return;
    static const char *const symbols[10]={
      "_ZN2nn5codec11OpusDecoderD2Ev","_ZN2nn5codec11OpusDecoderD0Ev",
      "_ZNK2nn5codec11OpusDecoder17GetWorkBufferSizeEii","_ZNK2nn5codec11OpusDecoder13IsInitializedEv",
      "_ZN2nn5codec11OpusDecoder10InitializeEiiPvm","_ZN2nn5codec11OpusDecoder8FinalizeEv",
      "_ZNK2nn5codec11OpusDecoder13GetSampleRateEv","_ZNK2nn5codec11OpusDecoder15GetChannelCountEv",
      "_ZN2nn5codec11OpusDecoder5ResetEv","_ZN2nn5codec11OpusDecoder17DecodeInterleavedEPmPiPsmPKvm"};
    const char *table_name="_ZTVN2nn5codec11OpusDecoderE";
    opus_shadow_table=guest_sym(table_name);
    module_t *module=opus_shadow_table?module_of(opus_shadow_table):NULL;
    if(!module)return;
    // Require the exact ten-slot Itanium interface, not merely a matching
    // prefix of an unfamiliar SDK version with additional virtual methods.
    int exact_size=0;
    for(unsigned i=0;i<module->nsym;i++) {
        const Elf64_Sym *s=module->symtab+i;
        if(s->st_shndx && !strcmp(module->strtab+s->st_name,table_name) &&
           module->base+s->st_value==opus_shadow_table && s->st_size==12*sizeof(void *))exact_size=1;
    }
    if(!exact_size)return;
    for(unsigned i=0;i<10;i++)if(!(opus_shadow_slots[i]=guest_sym(symbols[i])))return;
    opus_shadow_abi=1;
    if(opus_backend==DM_OPUS_NATIVE)
        atomic_store_explicit(&dm_opus_core_dispatch_enabled,1,memory_order_release);
}
static void opus_shadow_log(const char *line){LOG("%s",line);}
static void opus_shadow_maybe_attach(void *self) {
    pthread_once(&opus_shadow_once,opus_shadow_configure);
    if(opus_backend && opus_shadow_abi)
        dm_opus_attach_mode(self,opus_shadow_table,opus_shadow_slots,opus_shadow_log,opus_backend);
}
// Selection returns a tail target; the assembly restores ALL incoming args.
// It never guesses or forwards unknown variadic arguments through a C call.
void *dm_opus_math_select(unsigned kind,const uintptr_t args[8]) {
    if(kind>DM_OPUS_CORE_DECODE)die("invalid Opus core selector");
    pthread_once(&opus_math_once,opus_math_resolve);
    pthread_once(&opus_shadow_once,opus_shadow_configure);
    void *original=(void *)atomic_load_explicit(&dm_opus_original_math[kind],memory_order_acquire);
    return opus_backend==DM_OPUS_NATIVE && opus_shadow_abi?
        dm_opus_core_target(kind,args,original):original;
}
extern int dm_opus_init_entry(void *,int,int);
extern int dm_opus_ctl_entry(void *,int,...);
extern int dm_opus_decode_entry(void *,const unsigned char *,int,int16_t *,int,int);
// Same HLE_FORCE named-relocation mechanism; symbols remain original in
// guest_sym, so default SDK/unowned/multistream calls tail back transparently.
DM_HLE_SECTION("swforce") static const hle_force_ent opus_math_force[]={
 {"nncodecLibopus_opus_decoder_init",(void *)dm_opus_init_entry,"HWOpus adapter: capture exact original init state; all math remains original unless explicitly selected"},
 {"nncodecLibopus_opus_decoder_ctl",(void *)dm_opus_ctl_entry,"HWOpus adapter: only selected4028 reset; unowned varargs transparent ARM tail fallback"},
 {"nncodecLibopus_opus_decode",(void *)dm_opus_decode_entry,"HWOpus adapter: optional native core only; original SDK packet/capacity/error/mutex wrapper retained"}};
#endif
