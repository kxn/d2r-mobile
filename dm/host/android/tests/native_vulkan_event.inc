// Original NVN event payloads are consumed/overwritten by later GPU commands
// in the same recording. A completion CPU post cannot satisfy these cases.
static void gpu_event_payload_test(void) {
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&memory,&builder));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&memory,0,1u<<20);
  uint64_t alignment=V.props.limits.minStorageBufferOffsetAlignment;
  CHECK(alignment && !(alignment&(alignment-1)) && alignment<65536);
  uint64_t offsets[]={4,alignment>=4?alignment-4:0,alignment,alignment+4,4092,4096,8192,(2u<<20)-4};
  uint32_t *out=(void *)(bytes+(3u<<20));
  for(unsigned k=0;k<sizeof offsets/sizeof *offsets;k++) {
    uint64_t off=(2u<<20)+offsets[k];uint32_t *payload=(void *)(bytes+off);
    *payload=31;memset(out,0xcc,64);
    NVNeventBuilder eb;NVNevent event;CALL(nvnEventBuilderSetDefaults,&eb);
    CALL(nvnEventBuilderSetStorage,&eb,&memory,off);CHECK(CALL(nvnEventInitialize,&event,&eb));
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,10);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_ADD,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,5);
    CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)payload,(uintptr_t)(out+1),4,0);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_ADD,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,UINT32_MAX);
    CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)payload,(uintptr_t)(out+2),4,0);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,UINT32_MAX);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_ADD,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,1);
    CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)payload,(uintptr_t)(out+3),4,0);
    uint32_t update=9;
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)payload,4,0,4,&update);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_ADD,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,3);
    CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)payload,(uintptr_t)(out+4),4,0);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,7);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)payload,4,0,4,&update);
    submit(end());
    CHECK(out[0]==0xcccccccc && out[1]==15 && out[2]==14 && out[3]==0 && out[4]==12 && out[5]==0xcccccccc);
    CHECK(*payload==9);CHECK(CALL(nvnEventGetValue,&event)==9);
    CALL(nvnEventFinalize,&event);
  }
  // The native helper owns a separate compute layout/pipeline/descriptor.
  // An intervening ADD must not corrupt the game's retained compute state.
  uint8_t control[0x880];memset(control,2,sizeof control);NVNprogram program;
  NVNshaderData shader={.control=control};CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  uint32_t *src=(void *)(bytes+(1u<<20)+65536),*dst=src+1024;
  for(unsigned i=0;i<64;i++){src[i]=i*17;dst[i]=0;}
  NVNeventBuilder eb;NVNevent event;CALL(nvnEventBuilderSetDefaults,&eb);
  CALL(nvnEventBuilderSetStorage,&eb,&memory,(2u<<20)+32772);CHECK(CALL(nvnEventInitialize,&event,&eb));
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)src,256);
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)dst,256);
  CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
  CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,5);
  CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_ADD,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,8);
  CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
  submit(end());
  for(unsigned i=0;i<64;i++)CHECK(dst[i]==src[i]*3+7);
  CHECK(CALL(nvnEventGetValue,&event)==13);
  CALL(nvnEventFinalize,&event);CALL(nvnProgramFinalize,&program);
  CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&memory);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"GPU-native-NVN-event-payload\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);
}
