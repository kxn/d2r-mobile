// Owned production-parser/constant-allocation oracle. No game modules or clocks.
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
#include "../../../host/crypto.h"
#include <sys/stat.h>
extern void vk_test_driver_span_load(vk_shader *);
extern vk_buffer *vk_test_driver_span_constant(vk_shader *,int,uint64_t *,uint64_t *);

// Production arenas are UBO-only. Add TRANSFER_SRC solely to these fixture
// allocations so actual GPU copies can inspect their immutable bytes. This
// oracle does not claim real uniform-shader sampling or Android placement.
static PFN_vkCreateBuffer driver_span_original_create;
static PFN_vkDestroyBuffer driver_span_original_destroy;
static pthread_mutex_t driver_span_object_mu=PTHREAD_MUTEX_INITIALIZER;
static struct {VkBuffer handle;VkDeviceSize bytes;int destroyed,protected;} driver_span_objects[256];
static unsigned driver_span_object_count;
static VKAPI_ATTR VkResult VKAPI_CALL driver_span_create(VkDevice device_,const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator,VkBuffer *buffer) {
  VkBufferCreateInfo copy=*info;int arena=info->usage==VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if(arena)copy.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkResult result=driver_span_original_create(device_,&copy,allocator,buffer);
  if(result==VK_SUCCESS && arena) {
    pthread_mutex_lock(&driver_span_object_mu);
    if(driver_span_object_count==256)die("Driver span fixture object capacity");
    driver_span_objects[driver_span_object_count++]=(typeof(*driver_span_objects)){.handle=*buffer,.bytes=copy.size};
    pthread_mutex_unlock(&driver_span_object_mu);
  }
  return result;
}
static VKAPI_ATTR void VKAPI_CALL driver_span_destroy(VkDevice device_,VkBuffer buffer,
    const VkAllocationCallbacks *allocator) {
  pthread_mutex_lock(&driver_span_object_mu);
  for(unsigned i=driver_span_object_count;i-->0;)if(driver_span_objects[i].handle==buffer && !driver_span_objects[i].destroyed) {
    if(driver_span_objects[i].protected)die("Driver span fixture destroyed an in-flight arena");
    driver_span_objects[i].destroyed=1;break;
  }
  pthread_mutex_unlock(&driver_span_object_mu);
  driver_span_original_destroy(device_,buffer,allocator);
}
static void driver_span_protect(int active) {
  pthread_mutex_lock(&driver_span_object_mu);
  for(unsigned i=0;i<driver_span_object_count;i++)if(!driver_span_objects[i].destroyed)
    driver_span_objects[i].protected=active;
  pthread_mutex_unlock(&driver_span_object_mu);
}
static unsigned driver_span_live_objects(void) {
  unsigned live=0;pthread_mutex_lock(&driver_span_object_mu);
  for(unsigned i=0;i<driver_span_object_count;i++)live+=!driver_span_objects[i].destroyed;
  pthread_mutex_unlock(&driver_span_object_mu);return live;
}

static vk_shader driver_span_mock(unsigned key,vk_binding bindings[3]) {
  vk_shader s={.stage=5,.set=5,.nb=2,.bindings=bindings,
    .module=(VkShaderModule)(uintptr_t)(0x12340000u+key)};
  snprintf(s.key,sizeof s.key,"%016x",key);
  for(unsigned i=0;i<sizeof s.fingerprint;i++)s.fingerprint[i]=(uint8_t)(key+i*7+3);
  bindings[0]=(vk_binding){.kind=1,.reg=0,.binding=3,.count=1};
  bindings[1]=(vk_binding){.kind=1,.reg=2,.binding=7,.count=1};
  bindings[2]=bindings[0];return s;
}
// Fresh mock per attempt: a production module is loaded once, never revoked
// by rereading a malformed sidecar into an already authorized live shader.
static void driver_span_wire(vk_shader *s,const char *root,unsigned mutation) {
  uint32_t data[25]={UINT32_C(0x31505344),1,s->stage,s->set};
  memcpy(data+4,s->fingerprint,32);data[12]=3;data[13]=4096;data[14]=7;data[15]=4096;
  size_t bytes=96;
  switch(mutation) {
  case 0:break;
  case 2:data[0]^=1;break;
  case 3:data[1]=2;break;
  case 4:data[2]^=1;break;
  case 5:data[3]^=1;break;
  case 6:data[4]^=1;break;
  case 8:data[12]=98;data[14]=99;break;
  case 9:s->bindings[0].count=s->bindings[1].count=2;break;
  case 10:data[13]=data[15]=4097;break;
  case 11:data[13]=data[15]=65540;break;
  case 12:data[13]=8192;data[15]=65536;break; // proven high zero-tail read
  case 13:bytes=95;break;
  case 14:bytes=100;break;
  case 15:s->nb=3;break; // ambiguous c0 only; c2 is still independently valid
  default:break;
  }
  CC_SHA256(data,64,(uint8_t *)(data+16));if(mutation==7)data[16]^=1;
  char path[2048];CHECK(snprintf(path,sizeof path,"%s/%s.dspan",root,s->key)<(int)sizeof path);
  if(mutation==1) {CHECK(access(path,F_OK)!=0);return;} // missing proof
  FILE *file=fopen(path,"wb");CHECK(file!=NULL);
  CHECK(fwrite(data,1,bytes,file)==bytes);CHECK(!fclose(file));
}
static void driver_span_remove(vk_shader *s,const char *root) {
  char path[2048];CHECK(snprintf(path,sizeof path,"%s/%s.dspan",root,s->key)<(int)sizeof path);
  if(access(path,F_OK)==0)CHECK(!unlink(path));
}

typedef struct {uint64_t output;unsigned range,slot,variant;} driver_span_read;
static driver_span_read driver_span_reads[512];
static unsigned driver_span_read_count;
static uint64_t driver_span_output_end;
static void driver_span_copy(vk_buffer *source,uint64_t offset,unsigned range,vk_buffer *output,
    unsigned slot,unsigned variant) {
  CHECK(range==4096 || range==65536);CHECK(offset<=source->size && range<=source->size-offset);
  CHECK(driver_span_read_count<512);CHECK(driver_span_output_end+range<=output->size);
  CHECK(source->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkBufferMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=source->handle,.offset=offset,.size=range};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&before,0,NULL);
  VkBufferCopy copy={.srcOffset=offset,.dstOffset=driver_span_output_end,.size=range};
  vkCmdCopyBuffer(V.cmd,source->handle,output->handle,1,&copy);
  VkBufferMemoryBarrier after=before;after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;after.buffer=output->handle;after.offset=copy.dstOffset;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&after,0,NULL);
  driver_span_reads[driver_span_read_count++]=(driver_span_read){copy.dstOffset,range,slot,variant};
  driver_span_output_end+=range;
}
static vk_buffer *driver_span_get(vk_shader *s,unsigned slot,unsigned expected_range,
    uint64_t *offset) {
  uint64_t range=0;vk_buffer *buffer=vk_test_driver_span_constant(s,slot,offset,&range);
  CHECK(buffer!=NULL);CHECK(range==expected_range);return buffer;
}
static void driver_span_pixels(vk_buffer *output,uint8_t *tag,uint32_t *expected) {
  VK_CHECK(vk_memory_invalidate(output->allocation));
  for(unsigned r=0;r<driver_span_read_count;r++) {
    driver_span_read v=driver_span_reads[r];
    driver_fixture_expected(5,v.variant,tag,expected);
    const uint32_t *bank=expected+(v.slot==2?16384:0);
    for(unsigned word=0;word<v.range/4;word++) {
      uint32_t actual;memcpy(&actual,output->map+v.output+word*4,4);CHECK(actual==bank[word]);
    }
  }
}
static void driver_span_test(void) {
  mtl_init();mtl_finish();vk_state saved=S;uint64_t last,content,misses;int original_reference;
  vk_driver_constant_stats(&last,&content,&misses,&original_reference);vk_driver_constant_reference(0);
  const char *temp=getenv("DM_TEMP");CHECK(temp && temp[0]=='/');char root[2048];
  CHECK(snprintf(root,sizeof root,"%s/driver-span-XXXXXX",temp)<(int)sizeof root);CHECK(mkdtemp(root)!=NULL);
  const char *old_root=V.options.shader_root;V.options.shader_root=root;
  driver_span_original_create=vkCreateBuffer;driver_span_original_destroy=vkDestroyBuffer;
  driver_span_object_count=0;vkCreateBuffer=driver_span_create;vkDestroyBuffer=driver_span_destroy;
  uint8_t *tag=malloc(65536);uint32_t *expected=malloc(131072);CHECK(tag && expected);
  vk_buffer output;vk_buffer_create(&output,16<<20,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  driver_span_read_count=0;driver_span_output_end=0;mtl_sub_begin();S=(vk_state){0};driver_fixture_seed(5,0,tag);
  vk_binding compact_bindings[3],legacy_bindings[3];vk_shader compact=driver_span_mock(0x100,compact_bindings),legacy=driver_span_mock(0x101,legacy_bindings);
  driver_span_wire(&compact,root,0);vk_test_driver_span_load(&compact);
  CHECK(compact.driver_span[0]==4096 && compact.driver_span[1]==4096);
  CHECK(compact.driver_span_module==compact.module);CHECK(!compact.driver_schema[0] && !compact.driver_schema[1]);
  // Same normalized content/stage/slot/schema across shaders. A last HIT or
  // non-last content HIT must never turn a 64 KiB reader into a 4 KiB slot.
  for(unsigned slot=0;slot<=2;slot+=2) {
    uint64_t aoff,boff,again;vk_buffer *a=driver_span_get(&compact,slot,4096,&aoff);
    driver_span_copy(a,aoff,4096,&output,slot,0);
    vk_buffer *b=driver_span_get(&legacy,slot,65536,&boff);CHECK(a->handle!=b->handle);
    driver_span_copy(b,boff,65536,&output,slot,0);
    vk_buffer *c=driver_span_get(&compact,slot,4096,&again);CHECK(c==a && again==aoff);
    driver_span_copy(c,again,4096,&output,slot,0);
    c=driver_span_get(&legacy,slot,65536,&again);CHECK(c==b && again==boff);
    driver_span_copy(c,again,65536,&output,slot,0);
  }
  // Parser rejection cases are fresh unpublished mocks, then the real
  // production constant() range and GPU high-tail bytes are checked as well.
  for(unsigned test=1;test<=15;test++) {
    vk_binding bindings[3];vk_shader mock=driver_span_mock(0x200+test,bindings);
    driver_span_wire(&mock,root,test);vk_test_driver_span_load(&mock);
    CHECK(!mock.driver_span[0]);CHECK(mock.driver_span[1]==(test==15?4096:0));
    for(unsigned slot=0;slot<=2;slot+=2) {
      unsigned range=test==15 && slot==2?4096:65536;uint64_t off;
      vk_buffer *b=driver_span_get(&mock,slot,range,&off);driver_span_copy(b,off,range,&output,slot,0);
    }
    driver_span_remove(&mock,root);
  }
  vk_shader changed=compact;changed.module=(VkShaderModule)(uintptr_t)0x55667788;
  for(unsigned slot=0;slot<=2;slot+=2) {uint64_t off;vk_buffer *b=driver_span_get(&changed,slot,65536,&off);driver_span_copy(b,off,65536,&output,slot,0);}
  // More than 32 total arena chunks, split across the two classes, exercises
  // the one shared idle cap without an unbounded allocation/fixture loop.
  for(unsigned variant=1;variant<=136;variant++) {
    driver_fixture_seed(5,variant,tag);
    for(unsigned kind=0;kind<2;kind++) {
      vk_shader *s=kind?&compact:&legacy;uint64_t off;unsigned range=kind?4096:65536;
      vk_buffer *b=driver_span_get(s,0,range,&off);driver_span_copy(b,off,range,&output,0,variant);
    }
  }
  int gated=vk_async_submissions()&&V.timeline_semaphore;VkSemaphore gate=VK_NULL_HANDLE;
  if(gated) {gate=pool_copy_gate();driver_span_protect(1);}
  uint64_t first=vk_internal_commit();
  if(!gated)vk_internal_wait(first);
  mtl_sub_begin();
  for(unsigned variant=140;variant<144;variant++) {
    driver_fixture_seed(5,variant,tag);
    for(unsigned slot=0;slot<=2;slot+=2)for(unsigned kind=0;kind<2;kind++) {
      vk_shader *s=kind?&compact:&legacy;uint64_t off;unsigned range=kind?4096:65536;
      vk_buffer *b=driver_span_get(s,slot,range,&off);driver_span_copy(b,off,range,&output,slot,variant);
    }
  }
  uint64_t second=vk_internal_commit();
  if(gated) {
    CHECK(vk_completed_serial()<first);CHECK(driver_span_live_objects()>32);
    CHECK(mtl_trim_idle_resources()); // pending arenas remain protected
    driver_span_protect(0);pool_copy_signal(&gate);
  }
  vk_internal_wait(second);mtl_finish();driver_span_pixels(&output,tag,expected);
  if(gated)vkDestroySemaphore(V.device,gate,NULL);
  // Normal begin recycles the completed contexts. Two classes share one cap;
  // trimming idle storage must not change any already read-back GPU bytes.
  for(unsigned round=0;round<3;round++) {
    mtl_sub_begin();S=(vk_state){0};driver_fixture_seed(5,160+round,tag);
    for(unsigned kind=0;kind<2;kind++) {uint64_t off;unsigned range=kind?4096:65536;vk_buffer *b=driver_span_get(kind?&compact:&legacy,2,range,&off);driver_span_copy(b,off,range,&output,2,160+round);}
    vk_internal_wait(vk_internal_commit());mtl_finish();CHECK(vk_test_idle_driver_arenas()<=32);
  }
  driver_span_pixels(&output,tag,expected);
  mtl_sub_begin();vk_internal_wait(vk_internal_commit());mtl_finish();CHECK(vk_test_idle_driver_arenas()<=32);
  CHECK(mtl_trim_idle_resources());CHECK(!vk_test_idle_driver_arenas());
  driver_span_pixels(&output,tag,expected);vk_buffer_destroy(&output);
  vkCreateBuffer=driver_span_original_create;vkDestroyBuffer=driver_span_original_destroy;
  driver_span_remove(&compact,root);CHECK(!rmdir(root));V.options.shader_root=old_root;
  vk_driver_constant_reference(original_reference);S=saved;vk_pipeline_state_changed();free(expected);free(tag);
  LOG("[vulkan driver span fixture] actual GPU copy/readback: compact/legacy c0+c2 and zero tails, cross-shader last/content isolation, invalid DSP fallback, changed-module fallback, mixed classes pending=%d/recycle/shared idle cap; transfer-source usage is fixture-only, no shader/game/Android execution",gated);
}
#endif
