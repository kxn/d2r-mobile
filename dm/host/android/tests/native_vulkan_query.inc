// Functional arithmetic/ABI oracle. No wall-clock measurement or game data.
static uint64_t query_integer_reference(uint64_t raw,float period,uint32_t bits) {
  uint32_t multiplier;int32_t shift;
  CHECK(vk_query_clock_parameters(period,bits,&multiplier,&shift));
  if(bits<64)raw&=(UINT64_C(1)<<bits)-1;
  __uint128_t product=(__uint128_t)raw*multiplier;
  if(shift>=0) {
    // A product beyond128bits is already far above625*UINT64_MAX.
    if(shift>=128 || product>(~(__uint128_t)0>>shift))return UINT64_MAX;
    product<<=shift;
  }
  product/=625;
  if(shift<0)product=-shift>=128?0:product>>-shift;
  return product>UINT64_MAX?UINT64_MAX:(uint64_t)product;
}
static void gpu_query_math_test(void) {
  const float periods[]={1,13,19.2f,52,52.083333333f,76.92308f,0x1.000002p-20f,0x1p-126f,0x1p-149f,0x1p30f,0x1p40f,0x1.fffffep48f,FLT_MAX};
  const uint32_t widths[]={1,16,31,32,33,48,63,64};
  const uint64_t raw_values[]={0,1,625,UINT64_C(0xffff),UINT64_C(0x10000),UINT64_C(0x10001),UINT32_MAX,UINT64_C(0x100000000),UINT64_C(0x100000001),UINT64_C(0x123456789abc),UINT64_C(0x00ffffffffffffff),UINT64_MAX,UINT64_MAX-1,
    UINT64_C(0x7fffffffffffffff),UINT64_C(0x8000000000000000),
    UINT64_C(12489982394828155300),UINT64_C(12489982394828155301),UINT64_C(12489982394828155302),
    UINT64_C(12489982394828155303),UINT64_C(12489982394828155304)};
  uint64_t source_values[4];unsigned cases=0,rejected=0,max_results=0,mali_boundary_cases=0;
  vk_buffer source={0},destination={0};
  vk_buffer_create(&source,sizeof source_values,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  vk_buffer_create(&destination,64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  CHECK(source.map && destination.map);
  CHECK((source.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
      (destination.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
  for(unsigned p=0;p<sizeof periods/sizeof *periods;p++)for(unsigned w=0;w<sizeof widths/sizeof *widths;w++) {
    uint32_t multiplier;int32_t shift;
    int supported=vk_query_clock_parameters(periods[p],widths[w],&multiplier,&shift);
    if(!supported) {rejected++;continue;}
    for(unsigned i=0;i<sizeof raw_values/sizeof *raw_values;i++) {
      source_values[0]=raw_values[i];source_values[1]=UINT64_MAX;source_values[2]=2;source_values[3]=UINT64_C(0x123456789);
      memcpy(source.map,source_values,sizeof source_values);memset(destination.map,0xcc,64);
      mtl_sub_begin();CHECK(vk_query_native_test_dispatch(&source,&destination,4,periods[p],widths[w],3,1));
      vk_internal_wait(vk_internal_commit());
      if(!(destination.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))VK_CHECK(vk_memory_invalidate(destination.allocation));
      uint64_t actual[2];memcpy(actual,(uint8_t *)destination.map+4,sizeof actual);
      uint64_t expected[2]={source_values[1]+source_values[2]+source_values[3],query_integer_reference(raw_values[i],periods[p],widths[w])};
      CHECK(!memcmp(actual,expected,sizeof actual));
      if(expected[1]==UINT64_MAX)max_results++;
      uint32_t period_bits;memcpy(&period_bits,&periods[p],4);
      if(period_bits==0x4299d89eu && widths[w]==64 && raw_values[i]>=UINT64_C(12489982394828155300) && raw_values[i]<=UINT64_C(12489982394828155304)) {
        mali_boundary_cases++;
        const uint64_t boundary_expected[]={UINT64_MAX-3,UINT64_MAX-1,UINT64_MAX,UINT64_MAX,UINT64_MAX};
        CHECK(actual[1]==boundary_expected[raw_values[i]-UINT64_C(12489982394828155300)]);
      }
      uint32_t before,after;memcpy(&before,destination.map,4);memcpy(&after,(uint8_t *)destination.map+20,4);
      CHECK(before==0xcccccccc && after==0xcccccccc);cases++;
    }
  }
  const float invalid[]={0,-0.0f,-1,INFINITY,NAN};
  for(unsigned k=0;k<sizeof invalid/sizeof *invalid;k++) {uint32_t mul;int32_t shift;
    CHECK(!vk_query_clock_parameters(invalid[k],64,&mul,&shift));
    CHECK(!vk_query_native_test_dispatch(&source,&destination,4,invalid[k],64,3,1));}
  uint32_t mul;int32_t shift;CHECK(!vk_query_clock_parameters(1,0,&mul,&shift));
  CHECK(!vk_query_clock_parameters(1,65,&mul,&shift));
  CHECK(vk_query_clock_parameters(76.92308f,64,&mul,&shift));
  CHECK(vk_query_clock_parameters(FLT_MAX,64,&mul,&shift));
  CHECK(!rejected && max_results>0 && mali_boundary_cases==5);vk_buffer_destroy(&source);vk_buffer_destroy(&destination);
  printf("{\"test_details\":\"GPU-native-query-u32-pair-math\",\"cases\":%u,\"capability_rejections\":%u,\"max_or_saturated_results\":%u,\"actual_Mali_boundary_cases\":%u,\"Mali_period_bits\":1117378718,\"overflow_policy\":\"Android-u64-saturate\",\"shader_int64\":false,\"shader_float64\":false}\n",cases,rejected,max_results,mali_boundary_cases);
}

enum { QUERY_OBSERVATIONS=4096 };
typedef struct {uint64_t address,raw,sum,expected[2];uint32_t type;} query_observation;
static query_observation query_observed[QUERY_OBSERVATIONS];
static unsigned query_observed_count;
static int query_observed_overflow;
static unsigned query_observed_saturated;
static void query_observe_original_cpu(uint64_t address,uint32_t type,uint64_t raw,uint64_t sum) {
  // Only this fixture enables the callback. Retain the original CPU expression
  // as a byte oracle without writing any guest/Pool output in completion.
  if(query_observed_count==QUERY_OBSERVATIONS){query_observed_overflow=1;return;}
  long double scaled=(long double)raw*V.props.limits.timestampPeriod*0.0192L;
  // The old expression is an oracle only in its C-defined range. Explicit
  // saturation outside it matches the frozen Android __fixunstfdi object.
  uint64_t ticks;
  if(scaled>=0x1p64L){ticks=UINT64_MAX;query_observed_saturated++;}
  else ticks=(uint64_t)scaled;
  query_observed[query_observed_count++]=(query_observation){.address=address,.raw=raw,.sum=sum,
    .expected={type==NVN_COUNTER_TYPE_SAMPLES_PASSED?sum:ticks,ticks},.type=type};
}
static void query_report_snapshot(uint32_t type,uint64_t address,uint64_t snapshot) {
  CALL(nvnCommandBufferReportCounter,&cmd,type,address);
  CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,address,snapshot,16,0);
}
static void gpu_query_report_test(void) {
  enum { count=2053 };
  NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);
  CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);CALL(nvnMemoryPoolBuilderSetStorage,&b,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&memory,&b));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));CALL(nvnCommandBufferAddCommandMemory,&cmd,&memory,0,1u<<20);
  CHECK(vk_pool_shared_destination((uintptr_t)bytes+(4u<<20)-16,16)==1);
  CHECK(vk_pool_shared_destination((uintptr_t)bytes+(4u<<20)-8,16)==-1);
  CHECK(vk_pool_shared_destination(UINT64_MAX-7,16)==-1);
  CHECK(vk_pool_shared_destination((uintptr_t)&b,16)==0);
  vk_counter_native_test_observer(query_observe_original_cpu);
  uint64_t address=(uintptr_t)(bytes+(1u<<20)),snapshots=(uintptr_t)(bytes+(2u<<20));
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for(unsigned i=0;i<count;i++) {
    uint64_t destination=address+16*(i%16);
    query_report_snapshot(i&1?NVN_COUNTER_TYPE_TIMESTAMP_TOP:NVN_COUNTER_TYPE_TIMESTAMP,destination,snapshots+16*i);
    if(!(i%31)) {uint32_t overwrite[4]={9,9,9,9};
      CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,destination,16,0,16,overwrite);}
  }
  submit(end());CHECK(!query_observed_overflow && query_observed_count==count);
  for(unsigned i=0;i<count;i++) {
    CHECK(query_observed[i].address==address+16*(i%16));
    CHECK(query_observed[i].type==(uint32_t)(i&1?NVN_COUNTER_TYPE_TIMESTAMP_TOP:NVN_COUNTER_TYPE_TIMESTAMP));
    CHECK(!memcmp((const void *)(uintptr_t)(snapshots+16*i),query_observed[i].expected,16));
  }
  // A real later GPU overwrite stays last; no completion CPU counter store.
  uint64_t *last=(void *)(bytes+(1u<<20)+0x800);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_TIMESTAMP,(uintptr_t)last);
  const uint64_t final[2]={UINT64_C(0x9988776655443322),UINT64_C(0x123456789abcdef0)};
  CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)last,16,0,16,final);
  submit(end());CHECK(!memcmp(last,final,sizeof final));
  CHECK(query_observed_count==count+1);
  // Original sample prefix/reset accounting with real rasterized fragments.
  NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&memory,(3u<<20)+65536);NVNtexture target;
  CHECK(CALL(nvnTextureInitialize,&target,&tb));
  uint8_t controls[2][0x880];memset(controls[0],0,sizeof controls[0]);memset(controls[1],1,sizeof controls[1]);
  NVNshaderData shaders[2]={{.control=controls[0]},{.control=controls[1]}};NVNprogram program;
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  const float rgba[]={1,0,0,1},viewport[]={0,0,16,16};memcpy(bytes+(1u<<20)+0x10000,rgba,sizeof rgba);
  CALL(nvnMemoryPoolFlushMappedRange,&memory,(1u<<20)+0x10000,sizeof rgba);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const NVNtexture *targets[]={&target};
  CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,NULL,NULL,NULL);
  CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
  CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
  CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
  CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
  CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(bytes+(1u<<20)+0x10000),16);
  CALL(nvnCommandBufferClearColor,&cmd,0,rgba,15);
  CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
  for(unsigned k=0;k<4;k++) {
    if(k==2 || k==3)CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    if(k!=3)CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    query_report_snapshot(NVN_COUNTER_TYPE_SAMPLES_PASSED,address,snapshots+16*(count+1+k));
  }
  submit(end());CHECK(query_observed_count==count+5);
  const uint64_t expected_samples[]={256,512,256,0};
  for(unsigned k=0;k<4;k++) {query_observation *r=&query_observed[count+1+k];
    CHECK(r->type==NVN_COUNTER_TYPE_SAMPLES_PASSED && r->sum==expected_samples[k]);
    CHECK(!memcmp((const void *)(uintptr_t)(snapshots+16*(count+1+k)),r->expected,16));}
  // Private query compute descriptors/push constants must not poison the
  // original NVN compute state which is rebound after the native publisher.
  uint8_t compute_control[0x880];memset(compute_control,2,sizeof compute_control);
  NVNshaderData compute_shader={.control=compute_control};NVNprogram compute;
  CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&compute_shader));
  uint32_t *source=(void *)(bytes+(1u<<20)+0x20000),*middle=(void *)(bytes+(1u<<20)+0x30000),*output=(void *)(bytes+(1u<<20)+0x40000);
  for(unsigned k=0;k<64;k++)source[k]=k*17+5;
  CALL(nvnMemoryPoolFlushMappedRange,&memory,(1u<<20)+0x20000,256);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferBindProgram,&cmd,&compute,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)source,256);
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)middle,256);
  CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
  query_report_snapshot(NVN_COUNTER_TYPE_TIMESTAMP,address,snapshots+16*(count+5));
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)middle,256);
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)output,256);
  CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);submit(end());
  CHECK(query_observed_count==count+6);
  CHECK(!memcmp((void *)(uintptr_t)(snapshots+16*(count+5)),query_observed[count+5].expected,16));
  for(unsigned k=0;k<64;k++)CHECK(output[k]==(source[k]*3+7)*3+7);
  CALL(nvnProgramFinalize,&compute);
  vk_counter_native_test_observer(NULL);
  CALL(nvnTextureFinalize,&target);CALL(nvnProgramFinalize,&program);
  // Existing streaming regression spans several actual GPU submissions and
  // keeps cumulative query prefixes/reset boundaries through resource reuse.
  stream_submit_test(controls[0],controls[1]);
  CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&memory);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test_details\":\"GPU-native-query-original-CPU-byte-oracle\",\"timestamp_reports\":%u,\"sample_prefixes\":4,\"legacy_CPU_expression_exact\":%s,\"defined_saturating_reference\":true,\"saturated_reports\":%u,\"streaming_regression\":true,\"image_freshness_acceptance\":false}\n",count+2,query_observed_saturated?"false":"true",query_observed_saturated);
}
