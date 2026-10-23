// 经原 NVN 引导、跳板与命令录制层验证 Vulkan 的 GPU 执行和 CPU 可见时序。
// Verify Vulkan GPU execution and CPU-visible ordering through the original NVN
// bootstrap, thunks and command recorder.
#include "../../../gfx/nvn/nvn_dirty.h"
#include "../../../gfx/nvn/nvn_mtl.h"
#include "../../../gfx/nvn/nvn_obj.h"
#include "../../../gfx/nvn/nvn_protos.h"
#include "../../../gfx/vulkan/nvn_vk_shader.h"
#include "../../../gfx/vulkan/nvn_vk_window.h"
#include "../../../gfx/vulkan/nvn_vk_freeze.h"
#include "../../../gfx/vulkan/nvn_vk_extrap.h"
#include "hle.h"
#include <math.h>
#include <float.h>
#include <unistd.h>
static void *(*bootstrap)(const char *);
extern void crash_thread_init(void);
extern unsigned vk_validation_error_count(void);
static unsigned checks;
#define CALL(name, ...) ((NVNT_##name)bootstrap(#name))(__VA_ARGS__)
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      die("Vulkan check failed line %u: %s", __LINE__, #condition);            \
    checks++;                                                                  \
  } while (0)
static void pipeline_cache_test(void) {
  const char *path=vk_pipeline_cache_path();
  CHECK(path&&*path);vk_pipeline_cache_checkpoint();
  FILE *file=fopen(path,"rb");CHECK(file!=NULL);
  CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>=120);
  rewind(file);uint8_t *data=malloc(length);CHECK(data!=NULL);
  CHECK(fread(data,1,length,file)==(size_t)length);CHECK(!fclose(file));
  size_t loaded;VkPipelineCache trial=vk_pipeline_cache_load(path,&loaded);
  CHECK(loaded==(size_t)length-88);vkDestroyPipelineCache(V.device,trial,NULL);
  const unsigned corrupt[]={0,12,24,32,56,88};
  for(unsigned test=0;test<7;test++) {
    if(test<6) data[corrupt[test]]^=0x80;
    file=fopen(path,"wb");CHECK(file!=NULL);
    size_t size=test==6?16:(size_t)length;
    CHECK(fwrite(data,1,size,file)==size);CHECK(!fclose(file));
    trial=vk_pipeline_cache_load(path,&loaded);CHECK(loaded==0);
    vkDestroyPipelineCache(V.device,trial,NULL);
    if(test<6) data[corrupt[test]]^=0x80;
  }
  file=fopen(path,"wb");CHECK(file!=NULL);
  CHECK(fwrite(data,1,length,file)==(size_t)length);CHECK(!fclose(file));free(data);
  trial=vk_pipeline_cache_load(path,&loaded);CHECK(loaded==(size_t)length-88);
  vkDestroyPipelineCache(V.device,trial,NULL);
  LOG("[vulkan PSO cache fixture] real driver roundtrip, metadata/ABI/driver/UUID/checksum/truncation rejection and recovery passed");
}
static NVNdevice device;
static NVNqueue queue;
static NVNcommandBuffer cmd;
static NVNmemoryPool memory;
static uint8_t *bytes;
typedef struct {
  const void *address;
  size_t bytes;
  unsigned pages;
} dirty_visit;
static void visited(const void *address, size_t bytes, void *context) {
  dirty_visit *result = context;
  result->address = address;
  result->bytes += bytes;
  result->pages++;
}
static NVNcommandHandle end(void) {
  return CALL(nvnCommandBufferEndRecording, &cmd);
}
static void submit(NVNcommandHandle h) {
  CALL(nvnQueueSubmitCommands, &queue, 1, &h);
  CALL(nvnQueueFinish, &queue);
}
typedef struct {
  const vk_buffer *buffer;
  const uint32_t *copied;
  uint32_t value;
  int valid;
} lifetime_readback;
static void lifetime_readback_done(void *context) {
  lifetime_readback *r = context;
  r->valid = 1;
  for (unsigned i = 0; i < 64; i++) {
    uint32_t gpu;
    memcpy(&gpu, r->buffer->map + i * 4, 4);
    if (gpu != r->value || r->copied[i] != r->value) r->valid = 0;
  }
}
// 回调读取真实 GPU 暂存数据，随后游戏栅栏才完成；计时开关逐提交锁存。
// Callbacks consume real GPU staging before guest-fence completion. Diagnostic
// enablement is latched per submission, including disable after first use.
static void submission_lifetime_test(void) {
  mtl_init();
  int saved=V.options.gpu_profile;
  int supported = V.timestamp_bits && !getenv("DM_VULKAN_EXTERNAL_PROFILE");
  for (unsigned pass = 0; pass < 4; pass++) {
    int enabled = !(pass & 1);
    vk_gpu_profile_enabled(enabled);
    uint64_t samples = vk_gpu_profile_samples();
    mtl_sub_begin();
    vk_buffer *buffer = vk_temp_buffer(256);
    memset(buffer->map, 0, 256);
    uint32_t copied[64] = {0}, value = 0x91ab0000u + pass;
    lifetime_readback result = {buffer, copied, value, 0};
    // 本次锁存不受录制中环境变更影响，也不能在停用时重用旧查询。
    // Mid-record explicit requests do not change this submit, and a disabled
    // submit must not write a previously allocated query pool without reset.
    vk_gpu_profile_enabled(!enabled);
    unsigned region = vk_gpu_region_begin("fixture", NULL, 256, 1);
    CHECK(!!region == (supported && enabled));
    vk_gpu_region_label(region, "submit-lifetime");
    vkCmdFillBuffer(V.cmd, buffer->handle, 0, 256, value);
    vk_gpu_region_end(region);
    vk_barrier();
    vk_effect_copy(copied, buffer->map, sizeof copied);
    vk_completion(lifetime_readback_done, &result);
    volatile uint32_t event = 0;
    mtl_sub_event(&event, 0, value);
    uint64_t fence = mtl_sub_commit();
    CHECK(mtl_wait(fence, UINT64_MAX));
    CHECK(result.valid);
    CHECK(__atomic_load_n(&event, __ATOMIC_ACQUIRE) == value);
    for (unsigned i = 0; i < 64; i++) CHECK(copied[i] == value);
    CHECK(vk_gpu_profile_samples() - samples == (unsigned)(supported && enabled));
  }
  vk_gpu_profile_enabled(saved);
}
// 真 GPU 颜色读回后独立核 PNG 尺寸；外部 Pillow 再核全部像素。
// Check PNG dimensions after real GPU readback; external Pillow verifies
// every decoded pixel independently of the encoder.
static void capture_test(const tex_t *texture) {
  void vk_capture_present(const tex_t *, const int[4]);
  const char *dir = getenv("DM_CAPTURE_TEST_DIR");
  if (!dir) dir = getenv("TMPDIR");
  if (!dir) dir = ".";
  char path[2048];
  snprintf(path, sizeof path, "%s/vulkan-capture-fixture.png", dir);
  unlink(path);
  dm_gfx_shot_at(2, path);
  int crop[4] = {2, 3, 8, 6};
  vk_capture_present(texture, crop);
  CHECK(access(path, F_OK) != 0);
  vk_capture_present(texture, crop);
  FILE *f = fopen(path, "rb");
  CHECK(f != NULL);
  unsigned char header[24];
  CHECK(fread(header, 1, sizeof header, f) == sizeof header);
  CHECK(fclose(f) == 0);
  CHECK(!memcmp(header, "\x89PNG\r\n\x1a\n", 8));
  CHECK(header[16] == 0 && header[17] == 0 && header[18] == 0 && header[19] == 8 &&
        header[20] == 0 && header[21] == 0 && header[22] == 0 && header[23] == 6);
}
// 诊断快照须保留帧筛选、提交内配置寿命及真实 GPU 回读内容。
// Diagnostic snapshots preserve frame filtering, option lifetime and GPU pixels.
static void trace_test(const tex_t *texture) {
  const char *base = getenv("TMPDIR");
  char dir[2048], path[2304];
  snprintf(dir, sizeof dir, "%s/vulkan-trace-XXXXXX", base ? base : ".");
  CHECK(mkdtemp(dir) != NULL);
  snprintf(path, sizeof path, "%s/f%llu-d%llu-fixture-t%llu.ppm", dir,
      (unsigned long long)V.frame, (unsigned long long)V.draws,
      (unsigned long long)texture->serial);
  for (unsigned pass = 0; pass < 3; pass++) {
    vk_trace_request(pass!=2,V.frame+(pass==0),dir);
    mtl_sub_begin();
    // 变更显式请求不得使本次提交已快照的目录指针失效。
    // Request changes must not invalidate this submission's owned directory.
    vk_trace_request(pass!=2,V.frame,"/unused-after-submit-snapshot");
    vk_texture_trace(texture, NULL, "fixture");
    vk_internal_wait(vk_internal_commit());
    if (pass != 1) CHECK(access(path, F_OK) != 0);
    else {
      FILE *f = fopen(path, "rb");
      CHECK(f != NULL);
      char header[14];
      CHECK(fread(header, 1, 13, f) == 13 && !memcmp(header, "P6\n16 16\n255\n", 13));
      for (unsigned i = 0; i < 256; i++) {
        unsigned char pixel[3];
        CHECK(fread(pixel, 1, 3, f) == 3);
        CHECK(pixel[0] == 255 && pixel[1] == 128 && pixel[2] == 0);
      }
      CHECK(fgetc(f) == EOF && !ferror(f));
      CHECK(fclose(f) == 0);
      CHECK(unlink(path) == 0);
    }
  }
  CHECK(rmdir(dir) == 0);
  vk_trace_request(V.options.trace_frame_set,V.options.trace_frame,V.options.trace_directory);
}
// 原 clear_tex 的层/深度面、格式视图及数值清除语义；逐像素回读检查。
// Original clear_tex layer/plane, view-format and numeric clear semantics.
static void texture_clear_test(void) {
  NVNtextureBuilder builder;
  CALL(nvnTextureBuilderSetDefaults, &builder);
  CALL(nvnTextureBuilderSetDevice, &builder, &device);
  CALL(nvnTextureBuilderSetWidth, &builder, 4);
  CALL(nvnTextureBuilderSetHeight, &builder, 4);
  CALL(nvnTextureBuilderSetDepth, &builder, 4);
  CALL(nvnTextureBuilderSetLevels, &builder, 2);
  CALL(nvnTextureBuilderSetStorage, &builder, &memory, (3 << 20) + 917504);
  uint8_t pixels[4 * 4 * 4 * 4];
  for (unsigned volume = 0; volume < 2; volume++) {
    CALL(nvnTextureBuilderSetTarget, &builder, volume ? NVN_TEXTURE_TARGET_3D : NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetFormat, &builder, NVN_FORMAT_RGBA8);
    NVNtexture texture;
    CHECK(CALL(nvnTextureInitialize, &texture, &builder));
    CALL(nvnCommandBufferBeginRecording, &cmd);
    for (unsigned level = 0; level < 2; level++) {
      NVNtextureView view;
      CALL(nvnTextureViewSetDefaults, &view);
      CALL(nvnTextureViewSetLevels, &view, level, 1);
      NVNcopyRegion region = {0,0,0,4 >> level,4 >> level,volume ? 4 >> level : 4};
      float initial[4] = {.25f, .5f, level ? 1 : 0, 1};
      CALL(nvnCommandBufferClearTexture, &cmd, &texture, &view, &region, initial, 15);
    }
    NVNtextureView view;
    CALL(nvnTextureViewSetDefaults, &view);
    CALL(nvnTextureViewSetLayers, &view, volume ? 0 : 1, 1);
    // 数组：视图第 1 层 + zoffset 1；3D：第 1/2 深度面。
    // Array layer 1 plus zoffset 1; volume planes 1 and 2.
    NVNcopyRegion middle = {0,0,1,4,4,2};
    const uint32_t green[4] = {0,1,0,1};
    CALL(nvnCommandBufferClearTextureui, &cmd, &texture, &view, &middle, green, 15);
    submit(end());
    for (unsigned level = 0; level < 2; level++) {
      CALL(nvnTextureViewSetDefaults, &view);
      CALL(nvnTextureViewSetLevels, &view, level, 1);
      unsigned size = 4 >> level, depth = volume ? 4 >> level : 4;
      NVNcopyRegion region = {0,0,0,size,size,depth};
      CALL(nvnTextureReadTexels, &texture, &view, &region, pixels);
      for (unsigned z = 0; z < depth; z++) for (unsigned n = 0; n < size * size; n++) {
        int changed = !level && z >= (volume ? 1 : 2) && z < (volume ? 3 : 4);
        const uint8_t *pixel = pixels + (z * size * size + n) * 4;
        CHECK(pixel[0] == (changed ? 0 : 64) && pixel[1] == (changed ? 255 : 128) &&
              pixel[2] == (level ? 255 : 0) && pixel[3] == 255);
      }
    }
    CALL(nvnTextureFinalize, &texture);
  }
  CALL(nvnTextureBuilderSetTarget, &builder, NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetDepth, &builder, 1);
  CALL(nvnTextureBuilderSetLevels, &builder, 1);
  const int formats[] = {NVN_FORMAT_RGBA8UI, NVN_FORMAT_RGBA8I};
  for (unsigned test = 0; test < 2; test++) {
    CALL(nvnTextureBuilderSetFormat, &builder, formats[test]);
    NVNtexture texture;
    CHECK(CALL(nvnTextureInitialize, &texture, &builder));
    float numeric[4] = {17,33,65, test ? -7 : 129};
    NVNcopyRegion region = {0,0,0,4,4,1};
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferClearTexture, &cmd, &texture, NULL, &region, numeric, 15);
    submit(end());
    CALL(nvnTextureReadTexels, &texture, NULL, &region, pixels);
    for (unsigned i = 0; i < 64; i += 4)
      CHECK(pixels[i] == 17 && pixels[i+1] == 33 && pixels[i+2] == 65 &&
            pixels[i+3] == (test ? (uint8_t)-7 : 129));
    CALL(nvnTextureFinalize, &texture);
  }
  // 游戏实际采用的 ui 清除 + 整数格式视图；回读基格式的原始字节。
  // The game's ui clear with an integer view; read the original-format bytes.
  CALL(nvnTextureBuilderSetFormat, &builder, NVN_FORMAT_RGBA8);
  NVNtexture reinterpreted;
  CHECK(CALL(nvnTextureInitialize, &reinterpreted, &builder));
  NVNtextureView integer_view;
  CALL(nvnTextureViewSetDefaults, &integer_view);
  CALL(nvnTextureViewSetFormat, &integer_view, NVN_FORMAT_R32UI);
  const uint32_t bits[4] = {0x12345678,0,0,0};
  NVNcopyRegion integer_region = {0,0,0,4,4,1};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferClearTextureui, &cmd, &reinterpreted, &integer_view,
       &integer_region, bits, 15);
  submit(end());
  CALL(nvnTextureReadTexels, &reinterpreted, NULL, &integer_region, pixels);
  for (unsigned i = 0; i < 64; i += 4)
    CHECK(!memcmp(pixels+i, (uint8_t[]){0x78,0x56,0x34,0x12}, 4));
  CALL(nvnTextureFinalize, &reinterpreted);
  CALL(nvnTextureBuilderSetFormat, &builder, NVN_FORMAT_DEPTH32F_STENCIL8);
  NVNtexture depth;
  CHECK(CALL(nvnTextureInitialize, &depth, &builder));
  union { float f[4]; uint32_t u[4]; } clear = {.f = {.25f}};
  clear.u[1] = 0xa7;
  NVNcopyRegion region = {0,0,0,4,4,1};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferClearTexture, &cmd, &depth, NULL, &region, clear.f, 15);
  submit(end());
  // 深度/模板分别拷到独立 staging，独立核对清除参数槽位。
  // Read each aspect independently to verify the clear-value slots.
  mtl_sub_begin();
  VkImage native = vk_texture_native((const tex_t *)&depth);
  vk_buffer *readback = vk_temp_buffer(80);
  vk_barrier();
  VkBufferImageCopy copies[] = {
    {.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1}, .imageExtent = {4,4,1}},
    {.bufferOffset = 64, .imageSubresource = {VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1}, .imageExtent = {4,4,1}}};
  vkCmdCopyImageToBuffer(V.cmd, native, VK_IMAGE_LAYOUT_GENERAL, readback->handle, 2, copies);
  vk_internal_wait(vk_internal_commit());
  for (unsigned i = 0; i < 16; i++) {
    float z;
    memcpy(&z, readback->map + i * 4, 4);
    CHECK(z == .25f && readback->map[64+i] == 0xa7);
  }
  CALL(nvnTextureFinalize, &depth);
}
static double lut_pq(double v) {
  double p = pow(fabs(v), .1593017578125);
  return pow((.8359375 + 18.8515625*p)/(1 + 18.6875*p), 78.84375);
}
static double lut_inverse(double u) {
  double q = pow(u, 1/78.84375);
  return pow(fmax(q-.8359375,0)/fmax(18.8515625-18.6875*q,1e-6),1/.1593017578125);
}
static float lut_sample(const float *input, unsigned w, unsigned h, unsigned d,
                        const double uv[3], unsigned channel) {
  unsigned sizes[3] = {w,h,d};
  int lo[3], hi[3]; double fraction[3];
  for (unsigned i=0; i<3; i++) {
    double coordinate = uv[i]*sizes[i]-.5;
    int base = floor(coordinate);
    fraction[i] = coordinate-base;
    lo[i] = base < 0 ? 0 : base >= (int)sizes[i] ? sizes[i]-1 : base;
    hi[i] = base+1 < 0 ? 0 : base+1 >= (int)sizes[i] ? sizes[i]-1 : base+1;
  }
  double result=0;
  for (unsigned z=0; z<2; z++) for (unsigned y=0; y<2; y++) for (unsigned x=0; x<2; x++) {
    unsigned at = (((z?hi[2]:lo[2])*h+(y?hi[1]:lo[1]))*w+(x?hi[0]:lo[0]))*4+channel;
    result += input[at]*(x?fraction[0]:1-fraction[0])*
              (y?fraction[1]:1-fraction[1])*(z?fraction[2]:1-fraction[2]);
  }
  return result;
}
// 宿主增强不是 NVN 游戏命令：原 NVN 对象/登记 + 同提交 GPU 写入，再调用同一宿主路径。
// Host enhancements are not guest NVN commands. Use original NVN objects and
// registration, then GPU writes and the actual enhancement in the same submit.
static void bright_test(NVNtexturePool *pool) {
  CHECK(V.features.shaderStorageImageWriteWithoutFormat);
  const unsigned w=5,h=7,d=3,n=w*h*d;
  float input[5*7*3*4];
  uint8_t output[5*7*3*8], untouched[10*14*6*8];
  uint8_t *raw = bytes+(2<<20)+65536;
  const float exposures[] = {1,1.004f,1.5f,4,8,4};
  for (unsigned half=0; half<2; half++) {
    unsigned stride = half ? 8 : 4;
    for (unsigned z=0; z<d; z++) for (unsigned y=0; y<h; y++) for (unsigned x=0; x<w; x++) {
      float values[] = {.1f+.8f*x/(w-1),.1f+.8f*y/(h-1),.1f+.8f*z/(d-1),
                         .2f+.6f*(x+y+z)/(w+h+d-3)};
      unsigned at=((z*h+y)*w+x)*4;
      for (unsigned c=0; c<4; c++) {
        if (half) {
          _Float16 v=values[c]; memcpy(raw+(at+c)*2,&v,2); input[at+c]=(float)v;
        } else { raw[at+c]=lroundf(values[c]*255); input[at+c]=raw[at+c]/255.f; }
      }
    }
    NVNtextureBuilder builder;
    CALL(nvnTextureBuilderSetDefaults,&builder);
    CALL(nvnTextureBuilderSetDevice,&builder,&device);
    CALL(nvnTextureBuilderSetTarget,&builder,NVN_TEXTURE_TARGET_3D);
    CALL(nvnTextureBuilderSetFlags,&builder,NVN_TEXTURE_FLAGS_IMAGE);
    CALL(nvnTextureBuilderSetWidth,&builder,w*2);
    CALL(nvnTextureBuilderSetHeight,&builder,h*2);
    CALL(nvnTextureBuilderSetDepth,&builder,d*2);
    CALL(nvnTextureBuilderSetLevels,&builder,2);
    CALL(nvnTextureBuilderSetFormat,&builder,half?NVN_FORMAT_RGBA16F:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&builder,&memory,(3<<20)+983040);
    NVNtexture texture;
    CHECK(CALL(nvnTextureInitialize,&texture,&builder));
    NVNtextureView view;
    CALL(nvnTextureViewSetDefaults,&view);
    CALL(nvnTextureViewSetLevels,&view,1,1);
    CALL(nvnTexturePoolRegisterTexture,pool,3,&texture,&view);
    p_copy_b2t upload = {.tex=(const tex_t *)&texture, .has_view=1,
      .view=*(const view_t *)&view, .r={0,0,0,w,h,d}, .src=(uintptr_t)raw};
    p_clear_tex sentinel = {.tex=(const tex_t *)&texture, .r={0,0,0,w*2,h*2,d*2},
                            .mask=15, .c.f={.125f,.25f,.375f,1}};
    for (unsigned test=0; test<sizeof exposures/sizeof *exposures; test++) {
      mtl_sub_begin();
      vk_texture_clear(&sentinel);
      vk_texture_copy_b2t(&upload,0,0);
      uint64_t before=V.draws;
      CHECK(vk_bright_apply(3,exposures[test])==(test>=2));
      if (test==5) {
        // 同一提交重算原表后再增强，不能读上一次快照或被覆盖的曝光参数。
        // Recompute then enhance again in one submit: no stale snapshot or uniforms.
        vk_texture_copy_b2t(&upload,0,0);
        CHECK(vk_bright_apply(3,exposures[test]));
      }
      CHECK(V.draws-before==(test<2?0:test==5?2:1));
      vk_internal_wait(vk_internal_commit());
      NVNcopyRegion region={0,0,0,w,h,d};
      CALL(nvnTextureReadTexels,&texture,&view,&region,output);
      if (test<2) CHECK(!memcmp(output,raw,n*stride));
      for (unsigned z=0; z<d; z++) for (unsigned y=0; y<h; y++) for (unsigned x=0; x<w; x++) {
        double k=fmin(exposures[test],4), uv[3];
        unsigned sizes[3]={w,h,d}, index[3]={x,y,z}, at=((z*h+y)*w+x)*4;
        for (unsigned c=0;c<3;c++) uv[c]=fmin(fmax(lut_pq(k*lut_inverse((index[c]+.5)/sizes[c])),0),1);
        float b[4]; for (unsigned c=0;c<4;c++) b[c]=lut_sample(input,w,h,d,uv,c);
        double l=b[0]*.2126+b[1]*.7152+b[2]*.0722;
        double s=fmin(fmax((l-.65)/.35,0),1); s=s*s*(3-2*s);
        for (unsigned c=0;c<4;c++) {
          float got;
          if (half) { _Float16 v; memcpy(&v,output+(at+c)*2,2); got=v; }
          else got=output[at+c]/255.f;
          double expected=test<2?input[at+c]:c==3?b[c]:fmax(b[c]*(1-s)+input[at+c]*s,input[at+c]);
          double tolerance=half?.004:2.0/255;
          if (fabs(got-expected)>tolerance)
            fprintf(stderr,"bright mismatch half=%u test=%u xyz=%u,%u,%u c=%u got=%g expected=%g\n",half,test,x,y,z,c,got,expected);
          CHECK(fabs(got-expected)<=tolerance);
          if (c<3) CHECK(got+1.0/1024>=input[at+c]);
        }
      }
      region=(NVNcopyRegion){0,0,0,w*2,h*2,d*2};
      CALL(nvnTextureReadTexels,&texture,NULL,&region,untouched);
      for (unsigned i=0;i<w*2*h*2*d*2;i++) {
        if (half) { _Float16 v[4]; memcpy(v,untouched+i*8,8); CHECK(v[0]==.125f && v[1]==.25f && v[2]==.375f && v[3]==1); }
        else CHECK(!memcmp(untouched+i*4,(uint8_t[]){32,64,96,255},4));
      }
    }
    CALL(nvnTextureFinalize,&texture);
  }
}
#include "native_vulkan_freeze.inc"
static VkSemaphore pool_copy_gate(void);
static void pool_copy_signal(void *context);
#include "native_vulkan_buffer.inc"
#include "native_vulkan_pass.inc"
#include "native_vulkan_zero.inc"
#include "native_vulkan_nofxaa.inc"
#include "native_vulkan_bloom.inc"
#include "native_vulkan_specialization.inc"
#include "native_vulkan_dynamic.inc"
#include "native_vulkan_store.inc"
#include "native_vulkan_cover.inc"
#include "native_vulkan_mip_cover.inc"
#include "native_vulkan_mip_stamp_write.inc"
#include "native_vulkan_subpass_cache.inc"
#include "native_vulkan_quad.inc"
#include "native_vulkan_extrap.inc"
#include "native_vulkan_submit.inc"
#include "native_vulkan_descriptor_pool.inc"
#include "native_vulkan_command_reset.inc"
#include "native_vulkan_buffer_partition.inc"
#include "native_vulkan_pool.inc"
#include "native_vulkan_dirty_range.inc"
#include "native_vulkan_acquire.inc"
#include "native_vulkan_stream.inc"
#include "native_vulkan_read.inc"
#include "native_vulkan_depth_array.inc"
#include "native_vulkan_readonly_store.inc"
#include "native_vulkan_texel_storage.inc"
#include "native_vulkan_texel_bits.inc"
#include "native_vulkan_target_match.inc"
#include "native_vulkan_metadata_reuse.inc"
#include "native_vulkan_driver_key.inc"
#include "native_vulkan_buffer_visibility.inc"
#include "native_vulkan_initialized_span.inc"
#include "native_vulkan_pool_span.inc"
#include "native_vulkan_upload_dual.inc"
#include "native_vulkan_pool_window.inc"
#include "native_vulkan_pool_ranges.inc"
#include "native_vulkan_pool_binding_mask.inc"
#include "native_vulkan_buffer_scope.inc"
#include "native_vulkan_present.inc"
#include "native_vulkan_sampled_array.inc"
#include "native_vulkan_bc_cpu_scratch.inc"
#include "native_vulkan_upload_regions.inc"
#include "native_vulkan_clear_group.inc"
#include "native_vulkan_vertex_binding.inc"
#include "native_vulkan_vertex_require.inc"
#include "native_vulkan_logical_preflight.inc"
#include "native_vulkan_image_source_flow.inc"
#include "native_vulkan_deferred_mip_clear.inc"
#include "native_vulkan_pending_read.inc"
#include "native_vulkan_prewarm.inc"
#include "native_vulkan_pool_policy.inc"
#include "native_vulkan_pool_finite.inc"
#include "native_vulkan_pool_geometry.inc"
#include "native_vulkan_pool_span_index.inc"
#include "native_vulkan_pool_cache.inc"
#include "native_vulkan_pool_address.inc"
#include "native_vulkan_pool_authority_batch.inc"
#include "native_vulkan_pool_copy_accounting.inc"
#include "native_vulkan_pool_flags.inc"
#include "native_vulkan_pool_publication.inc"
#include "native_vulkan_upload_page_span.inc"
#include "native_vulkan_scan_pairs.inc"
#include "native_vulkan_idle_trim.inc"
#include "native_vulkan_driver_span.inc"
#include "native_vulkan_pool_access_cache.inc"
#include "native_vulkan_temp_age.inc"
#include "native_vulkan_image_trim.inc"
#include "native_vulkan_retained_update.inc"
#include "native_vulkan_pixel_local.inc"
#include "native_vulkan_ds_plane_store.inc"
#include "native_vulkan_astc_decode.inc"
#include "native_vulkan_event.inc"
#include "native_vulkan_compute_bind.inc"
#ifdef DM_VULKAN_SHARED_POOLS
extern int vk_test_fixed_shared_pool(uint64_t,int *,int *,uint32_t *);
extern void vk_test_shared_close_arm(void),vk_test_shared_close_wait(void);
typedef struct {NVNmemoryPool *pool;_Atomic int done;} fixed_shared_close_context;
static void *fixed_shared_close_thread(void *arg) {fixed_shared_close_context *c=arg;CALL(nvnMemoryPoolFinalize,c->pool);atomic_store(&c->done,1);return NULL;}
static void fixed_shared_close_test(void) {
  uint8_t *memory;CHECK(!posix_memalign((void **)&memory,65536,65536));memset(memory,0x7b,65536);
  NVNmemoryPool source;NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&b,memory,65536);CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&source,&b));
  VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore gate;
  VK_CHECK(vkCreateSemaphore(V.device,&create,NULL,&gate));uint64_t value=1;
  VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
  VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo block={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,.waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
  pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&block,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);
  CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)memory,(uintptr_t)(bytes+(2u<<20)),256,0);
  NVNcommandHandle h=end();CALL(nvnQueueSubmitCommands,&queue,1,&h);
  fixed_shared_close_context context={.pool=&source};pthread_t closer;vk_test_shared_close_arm();
  CHECK(!pthread_create(&closer,NULL,fixed_shared_close_thread,&context));vk_test_shared_close_wait();CHECK(!atomic_load(&context.done));
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
  CHECK(!pthread_join(closer,NULL));CHECK(atomic_load(&context.done));
  for(unsigned k=0;k<256;k++)CHECK(bytes[(2u<<20)+k]==0x7b);
  vk_pool_span_info span;CHECK(vk_pool_span_query((uintptr_t)memory,4096,&span)==VK_POOL_SPAN_NONE);
  // Reuse exact original address only after close completes: fresh identity,
  // no stale handle/shadow/pin may survive this ABA lifetime.
  CHECK(CALL(nvnMemoryPoolInitialize,&source,&b));CHECK(vk_pool_span_query((uintptr_t)memory,4096,&span)==VK_POOL_SPAN_UNBACKED);
  CALL(nvnMemoryPoolFinalize,&source);free(memory);vkDestroySemaphore(V.device,gate,NULL);
}


// Independent bounded review oracle: original completion-effect store must
// not overtake a later ordered GPU writer of the same NVN buffer bytes.
static pthread_mutex_t review_effect_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t review_effect_cv=PTHREAD_COND_INITIALIZER;
static int review_effect_entered,review_effect_release;
static void review_effect_gate(void *unused) {
  (void)unused;pthread_mutex_lock(&review_effect_mu);review_effect_entered=1;
  pthread_cond_broadcast(&review_effect_cv);
  while(!review_effect_release)pthread_cond_wait(&review_effect_cv,&review_effect_mu);
  pthread_mutex_unlock(&review_effect_mu);
}
static void review_cpu_effect_then_gpu_write(void) {
  uint32_t *destination=(void *)(bytes+(3u<<20));*destination=0;
  mtl_sub_begin();vk_completion(review_effect_gate,NULL);
  mtl_sub_event(destination,0,7);uint64_t first=vk_internal_commit();
  pthread_mutex_lock(&review_effect_mu);
  while(!review_effect_entered)pthread_cond_wait(&review_effect_cv,&review_effect_mu);
  pthread_mutex_unlock(&review_effect_mu);
  mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  CHECK(vk_pool_address_overwrite((uintptr_t)destination,4,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vkCmdFillBuffer(V.cmd,buffer->handle,offset,4,9);
  vk_buffer_transfer_written_range((uintptr_t)destination,4);
  vk_buffer_writeback_stage((uintptr_t)destination,4,VK_PIPELINE_STAGE_TRANSFER_BIT);
  uint64_t last=vk_internal_commit();CHECK(last>first);
  pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueWaitIdle(V.queue));pthread_mutex_unlock(&V.queue_mu);
  pthread_mutex_lock(&review_effect_mu);review_effect_release=1;
  pthread_cond_broadcast(&review_effect_cv);pthread_mutex_unlock(&review_effect_mu);
  vk_internal_wait(last);
  LOG("[fixed shared review] ordered CPUeffect7 then GPUwriter9 final=%u expected=9",*destination);
  CHECK(*destination==9);
}

// Revision oracle: the releaser waits for the actual conflicting range
// dependency. No elapsed time or arbitrary sleep is used as evidence.
static uint64_t review_dependency_seen;
static void *review_release_on_dependency(void *unused) {
  (void)unused;extern uint64_t vk_test_shared_dependency_observed(void);
  review_dependency_seen=vk_test_shared_dependency_observed();
  pthread_mutex_lock(&review_effect_mu);review_effect_release=1;
  pthread_cond_broadcast(&review_effect_cv);pthread_mutex_unlock(&review_effect_mu);return NULL;
}
static void review_fill_shared(uint32_t *destination,uint32_t value) {
  vk_buffer *b;uint64_t offset;
  CHECK(vk_pool_address_overwrite((uintptr_t)destination,4,(void **)&b,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vkCmdFillBuffer(V.cmd,b->handle,offset,4,value);
  vk_buffer_transfer_written_range((uintptr_t)destination,4);
  vk_buffer_writeback_stage((uintptr_t)destination,4,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
static void review_generic_cpu_write(void *destination) {
  // Keep the range-ownership oracle a real completion CPU writer after NVN
  // events become GPU-native. Query/other CPU callbacks still need this guard.
  dm_guest_write_prepare(destination,sizeof(uint32_t));
  __atomic_store_n((uint32_t *)destination,7,__ATOMIC_RELEASE);
}
#ifndef DM_ANDROID_APP
// Real GPU publication held pending by the existing completion callback gate.
// CPU begin either observes disjoint bytes safely or rejects a true dependency;
// no sleeps, elapsed measurements, completion spoofing or game code.
static void shared_image_observe_dependency_test(const char *mode) {
  int cpu=!strcmp(mode,"image-observe-cpu"),same=!strcmp(mode,"image-observe-same"),
      cache=!strcmp(mode,"image-observe-cache");
  CHECK(cpu || same || cache || !strcmp(mode,"image-observe-neighbor"));
  CHECK(V.timeline_semaphore && vk_async_submissions());
  int on=V.options.shared_image_raw_observe;
  CHECK(!setenv("DM_VK_SHARED_IMAGE_RAW_OBSERVE",on?"0":"1",1));
  CHECK(V.options.shared_image_raw_observe==on); // cold selection never re-reads env
  uint32_t *a=(void *)(bytes+(3u<<20)),*b=same?a:a+64,*c=a+128;
  *a=0;*b=0x55;*c=0;
  // Import real coherent Buffer storage before acquiring its CPU image scope.
  mtl_sub_begin();review_fill_shared(c,3);vk_internal_wait(vk_internal_commit());
  nvn_dirty_scope *scope=nvn_dirty_scope_acquire(b,4);CHECK(scope);
  if(cache) {extern void vk_test_shared_dependency_arm(int);vk_test_shared_dependency_arm(1);}
  extern int vk_test_shared_image_raw_scope(uint64_t,uint64_t,int);
  int allow=vk_test_shared_image_raw_scope((uintptr_t)b,4,0);
  CHECK(allow==(on && !cache && !getenv("DM_SHARED_IMAGE_FORCE_PREWRITE_GATE")));
  for(int edge=1;edge<=4;edge++)CHECK(!vk_test_shared_image_raw_scope((uintptr_t)b,4,edge));
  mtl_sub_begin();if(!cpu)review_fill_shared(a,7);
  vk_completion(review_effect_gate,NULL);
  if(cpu)vk_completion_write(review_generic_cpu_write,a,a,sizeof *a);
  uint64_t first=vk_internal_commit();
  pthread_mutex_lock(&review_effect_mu);
  while(!review_effect_entered)pthread_cond_wait(&review_effect_cv,&review_effect_mu);
  pthread_mutex_unlock(&review_effect_mu);
  // This old raw query may cache a negative. It cannot suppress the larger
  // CPU-effect page query in the split-scope prepare/begin below.
  mtl_sub_begin();CHECK(vk_pool_pending_writes((uintptr_t)b,4)==same);
  int current_gpu=!cpu && !cache && !getenv("DM_SHARED_IMAGE_FORCE_PREWRITE_GATE");
  if(current_gpu)review_fill_shared(c,9);uint64_t recording=vk_recording_serial();VkCommandBuffer command=V.cmd;
  vk_shared_cpu_observation token={0};
  if(allow && !cpu && !same) {
    CHECK(vk_pool_shared_cpu_observe_prepare((uintptr_t)b,4));
    CHECK(vk_recording_serial()==recording && V.cmd==command); // no neighbor-only forced submit
    CHECK(vk_pool_shared_cpu_observe_begin((uintptr_t)b,4,&token));
    CHECK(*b==0x55);vk_pool_shared_cpu_observe_end(&token);
    pthread_mutex_lock(&review_effect_mu);CHECK(!review_effect_release);pthread_mutex_unlock(&review_effect_mu);
  } else CHECK(!vk_pool_shared_cpu_observe_begin((uintptr_t)b,4,&token));
  pthread_mutex_lock(&review_effect_mu);review_effect_release=1;
  pthread_cond_broadcast(&review_effect_cv);pthread_mutex_unlock(&review_effect_mu);
  vk_internal_wait(first);vk_internal_wait(vk_internal_commit());
  CHECK(*a==7 && *b==(same?7u:0x55u) && *c==(current_gpu?9u:3u));
  // True producer completion now makes the same real image source readable.
  CHECK(vk_pool_shared_cpu_observe_prepare((uintptr_t)b,4));
  CHECK(vk_pool_shared_cpu_observe_begin((uintptr_t)b,4,&token));
  CHECK(*b==(same?7u:0x55u));vk_pool_shared_cpu_observe_end(&token);
  uint32_t gpu_read=0;mtl_sub_begin();pool_window_read((uint8_t *)b,4,(uint8_t *)&gpu_read);
  vk_internal_wait(vk_internal_commit());CHECK(gpu_read==(same?7u:0x55u));
  nvn_dirty_scope_release(scope);
  printf("{\"test_details\":\"shared-image-byte-observation\",\"mode\":\"%s\",\"startup_raw\":%d,\"actual_GPU_bytes\":true,\"CPU_page_preserved\":true,\"raw_negative_cache_not_page_proof\":true,\"guard_metadata_edges_not_NC_hardware\":true}\n",mode,on);
}
#endif
static void fixed_shared_dependency_test(void) {
  const char *mode=getenv("DM_FIXED_SHARED_DEPENDENCY_TEST");
#ifndef DM_ANDROID_APP
  if(!strncmp(mode,"image-observe-",14)) {shared_image_observe_dependency_test(mode);return;}
#endif
  int separate=!strcmp(mode,"disjoint"),cache=!strcmp(mode,"cache-phase"),page=!strcmp(mode,"page-effect") || !strcmp(mode,"page-effect-roi-negative");
  int gpu_only=!strcmp(mode,"gpu-only");
  CHECK(separate || cache || page || gpu_only || !strcmp(mode,"cpu-effect") || !strcmp(mode,"same-recording"));
  uint32_t *a=(void *)(bytes+(3u<<20)),*b=separate?(void *)(bytes+(3u<<20)+4096):cache?a+1:page?a+64:a;
  *a=0;*b=0;mtl_sub_begin();
  if(!strcmp(mode,"same-recording")) {
    vk_completion_write(review_generic_cpu_write,a,a,sizeof *a);review_fill_shared(a,9);vk_internal_commit();
    die("Unsupported same-recording CPUeffect/shared GPU combination accepted");
  }
  if(cache || gpu_only)review_fill_shared(a,7);
  vk_completion(review_effect_gate,NULL);
  if(!cache && !gpu_only)vk_completion_write(review_generic_cpu_write,a,a,sizeof *a);
  uint64_t first=vk_internal_commit();
  pthread_mutex_lock(&review_effect_mu);
  while(!review_effect_entered)pthread_cond_wait(&review_effect_cv,&review_effect_mu);
  pthread_mutex_unlock(&review_effect_mu);
  pthread_t releaser;
  if(!separate && !gpu_only) {
    extern void vk_test_shared_dependency_arm(int);vk_test_shared_dependency_arm(cache);
    CHECK(!pthread_create(&releaser,NULL,review_release_on_dependency,NULL));
  }
  if(page) {
    extern void vk_test_shared_prearm_host_phase(uint64_t,uint64_t,int);
    vk_test_shared_prearm_host_phase((uintptr_t)b,4,strcmp(mode,"page-effect-roi-negative")!=0);
    CHECK(*a==7); // CPU-only prior callback in another atom of the same host page
  }
  mtl_sub_begin();review_fill_shared(b,9);uint64_t last=vk_internal_commit();CHECK(last>first);
  if(separate || gpu_only) {
    // A GPU-only oracle fence observes execution while S1 completion is still
    // gated. Production never gains a queue wait from this fixture.
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueWaitIdle(V.queue));pthread_mutex_unlock(&V.queue_mu);
    pthread_mutex_lock(&review_effect_mu);review_effect_release=1;
    pthread_cond_broadcast(&review_effect_cv);pthread_mutex_unlock(&review_effect_mu);
  } else {CHECK(!pthread_join(releaser,NULL));CHECK(review_dependency_seen==first);}
  vk_internal_wait(last);CHECK(*b==9);if(b!=a)CHECK(*a==7);
  // Early manual writeback flush keeps the pending CPU-read contract.
  mtl_sub_begin();review_fill_shared(b,11);vk_buffer_writebacks_flush();
  CHECK(vk_pool_pending_writes((uintptr_t)b,4));
  vk_internal_wait(vk_internal_commit());CHECK(*b==11);
  int ready,shadow;uint32_t properties;
  CHECK(vk_test_fixed_shared_pool((uintptr_t)bytes,&ready,&shadow,&properties));
  printf("{\"test_details\":\"fixed-shared-range-ownership\",\"mode\":\"%s\",\"actual_properties\":%u,\"cache_admission_injected\":%s,\"no_actual_noncoherent_claim\":true}\n",mode,properties,cache?"true":"false");
}

#include "native_vulkan_query.inc"

static void fixed_shared_buffer_test(void) {
  CHECK(vk_pool_shared_mode());
  CHECK(!setenv("DM_VULKAN_SHARED_POOLS","0",1)); // cannot change selected mode
  CHECK(vk_pool_shared_mode());
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&memory,&builder));CHECK(!nvn_dirty_tracked(bytes,4u<<20));
  int ready,shadow;uint32_t properties;
  CHECK(vk_test_fixed_shared_pool((uintptr_t)bytes,&ready,&shadow,&properties));CHECK(!ready && !shadow);
  vk_pool_span_info meta;CHECK(vk_pool_span_query((uintptr_t)bytes,4096,&meta)==VK_POOL_SPAN_UNBACKED);
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));CALL(nvnCommandBufferAddCommandMemory,&cmd,&memory,0,1u<<20);
  // Existing unaligned byte Copy/recorded Update/CPU-after-GPU roundtrip oracle.
  buffer_byte_test();
  uint8_t control[0x880];memset(control,2,sizeof control);NVNprogram program;
  NVNshaderData shader={.control=control};CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  uint32_t *src=(void *)(bytes+(1u<<20)),*dst=(void *)(bytes+(2u<<20));
  for(unsigned round=0;round<4;round++) {
    for(unsigned i=0;i<64;i++) {src[i]=round*77+i*17;dst[i]=0xfefefefe;}
    CALL(nvnMemoryPoolFlushMappedRange,&memory,(1u<<20),256);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)src,256);
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)dst,256);
    CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);submit(end());
    CALL(nvnMemoryPoolInvalidateMappedRange,&memory,(2u<<20),256);
    for(unsigned i=0;i<64;i++)CHECK(dst[i]==src[i]*3+7);
    CHECK(vk_test_fixed_shared_pool((uintptr_t)bytes,&ready,&shadow,&properties));CHECK(ready && !shadow);
    CHECK(vk_pool_span_query((uintptr_t)src,256,&meta)==VK_POOL_SPAN_SINGLE);
    CHECK(meta.logical_begin==0 && meta.logical_end==(4u<<20));
    CHECK(!nvn_dirty_tracked(bytes,4u<<20));
  }
  if(getenv("DM_FIXED_SHARED_CACHELINE_NEGATIVE")) {
    CHECK(V.timeline_semaphore);
    VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
    VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore gate;
    VK_CHECK(vkCreateSemaphore(V.device,&create,NULL,&gate));uint64_t value=1;
    VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo block={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,.waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&block,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)src,(uintptr_t)dst,64,0);
    NVNcommandHandle h=end();CALL(nvnQueueSubmitCommands,&queue,1,&h);
    // Cache API on another word in that destination atom/page must reject,
    // never hide a wait or invalidate CPU data while GPU work is pending.
    CALL(nvnMemoryPoolFlushMappedRange,&memory,(2u<<20)+4,4);
    die("Shared cacheline negative unexpectedly accepted");
  }
  if(getenv("DM_FIXED_SHARED_IMAGE_NEGATIVE")) {
    dirty_range_fixture image={0};dirty_range_create(&image);
    dirty_range_fill(image.data,4096,(uint8_t[]){255,0,0,255});dirty_range_pixels(&image.tex[0],(uint8_t[]){255,0,0,255});
    die("Shared unsupported packaged image unexpectedly accepted");
  }
  if(getenv("DM_FIXED_SHARED_EVENT_GPU_REVIEW"))review_cpu_effect_then_gpu_write();
  fixed_shared_close_test();
  // Backend CPU-demand fixture deliberately owns a raw host submit serial.
  // Run after all NVN Submit jobs; never reset or falsify either fence counter.
  buffer_cpu_read_test();
  CALL(nvnProgramFinalize,&program);CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&memory);
  CHECK(vk_pool_span_query((uintptr_t)bytes,4096,&meta)==VK_POOL_SPAN_NONE);
  LOG("[fixed shared fixture] actual properties=%#x atom=%llu, host coherent=%u; no shadow; linear NVN-only, packaged image contract remains blocked",properties,(unsigned long long)V.props.limits.nonCoherentAtomSize,!!(properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
}
#include "native_vulkan_shared_image.inc"
#include "native_vulkan_shared_import_windows.inc"
#include "native_vulkan_shared_lazy_alias.inc"
#include "native_vulkan_uniform_plan.inc"
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include "native_vulkan_native_resource.inc"
#include "native_vulkan_native_lane.inc"
#include "native_vulkan_native_retirement.inc"
#include "native_vulkan_native_scope.inc"
#endif
int dm_vulkan_test_main(void) {
  crash_thread_init();
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  const char *span_reference=getenv("DM_DRIVER_SPAN_REFERENCE");
  if(span_reference) {
    CHECK(!strcmp(span_reference,"0") || !strcmp(span_reference,"1"));
    vk_driver_span_reference_startup(!strcmp(span_reference,"1"));
  }
#endif
#ifdef DM_VULKAN_SHARED_POOLS
  if(getenv("DM_LEGACY_ASTC_COPY_FIXTURE"))vk_legacy_astc_copy_startup(1);
#endif
  if(getenv("DM_IDLE_CACHE_TRIM_FIXTURE")) {
    extern void nvn_test_idle_trim_cold_poll(void);
    CHECK(!V.device);CHECK(dm_gfx_vma_dump_request());CHECK(nvn_test_vma_dump_pending());dm_gfx_trim_memory_request();
    nvn_test_idle_trim_cold_poll();
    CHECK(!V.device && nvn_test_idle_trim_pending());
    extern int nvn_test_idle_trim_started(void);
    dm_gfx_suspend(1);
    CHECK(!V.device && !nvn_test_idle_trim_started() && nvn_test_idle_trim_pending());
    CHECK(!nvn_test_vma_dump_pending()); // no GPU/thread startup just for optional dump
    dm_gfx_suspend(0);
  }
  vk_graphics_prepare_mode(2);
#ifndef DM_ANDROID_APP
  if(!getenv("DM_DESCRIPTOR_POOL_BLOCK_FIXTURE"))
#endif
    setenv("DM_TEST_VULKAN_DESCRIPTOR_POOL_SETS", "4", 1);
  setenv("DM_VULKAN_GPU_PROFILE", "1", 1);
  setenv("DM_VULKAN_STORE_CHECK", "1", 1);
  {
    const char *base=getenv("DM_CAPTURE_TEST_DIR");if(!base) base=getenv("TMPDIR");
    char path[4096];snprintf(path,sizeof path,"%s/frame-dump-async.ppm",base?base:".");
    CHECK(!setenv("DM_VULKAN_FRAME_DUMP",path,1));
    CHECK(!setenv("DM_VULKAN_FRAME_DUMP_AT","2",1));
  }
  if(!getenv("DM_VULKAN_PIPELINE_CACHE_DIR")) {
    const char *base=getenv("DM_CAPTURE_TEST_DIR");if(!base) base=getenv("TMPDIR");
    char directory[4096];snprintf(directory,sizeof directory,"%s/pso-fixture-XXXXXX",base?base:".");
    CHECK(mkdtemp(directory)!=NULL);CHECK(!setenv("DM_VULKAN_PIPELINE_CACHE_DIR",directory,1));
  }
  extern const unsigned char __start_swforce[], __stop_swforce[];
  for (const hle_force_ent *e = (const void *)__start_swforce;
       e < (const hle_force_ent *)(const void *)__stop_swforce; e++)
    if (!strcmp(e->name, "nvnBootstrapLoader"))
      bootstrap = e->fn;
  CHECK(bootstrap != NULL);
  // 丢弃呈现必须在触及纹理/Surface 前返回，也不得制造游戏栅栏。
  // Discard returns before touching a texture/Surface and creates no guest fence.
  void mtl_present_discard(int);
  uint64_t before_discard = mtl_last_fence();
  mtl_present_discard(1);
  CHECK(mtl_present(NULL, 0, NULL) == 0);
  CHECK(mtl_last_fence() == before_discard);
  CHECK(mtl_presented_time(1) == -1);
  mtl_present_discard(0);
  // 恢复后的编号包含已丢弃画面；未知画面不能复用旧时间。
  // Resumed numbering includes dropped frames; unknown frames have no timing.
  CHECK(vk_window_begin_present() == 2);
  CHECK(mtl_presented_time(2) == 0);
#ifdef DM_VULKAN_SHARED_POOLS
  if(getenv("DM_SHARED_GUEST_HEAP_FIXTURE")) {
    // The real early selector/device -> first SVC order, not a finite injected
    // certificate. Default host fixtures do not link SVC and keep old behavior.
    extern int dm_guest_heap_startup_for_fixture(int) __attribute__((weak));
    extern uint64_t svc_dispatch(int,uint64_t *,uint64_t) __attribute__((weak));
    extern int dm_guest_heap_shared_mode(void) __attribute__((weak));
    CHECK(!V.device && dm_guest_heap_startup_for_fixture && svc_dispatch && dm_guest_heap_shared_mode);
    int requested=atoi(getenv("DM_SHARED_GUEST_HEAP_FIXTURE"));
    CHECK(dm_guest_heap_startup_for_fixture(requested)==vk_pool_shared_mode());
    uint64_t r[32]={0};r[1]=4u<<20;svc_dispatch(0x01,r,0);CHECK(!r[0]);bytes=(void *)(uintptr_t)r[1];
    CHECK(dm_guest_heap_shared_mode()==vk_pool_shared_mode());
  } else
#endif
  CHECK(!posix_memalign((void **)&bytes, 65536, 4 << 20));
  memset(bytes, 0, 4 << 20);
  NVNdeviceBuilder db;
  CALL(nvnDeviceBuilderSetDefaults, &db);
  CHECK(CALL(nvnDeviceInitialize, &device, &db));
  NVNqueueBuilder qb;
  CALL(nvnQueueBuilderSetDefaults, &qb);
  CALL(nvnQueueBuilderSetDevice, &qb, &device);
  CHECK(CALL(nvnQueueInitialize, &queue, &qb));
#ifndef DM_ANDROID_APP
  if(getenv("DM_COMPUTE_BIND_FIXTURE")) {compute_bind_test();return 0;}
  if(getenv("DM_DESCRIPTOR_POOL_BLOCK_FIXTURE")) {
    descriptor_pool_block_test();CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"descriptor-pool-blocks\",\"checks\":%u,\"physical_sets\":512,\"retention_budget\":2048,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(getenv("DM_IMAGE_SOURCE_FLOW_FORCE")){vk_test_image_source_flow(1);source_flow_observe=1;}
  if(getenv("DM_IMAGE_SOURCE_FLOW_FIXTURE")) {
    image_source_flow_test();CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"operation-image-source-flow\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  if(getenv("DM_TEXTURE_LOOKUP_FIXTURE")) {
    uint8_t controls[13][0x880];for(unsigned n=0;n<13;n++)memset(controls[n],n,sizeof controls[n]);
    logical_preflight_test(controls[3],controls[4]);
    recipe_reentrant_test(controls[12],controls[4]);
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"draw-logical-lookup\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
  if(getenv("DM_BINDING_CONSUMER_FIXTURE")) {
    extern unsigned vk_test_binding_consumer_edges(void);
    extern void vk_test_binding_consumer_counts(uint64_t *,uint64_t *);
    checks+=vk_test_binding_consumer_edges();uint8_t vertex[0x880]={0};
    uint64_t compiled0,reference0,compiled,reference;vk_test_binding_consumer_counts(&compiled0,&reference0);
    int mode=vk_binding_consumer_reference_mode();
    // Existing Queue helpers need an owned command arena even on this early
    // fixture entry. Keep all NVN submissions before direct host-only submits.
    uint8_t *arena;CHECK(!posix_memalign((void **)&arena,65536,1<<20));
    NVNmemoryPoolBuilder builder;NVNmemoryPool command_pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,arena,1<<20);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&command_pool,&builder));CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
    CALL(nvnCommandBufferAddCommandMemory,&cmd,&command_pool,0,1<<20);
    descriptor_shader_hint_test(vertex);dynamic_uniform_test(vertex);
    uint8_t controls[13][0x880];for(unsigned n=0;n<13;n++)memset(controls[n],n,sizeof controls[n]);
    storage_offset_dependency_test(controls[5]);compute_texel_dependencies_test(controls[2]);
    logical_preflight_test(controls[3],controls[4]);recipe_reentrant_test(controls[12],controls[4]);
    texel_storage_test(vertex);texel_storage_bits_test(vertex);
    CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&command_pool);free(arena);
    vk_test_binding_consumer_counts(&compiled,&reference);CHECK(mode?reference>reference0:compiled>compiled0);
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"compiled-binding-consumer\",\"checks\":%u,\"reference\":%d,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks,mode);return 0;
  }
  if(getenv("DM_DESCRIPTOR_SHADER_HINT_FIXTURE")) {
    uint8_t vertex[0x880]={0};descriptor_shader_hint_test(vertex);
    dynamic_uniform_test(vertex);CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"descriptor-shader-slot-hint\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  if(getenv("DM_GPU_EVENT_PAYLOAD_FIXTURE")) {gpu_event_payload_test();return 0;}
#ifdef DM_VULKAN_SHARED_POOLS
  if(getenv("DM_FIXED_SHARED_QUERY_REPORT_TEST")) {
    CHECK(vk_pool_shared_mode());gpu_query_report_test();
    printf("{\"test\":\"GPU-native-NVN-query-publication\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
  if(getenv("DM_FIXED_SHARED_QUERY_MATH_TEST")) {
    CHECK(vk_pool_shared_mode());gpu_query_math_test();CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"GPU-native-query-math\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;
  }
  if(getenv("DM_FIXED_SHARED_POLICY_TEST")) {
    const char *expected=getenv("DM_FIXED_SHARED_POLICY_TEST");
    int shared=!strcmp(expected,"shared"),sync=!strcmp(expected,"sync-unsupported");
    CHECK(shared || sync || !strcmp(expected,"legacy"));
    CHECK(vk_pool_shared_mode()==shared);
    CHECK(V.options.pool_window_bytes==(shared?0:16u<<20));
    CHECK(V.options.pool_mirror_reference==!shared);
    CHECK(V.options.pool_defer_publication==(!shared && !sync));
    CHECK(mtl_pool_defer_publication_mode()==V.options.pool_defer_publication);
    CHECK(!V.cmd);
    CHECK(!setenv("DM_VULKAN_SHARED_POOLS",shared?"0":"1",1));
    CHECK(vk_pool_shared_mode()==shared); // startup selection stays immutable
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"fixed-shared-cold-policy\",\"mode\":\"%s\",\"window_bytes\":%llu,\"mirror\":%d,\"defer\":%d,\"checks\":%u,\"result\":\"passed\"}\n",expected,
        (unsigned long long)V.options.pool_window_bytes,V.options.pool_mirror_reference,V.options.pool_defer_publication,checks);
    return 0;
  }
  if(getenv("DM_LEGACY_ASTC_COPY_FIXTURE")) {
    CHECK(!vk_pool_shared_mode() && vk_legacy_astc_copy_mode());
    legacy_astc_cpu_escape_test();legacy_image_outer_preflight_test();astc_copy_encoding_test();shared_image_cpu_observation_test();
    int astc_gpu=V.features.textureCompressionASTC_LDR && V.props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU;
    printf("{\"test\":\"legacy-ASTC-copy-encoding\",\"checks\":%u,\"result\":\"%s\",\"actual_ASTC_GPU\":%s,\"CPU_phase_result\":\"passed\",\"game_modules_executed\":false}\n",checks,astc_gpu?"passed":"unsupported",astc_gpu?"true":"false");return 0;
  }
  if(getenv("DM_MIXED_COMPRESSED_COPY_FIXTURE")) {mixed_compressed_copy_test();return 0;}
  if(getenv("DM_SHARED_WINDOW_STARTUP_FALLBACK_FIXTURE")) {shared_window_startup_fallback_test();return 0;}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP) && defined(DM_VULKAN_SHARED_IMPORT_WINDOWS)
  if(getenv("DM_UNIFORM_PLAN_FIXTURE")) {uniform_plan_gpu_test();return 0;}
  if(getenv("DM_SHARED_LAZY_ALIAS_FIXTURE") && !strcmp(getenv("DM_SHARED_LAZY_ALIAS_FIXTURE"),"1")) {shared_lazy_alias_gpu_test();return 0;}
#endif
  if(getenv("DM_SHARED_IMPORT_WINDOW_FIXTURE")) {shared_import_windows_test();return 0;}
  if(getenv("DM_SHARED_IMAGE_NC_GATE_FIXTURE"))return shared_image_nc_gate_test();
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(getenv("DM_NATIVE_RETIREMENT_FIXTURE")) {native_retirement_gpu_test();return 0;}
  if(getenv("DM_NATIVE_ACQUIRE_SCOPE_FIXTURE")) {native_acquire_scope_gpu_test();
#ifndef DM_ANDROID_APP
    if(getenv("DM_IMAGE_SOURCE_FLOW_FORCE"))image_source_flow_check();
#endif
    return 0;}
  if(getenv("DM_NATIVE_LANE_WINDOW_FIXTURE")) {native_lane_gpu_test();return 0;}
  const char *native_fixture=getenv("DM_NATIVE_RESOURCE_NVN_FIXTURE");
  if(native_fixture)return native_resource_gpu_test(native_fixture);
#endif
  if(getenv("DM_SHARED_IMAGE_DIRTY_FIXTURE")) {shared_image_dirty_test();return 0;}
  if(getenv("DM_FIXED_SHARED_DEPENDENCY_TEST")) {
    CHECK(vk_pool_shared_mode());
    NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&b,bytes,4u<<20);CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&memory,&b));fixed_shared_dependency_test();
    CALL(nvnMemoryPoolFinalize,&memory);CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"fixed-shared-range-ownership\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;
  }
  if(getenv("DM_FIXED_SHARED_EVENT_ONLY_REVIEW")) {
    NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&b,bytes,4u<<20);CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&memory,&b));review_cpu_effect_then_gpu_write();
    CALL(nvnMemoryPoolFinalize,&memory);CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"completion-effect-before-later-GPU-writer-review\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;
  }
  if(getenv("DM_FIXED_SHARED_BUFFER_FIXTURE")) {
    fixed_shared_buffer_test();CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"fixed-shared-NVN-buffer\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  if(getenv("DM_READBACK_BATCH_FIXTURE")) {readback_batch_order_test();readback_batch_pool_test();command_pool_reset_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"readback-batch\",\"checks\":%u,\"NC_invalidations\":%llu,\"GPU_ROI_count\":35,\"pool_GPU_bytes\":true,\"event_after_fence\":true,\"reset_blocked\":true,\"game_modules_executed\":false,\"result\":\"passed\"}\n",checks,(unsigned long long)readback_pool_invalidations);return 0;}
  if(getenv("DM_BUFFER_HISTORY_FIXTURE")) {
    // Initialize the active submission policy before the existing partition
    // fixture checks it; neither oracle's byte assertions are changed.
    mtl_sub_begin();vk_internal_wait(vk_internal_commit());
    buffer_partition_test();buffer_scope_test();
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"buffer-history\",\"checks\":%u,\"result\":\"passed\"}\n",checks);
    return 0;
  }
  if(getenv("DM_ASTC_DECODE_FIXTURE")) {
    extern unsigned vk_texture_test_astc_contract(void);
    mtl_init();checks+=vk_texture_test_astc_contract();
    int gpu=astc_decode_gpu_test();
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"astc-decode-metadata\",\"checks\":%u,\"astc_supported\":%u,\"gpu_fixture_executed\":%d,\"decode_extension_enabled\":%u,\"result\":\"passed\"}\n",checks,V.features.textureCompressionASTC_LDR,gpu,V.astc_decode_mode);
    return 0;
  }
  if(getenv("DM_IMAGE_PRESSURE_TRIM_FIXTURE")) {image_pressure_trim_test();printf("{\"test\":\"image-pressure-trim\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(getenv("DM_TEXTURE_UPLOAD_REGIONS_FIXTURE")) {upload_regions_gpu_test();printf("{\"test\":\"texture-upload-regions\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
#endif
  if(getenv("DM_BC_CPU_SCRATCH_FIXTURE")) {bc_cpu_scratch_test();printf("{\"test\":\"bc-cpu-scratch\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_RETAINED_UPDATE_FIXTURE")) {retained_update_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"retained-update\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_TEXTURE_RANGE_DIRTY_FIXTURE")) {texture_range_dirty_proof_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"texture-range-dirty\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_UPLOAD_SCAN_PAIRS_FIXTURE")) {pool_upload_pair_boundaries_test();pool_upload_pair_gpu_tails_test();pool_upload_patterns_test();upload_page_span_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"upload-scan-pairs\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_UPLOAD_PAGE_SPAN_FIXTURE")) {upload_page_span_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"upload-page-span\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_ACCESS_CACHE_FIXTURE")) {pool_access_cache_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-access-cache\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_IDLE_CACHE_TRIM_FIXTURE")) {idle_cache_trim_test();printf("{\"test\":\"idle-cache-trim\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(getenv("DM_DRIVER_SPAN_FIXTURE")) {driver_span_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"driver-span\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;}
#endif
  if(getenv("DM_TEMP_CACHE_AGE_FIXTURE")) {temporary_cache_age_test();printf("{\"test\":\"temporary-cache-age\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_PUBLICATION_FIXTURE")) {pool_publication_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-publication\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_BINDING_MASK_FIXTURE")) {shader_pool_ranges_test();pool_binding_mask_gpu_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-binding-mask\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_FLAGS_FIXTURE")) {pool_flags_test();printf("{\"test\":\"pool-flags\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
#ifndef DM_ANDROID_APP
  if(getenv("DM_PLANNED_FORWARD_FIXTURE")&&!getenv("DM_PLANNED_FORWARD_GPU_ONLY")) {
    mtl_init();CALL(nvnQueueFinish,&queue);
    extern unsigned vk_pass_test_forward_reuse(void);
    checks+=vk_pass_test_forward_reuse();
  }
#endif
  ds_plane_store_test();
  if(getenv("DM_DS_PLANE_STORE_FIXTURE")) {CHECK(vk_validation_error_count()==0);printf("{\"test\":\"ds-plane-store\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_PIXEL_LOCAL_RENDER_FIXTURE"))return pixel_local_render_test();
  if(getenv("DM_POOL_COPY_ACCOUNTING_FIXTURE")) {pool_copy_accounting_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-copy-accounting\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_AUTHORITY_BATCH_FIXTURE")) {pool_authority_batch_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-authority-batch\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_ADDRESS_DEATH"))pool_address_death_test(getenv("DM_POOL_ADDRESS_DEATH"));
  if(getenv("DM_POOL_ADDRESS_FIXTURE")) {pool_address_proof_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-address-proof\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_POOL_SPAN_CACHE_FIXTURE")) {pool_span_cache_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-span-cache\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(getenv("DM_POOL_OPERATION_GEOMETRY_FIXTURE")) {pool_operation_geometry_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"operation-buffer-geometry\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;}
  if(getenv("DM_POOL_SPAN_SEARCH_INDEX_FIXTURE")) {pool_span_search_index_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-span-search-index\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;}
#endif
  if(getenv("DM_POOL_FINITE_FIXTURE")) {pool_finite_consolidation_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pool-finite\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if(getenv("DM_VERTEX_REQUIRE_FIXTURE")) {vertex_require_dedup_test();CHECK(vk_validation_error_count()==0);printf("{\"test\":\"vertex-require\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;}
  if (getenv("DM_VULKAN_ACQUIRE_TEST")) return acquire_sync_scope_test();
  int old_upload_dual=upload_dual_fixture_begin();
  if (getenv("DM_VULKAN_MEMORY_TEST")) {
    memory_suballocation_test();
    // Exercise memory visibility independently of graphics-format/shader
    // fixtures. These are the same real, gated-GPU oracles as the full suite;
    // the full suite still runs them and retains all graphics assertions.
    unchanged_upload_publication_test();
    complete_pool_overwrite_test();
    pool_upload_patterns_test();
    pool_page_generation_test();
    queued_upload_overlap_test();
    pool_stage_ranges_test();
    pool_word_coverage_test();
    grouped_upload_ranges_test();
    pool_upload_hazard_test();
    CHECK(vk_validation_error_count() == 0);
    vk_test_upload_dual_enable(old_upload_dual);
    printf("{\"backend\":\"vulkan\",\"test\":\"memory-pool-visibility\",\"checks\":%u,\"result\":\"passed\"}\n", checks);
    fflush(NULL);
    _exit(0);
  }
  submission_lifetime_test();
  NVNmemoryPoolBuilder mb;
  CALL(nvnMemoryPoolBuilderSetDefaults, &mb);
  CALL(nvnMemoryPoolBuilderSetDevice, &mb, &device);
  CALL(nvnMemoryPoolBuilderSetStorage, &mb, bytes, 4 << 20);
  CALL(nvnMemoryPoolBuilderSetFlags, &mb,
       NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize, &memory, &mb));
  // 验脏页批量遍历的边界与未跟踪回退，含 Android 带标签分配地址。
  // Check batched dirty traversal boundaries and untracked fallback, including
  // tagged Android allocation addresses.
  size_t page_size = getpagesize();
  uint8_t *last_pages = bytes + (4 << 20) - 2 * page_size;
  nvn_dirty_protect(last_pages, 2 * page_size);
  uint64_t stamp = nvn_dirty_now();
  last_pages[2 * page_size - 1] = 17;
  dirty_visit visit = {0};
  nvn_dirty_visit(last_pages, 2 * page_size, stamp, visited, &visit);
  CHECK(visit.pages == 1 && visit.bytes == page_size &&
        visit.address == last_pages + page_size);
  visit = (dirty_visit){0};
  nvn_dirty_visit(last_pages + page_size + 7, 13, stamp, visited, &visit);
  CHECK(visit.pages == 1 && visit.bytes == 13 &&
        visit.address == last_pages + page_size + 7);
  visit = (dirty_visit){0};
  nvn_dirty_visit(last_pages, 2 * page_size, 0, visited, &visit);
  CHECK(visit.pages == 2 && visit.bytes == 2 * page_size);
  visit = (dirty_visit){0};
  nvn_dirty_visit(&stamp, sizeof stamp, stamp, visited, &visit);
  CHECK(visit.pages == 1 && visit.bytes == sizeof stamp &&
        visit.address == &stamp);
  visit = (dirty_visit){0};
  nvn_dirty_visit(last_pages, 0, stamp, visited, &visit);
  CHECK(visit.pages == 0);
  CHECK(CALL(nvnCommandBufferInitialize, &cmd, &device));
  CALL(nvnCommandBufferAddCommandMemory, &cmd, &memory, 0, 1 << 20);
#ifndef DM_ANDROID_APP
  if(getenv("DM_TEXTURE_RETIREMENT_INDEX_FIXTURE")) {
    texture_registration_retirement_test();image_retirement_test();
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"texture-retirement-index\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  uint32_t *src = (void *)(bytes + (1 << 20)),
           *dst = (void *)(bytes + (2 << 20)), *dst2 = dst + 512;
  for (unsigned i = 0; i < 256; i++)
    src[i] = i * 17 + 9;
  NVNeventBuilder eb;
  CALL(nvnEventBuilderSetDefaults, &eb);
  CALL(nvnEventBuilderSetStorage, &eb, &memory, (3 << 20));
  NVNevent event;
  CHECK(CALL(nvnEventInitialize, &event, &eb));
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferCopyBufferToBuffer, &cmd, (uintptr_t)src, (uintptr_t)dst,
       1024, 0);
  CALL(nvnCommandBufferCopyBufferToBuffer, &cmd, (uintptr_t)dst,
       (uintptr_t)dst2, 1024, 0);
  CALL(nvnCommandBufferSignalEvent, &cmd, &event, NVN_EVENT_SIGNAL_MODE_WRITE,
       NVN_EVENT_SIGNAL_LOCATION_BOTTOM, 0, 91);
  NVNcommandHandle h = end();
  CHECK(*(uint32_t *)(bytes + (3 << 20)) == 0);
  CHECK(dst[0] == 0);
  submit(h);
  CHECK(!memcmp(src, dst, 1024));
  CHECK(!memcmp(src, dst2, 1024));
  CHECK(*(uint32_t *)(bytes + (3 << 20)) == 91);
  uint32_t update[4] = {11, 22, 33, 44};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferUpdateUniformBuffer, &cmd, (uintptr_t)dst, 1024, 16,
       sizeof update, update);
  h = end();
  memset(update, 0, sizeof update);
  submit(h);
  CHECK(dst[4] == 11 && dst[5] == 22 && dst[6] == 33 && dst[7] == 44);
  src[4] = 0xf00dd00d;
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferCopyBufferToBuffer, &cmd, (uintptr_t)src, (uintptr_t)dst,
       1024, 0);
  submit(end());
  CHECK(dst[4] == 0xf00dd00d);
  buffer_byte_test();
  buffer_cpu_read_test();
  writeback_snapshot_test();
  cpu_known_copy_test();
  texel_submission_test();
  NVNtextureBuilder tb;
  CALL(nvnTextureBuilderSetDefaults, &tb);
  CALL(nvnTextureBuilderSetDevice, &tb, &device);
  CALL(nvnTextureBuilderSetTarget, &tb, NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetFlags, &tb, NVN_TEXTURE_FLAGS_IMAGE);
  CALL(nvnTextureBuilderSetWidth, &tb, 16);
  CALL(nvnTextureBuilderSetHeight, &tb, 16);
  CALL(nvnTextureBuilderSetFormat, &tb, NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage, &tb, &memory, (3 << 20) + 65536);
  NVNtexture tex;
  CHECK(CALL(nvnTextureInitialize, &tex, &tb));
  const float rgba[4] = {1, 0.5f, 0, 1};
  NVNcopyRegion region = {0, 0, 0, 16, 16, 1};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferClearTexture, &cmd, &tex, NULL, &region, rgba, 15);
  submit(end());
  uint8_t pixels[16 * 16 * 4];
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 255 && pixels[i + 1] == 128 && pixels[i + 2] == 0 &&
          pixels[i + 3] == 255);
  capture_test((const tex_t *)&tex);
  trace_test((const tex_t *)&tex);
  frame_dump_async_test((const tex_t *)&tex);
  uint64_t *report = (void *)(bytes + (3 << 20) + 32768);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_TIMESTAMP,
       (uintptr_t)report);
  submit(end());
  CHECK(report[0] > 0 && report[0] == report[1]);

  uint8_t controls[13][0x880];
  for (int i = 0; i < 13; i++)
    memset(controls[i], i, sizeof controls[i]);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(getenv("DM_IMAGE_PHASE_ARTIFACT_FIXTURE")) {
    extern unsigned vk_test_image_phase_artifact(void);
    checks+=vk_test_image_phase_artifact();
    pool_window_production_test(controls[0],controls[1],controls[5],controls[2]);
    recipe_reentrant_test(controls[12],controls[4]);
    // image_dependency_test normally runs after main installed a full16x16
    // scissor. The focused entry must establish that original precondition.
    LOG("[image phase focused setup] prior scissor enabled=%d rect=%d/%d/%d/%d",S.has_scissor,S.scissor.x,S.scissor.y,S.scissor.w,S.scissor.h);
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);submit(end());
    NVNprogram phase_fill;CHECK(CALL(nvnProgramInitialize,&phase_fill,&device));
    NVNshaderData phase_sd={.control=controls[7]};
    CHECK(CALL(nvnProgramSetShaders,&phase_fill,1,&phase_sd));
    for(int hint=0;hint<2;hint++)for(int srgb=0;srgb<2;srgb++)image_dependency_test(&phase_fill,hint,srgb);
    CALL(nvnProgramFinalize,&phase_fill);CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"operation-image-phase-artifact\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
  if(getenv("DM_STORE_WALK_FUSION_FIXTURE")) {
    extern unsigned vk_pass_test_store_walk_begin(void),vk_pass_test_store_walk_end(void);
    CALL(nvnQueueFinish,&queue);
    checks+=vk_pass_test_store_walk_begin();
    store_overwrite_test(controls[0],controls[9],NVN_FORMAT_DEPTH32F_STENCIL8);
    store_overwrite_test(controls[0],controls[9],NVN_FORMAT_DEPTH24_STENCIL8);
    checks+=vk_pass_test_store_walk_end();
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"submission-store-walk-fusion\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
  if(getenv("DM_POOL_GEOMETRY_POLICY_FIXTURE")) {
    pool_window_production_test(controls[0],controls[1],controls[5],controls[2]);
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"operation-geometry-production-policy\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  NVNshaderData sd[2] = {{.control = controls[0]}, {.control = controls[1]}};
  NVNprogram graphics, compute;
  CHECK(CALL(nvnProgramInitialize, &graphics, &device));
  CHECK(CALL(nvnProgramSetShaders, &graphics, 2, sd));
  CHECK(CALL(nvnProgramInitialize, &compute, &device));
  sd[0].control = controls[2];
  CHECK(CALL(nvnProgramSetShaders, &compute, 1, sd));
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferBindProgram, &cmd, &compute,
       NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_COMPUTE, 0,
       (uintptr_t)src, 1024);
  CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_COMPUTE, 1,
       (uintptr_t)dst, 1024);
  CALL(nvnCommandBufferDispatchCompute, &cmd, 4, 1, 1);
  submit(end());
  for (unsigned i = 0; i < 256; i++)
    CHECK(dst[i] == src[i] * 3 + 7);
  descriptor_inflight_test((const prog_t *)&compute, 0,6);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_COMPUTE, 0,
       (uintptr_t)(src + 1), 1024);
  CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_COMPUTE, 1,
       (uintptr_t)(dst + 3), 1024);
  CALL(nvnCommandBufferDispatchCompute, &cmd, 4, 1, 1);
  submit(end());
  for (unsigned i = 0; i < 256; i++)
    CHECK(dst[i + 3] == src[i + 1] * 3 + 7);
  float *color = (void *)(bytes + (3 << 20) + 16384);
  color[0] = 0;
  color[1] = 1;
  color[2] = 0.25f;
  color[3] = 1;
  NVNcolorState cs;
  NVNpolygonState ps;
  CALL(nvnColorStateSetDefaults, &cs);
  CALL(nvnPolygonStateSetDefaults, &ps);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  const NVNtexture *colors[] = {&tex};
  CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
  CALL(nvnCommandBufferBindProgram, &cmd, &graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindColorState, &cmd, &cs);
  CALL(nvnCommandBufferBindPolygonState, &cmd, &ps);
  CALL(nvnCommandBufferBindUniformBuffer, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       (uintptr_t)color, 16);
  CALL(nvnCommandBufferResetCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED,
       (uintptr_t)report);
  submit(end());
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 64 &&
          pixels[i + 3] == 255);
  CHECK(report[0] == 256);
  // 同地址常量经 GPU 更新后必须重新快照，不能命中提交内旧常量缓存。
  // Updating the same uniform address on GPU must invalidate an older snapshot
  // cached within the submission.
  const float blue[4] = {0, 0, 1, 1};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  CALL(nvnCommandBufferUpdateUniformBuffer, &cmd, (uintptr_t)color, 16, 0, 16,
       blue);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255 &&
          pixels[i + 3] == 255);
  // 视口小于附件时保留外围内容；同提交扩大/移动视口必须重新覆盖新区域。
  // Preserve attachment pixels outside a smaller viewport, and cover a moved
  // or enlarged viewport later in the same submit.
  const float outside[4] = {1, 0, 1, 1};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferClearTexture, &cmd, &tex, NULL, &region, outside, 15);
  const float viewport_small[4] = {2, 3, 6, 5},
              viewport_large[4] = {8, 6, 8, 7},
              viewport_full[4] = {0, 0, 16, 16};
  CALL(nvnCommandBufferSetViewports, &cmd, 0, 1, viewport_small);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  CALL(nvnCommandBufferSetViewports, &cmd, 0, 1, viewport_large);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  CALL(nvnCommandBufferSetViewports, &cmd, 0, 1, viewport_full);
  submit(end());
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  for (unsigned y = 0; y < 16; y++)
    for (unsigned x = 0; x < 16; x++) {
      int covered =
          (x >= 2 && x < 8 && y >= 3 && y < 8) || (x >= 8 && y >= 6 && y < 13);
      const uint8_t expected_pixel[4] = {covered ? 0 : 255, 0, 255, 255};
      CHECK(!memcmp(pixels + (y * 16 + x) * 4, expected_pixel, 4));
    }
  // 同一只读未对齐 SSBO 连续绘制保持通道；同提交 GPU 更新后必须重新快照。
  // Repeated read-only unaligned SSBO draws retain the pass, but a GPU update
  // within the same submit must invalidate the cached copy.
  NVNprogram storage_graphics;
  CHECK(CALL(nvnProgramInitialize, &storage_graphics, &device));
  sd[0].control = controls[0];
  sd[1].control = controls[5];
  CHECK(CALL(nvnProgramSetShaders, &storage_graphics, 2, sd));
  float *storage_color = color + 17;
  memcpy(storage_color, rgba, 16);
  uint64_t storage_passes_before, storage_draws_before, storage_passes_after,
      storage_draws_after;
  mtl_stats(&storage_passes_before, &storage_draws_before);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferBindProgram, &cmd, &storage_graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       (uintptr_t)storage_color, 16);
  for (unsigned i = 0; i < 128; i++) {
    CALL(nvnCommandBufferBarrier, &cmd, NVN_BARRIER_ORDER_FRAGMENTS_BIT);
    if (i == 64)
      CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_TIMESTAMP_TOP,
           (uintptr_t)report);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  }
  submit(end());
  mtl_stats(&storage_passes_after, &storage_draws_after);
  CHECK(storage_draws_after - storage_draws_before == 128 &&
        storage_passes_after - storage_passes_before < 4);
  CHECK(report[0] > 0 && report[0] == report[1]);
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 255 && pixels[i + 1] == 128 && pixels[i + 2] == 0 &&
          pixels[i + 3] == 255);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  CALL(nvnCommandBufferUpdateUniformBuffer, &cmd, (uintptr_t)storage_color, 16,
       0, 16, blue);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255 &&
          pixels[i + 3] == 255);
  // 三块屏幕分别用 A/B/A 的 c0/c2
  // 表，既验内容复用也验旧绘制不会被后续绑定改写。 Three screen regions use
  // A/B/A c0/c2 tables, checking reuse and immutable snapshots across later
  // binding changes in the same command buffer.
  NVNprogram driver_graphics;
  CHECK(CALL(nvnProgramInitialize, &driver_graphics, &device));
  sd[0].control = controls[0];
  sd[1].control = controls[6];
  CHECK(CALL(nvnProgramSetShaders, &driver_graphics, 2, sd));
  uint64_t driver_last_before,driver_content_before,driver_misses_before;
  int driver_original_reference;
  vk_driver_constant_stats(&driver_last_before,&driver_content_before,
                          &driver_misses_before,&driver_original_reference);
  for(unsigned driver_mode=0;driver_mode<4;driver_mode++) {
    vk_driver_constant_reference(driver_mode==0||driver_mode==3);
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferBindProgram, &cmd, &driver_graphics,
         NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    // More immutable c0/c2 snapshots than one arena chunk, in the same
    // submission. The shader also checks the last vec4 of both 64 KiB ranges.
    uint8_t driver_values[2][4];
    for(unsigned group=0;group<7;group++) {
      const uint8_t base_values[2][4] = {{16, 32, 33, 7}, {64, 48, 17, 3}};
      for(unsigned v=0;v<2;v++) for(unsigned c=0;c<4;c++)
        driver_values[v][c]=base_values[v][c]+group+driver_mode;
      for (unsigned part = 0; part < 3; part++) {
        const uint8_t *values = driver_values[part == 1];
        CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
             (uintptr_t)color, values[0]);
        CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_FRAGMENT, 1,
             (uintptr_t)color, values[1]);
        NVNseparateTextureHandle texture_handle = {.value = values[2]};
        NVNseparateSamplerHandle sampler_handle = {.value = (uint32_t)values[3]
                                                            << 20};
        CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_FRAGMENT,
             0, 1, &texture_handle);
        CALL(nvnCommandBufferBindSeparateSamplers, &cmd, NVN_SHADER_STAGE_FRAGMENT,
             0, 1, &sampler_handle);
        CALL(nvnCommandBufferSetScissor, &cmd, part * 5, 0, part == 2 ? 6 : 5, 16);
        for(unsigned repeat=0;repeat<2;repeat++)
          CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
      }
    }
    CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 16, 16);
    submit(end());
    CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
    for (unsigned y = 0; y < 16; y++)
      for (unsigned x = 0; x < 16; x++)
        CHECK(!memcmp(pixels + (y * 16 + x) * 4, driver_values[x >= 5 && x < 10],
                      4));
  }
  uint64_t driver_last_after,driver_content_after,driver_misses_after;
  int driver_final_reference;
  vk_driver_constant_stats(&driver_last_after,&driver_content_after,
                          &driver_misses_after,&driver_final_reference);
  CHECK(driver_last_after>driver_last_before);
  CHECK(driver_content_after>driver_content_before);
  CHECK(driver_misses_after>driver_misses_before);
  CHECK(driver_final_reference==1);
  vk_driver_constant_reference(driver_original_reference);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(getenv("DM_COLD_PHASE_FIXTURE")) {
    // Reuse the actual read-only alignment GPU-update checks and driver
    // last/content/MISS pixel assertions above, then the existing full/sparse
    // key fixtures. No implementation-shaped oracle or target statistic.
    driver_compact_key_test();CHECK(vk_validation_error_count()==0);
    CALL(nvnProgramFinalize,&storage_graphics);CALL(nvnProgramFinalize,&driver_graphics);
    printf("{\"test\":\"cold-phase-content\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  uint8_t *upload = bytes + (3 << 20) + 262144;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      uint8_t *p = upload + (y * 16 + x) * 4;
      p[0] = x * 17;
      p[1] = y * 17;
      p[2] = 64;
      p[3] = 255;
    }
  const float vertices[12] = {-1, 1, 0, 0, 3, 1, 2, 0, -1, -3, 0, 2};
  float *vertex = (void *)(bytes + (2 << 20) + 8192);
  memcpy(vertex, vertices, sizeof vertices);
  NVNtexture output;
  CALL(nvnTextureBuilderSetStorage, &tb, &memory, (3 << 20) + 131072);
  CHECK(CALL(nvnTextureInitialize, &output, &tb));
  NVNtexturePool tp;
  CHECK(CALL(nvnTexturePoolInitialize, &tp, &memory, (3 << 20) + 196608, 4));
  CALL(nvnTexturePoolRegisterTexture, &tp, 1, &tex, NULL);
  NVNsamplerBuilder sb;
  CALL(nvnSamplerBuilderSetDefaults, &sb);
  CALL(nvnSamplerBuilderSetDevice, &sb, &device);
  CALL(nvnSamplerBuilderSetMinMagFilter, &sb, NVN_MIN_FILTER_NEAREST,
       NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode, &sb, NVN_WRAP_MODE_CLAMP_TO_EDGE,
       NVN_WRAP_MODE_CLAMP_TO_EDGE, NVN_WRAP_MODE_CLAMP_TO_EDGE);
  NVNsampler sampler;
  CHECK(CALL(nvnSamplerInitialize, &sampler, &sb));
  NVNsamplerPool sp;
  CHECK(CALL(nvnSamplerPoolInitialize, &sp, &memory, (3 << 20) + 197632, 4));
  CALL(nvnSamplerPoolRegisterSampler, &sp, 1, &sampler);
  NVNseparateTextureHandle th =
      CALL(nvnDeviceGetSeparateTextureHandle, &device, 1);
  NVNseparateSamplerHandle sh =
      CALL(nvnDeviceGetSeparateSamplerHandle, &device, 1);
  sd[0].control = controls[3];
  sd[1].control = controls[4];
  CHECK(CALL(nvnProgramSetShaders, &graphics, 2, sd));
  NVNvertexAttribState attrs[2];
  for (int i = 0; i < 2; i++) {
    CALL(nvnVertexAttribStateSetDefaults, &attrs[i]);
    CALL(nvnVertexAttribStateSetFormat, &attrs[i], NVN_FORMAT_RG32F, i * 8);
  }
  NVNvertexStreamState stream;
  CALL(nvnVertexStreamStateSetDefaults, &stream);
  CALL(nvnVertexStreamStateSetStride, &stream, 16);
  NVNbufferRange vr = {(uintptr_t)vertex, sizeof vertices};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetCopyRowStride, &cmd, 64);
  CALL(nvnCommandBufferCopyBufferToTexture, &cmd, (uintptr_t)upload, &tex, NULL,
       &region, 0);
  colors[0] = &output;
  CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
  CALL(nvnCommandBufferBindProgram, &cmd, &graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindVertexAttribState, &cmd, 2, attrs);
  CALL(nvnCommandBufferBindVertexStreamState, &cmd, 1, &stream);
  CALL(nvnCommandBufferBindVertexBuffers, &cmd, 0, 1, &vr);
  CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       1, &th);
  CALL(nvnCommandBufferBindSeparateSamplers, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       1, &sh);
  const unsigned repeated_draws = 300;
  uint64_t *timestamps = (void *)(bytes + (1 << 20) + 65536);
  uint64_t *prefix_reports = timestamps + repeated_draws * 2 + 16;
  uint64_t queries_before, queries_after, ended_queries;
  int query_spans;
  vk_counter_query_stats(&queries_before,&ended_queries,&query_spans);
  uint64_t passes_before, draws_before, passes_after, draws_after;
  mtl_stats(&passes_before, &draws_before);
  CALL(nvnCommandBufferResetCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED);
  for (unsigned repeat = 0; repeat < repeated_draws; repeat++) {
    CALL(nvnCommandBufferBarrier, &cmd,
         NVN_BARRIER_ORDER_PRIMITIVES_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT |
             NVN_BARRIER_ORDER_INDIRECT_DATA_BIT);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_TIMESTAMP,
         (uintptr_t)(timestamps + repeat * 2));
    if (repeat == 0 || repeat == 255)
      CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED,
           (uintptr_t)(prefix_reports + (repeat ? 2 : 0)));
  }
  CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED,
       (uintptr_t)report);
  submit(end());
  CHECK(report[0] == repeated_draws * 256);
  CHECK(prefix_reports[0] == 256 && prefix_reports[2] == 256 * 256);
  vk_counter_query_stats(&queries_after,&ended_queries,&query_spans);
  CHECK(queries_after == ended_queries);
  CHECK(query_spans ? queries_after-queries_before < 10 :
                     queries_after-queries_before == repeated_draws);
  LOG("[vulkan occlusion span fixture] span=%d draws=%u queries=%llu prefixes=256/65536 total=%llu",
      query_spans,repeated_draws,(unsigned long long)(queries_after-queries_before),
      (unsigned long long)report[0]);
  mtl_stats(&passes_after, &draws_after);
  CHECK(draws_after - draws_before == repeated_draws &&
        passes_after - passes_before < 10);
  for (unsigned repeat = 0; repeat < repeated_draws; repeat++)
    CHECK(timestamps[repeat * 2] &&
          timestamps[repeat * 2] == timestamps[repeat * 2 + 1] &&
          (!repeat || timestamps[repeat * 2] >= timestamps[(repeat - 1) * 2]));
  // 同一通道内报告后继续画、再重置，以及跨提交累计，均保留 NVN 前缀语义。
  // Preserve report/continue/reset prefixes inside a pass and accumulation
  // across submissions, including reports retaining pre-reset query slots.
  uint64_t *boundaries=prefix_reports+4;
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)boundaries);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(boundaries+2));
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(boundaries+4));
  submit(end());
  CHECK(boundaries[0]==256&&boundaries[2]==256&&boundaries[4]==512);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  submit(end());
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(boundaries+6));
  submit(end());
  CHECK(boundaries[6]==512);
  // Count liveness is independent of drawing: both halves must be shaded,
  // while only samples after the second reset may appear in the report.
  // Cover separate submitted handles, nested calls and repeated use of the
  // exact same child command payload at different liveness positions.
  uint64_t liveness_live,liveness_dead;
  void vk_sample_liveness_stats(uint64_t *,uint64_t *);
  uint64_t liveness_elided_before,liveness_elided_after;int liveness_original_reference,liveness_actual_reference;
  vk_sample_query_elision_stats(&liveness_elided_before,&liveness_original_reference);
  for(unsigned query_reference=0;query_reference<2;query_reference++) {
  vk_sample_liveness_reference(query_reference);
  for(unsigned shape=0;shape<3;shape++) {
    vk_counter_query_stats(&queries_before,&ended_queries,&query_spans);
    mtl_stats(&passes_before,&draws_before);
    NVNcommandHandle handles[2];
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){0,0,0,1},15);
    CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,16);
    if(shape==2) {
      NVNcommandHandle setup=end();
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      NVNcommandHandle child=end();
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferCopyCommands,&cmd,1,&setup);
      CALL(nvnCommandBufferCopyCommands,&cmd,1,&child);
      CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
      CALL(nvnCommandBufferSetScissor,&cmd,8,0,8,16);
      CALL(nvnCommandBufferCopyCommands,&cmd,1,&child);
    } else {
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      if(shape==1) { handles[0]=end();CALL(nvnCommandBufferBeginRecording,&cmd); }
      CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
      CALL(nvnCommandBufferSetScissor,&cmd,8,0,8,16);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(boundaries+8));
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    handles[1]=end();
    if(shape==1) { CALL(nvnQueueSubmitCommands,&queue,2,handles);CALL(nvnQueueFinish,&queue); }
    else submit(handles[1]);
    CHECK(boundaries[8]==128);
    mtl_stats(&passes_after,&draws_after);CHECK(draws_after-draws_before==2);
    vk_sample_liveness_stats(&liveness_live,&liveness_dead);
    vk_counter_query_stats(&queries_after,&ended_queries,&query_spans);
    vk_sample_query_elision_stats(&liveness_elided_after,&liveness_actual_reference);
    CHECK(liveness_actual_reference==(int)query_reference);
    CHECK(queries_after-queries_before==(query_reference?2:1));
    CALL(nvnTextureReadTexels,&output,NULL,&region,pixels);
    for(int y=0;y<16;y++) for(int x=0;x<16;x++) {
      const uint8_t *p=pixels+(y*16+x)*4;
      CHECK(p[0]==x*17&&p[1]==y*17&&p[2]==64&&p[3]==255);
    }
  }
  }
  vk_sample_query_elision_stats(&liveness_elided_after,&liveness_actual_reference);
  CHECK(liveness_elided_after>=liveness_elided_before+3);
  vk_sample_liveness_reference(liveness_original_reference);
  LOG("[vulkan counter replay boundary fixture] live=%llu reset_before_report=%llu; single/multiple handles, nested/repeated calls, samples=128 and both shaded halves passed",
      (unsigned long long)liveness_live,(unsigned long long)liveness_dead);
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      uint8_t *p = pixels + (y * 16 + x) * 4;
      CHECK(p[0] == x * 17 && p[1] == y * 17 && p[2] == 64 && p[3] == 255);
    }
#ifndef DM_ANDROID_APP
  if(getenv("DM_PLANNED_FORWARD_FIXTURE")) {
    CHECK(vk_validation_error_count()==0);
    printf("{\"test\":\"planned-forward-reuse\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);return 0;
  }
#endif
  // 重复注册不能泄漏；替换过滤/边界状态后检查真实采样，旧对象延迟回收。
  // Re-registration must not leak. Verify changed sampling on the GPU while
  // retaining replaced samplers through the original snapshot grace period.
  VkSampler original_sampler = vk_sampler(1);
  uint32_t sampler_baseline = vk_sampler_count();
  for (unsigned i = 0; i < 16; i++) {
    CALL(nvnSamplerPoolRegisterSampler, &sp, 1, &sampler);
    CHECK(vk_sampler(1) == original_sampler);
    CHECK(vk_sampler_count() == sampler_baseline);
  }
  for (unsigned pass = 0; pass < (V.sampler_mirror_clamp ? 3u : 2u); pass++) {
    const float white[4] = {1, 1, 1, 1};
    NVNwrapMode mode = pass == 2 ? NVN_WRAP_MODE_MIRROR_CLAMP_TO_EDGE
                       : pass    ? NVN_WRAP_MODE_CLAMP_TO_EDGE
                                 : NVN_WRAP_MODE_CLAMP_TO_BORDER;
    CALL(nvnSamplerBuilderSetWrapMode, &sb, mode, mode,
         NVN_WRAP_MODE_CLAMP_TO_EDGE);
    CALL(nvnSamplerBuilderSetBorderColor, &sb, white);
    // A non-mipmapped filter still samples level zero when LOD clamps are set.
    NVNsamplerBuilder original_builder=sb;
    if(pass==2) CALL(nvnSamplerBuilderSetLodClamp,&sb,2,4);
    CHECK(CALL(nvnSamplerInitialize, &sampler, &sb));
    sb=original_builder;
    CALL(nvnSamplerPoolRegisterSampler, &sp, 1, &sampler);
    CHECK(vk_sampler(1) != original_sampler);
    CHECK(vk_sampler_count() == sampler_baseline + pass + 1);
    if (!pass) {
      for (unsigned v = 0; v < 3; v++)
        vertex[v * 4 + 2] = vertex[v * 4 + 3] = -1;
    } else if (pass == 2) {
      for (unsigned v = 0; v < 3; v++) {
        // Mirrored centers of texels (4,8), away from nearest-filter boundaries.
        // Exact boundary coordinates let interpolation/coordinate rounding
        // select the adjacent texel on another GPU; keep exact pixel checks.
        vertex[v * 4 + 2] = -4.5f / 16;
        vertex[v * 4 + 3] = -8.5f / 16;
      }
    } else {
      memcpy(vertex, vertices, sizeof vertices);
    }
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    submit(end());
    CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        uint8_t *p = pixels + (y * 16 + x) * 4;
        if (p[0] != (pass == 2 ? 68 : pass ? x * 17 : 255) ||
            p[1] != (pass == 2 ? 136 : pass ? y * 17 : 255) ||
            p[2] != (pass ? 64 : 255) || p[3] != 255)
          LOG("sampler wrap mismatch pass=%u xy=%d,%d rgba=%u,%u,%u,%u",
              pass,x,y,p[0],p[1],p[2],p[3]);
        CHECK(p[0] == (pass == 2 ? 68
                       : pass    ? x * 17
                                 : 255) &&
              p[1] == (pass == 2 ? 136
                       : pass    ? y * 17
                                 : 255) &&
              p[2] == (pass ? 64 : 255) && p[3] == 255);
      }
  }
  memcpy(vertex, vertices, sizeof vertices);
  // 计算与片元阶段写存储图像后，同提交立即采样，核实 NVN
  // 屏障没有丢失真实写依赖。 Sample compute/fragment storage-image writes
  // within the same submission, verifying that NVN barrier coalescing preserves
  // real write dependencies.
  NVNprogram image_compute_program, image_graphics_program;
  CHECK(CALL(nvnProgramInitialize, &image_compute_program, &device));
  sd[0].control = controls[7];
  CHECK(CALL(nvnProgramSetShaders, &image_compute_program, 1, sd));
  CHECK(CALL(nvnProgramInitialize, &image_graphics_program, &device));
  sd[0].control = controls[0];
  sd[1].control = controls[8];
  CHECK(CALL(nvnProgramSetShaders, &image_graphics_program, 2, sd));
  const uint8_t image_results[2][4] = {{255, 0, 128, 255}, {0, 64, 255, 255}};
  NVNimageHandle image_handle = CALL(nvnDeviceGetImageHandle, &device, 1);
  for (unsigned pass = 0; pass < 2; pass++) {
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferBindProgram, &cmd,
         pass ? &image_graphics_program : &image_compute_program,
         pass ? NVN_SHADER_STAGE_ALL_GRAPHICS_BITS
              : NVN_SHADER_STAGE_COMPUTE_BIT);
    CALL(nvnCommandBufferBindImages, &cmd,
         pass ? NVN_SHADER_STAGE_FRAGMENT : NVN_SHADER_STAGE_COMPUTE, 0, 1,
         &image_handle);
    if (pass)
      CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0,
           3);
    else
      CALL(nvnCommandBufferDispatchCompute, &cmd, 4, 4, 1);
    CALL(nvnCommandBufferBarrier, &cmd,
         NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    CALL(nvnCommandBufferBindProgram, &cmd, &graphics,
         NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    submit(end());
    CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
    for (unsigned i = 0; i < sizeof pixels; i += 4)
      CHECK(!memcmp(pixels + i, image_results[pass], 4));
  }
  // Producers precede the pass, but the second texture is first bound after
  // the first draw. Its dependency belongs before BeginRenderPass, preserving
  // one physical pass and both pixel regions across repeated submissions.
  NVNtexture late_sample;
  uint8_t saved_upload[16*16*4];memcpy(saved_upload,upload,sizeof saved_upload);
  CALL(nvnTextureBuilderSetStorage,&tb,&memory,(3<<20)+393216);
  CALL(nvnTextureBuilderSetFlags,&tb,0);
  CHECK(CALL(nvnTextureInitialize,&late_sample,&tb));
  CALL(nvnTextureBuilderSetFlags,&tb,NVN_TEXTURE_FLAGS_IMAGE);
  CALL(nvnTexturePoolRegisterTexture,&tp,2,&late_sample,NULL);
  NVNseparateTextureHandle late_handle=CALL(nvnDeviceGetSeparateTextureHandle,&device,2);
  for(unsigned iteration=0;iteration<2;iteration++) {
    const uint8_t late_color[4]={0,iteration?0:255,iteration?255:0,255};
    for(unsigned p=0;p<256;p++) memcpy(upload+p*4,late_color,4);
    mtl_stats(&passes_before,&draws_before);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferBindProgram,&cmd,&image_compute_program,NVN_SHADER_STAGE_COMPUTE_BIT);
    CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_COMPUTE,0,1,&image_handle);
    CALL(nvnCommandBufferDispatchCompute,&cmd,4,4,1);
    CALL(nvnCommandBufferSetCopyRowStride,&cmd,64);
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)upload,&late_sample,NULL,&region,0);
    CALL(nvnCommandBufferBindProgram,&cmd,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&late_handle);
    CALL(nvnCommandBufferSetScissor,&cmd,8,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    submit(end());
    mtl_stats(&passes_after,&draws_after);
    CHECK(draws_after-draws_before==3);
    LOG("[vulkan late fragment dependency fixture] iteration=%u reference=%d physical_passes=%llu",
        iteration,vk_submission_barrier_mode(),(unsigned long long)(passes_after-passes_before));
    CHECK(passes_after-passes_before==1);
    CALL(nvnTextureReadTexels,&output,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      const uint8_t *expected=x<8?image_results[0]:
          iteration?(const uint8_t[]){0,0,255,255}:(const uint8_t[]){0,255,0,255};
      CHECK(!memcmp(pixels+(y*16+x)*4,expected,4));
    }
  }
  // An attachment-produced image first sampled by a later draw needs its
  // layout transition before the pending primary pass, not a third pass.
  for(unsigned iteration=0;iteration<2;iteration++) {
    const float color[4]={0,iteration?0:1,iteration?1:0,1};
    const NVNtexture *producer_targets[]={&late_sample};
    mtl_stats(&passes_before,&draws_before);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,producer_targets,NULL,NULL,NULL);
    CALL(nvnCommandBufferClearColor,&cmd,0,color,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,NULL,NULL);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&late_handle);
    CALL(nvnCommandBufferSetScissor,&cmd,8,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    submit(end());
    mtl_stats(&passes_after,&draws_after);
    LOG("[vulkan late layout fixture] iteration=%u physical_passes=%llu",
        iteration,(unsigned long long)(passes_after-passes_before));
    CHECK(passes_after-passes_before==2);
    CALL(nvnTextureReadTexels,&output,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      const uint8_t *expected=x<8?image_results[0]:
          iteration?(const uint8_t[]){0,0,255,255}:(const uint8_t[]){0,255,0,255};
      CHECK(!memcmp(pixels+(y*16+x)*4,expected,4));
    }
  }
  // A sampled descriptor and a storage descriptor alias one capable image.
  // Actual reads/writes use disjoint halves. Both descriptors must agree on
  // GENERAL even if sampling was reflected before the storage binding.
  NVNprogram image_alias_program;uint8_t alias_control[0x880];
  memset(alias_control,34,sizeof alias_control);
  CHECK(CALL(nvnProgramInitialize,&image_alias_program,&device));
  NVNshaderData alias_shaders[2]={{.control=controls[0]},{.control=alias_control}};
  CHECK(CALL(nvnProgramSetShaders,&image_alias_program,2,alias_shaders));
  const NVNtexture *alias_targets[]={&tex};
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,alias_targets,NULL,NULL,NULL);
  CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){.25,.5,.75,1},15);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,NULL,NULL);
  CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){0,0,0,1},15);
  CALL(nvnCommandBufferBindProgram,&cmd,&image_alias_program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
  CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&image_handle);
  CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,16);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
  CALL(nvnCommandBufferBindProgram,&cmd,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  submit(end());
  for(unsigned target=0;target<2;target++) {
    CALL(nvnTextureReadTexels,target?&tex:&output,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      const uint8_t *expected=x<8?(const uint8_t[]){64,128,191,255}:
          target?(const uint8_t[]){191,64,128,255}:(const uint8_t[]){0,0,0,255};
      for(unsigned c=0;c<4;c++) CHECK(abs(pixels[(y*16+x)*4+c]-expected[c])<=1);
    }
  }
  CALL(nvnProgramFinalize,&image_alias_program);
  LOG("[vulkan sampled/storage alias fixture] attachment producer, sampled-first descriptors, disjoint accesses and both image halves passed");
  CALL(nvnTextureFinalize,&late_sample);
  memcpy(upload,saved_upload,sizeof saved_upload);
  LOG("[vulkan late fragment dependency fixture] compute/transfer producers, late first binding, one physical pass and both pixel halves across submissions passed");
  separate_mip_layout_test(&graphics,&image_compute_program,&tp,&output);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetCopyRowStride, &cmd, 64);
  CALL(nvnCommandBufferCopyBufferToTexture, &cmd, (uintptr_t)upload, &tex, NULL,
       &region, 0);
  submit(end());
  // 原格式视图：同提交 GPU 写入后升级，采样须做 sRGB 解码，原位回读仍为原始字节。
  // Original format views: upgrade after a GPU write in the same submit,
  // decode sRGB when sampled, and retain raw encoded bytes on readback.
  NVNtextureView srgb_view;
  CALL(nvnTextureViewSetDefaults, &srgb_view);
  CALL(nvnTextureViewSetFormat, &srgb_view, NVN_FORMAT_RGBA8_SRGB);
  uint8_t view_source[16 * 16 * 4];
  for (unsigned i = 0; i < sizeof view_source; i += 4) {
    view_source[i] = 128; view_source[i + 1] = 64;
    view_source[i + 2] = 192; view_source[i + 3] = 128;
  }
  CALL(nvnTexturePoolRegisterTexture, &tp, 1, &tex, &srgb_view);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferUpdateUniformBuffer, &cmd, (uintptr_t)upload, sizeof view_source, 0,
       sizeof view_source, view_source);
  CALL(nvnCommandBufferCopyBufferToTexture, &cmd, (uintptr_t)upload, &tex, NULL, &region, 0);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(abs(pixels[i] - 55) <= 1 && abs(pixels[i + 1] - 13) <= 1 &&
          abs(pixels[i + 2] - 134) <= 1 && pixels[i + 3] == 128);
  CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
  CHECK(!memcmp(pixels, view_source, sizeof pixels));
  // sRGB 附件/清纹理均按视图编码；回到普通视图后不能沿用旧 swizzle/格式。
  // Attachment and texture clears encode through the sRGB view; switching
  // back to the base view must not reuse a stale format or swizzle.
  const float view_clear[4] = {.25f, .5f, .75f, .5f};
  const NVNtexture *view_colors[] = {&tex};
  const NVNtextureView *view_views[] = {&srgb_view};
  for (unsigned pass = 0; pass < 2; pass++) {
    CALL(nvnCommandBufferBeginRecording, &cmd);
    if (!pass) {
      CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, view_colors, view_views, NULL, NULL);
      CALL(nvnCommandBufferClearColor, &cmd, 0, view_clear, 15);
    } else
      CALL(nvnCommandBufferClearTexture, &cmd, &tex, &srgb_view, &region, view_clear, 15);
    submit(end());
    CALL(nvnTextureReadTexels, &tex, NULL, &region, pixels);
    for (unsigned i = 0; i < sizeof pixels; i += 4)
      CHECK(abs(pixels[i] - 137) <= 1 && abs(pixels[i + 1] - 188) <= 1 &&
            abs(pixels[i + 2] - 225) <= 1 && pixels[i + 3] == 128);
  }
  CALL(nvnTexturePoolRegisterTexture, &tp, 1, &tex, NULL);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(abs(pixels[i] - 137) <= 1 && abs(pixels[i + 1] - 188) <= 1 &&
          abs(pixels[i + 2] - 225) <= 1 && pixels[i + 3] == 128);
  memcpy(upload, view_source, sizeof view_source);
  // 恢复原夹具源数据，后续深度/压缩测试保持各自的独立输入。
  // Restore the original fixture input for independent depth/BC checks.
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    uint8_t *pixel = upload + (y * 16 + x) * 4;
    pixel[0] = x * 17; pixel[1] = y * 17; pixel[2] = 64; pixel[3] = 255;
  }
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferCopyBufferToTexture, &cmd, (uintptr_t)upload, &tex, NULL, &region, 0);
  submit(end());
  // 只读深度附件可在同通道采样；真正写过深度后必须保留 NVN 屏障的读依赖。
  // Read-only depth attachments can be sampled in one pass, while a prior
  // depth write must retain its NVN read dependency.
  NVNtexture depth_texture;
  CALL(nvnTextureBuilderSetFormat, &tb, NVN_FORMAT_DEPTH32F);
  CALL(nvnTextureBuilderSetStorage, &tb, &memory, (3 << 20) + 450560);
  CHECK(CALL(nvnTextureInitialize, &depth_texture, &tb));
  CALL(nvnTexturePoolRegisterTexture, &tp, 2, &depth_texture, NULL);
  NVNprogram depth_graphics;
  CHECK(CALL(nvnProgramInitialize, &depth_graphics, &device));
  sd[0].control = controls[0];
  sd[1].control = controls[9];
  CHECK(CALL(nvnProgramSetShaders, &depth_graphics, 2, sd));
  NVNdepthStencilState depth_state;
  CALL(nvnDepthStencilStateSetDefaults, &depth_state);
  CALL(nvnDepthStencilStateSetDepthTestEnable, &depth_state, 1);
  CALL(nvnDepthStencilStateSetDepthWriteEnable, &depth_state, 0);
  CALL(nvnDepthStencilStateSetDepthFunc, &depth_state, NVN_DEPTH_FUNC_ALWAYS);
  NVNseparateTextureHandle depth_handle =
      CALL(nvnDeviceGetSeparateTextureHandle, &device, 2);
  mtl_stats(&passes_before, &draws_before);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, &depth_texture,
       NULL);
  CALL(nvnCommandBufferClearDepthStencil, &cmd, .25f, 1, 0, 0);
  CALL(nvnCommandBufferBindDepthStencilState, &cmd, &depth_state);
  CALL(nvnCommandBufferBindProgram, &cmd, &depth_graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       1, &depth_handle);
  for (unsigned i = 0; i < 128; i++) {
    CALL(nvnCommandBufferBarrier, &cmd,
         NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  }
  submit(end());
  mtl_stats(&passes_after, &draws_after);
  CHECK(draws_after - draws_before == 128 && passes_after - passes_before < 4);
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 64 && pixels[i + 1] == 0 && pixels[i + 2] == 0 &&
          pixels[i + 3] == 255);
  // Prove a depth-test-only pass uses a readonly attachment and preserves the
  // existing depth values, which the next independent pass samples below.
  uint64_t readonly_before,readonly_after;int readonly_reference;
  vk_readonly_depth_stats(&readonly_before,&readonly_reference);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferBindProgram,&cmd,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  submit(end());
  vk_readonly_depth_stats(&readonly_after,&readonly_reference);
  // A distinct color-only pass samples the depth attachment without feedback.
  // Exercise real attachment -> readonly layout -> attachment transitions.
  uint64_t depth_attachment_before,depth_sampled_before,depth_attachment_after,depth_sampled_after;
  int depth_reference,layout_reference;uint64_t transitions,safe_uses,fallbacks;
  vk_depth_layout_stats(&depth_attachment_before,&depth_sampled_before,&depth_reference);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,NULL,NULL);
  CALL(nvnCommandBufferBindProgram,&cmd,&depth_graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&depth_handle);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  submit(end());
  vk_depth_layout_stats(&depth_attachment_after,&depth_sampled_after,&depth_reference);
  vk_image_layout_stats(&transitions,&safe_uses,&fallbacks,&layout_reference);
  CHECK(readonly_after>readonly_before || readonly_reference || depth_reference || layout_reference);
  CHECK(depth_sampled_after>depth_sampled_before || depth_reference || layout_reference);
  CALL(nvnTextureReadTexels,&output,NULL,&region,pixels);
  for(unsigned i=0;i<sizeof pixels;i+=4)
    CHECK(pixels[i]==64&&pixels[i+1]==0&&pixels[i+2]==0&&pixels[i+3]==255);
  LOG("[vulkan depth layout fixture] real depth attachment sampled in distinct color pass; attachment feedback retains GENERAL");
  // A partial depth clear must also preserve outside pixels; sample the whole
  // image after restoring the full scissor.
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,&depth_texture,NULL);
  CALL(nvnCommandBufferSetScissor, &cmd, 2, 4, 8, 6);
  CALL(nvnCommandBufferClearDepthStencil, &cmd, .75f, 1, 0, 0);
  CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 16, 16);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      uint8_t *pixel = pixels + (y * 16 + x) * 4;
      int inside = x >= 2 && x < 10 && y >= 4 && y < 10;
      CHECK(pixel[0] == (inside ? 191 : 64) && pixel[1] == 0 && pixel[2] == 0 &&
            pixel[3] == 255);
    }
  CALL(nvnCommandBufferBeginRecording, &cmd);
  // An identical logical target rebind separates planning proofs, while the
  // physical pass may remain open. A later writer must upgrade the readonly
  // layout through a real pass end before writing, never discard the write.
  CALL(nvnCommandBufferBindProgram,&cmd,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
  CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,&depth_texture,NULL);
  CALL(nvnDepthStencilStateSetDepthWriteEnable, &depth_state, 1);
  CALL(nvnCommandBufferBindDepthStencilState, &cmd, &depth_state);
  CALL(nvnCommandBufferBindProgram, &cmd, &storage_graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  CALL(nvnCommandBufferBarrier, &cmd,
       NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
  CALL(nvnDepthStencilStateSetDepthWriteEnable, &depth_state, 0);
  CALL(nvnCommandBufferBindDepthStencilState, &cmd, &depth_state);
  CALL(nvnCommandBufferBindProgram, &cmd, &depth_graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&depth_handle);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 0 &&
          pixels[i + 3] == 255);
  // 连续颜色/深度清除与绘制共用通道；清除不计入遮挡样本，查询重置不能重做清除。
  // Consecutive color/depth clears and draws share a pass. Clears must not
  // count as occlusion samples, nor be replayed when a query reset ends a pass.
  const float clear_background[4] = {1, 0, 1, 1};
  mtl_stats(&passes_before, &draws_before);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnDepthStencilStateSetDefaults, &depth_state);
  CALL(nvnCommandBufferBindDepthStencilState, &cmd, &depth_state);
  CALL(nvnCommandBufferBindProgram, &cmd, &graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       1, &th);
  CALL(nvnCommandBufferResetCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED);
  for (unsigned repeat = 0; repeat < 8; repeat++) {
    CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 16, 16);
    CALL(nvnCommandBufferClearColor, &cmd, 0, clear_background, 15);
    CALL(nvnCommandBufferClearDepthStencil, &cmd, .5f, 1, 0, 0);
    CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 8, 16);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_TIMESTAMP,
         (uintptr_t)(timestamps + repeat * 2));
  }
  CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 16, 16);
  CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_SAMPLES_PASSED,
       (uintptr_t)report);
  submit(end());
  mtl_stats(&passes_after, &draws_after);
  CHECK(draws_after - draws_before == 8 && passes_after - passes_before < 5);
  CHECK(report[0] == 8 * 128);
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      uint8_t *pixel = pixels + (y * 16 + x) * 4;
      CHECK(pixel[0] == (x < 8 ? x * 17 : 255) &&
            pixel[1] == (x < 8 ? y * 17 : 0) &&
            pixel[2] == (x < 8 ? 64 : 255) && pixel[3] == 255);
    }
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnDepthStencilStateSetDefaults, &depth_state);
  CALL(nvnCommandBufferBindDepthStencilState, &cmd, &depth_state);
  CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
  CALL(nvnCommandBufferBindProgram, &cmd, &graphics,
       NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0,
       1, &th);
  submit(end());
  CALL(nvnTextureFinalize, &depth_texture);
  // 使用独立 BC1 色块检查真机 CPU 解码后备路径以及原生 BC 采样路径。
  // Independent BC1 blocks check the phone's CPU decode fallback and native BC
  // sampling.
  const uint16_t endpoints[4] = {0xf800, 0x07e0, 0x001f, 0xffff};
  const uint8_t expected[4][4] = {{255, 0, 0, 255},
                                  {0, 255, 0, 255},
                                  {0, 0, 255, 255},
                                  {255, 255, 255, 255}};
  memset(upload, 0, 128);
  for (unsigned block = 0; block < 16; block++)
    memcpy(upload + block * 8, &endpoints[block % 4], 2);
  NVNtexture compressed;
  CALL(nvnTextureBuilderSetFormat, &tb, NVN_FORMAT_RGB_DXT1);
  CALL(nvnTextureBuilderSetStorage, &tb, &memory, (3 << 20) + 327680);
  CHECK(CALL(nvnTextureInitialize, &compressed, &tb));
  CALL(nvnTexturePoolRegisterTexture, &tp, 1, &compressed, NULL);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetCopyRowStride, &cmd, 32);
  CALL(nvnCommandBufferCopyBufferToTexture, &cmd, (uintptr_t)upload,
       &compressed, NULL, &region, 0);
  CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
      CHECK(!memcmp(pixels + (y * 16 + x) * 4, expected[x / 4], 4));
  // 左上原点下按裁剪空间绕序剔除；两个方向分别验证可见和被剔除。
  // With an upper-left origin, cull using clip-space winding; verify both
  // visible and culled orientations.
  const float magenta[4] = {1, 0, 1, 1};
  CALL(nvnPolygonStateSetCullFace, &ps, NVN_FACE_BACK);
  for (unsigned pass = 0; pass < 2; pass++) {
    CALL(nvnPolygonStateSetFrontFace, &ps,
         pass ? NVN_FRONT_FACE_CCW : NVN_FRONT_FACE_CW);
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferClearTexture, &cmd, &output, NULL, &region, magenta,
         15);
    CALL(nvnCommandBufferBindPolygonState, &cmd, &ps);
    CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    submit(end());
    CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
    const uint8_t clear_pixel[4] = {255, 0, 255, 255};
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++)
        CHECK(!memcmp(pixels + (y * 16 + x) * 4,
                      pass ? clear_pixel : expected[x / 4], 4));
  }
  CALL(nvnTextureFinalize, &compressed);
  // 非原尺寸拷贝与半精度到 UNORM 转换必须由 GPU 执行，且保留目标区域外内容。
  // GPU scaling and half-float-to-UNORM conversion must preserve pixels outside
  // the destination region.
  NVNcopyRegion small = {4, 4, 0, 8, 8, 1};
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferClearTexture, &cmd, &output, NULL, &region, magenta, 15);
  CALL(nvnCommandBufferCopyTextureToTexture, &cmd, &tex, NULL, &region, &output,
       NULL, &small, NVN_COPY_FLAGS_ENGINE_2D_BIT);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      uint8_t *p = pixels + (y * 16 + x) * 4;
      if (x >= 4 && x < 12 && y >= 4 && y < 12)
        CHECK(p[0] == ((x - 4) * 2 + 1) * 17 &&
              p[1] == ((y - 4) * 2 + 1) * 17 && p[2] == 64 && p[3] == 255);
      else
        CHECK(p[0] == 255 && p[1] == 0 && p[2] == 255 && p[3] == 255);
    }
  NVNtexture half_float;
  CALL(nvnTextureBuilderSetFormat, &tb, NVN_FORMAT_RGBA16F);
  CALL(nvnTextureBuilderSetStorage, &tb, &memory, (3 << 20) + 393216);
  CHECK(CALL(nvnTextureInitialize, &half_float, &tb));
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferClearTexture, &cmd, &half_float, NULL, &region, rgba,
       15);
  CALL(nvnCommandBufferCopyTextureToTexture, &cmd, &half_float, NULL, &region,
       &output, NULL, &region,
       NVN_COPY_FLAGS_ENGINE_2D_BIT | NVN_COPY_FLAGS_LINEAR_FILTER_BIT);
  submit(end());
  CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
  for (unsigned i = 0; i < sizeof pixels; i += 4)
    CHECK(pixels[i] == 255 && pixels[i + 1] == 128 && pixels[i + 2] == 0 &&
          pixels[i + 3] == 255);
  // 局部/越界清除只改裁剪矩形，不能损坏附件其余像素。
  // Partial and clipped clears only change their rectangle, preserving all
  // pixels outside it even when the render area is restricted.
  const int rectangles[3][4] = {{3, 5, 7, 4}, {-2, -3, 6, 7}, {20, 20, 4, 4}};
  for (unsigned pass = 0; pass < 3; pass++) {
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
    CALL(nvnCommandBufferClearTexture, &cmd, &output, NULL, &region, magenta,
         15);
    const int *r = rectangles[pass];
    CALL(nvnCommandBufferSetScissor, &cmd, r[0], r[1], r[2], r[3]);
    CALL(nvnCommandBufferClearColor, &cmd, 0, rgba, 15);
    submit(end());
    CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        uint8_t *pixel = pixels + (y * 16 + x) * 4;
        int inside =
            x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
        CHECK(pixel[0] == 255 && pixel[1] == (inside ? 128 : 0) &&
              pixel[2] == (inside ? 0 : 255) && pixel[3] == 255);
      }
  }
  // 首次局部清除仍须保留 CPU 初始像素；空矩形不能把未上传图像当已初始化。
  // A first partial clear must retain initial CPU pixels. An empty rectangle
  // must not turn an unuploaded image into initialized GPU contents.
  for (unsigned pass = 0; pass < 2; pass++) {
    size_t offset = (3 << 20) + 524288 + pass * 65536;
    for (size_t i = 0; i < 65536; i += 4)
      memcpy(bytes + offset + i, (uint8_t[]){64, 128, 192, 255}, 4);
    NVNtexture fresh;
    NVNpackagedTextureLayout packaged = {{0}};
    CALL(nvnTextureBuilderSetPackagedTextureLayout, &tb, &packaged);
    CALL(nvnTextureBuilderSetFormat, &tb, NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage, &tb, &memory, offset);
    CHECK(CALL(nvnTextureInitialize, &fresh, &tb));
    colors[0] = &fresh;
    CALL(nvnCommandBufferBeginRecording, &cmd);
    CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
    CALL(nvnCommandBufferSetScissor, &cmd, pass ? 20 : 3, pass ? 20 : 5, 7, 4);
    CALL(nvnCommandBufferClearColor, &cmd, 0, rgba, 15);
    submit(end());
    CALL(nvnTextureReadTexels, &fresh, NULL, &region, pixels);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        uint8_t *pixel = pixels + (y * 16 + x) * 4;
        int inside = !pass && x >= 3 && x < 10 && y >= 5 && y < 9;
        CHECK(pixel[0] == (inside ? 255 : 64) && pixel[1] == 128 &&
              pixel[2] == (inside ? 0 : 192) && pixel[3] == 255);
      }
    CALL(nvnTextureFinalize, &fresh);
  }
  colors[0] = &output;
  // 时间戳块边界不能丢报告，且每条结果来自各自命令位置。
  // Crossing timestamp blocks must preserve every report and its command
  // position.
  CALL(nvnCommandBufferBeginRecording, &cmd);
  for (unsigned i = 0; i < 2051; i++)
    CALL(nvnCommandBufferReportCounter, &cmd, NVN_COUNTER_TYPE_TIMESTAMP,
         (uintptr_t)(timestamps + i * 2));
  submit(end());
  for (unsigned i = 0; i < 2051; i++)
    CHECK(timestamps[i * 2] && timestamps[i * 2] == timestamps[i * 2 + 1] &&
          (!i || timestamps[i * 2] >= timestamps[(i - 1) * 2]));
  // 原 Metal stepRate 对应实例除数：跨管线重用与非零 baseInstance 都要实测。
  // Original Metal stepRate maps to instance divisors; verify pipeline cache
  // separation and nonzero baseInstance through actual GPU pixels.
  NVNprogram instance_graphics;
  CHECK(CALL(nvnProgramInitialize, &instance_graphics, &device));
  sd[0].control = controls[10]; sd[1].control = controls[11];
  CHECK(CALL(nvnProgramSetShaders, &instance_graphics, 2, sd));
  float *instance_colors = (void *)(bytes + (2 << 20) + 12288);
  const float instance_values[4][4] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,1,1}};
  memcpy(instance_colors, instance_values, sizeof instance_values);
  uint16_t *indices = (void *)(bytes + (2 << 20) + 12544);
  indices[0] = 0; indices[1] = 1; indices[2] = 2;
  NVNvertexAttribState instance_attr;
  CALL(nvnVertexAttribStateSetDefaults, &instance_attr);
  CALL(nvnVertexAttribStateSetFormat, &instance_attr, NVN_FORMAT_RGBA32F, 0);
  NVNdepthStencilState instance_depth;
  CALL(nvnDepthStencilStateSetDefaults, &instance_depth);
  CALL(nvnDepthStencilStateSetDepthTestEnable, &instance_depth, 0);
  CALL(nvnDepthStencilStateSetStencilTestEnable, &instance_depth, 0);
  CALL(nvnPolygonStateSetCullFace, &ps, NVN_FACE_NONE);
  NVNbufferRange instance_range = {(uintptr_t)instance_colors, sizeof instance_values};
  const unsigned rates[] = {2, 3, 2}, instances[] = {3, 7, 3}, bases[] = {0, 0, 1};
  const uint8_t expected_instances[][4] = {{0,255,0,96}, {0,0,255,223}, {0,0,255,96}};
  for (unsigned pass = 0; pass < 6; ++pass) {
    vk_vertex_divisor_fallback(pass>=3);
    VkSemaphore unrelated_gate=VK_NULL_HANDLE;
    if(pass==4 && vk_async_submissions() && V.timeline_semaphore) {
      unrelated_gate=block_gpu_queue();
      mtl_sub_begin();vk_buffer *unrelated=vk_temp_buffer(16);
      vkCmdFillBuffer(V.cmd,unrelated->handle,0,16,0x35791357);
      vk_internal_commit();
    }
    unsigned test = pass % 3;
    CALL(nvnVertexStreamStateSetDivisor, &stream, rates[test]);
    CALL(nvnCommandBufferBeginRecording, &cmd);
    const float yellow[4] = {1,1,0,1};
    if (pass == 3)
      CALL(nvnCommandBufferUpdateUniformBuffer, &cmd, (uintptr_t)instance_colors,
           sizeof instance_values, 16, 16, yellow);
    colors[0] = &output;
    CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
    CALL(nvnCommandBufferBindProgram, &cmd, &instance_graphics, NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindDepthStencilState, &cmd, &instance_depth);
    CALL(nvnCommandBufferBindColorState, &cmd, &cs);
    CALL(nvnCommandBufferBindPolygonState, &cmd, &ps);
    CALL(nvnCommandBufferBindVertexAttribState, &cmd, 1, &instance_attr);
    CALL(nvnCommandBufferBindVertexStreamState, &cmd, 1, &stream);
    CALL(nvnCommandBufferBindVertexBuffers, &cmd, 0, 1, &instance_range);
    CALL(nvnCommandBufferSetViewports, &cmd, 0, 1, viewport_full);
    CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 16, 16);
    CALL(nvnCommandBufferDrawElementsInstanced, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLES,
         NVN_INDEX_TYPE_UNSIGNED_SHORT, 3, (uintptr_t)indices, 0, bases[test], instances[test]);
    uint64_t before_fence = mtl_last_fence();
    if(unrelated_gate) {
      vk_buffer_sync_stats before,after;vk_buffer_sync_snapshot(&before);
      NVNcommandHandle handle=end();CALL(nvnQueueSubmitCommands,&queue,1,&handle);
      // Liveness deadline only, never a performance measurement. The NVN
      // translation must submit CPU-authored instance data while GPU is gated.
      uint64_t deadline=now_ns()+UINT64_C(2000000000);
      do { vk_buffer_sync_snapshot(&after);if(after.guest_submits>before.guest_submits) break;
        usleep(1000); } while(now_ns()<deadline);
      int submitted=after.guest_submits>before.guest_submits;
      VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
        .semaphore=unrelated_gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
      CALL(nvnQueueFinish,&queue);vkDestroySemaphore(V.device,unrelated_gate,NULL);
      CHECK(submitted);
      LOG("[vulkan instance CPU source fixture] translation submitted while unrelated GPU work was gated");
    } else submit(end());
    CHECK(mtl_last_fence() == before_fence + 1);
    CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
    for (unsigned i = 0; i < sizeof pixels; i += 4)
      CHECK(!memcmp(pixels + i, pass == 3 ? (const uint8_t[]){255,255,0,96} : expected_instances[test], 4));
  }
  CHECK(instance_colors[4] == 1 && instance_colors[5] == 1 && instance_colors[6] == 0);
  vk_vertex_divisor_fallback(V.options.vertex_divisor_fallback);
  // 格式升级必须保留所有 mip/数组层的 GPU 内容，且只改被清除的子资源。
  // A format upgrade preserves GPU contents of every mip/layer and modifies
  // only the selected subresource, even when writes precede it in this submit.
  NVNtextureBuilder array_builder;
  CALL(nvnTextureBuilderSetDefaults, &array_builder);
  CALL(nvnTextureBuilderSetDevice, &array_builder, &device);
  CALL(nvnTextureBuilderSetTarget, &array_builder, NVN_TEXTURE_TARGET_2D_ARRAY);
  CALL(nvnTextureBuilderSetWidth, &array_builder, 8);
  CALL(nvnTextureBuilderSetHeight, &array_builder, 8);
  CALL(nvnTextureBuilderSetDepth, &array_builder, 2);
  CALL(nvnTextureBuilderSetLevels, &array_builder, 3);
  CALL(nvnTextureBuilderSetFormat, &array_builder, NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage, &array_builder, &memory, (3 << 20) + 786432);
  NVNtexture array_texture;
  CHECK(CALL(nvnTextureInitialize, &array_texture, &array_builder));
  NVNtextureView subviews[3][2];
  CALL(nvnCommandBufferBeginRecording, &cmd);
  for (unsigned level = 0; level < 3; level++) for (unsigned layer = 0; layer < 2; layer++) {
    CALL(nvnTextureViewSetDefaults, &subviews[level][layer]);
    CALL(nvnTextureViewSetLevels, &subviews[level][layer], level, 1);
    CALL(nvnTextureViewSetLayers, &subviews[level][layer], layer, 1);
    NVNcopyRegion subregion = {0, 0, 0, 8 >> level, 8 >> level, 1};
    float subcolor[4] = {level * .25f, layer * .5f, 1, .5f};
    CALL(nvnCommandBufferClearTexture, &cmd, &array_texture, &subviews[level][layer],
         &subregion, subcolor, 15);
  }
  NVNtextureView converted = subviews[1][1];
  CALL(nvnTextureViewSetFormat, &converted, NVN_FORMAT_RGBA8_SRGB);
  NVNcopyRegion converted_region = {0, 0, 0, 4, 4, 1};
  CALL(nvnCommandBufferClearTexture, &cmd, &array_texture, &converted, &converted_region,
       view_clear, 15);
  submit(end());
  for (unsigned level = 0; level < 3; level++) for (unsigned layer = 0; layer < 2; layer++) {
    NVNcopyRegion subregion = {0, 0, 0, 8 >> level, 8 >> level, 1};
    CALL(nvnTextureReadTexels, &array_texture, &subviews[level][layer], &subregion, pixels);
    for (unsigned i = 0; i < (unsigned)(subregion.width * subregion.height * 4); i += 4) {
      if (level == 1 && layer == 1)
        CHECK(abs(pixels[i] - 137) <= 1 && abs(pixels[i + 1] - 188) <= 1 &&
              abs(pixels[i + 2] - 225) <= 1 && pixels[i + 3] == 128);
      else
        CHECK(pixels[i] == (level ? level == 1 ? 64 : 128 : 0) &&
              pixels[i + 1] == (layer ? 128 : 0) && pixels[i + 2] == 255 && pixels[i + 3] == 128);
    }
  }
  CALL(nvnTextureFinalize, &array_texture);
  strided_read_texels_test();
  texture_copy_destination_test();
  astc_legacy_copy_test();
  texture_clear_test();
  bright_test(&tp);
  // 原跟手图：同格底色绘制三次、图标一次，按贴图频率挑图标并重放到手指上方。
  // Original follow image: three common-background draws and one icon draw
  // at the same cell; replay the least-used texture at the requested center.
  NVNprogram ui_program;
  CHECK(CALL(nvnProgramInitialize, &ui_program, &device));
  sd[0].control = controls[12]; sd[1].control = controls[4];
  CHECK(CALL(nvnProgramSetShaders, &ui_program, 2, sd));
  NVNtextureBuilder icon_builder;
  CALL(nvnTextureBuilderSetDefaults, &icon_builder);
  CALL(nvnTextureBuilderSetDevice, &icon_builder, &device);
  CALL(nvnTextureBuilderSetTarget, &icon_builder, NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth, &icon_builder, 4);
  CALL(nvnTextureBuilderSetHeight, &icon_builder, 4);
  CALL(nvnTextureBuilderSetFormat, &icon_builder, NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage, &icon_builder, &memory, (3 << 20) + 851968);
  NVNtexture icon;
  CHECK(CALL(nvnTextureInitialize, &icon, &icon_builder));
  CALL(nvnTexturePoolRegisterTexture, &tp, 1, &tex, NULL);
  CALL(nvnTexturePoolRegisterTexture, &tp, 3, &icon, NULL);
  NVNseparateTextureHandle icon_handle = CALL(nvnDeviceGetSeparateTextureHandle, &device, 3);
  float *quad = (void *)(bytes + (2 << 20) + 32768);
  memcpy(quad, (float[]){-.75f,-.75f,0,0,0, -.25f,-.75f,0,1,0,
                         -.75f,-.25f,0,0,1, -.25f,-.25f,0,1,1}, 80);
  float *matrix = quad + 32;
  memset(matrix, 0, 64); matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
  CALL(nvnVertexAttribStateSetFormat, &attrs[0], NVN_FORMAT_RGB32F, 0);
  CALL(nvnVertexAttribStateSetFormat, &attrs[1], NVN_FORMAT_RG32F, 12);
  CALL(nvnVertexStreamStateSetStride, &stream, 20);
  CALL(nvnVertexStreamStateSetDivisor, &stream, 0);
  NVNbufferRange quad_range = {(uintptr_t)quad, 80};
  colors[0] = &output;
  void mtl_ui_ghost(int, float, float, const float[4], float, float);
  int mtl_ui_ghost_rect(float[4]);
  const float grid[4] = {.125f,.125f,.25f,.25f}, black[4] = {0,0,0,1},
              cyan[4] = {0,1,.5f,1};
  for (unsigned pass = 0; pass < 2; pass++) {
    mtl_ui_ghost(!pass, .25f, .25f, grid, .75f, .75f);
    mtl_stats(&passes_before, &draws_before);
    CALL(nvnCommandBufferBeginRecording, &cmd);
    NVNcopyRegion icon_region = {0,0,0,4,4,1};
    CALL(nvnCommandBufferClearTexture, &cmd, &icon, NULL, &icon_region, cyan, 15);
    CALL(nvnCommandBufferSetRenderTargets, &cmd, 1, colors, NULL, NULL, NULL);
    CALL(nvnCommandBufferSetViewports, &cmd, 0, 1, viewport_full);
    CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 16, 16);
    CALL(nvnCommandBufferClearColor, &cmd, 0, black, 15);
    CALL(nvnCommandBufferSetScissor, &cmd, 0, 0, 8, 8);
    CALL(nvnCommandBufferBindProgram, &cmd, &ui_program, NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindVertexAttribState, &cmd, 2, attrs);
    CALL(nvnCommandBufferBindVertexStreamState, &cmd, 1, &stream);
    CALL(nvnCommandBufferBindVertexBuffers, &cmd, 0, 1, &quad_range);
    CALL(nvnCommandBufferBindUniformBuffer, &cmd, NVN_SHADER_STAGE_VERTEX, 0, (uintptr_t)matrix, 64);
    for (unsigned i = 0; i < 4; i++) {
      CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_FRAGMENT, 0, 1,
           i == 3 ? &icon_handle : &th);
      CALL(nvnCommandBufferDrawArrays, &cmd, NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP, 0, 4);
    }
    submit(end());
    mtl_stats(&passes_after, &draws_after);
    LOG("[vulkan ghost fixture] pass=%u draws=%llu render_passes=%llu",pass,
        (unsigned long long)(draws_after-draws_before),(unsigned long long)(passes_after-passes_before));
    CHECK(draws_after - draws_before == (pass ? 4 : 5));
    float recognized[4];
    CHECK(mtl_ui_ghost_rect(recognized) == !pass);
    if (!pass) for (unsigned i = 0; i < 4; i++)
      CHECK(fabsf(recognized[i] - (i < 2 ? .125f : .375f)) < .00002f);
    CALL(nvnTextureReadTexels, &output, NULL, &region, pixels);
    for (unsigned y = 0; y < 16; y++) for (unsigned x = 0; x < 16; x++) {
      int visible = (x >= 2 && x < 6 && y >= 2 && y < 6) ||
                    (!pass && x >= 10 && x < 14 && y >= 10 && y < 14);
      if (memcmp(pixels + (y * 16 + x) * 4,
                 visible ? (uint8_t[]){0,255,128,255} : (uint8_t[]){0,0,0,255}, 4))
        fprintf(stderr, "ghost pixel pass=%u x=%u y=%u got=%u,%u,%u,%u visible=%d\n",
                pass, x, y, pixels[(y*16+x)*4], pixels[(y*16+x)*4+1],
                pixels[(y*16+x)*4+2], pixels[(y*16+x)*4+3], visible);
      CHECK(!memcmp(pixels + (y * 16 + x) * 4,
                    visible ? (uint8_t[]){0,255,128,255} : (uint8_t[]){0,0,0,255}, 4));
    }
  }
  CALL(nvnTextureFinalize, &icon);
  CALL(nvnProgramFinalize, &ui_program);
  CALL(nvnProgramFinalize, &instance_graphics);
  store_overwrite_test(controls[0],controls[9],NVN_FORMAT_DEPTH32F_STENCIL8);
  store_overwrite_test(controls[0],controls[9],NVN_FORMAT_DEPTH24_STENCIL8);
  freeze_test(&image_compute_program,&tp,controls[0],controls[1]);
  extrap_kernel_test(NVN_FORMAT_DEPTH32F_STENCIL8);
  extrap_kernel_test(NVN_FORMAT_DEPTH24_STENCIL8);
  extrap_runtime_test(controls[0],controls[1],NVN_FORMAT_DEPTH32F_STENCIL8);
  extrap_runtime_test(controls[0],controls[1],NVN_FORMAT_DEPTH24_STENCIL8);
  quad_draw_test(controls[11]);
  vk_graphics_prepare_mode(2);
  buffer_binding_byte_test(controls[0],controls[1],controls[5],controls[2]);
  storage_offset_dependency_test(controls[5]);
  compute_texel_dependencies_test(controls[2]);
  texture_dirty_proof_test();
  texture_range_dirty_proof_test();
  clear_sibling_preservation_test();
  clear_active_target_test(controls[0],controls[1]);
  target_match_test(controls[0],controls[1]);
  unknown_stage_reuse_test(controls[0],controls[1]);
  recipe_reentrant_test(controls[12],controls[4]);
  pass_depth_test(controls[0],controls[1],NVN_FORMAT_DEPTH32F_STENCIL8);
  pass_depth_test(controls[0],controls[1],NVN_FORMAT_DEPTH24_STENCIL8);
  load_cover_test();
  uint64_t prepare_reuses,prepare_lookups,prepare_checks;
  vk_graphics_prepare_stats(&prepare_reuses,&prepare_lookups,&prepare_checks);
  fprintf(stderr,"[vulkan pipeline preparation fixture] reuses=%llu lookups=%llu checks=%llu\n",
    (unsigned long long)prepare_reuses,(unsigned long long)prepare_lookups,(unsigned long long)prepare_checks);
  vk_pass_index_stats index_stats;vk_pass_index_snapshot(&index_stats);
  CHECK(index_stats.mismatches==0);
  const char *store_reference=getenv("DM_VULKAN_STORE_REFERENCE");
  if(store_reference&&!strcmp(store_reference,"1")) CHECK(index_stats.indexed==0&&index_stats.reference>0);
  else CHECK(index_stats.indexed>0&&index_stats.comparisons==index_stats.indexed&&index_stats.fallbacks>=2);
  fprintf(stderr,"[vulkan store index fixture] builds=%llu indexed=%llu reference_ops=%llu query_events=%llu comparisons=%llu mismatches=%llu fallbacks=%llu\n",
    (unsigned long long)index_stats.builds,(unsigned long long)index_stats.indexed,
    (unsigned long long)index_stats.reference_ops,(unsigned long long)index_stats.query_events,
    (unsigned long long)index_stats.comparisons,(unsigned long long)index_stats.mismatches,(unsigned long long)index_stats.fallbacks);
  stream_submit_test(controls[0],controls[1]);
  image_dependency_test(&image_compute_program,1,0);
  image_dependency_test(&image_compute_program,0,0);
  image_dependency_test(&image_compute_program,0,1);
  image_dependency_test(&image_compute_program,1,1);
  CALL(nvnTextureFinalize, &half_float);
  CALL(nvnTextureFinalize, &output);
  CALL(nvnSamplerFinalize, &sampler);
  CALL(nvnProgramFinalize, &graphics);
  CALL(nvnProgramFinalize, &compute);
  CALL(nvnProgramFinalize, &storage_graphics);
  CALL(nvnProgramFinalize, &driver_graphics);
  CALL(nvnProgramFinalize, &image_compute_program);
  CALL(nvnProgramFinalize, &image_graphics_program);
  CALL(nvnProgramFinalize, &depth_graphics);
  CALL(nvnTextureFinalize, &tex);
  // Finalize 后已排队命令仍可用旧图像；只在原快照宽限期与 GPU 完成之后回收。
  // Finalize retains images for queued work, then reclaims them after the
  // original snapshot grace period and actual GPU completion.
  CALL(nvnCommandBufferBeginRecording, &cmd);
  submit(end());
  mtl_res_count resource_count;
  mtl_res_snapshot(&resource_count);
  CHECK(resource_count.live_tex > 0);
  for (int i = 0; i < 8; i++) {
    CALL(nvnCommandBufferBeginRecording, &cmd);
    submit(end());
  }
  mtl_res_snapshot(&resource_count);
  CHECK(resource_count.live_tex == 0);
  CHECK(vk_sampler_count() == sampler_baseline);
  image_retirement_test();
  bc_cpu_scratch_test();
  command_pool_reset_test();
  buffer_partition_test();
  pipeline_prewarm_test(controls[0],controls[1]);
  dynamic_uniform_test(controls[0]);
  sampled_mip_load_cover_test();
  mip_stamp_test(controls[3],controls[4]);
  mip_stamp_write_test();
  subpass_render_cache_test();
  buffer_scope_test();
  initialized_span_test();
  pool_backing_span_test();
  pool_span_cache_test();
  pool_address_proof_test();
  pool_authority_batch_test();
  pool_copy_accounting_test();
  pool_flags_test();
  pool_publication_test();
  pool_upload_pair_boundaries_test();
  pool_upload_pair_gpu_tails_test();
  upload_page_span_test();
  retained_update_test();
  pool_finite_consolidation_test();
  pool_window_reservation_test();
  pool_window_production_test(controls[0],controls[1],controls[5],controls[2]);
  shader_pool_ranges_test();
  pool_binding_mask_gpu_test();
  shader_present_test();
  sampled_color_array_layout_test(controls[3],controls[4]);
  texel_storage_test(controls[0]);
  uniform_predicate_test(controls[0]);
  clear_group_depth_test(controls[0],controls[1],controls[2]);
  vertex_index_binding_test(controls[12],controls[4]);
  vertex_require_dedup_test();
  deferred_mip_clear_test(controls[0],controls[1],controls[5],controls[9],controls[2]);
  logical_preflight_test(controls[3],controls[4]);
  pending_cpu_read_test();
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  driver_compact_key_test();
#endif
  buffer_visibility_intersection_test(controls[0],controls[5]);
  texel_storage_bits_test(controls[0]);
  compute_sample_layout_test();
  depth_array_layout_test(controls[0],controls[9]);
  readonly_store_test(controls[0],controls[1]);
  zero_outline_test(controls[0]);
  nofxaa_test();
  bloom_quarter_test();
  uniform_specialization_test(controls[0],0);
  uniform_specialization_test(controls[0],1);
  unchanged_upload_publication_test();
  complete_pool_overwrite_test();
  lazy_pool_mirror_test();
  pool_upload_patterns_test();
  pool_page_generation_test();
  queued_upload_overlap_test();
  pool_stage_ranges_test();
  pool_word_coverage_test();
  grouped_upload_ranges_test();
  pool_upload_hazard_test();
  core_submission_overlap_test();
  internal_present_chain_test();
  readback_batch_order_test();
  readback_batch_pool_test();
  large_readback_reuse_test();
  deferred_pool_upload_test();
  float16_arithmetic_test();
  // 原 mtl_gpu_ms ABI 只报告已完成的 GPU 时间，同帧累加且不能读到环槽旧帧。
  // Original timing ABI: completed GPU timestamps, same-frame accumulation,
  // and no stale result after the ring slot is reused.
  const uint64_t timing_frame = 0x100000049ull;
  CHECK(mtl_gpu_ms(timing_frame) < 0);
  mtl_set_frame(timing_frame);
  CALL(nvnCommandBufferBeginRecording, &cmd);
  submit(end());
  if (V.timestamp_bits && !getenv("DM_VULKAN_EXTERNAL_PROFILE")) {
    double first_ms = mtl_gpu_ms(timing_frame);
    CHECK(first_ms >= 0 && first_ms < 30000);
    CALL(nvnCommandBufferBeginRecording, &cmd);
    submit(end());
    CHECK(mtl_gpu_ms(timing_frame) >= first_ms);
    CHECK(mtl_gpu_ms(timing_frame + 128) < 0);
    mtl_set_frame(timing_frame + 128);
    CALL(nvnCommandBufferBeginRecording, &cmd);
    submit(end());
    CHECK(mtl_gpu_ms(timing_frame) < 0);
    CHECK(mtl_gpu_ms(timing_frame + 128) >= 0);
  } else {
    CHECK(mtl_gpu_ms(timing_frame) < 0);
  }
  memory_suballocation_test();
  CALL(nvnEventFinalize, &event);
  CALL(nvnCommandBufferFinalize, &cmd);
  CALL(nvnQueueFinalize, &queue);
  CALL(nvnMemoryPoolFinalize, &memory);
#ifdef DM_VULKAN_SHARED_POOLS
  // Real SVC heap (including requested legacy/private mode) owns its initial
  // process-lifetime AS; it is not the ordinary posix_memalign allocation.
  if(!getenv("DM_SHARED_GUEST_HEAP_FIXTURE"))
#endif
  free(bytes);
  CHECK(vk_validation_error_count() == 0);
  if (getenv("DM_VULKAN_EXTERNAL_PROFILE")) {
    CHECK(vk_gpu_profile_samples() == 0);
    CHECK(mtl_gpu_ms(timing_frame) < 0);
  } else if (V.timestamp_bits) CHECK(vk_gpu_profile_samples() > 0);
  vk_buffer_sync_stats memory_sync;vk_buffer_sync_snapshot(&memory_sync);
  LOG("[vulkan readback memory fixture] coherent_reference=%d readback_bytes=%llu cached_bytes=%llu invalidations=%llu; repeated GPU writes/byte guards passed",
    memory_sync.readback_reference,(unsigned long long)memory_sync.readback_bytes,
    (unsigned long long)memory_sync.cached_readback_bytes,(unsigned long long)memory_sync.readback_invalidations);
  uint64_t relaxed_barriers,fallback_barriers;int stage_reference;
  vk_graphics_stage_stats(&relaxed_barriers,&fallback_barriers,&stage_reference);
  if(!stage_reference) CHECK(relaxed_barriers>0);
  CHECK(fallback_barriers>0);
  LOG("[vulkan stage dependency fixture] reference=%d relaxed=%llu fallback=%llu; pixel and GPU-update regressions passed",
      stage_reference,(unsigned long long)relaxed_barriers,(unsigned long long)fallback_barriers);
  uint64_t storage_direct,storage_copies,storage_bytes;int storage_reference;
  vk_storage_bias_stats(&storage_direct,&storage_copies,&storage_bytes,&storage_reference);
  CHECK(storage_copies>0&&storage_bytes>0);
  if(vk_shader_get(controls[5])->storage_bias_mask&&!storage_reference) CHECK(storage_direct>0);
  else CHECK(storage_direct==0);
  LOG("[vulkan storage alignment fixture] reference=%d direct=%llu copies=%llu bytes=%llu",
      storage_reference,(unsigned long long)storage_direct,(unsigned long long)storage_copies,(unsigned long long)storage_bytes);
  uint64_t full_pool_bytes,used_pool_bytes,protected_pool_bytes;int pool_reference;
  vk_pool_sync_policy_stats(&full_pool_bytes,&used_pool_bytes,&protected_pool_bytes,&pool_reference);
  LOG("[vulkan pool upload policy fixture] reference=%d full_ranges=%llu used_ranges=%llu protected_ranges=%llu bytes",
      pool_reference,(unsigned long long)full_pool_bytes,(unsigned long long)used_pool_bytes,(unsigned long long)protected_pool_bytes);
  uint64_t image_transitions,image_safe,image_fallback;int image_reference;
  vk_image_layout_stats(&image_transitions,&image_safe,&image_fallback,&image_reference);
  CHECK(image_fallback>0);
  if(!image_reference) CHECK(image_transitions>0 && image_safe>0);
  LOG("[vulkan image layout fixture] reference=%d transitions=%llu safe_uses=%llu fallbacks=%llu; GPU pixel regressions passed",
      image_reference,(unsigned long long)image_transitions,(unsigned long long)image_safe,(unsigned long long)image_fallback);
  pipeline_cache_test();
  vk_test_upload_dual_enable(old_upload_dual);
  printf("{\"backend\":\"vulkan\",\"original_nvn\":true,\"checks\":%u,"
         "\"result\":\"passed\"}\n",
         checks);
  fflush(NULL);
  _exit(0);
}

#ifndef DM_VULKAN_TEST_EMBED
int main(void) { return dm_vulkan_test_main(); }
#endif
