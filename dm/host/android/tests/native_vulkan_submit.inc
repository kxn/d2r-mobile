// 两份真实 Vulkan 提交在途时，回收一份的描述符不得影响另一份。
// Two real Vulkan submissions in flight: recycling one descriptor owner must
// not affect the other. Timeline semaphores make outstanding work deterministic.
static void float16_arithmetic_test(void) {
  if(!V.shader_float16) {
    LOG("[vulkan Float16 fixture] skipped: device feature unavailable");return;
  }
  vk_state saved=S;
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));memset(data,0xa5,65536);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  uint8_t control[0x880];memset(control,35,sizeof control);
  NVNprogram program;NVNshaderData shader={.control=control};
  CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  uint32_t *input=(void *)(data+4096),*output=(void *)(data+8192);
  for(unsigned pass=0;pass<2;pass++) {
    dm_guest_write_prepare(input,64*sizeof *input);
    for(unsigned i=0;i<64;i++) input[i]=pass*1000+i;
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)input,256);
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)output,256);
    CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);submit(end());
    for(unsigned i=0;i<64;i++) {
      float expected=(float)(input[i]&1023u)*.5f+.5f;uint32_t bits;memcpy(&bits,&expected,4);
      CHECK(output[i]==bits);
    }
    CHECK(output[-1]==UINT32_C(0xa5a5a5a5));CHECK(output[64]==UINT32_C(0xa5a5a5a5));
  }
  CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  S=saved;vk_pipeline_state_changed();
  LOG("[vulkan Float16 fixture] explicit half arithmetic, 32-bit storage interface, repeated CPU edits and byte guards passed");
}
static void descriptor_inflight_test(const prog_t *program, int texel,unsigned set_count,int persistent) {
  CHECK(V.cmd == VK_NULL_HANDLE);
  if (!V.timeline_semaphore) {
    LOG("[vulkan descriptor inflight fixture] skipped: timeline semaphore unavailable");
    return;
  }
  vk_shader *stages[6];
  vk_program_shaders(program, stages);
  vk_shader *shader = stages[5];
  CHECK(shader && shader->compute && shader->nb == 2);
  enum { RUNS = 3, WORDS = 64 };
  int reference=getenv("DM_VULKAN_TEXEL_VIEW_REFERENCE") && !strcmp(getenv("DM_VULKAN_TEXEL_VIEW_REFERENCE"),"1");
  VkBufferView *first_views=persistent?calloc(set_count,sizeof *first_views):NULL;
  CHECK(!persistent || (texel && first_views));
  VkDeviceSize align = V.props.limits.minStorageBufferOffsetAlignment;
  if (texel && align < V.props.limits.minTexelBufferOffsetAlignment)
    align = V.props.limits.minTexelBufferOffsetAlignment;
  VkDeviceSize stride = (WORDS * 4 + align - 1) / align * align;
  vk_buffer input[RUNS] = {0}, output[RUNS] = {0};
  VkCommandBuffer commands[RUNS];
  // These deliberately outstanding direct submissions are independent of the
  // backend's completed-resource command pools.
  VkCommandPool command_pool;
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = V.queue_family};
  VK_CHECK(vkCreateCommandPool(V.device, &pool_info, NULL, &command_pool));
  VkFence fences[RUNS];
  VkSemaphore gates[2];
  vk_shader_submission *owners[2] = {0};
  vk_texture_submission *texture_owners[2] = {0};
  VkSemaphoreTypeCreateInfoKHR type = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type};
  for (unsigned i = 0; i < 2; i++)
    VK_CHECK(vkCreateSemaphore(V.device, &semaphore_info, NULL, &gates[i]));
  VkCommandBufferAllocateInfo allocate = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = RUNS};
  VK_CHECK(vkAllocateCommandBuffers(V.device, &allocate, commands));
  uint64_t guest_fence = mtl_last_fence();
  for (unsigned run = 0; run < RUNS; run++) {
    unsigned owner = run == 2 ? 0 : run;
    if (run == 2) {
      // B 仍在等自己的信号量；只完成 A 后回收 A，再用 A 编 C。
      // B still waits on its own semaphore. Complete/recycle A alone, then record C
      // using A's pools while B remains outstanding.
      CHECK(vkGetFenceStatus(V.device, fences[1]) == VK_NOT_READY);
      VkSemaphoreSignalInfoKHR signal = {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
          .semaphore = gates[0], .value = 1};
      VK_CHECK(V.signal_timeline(V.device, &signal));
      VK_CHECK(vkWaitForFences(V.device, 1, fences, VK_TRUE, 5000000000ull));
      CHECK(vkGetFenceStatus(V.device, fences[1]) == VK_NOT_READY);
      vk_shader_recycle(owners[0]);
#ifndef DM_ANDROID_APP
      if(getenv("DM_DESCRIPTOR_POOL_BLOCK_FIXTURE")) {
        extern void vk_test_descriptor_pool_ownership(vk_shader_submission *,unsigned *,unsigned *);
        unsigned blocks,retained;vk_test_descriptor_pool_ownership(owners[0],&blocks,&retained);
        CHECK(blocks>=2);
        CHECK(set_count>V.options.descriptor_sets?retained==0:retained>=set_count);
      }
#endif
      if (texel) vk_texture_recycle(texture_owners[0]);
    }
    vk_buffer *input_owner=&input[persistent?0:run];
    if(!persistent || !run)
      vk_buffer_create(input_owner, stride * set_count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
          (texel ? VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT : 0));
    vk_buffer_create(&output[run], stride * set_count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    memset(output[run].map, 0, stride * set_count);
    if(!persistent || !run)
      for (unsigned set = 0; set < set_count; set++)
        for (unsigned word = 0; word < WORDS; word++) {
          uint32_t value = 10000 * run + 100 * set + word;
          memcpy(input_owner->map + stride * set + word * 4, &value, 4);
        }
    owners[owner] = vk_shader_begin(owners[owner]);
    if (texel) texture_owners[owner] = vk_texture_begin(texture_owners[owner]);
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(vkBeginCommandBuffer(commands[run], &begin));
    vkCmdBindPipeline(commands[run], VK_PIPELINE_BIND_POINT_COMPUTE, shader->compute);
    if(shader->storage_bias_mask) {
      const uint32_t words[32]={0};
      vkCmdPushConstants(commands[run],shader->compute_layout,
        VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,
        0,sizeof words,words);
    }
    for (unsigned set = 0; set < set_count; set++) {
      VkDescriptorSet descriptors = vk_descriptor_allocate(shader->descriptors);
      VkDescriptorBufferInfo buffers[2] = {
          {input_owner->handle, stride * set, WORDS * 4},
          {output[run].handle, stride * set, WORDS * 4}};
      VkWriteDescriptorSet writes[2] = {0};
      for (unsigned binding = 0; binding < 2; binding++)
        writes[binding] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptors,
            .dstBinding = binding + 1, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffers[binding]};
      VkBufferView view;
      if (texel) {
        if(persistent) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
          extern VkBufferView vk_test_texel_view(vk_buffer *,VkFormat,VkDeviceSize,VkDeviceSize);
          view=vk_test_texel_view(input_owner,VK_FORMAT_R32_UINT,stride*set,WORDS*4);
          if(!run)first_views[set]=view;
          else if(!reference)CHECK(view==first_views[set]);
#else
          die("persistent texel owner fixture is host-only");
#endif
        } else {
          VkBufferViewCreateInfo create = {.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
              .buffer = input_owner->handle, .format = VK_FORMAT_R32_UINT,
              .offset = stride * set, .range = WORDS * 4};
          VK_CHECK(vkCreateBufferView(V.device, &create, NULL, &view));
          vk_texture_own_buffer_view(view);
        }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        writes[0].pBufferInfo = NULL;
        writes[0].pTexelBufferView = &view;
      }
      vkUpdateDescriptorSets(V.device, 2, writes, 0, NULL);
      vkCmdBindDescriptorSets(commands[run], VK_PIPELINE_BIND_POINT_COMPUTE,
                              shader->compute_layout, shader->set, 1,
                              &descriptors, 0, NULL);
      vkCmdDispatch(commands[run], 1, 1, 1);
    }
    VkMemoryBarrier readback = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(commands[run], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &readback, 0, NULL, 0, NULL);
    VK_CHECK(vkEndCommandBuffer(commands[run]));
#ifndef DM_ANDROID_APP
    if(getenv("DM_DESCRIPTOR_POOL_BLOCK_FIXTURE")) {
      extern void vk_test_descriptor_pool_ownership(vk_shader_submission *,unsigned *,unsigned *);
      unsigned blocks,retained;vk_test_descriptor_pool_ownership(owners[owner],&blocks,&retained);
      CHECK(V.options.descriptor_sets==2048 && blocks>=2 && retained>=set_count);
      if(set_count==768)CHECK(retained>512 && retained<=V.options.descriptor_sets);
    }
#endif
    vk_shader_end(owners[owner]);
    if (texel) vk_texture_end(texture_owners[owner]);
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(V.device, &fence_info, NULL, &fences[run]));
    uint64_t wait_value = 1;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkTimelineSemaphoreSubmitInfoKHR timeline = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
        .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &wait_value};
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .pNext = run < 2 ? &timeline : NULL,
                                .waitSemaphoreCount = run < 2 ? 1 : 0,
                                .pWaitSemaphores = run < 2 ? &gates[run] : NULL,
                                .pWaitDstStageMask = &wait_stage,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &commands[run]};
    pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueSubmit(V.queue, 1, &submit_info, fences[run]));
    pthread_mutex_unlock(&V.queue_mu);
    CHECK(vkGetFenceStatus(V.device, fences[run]) == VK_NOT_READY);
  }
  VkSemaphoreSignalInfoKHR signal = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
      .semaphore = gates[1], .value = 1};
  VK_CHECK(V.signal_timeline(V.device, &signal));
  VK_CHECK(vkWaitForFences(V.device, RUNS, fences, VK_TRUE, 5000000000ull));
  for (unsigned run = 0; run < RUNS; run++) {
    for (unsigned set = 0; set < set_count; set++)
      for (unsigned word = 0; word < WORDS; word++) {
        uint32_t value;
        memcpy(&value, output[run].map + stride * set + word * 4, 4);
        CHECK(value == (10000 * (persistent?0:run) + 100 * set + word) * 3 + 7);
      }
    vkDestroyFence(V.device, fences[run], NULL);
  }
  vkDestroyCommandPool(V.device, command_pool, NULL);
  for (unsigned i = 0; i < 2; i++) {
    vkDestroySemaphore(V.device, gates[i], NULL);
    vk_shader_destroy(owners[i]);
    vk_texture_destroy(texture_owners[i]);
  }
  for (unsigned run = 0; run < RUNS; run++) {
    vk_buffer_destroy(&input[run]);
    vk_buffer_destroy(&output[run]);
  }
  free(first_views);
  CHECK(mtl_last_fence() == guest_fence);
  LOG("[vulkan persistent texel GPU fixture] persistent=%d reference=%d real two owners / three submissions / independent fences passed",persistent,reference);
  LOG("[vulkan descriptor inflight fixture] passed: texel=%d two owners, three submits, %u descriptor sets, %u GPU words",texel,RUNS*set_count,RUNS*set_count*WORDS);
}

// 登记范围在提交尾读取最终值；显式封口后再写源缓冲也不能污染回读快照。
// Registered ranges read final submit contents. Overwriting the source after
// an explicit flush must not corrupt the CPU-visible snapshot or its gaps.
static void writeback_snapshot_test(void) {
  uint8_t *zone = bytes + (2 << 20) + 16384;
  for (unsigned pass = 0; pass < 2; pass++) {
    memset(zone, 0xcc, 96);
    mtl_sub_begin();
    vk_buffer *buffer;
    uint64_t offset;
    CHECK(mtl_addr((uintptr_t)zone, 96, (void **)&buffer, &offset));
    vkCmdFillBuffer(V.cmd, buffer->handle, offset, 96, 0x11111111);
    const unsigned ranges[][2] = {{24,8},{7,13},{19,7},{32,8},{9,2},{48,8}};
    for (unsigned i = 0; i < sizeof ranges / sizeof *ranges; i++)
      vk_buffer_writeback((uintptr_t)zone + ranges[i][0], ranges[i][1]);
    CHECK(vk_pool_pending_writes((uintptr_t)zone + 7, 1));
    CHECK(vk_pool_pending_writes((uintptr_t)zone + 6, 2));
    CHECK(!vk_pool_pending_writes((uintptr_t)zone + 40, 8));
    CHECK(!vk_pool_pending_writes((uintptr_t)zone, 7));
    if (!pass) vk_buffer_writebacks_flush();
    CHECK(vk_pool_pending_writes((uintptr_t)zone + 48, 8));
    vk_barrier();
    vkCmdFillBuffer(V.cmd, buffer->handle, offset, 96, 0x22222222);
    vk_internal_wait(vk_internal_commit());
    CHECK(!vk_pool_pending_writes((uintptr_t)zone, 96));
    for (unsigned i = 0; i < 96; i++) {
      int copied = (i >= 7 && i < 40) || (i >= 48 && i < 56);
      CHECK(zone[i] == (copied ? (pass ? 0x22 : 0x11) : 0xcc));
      CHECK(buffer->map[offset + i] == 0x22);
    }
  }
  // 恢复夹具脏页；下次 begin 沿正常 CPU→GPU 同步更新镜像。
  // Restore fixture dirty pages through the normal CPU→GPU path on next begin.
  memset(zone, 0x55, 96);
  LOG("[vulkan writeback snapshot fixture] passed: final values, overwritten source, unaligned merged ranges and gaps");
}

// 翻译宽限期已满足，但真实 GPU 尚未完成时，image/sampler 仍须保活。
// Translation grace can expire before actual GPU completion. Image/sampler
// retirement must retain every pending use, including CPU-visible effects.
static void cpu_known_pending_copy_test(uint8_t *source,uint8_t *destination) {
  if(!vk_async_submissions()||!V.timeline_semaphore) return;
  memset(source,0x22,64);memset(destination,0xcc,64);
  VkSemaphore gate;
  VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
    .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
  VK_CHECK(vkCreateSemaphore(V.device,&info,NULL,&gate));
  uint64_t value=1;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
    .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
  VkSubmitInfo block={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
    .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
  pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&block,VK_NULL_HANDLE));
  pthread_mutex_unlock(&V.queue_mu);
  mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  CHECK(mtl_addr((uintptr_t)source,64,(void **)&buffer,&offset));
  vkCmdFillBuffer(V.cmd,buffer->handle,offset,64,0x66666666);
  vk_buffer_transfer_written();vk_buffer_writeback((uintptr_t)source,64);
  uint64_t first=vk_internal_commit();
  CHECK(vk_completed_serial()<first);
  CHECK(vk_pool_pending_writes((uintptr_t)source,64));
  mtl_sub_begin();
  uint64_t snapshots0,published,fallbacks0,snapshots1,fallbacks1;int reference;
  vk_cpu_copy_stats(&snapshots0,&published,&fallbacks0,&reference);
  mtl_sub_copy((uintptr_t)source+1,(uintptr_t)destination+3,31);
  vk_cpu_copy_stats(&snapshots1,&published,&fallbacks1,&reference);
  CHECK(snapshots0==snapshots1&&fallbacks1-fallbacks0==31);
  uint64_t last=vk_internal_commit();
  CHECK(vk_completed_serial()<first&&last>first);
  CHECK(source[0]==0x22&&destination[3]==0xcc);
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
    .semaphore=gate,.value=1};
  VK_CHECK(V.signal_timeline(V.device,&signal));
  vk_internal_wait(last);
  for(unsigned n=0;n<64;n++) CHECK(destination[n]==(n>=3&&n<34?0x66:0xcc));
  vkDestroySemaphore(V.device,gate,NULL);
}
static void cpu_known_copy_test(void) {
  uint8_t *source=bytes+(2<<20)+32768,*destination=source+8192;
  uint64_t snapshots,published,fallbacks;int saved;
  vk_cpu_copy_stats(&snapshots,&published,&fallbacks,&saved);
  int initial_reference=saved;
  for(unsigned mode=0;mode<3;mode++) {
    int reference=mode!=1;vk_cpu_copy_reference(reference);
    for(unsigned kind=0;kind<8;kind++) {
      uint8_t original[64];
      for(unsigned n=0;n<64;n++) original[n]=source[n]=(uint8_t)(n+11);
      memset(destination,0xcc,64);
      volatile uint32_t event=0;
      mtl_sub_begin();
      vk_buffer *src,*dst;uint64_t so,doff;
      CHECK(mtl_addr((uintptr_t)source,64,(void **)&src,&so));
      CHECK(mtl_addr((uintptr_t)destination,64,(void **)&dst,&doff));
      vk_buffer_sync_stats before,after;vk_buffer_sync_snapshot(&before);
      uint64_t published_before=published;
      vk_cpu_copy_stats(&snapshots,&published_before,&fallbacks,&saved);
      if(kind==3||kind==5) {
        vk_barrier();
        vkCmdFillBuffer(V.cmd,kind==3?src->handle:dst->handle,kind==3?so:doff,64,0x77777777);
        vk_buffer_transfer_written();
        vk_buffer_writeback((uintptr_t)(kind==3?source:destination)+(kind==3?0:3),kind==3?64:31);
      }
      mtl_sub_copy((uintptr_t)source+1,(uintptr_t)destination+3,31);
      if(kind==7) {
        // Lazy uploads happen at commit: later CPU edits must reach both the
        // GPU copy and its deferred CPU publication snapshot.
        for(unsigned n=0;n<64;n++) original[n]=source[n]=(uint8_t)(n+73);
      }
      if(kind==4) vk_buffer_writebacks_flush();
      if(kind==1||kind==2||kind==4) {
        vk_barrier();
        int change_source=kind!=2;
        vkCmdFillBuffer(V.cmd,change_source?src->handle:dst->handle,
          change_source?so:doff+16,change_source?64:8,0xaaaaaaaa);
        vk_buffer_transfer_written();
        vk_buffer_writeback((uintptr_t)(change_source?source:destination)+(change_source?0:16),change_source?64:8);
      }
      if(kind==6) {
        vk_buffer *two=vk_temp_buffer(2);memset(two->map,0xaa,2);
        vk_barrier();VkBufferCopy adjacent={.dstOffset=doff+34,.size=2};
        vkCmdCopyBuffer(V.cmd,two->handle,dst->handle,1,&adjacent);
        vk_buffer_transfer_written();vk_buffer_writeback((uintptr_t)destination+34,2);
      }
      mtl_sub_event(&event,0,1);
      CHECK(!event);
      for(unsigned n=0;n<64;n++) CHECK(destination[n]==0xcc);
      CHECK(vk_pool_pending_writes((uintptr_t)destination+3,31));
      uint64_t fence=mtl_last_fence();vk_internal_wait(vk_internal_commit());
      CHECK(mtl_last_fence()==fence&&event==1);
      for(unsigned n=0;n<64;n++) {
        uint8_t expected=n>=3&&n<34?(kind==3?0x77:original[n-2]):0xcc;
        if((kind==2&&n>=16&&n<24)||(kind==6&&n>=34&&n<36)) expected=0xaa;
        if(destination[n]!=expected) die("CPU known copy fixture mode=%u kind=%u byte=%u actual=%u expected=%u",mode,kind,n,destination[n],expected);
        CHECK(destination[n]==expected);
      }
      vk_cpu_copy_stats(&snapshots,&published,&fallbacks,&saved);
      vk_buffer_sync_snapshot(&after);
      int known=!reference&&kind!=2&&kind!=3&&kind!=6;
      CHECK(published-published_before==(known?31:0));
      if(kind==0||kind==5) CHECK(after.readback_bytes-before.readback_bytes==(known?0:31));
    }
    if(!reference) cpu_known_pending_copy_test(source,destination);
  }
  // Preserve the caller's reference/candidate mode for the remaining full suite.
  vk_cpu_copy_reference(initial_reference);
  LOG("[vulkan CPU known copy fixture] passed: deferred publication, byte guards, source/destination GPU overwrite, adjacent invalidation, explicit flush and reference/candidate/reference");
}
// Exercise logical snapshot lifetime independently of the dispatch's own
// scope. The following real compute dispatch checks the sampled bytes too;
// a wrong sRGB recipe installed here changes that independent GPU result.
static void texture_snapshot_scope_test(int descriptor,const tex_t *texture) {
  const view_t srgb={.set_format=1,.format=NVN_FORMAT_RGBA8_SRGB};
  vk_texture_binding_scope_begin();
  vk_texture_prepare_binding(descriptor,0,VK_SHADER_STAGE_COMPUTE_BIT);
  VkImageView plain=vk_texture_view(descriptor,0); // Fresh prepare MISS handoff.
  CHECK(plain);
  // ID reuse/alternate view invalidates the saved descriptor, not merely the
  // physical recipe. Registry growth must never leave a retained vector ptr.
  mtl_reg_tex(descriptor,texture,NULL);
  vk_texture_prepare_binding(descriptor,0,VK_SHADER_STAGE_COMPUTE_BIT);
  mtl_set_tex_pool_n(4096);mtl_reg_tex(2048,texture,NULL);
  mtl_reg_tex(descriptor,texture,&srgb);
  VkImageView formatted=vk_texture_view(descriptor,0);CHECK(formatted!=plain);
  mtl_reg_tex(descriptor,texture,NULL);
  plain=vk_texture_view(descriptor,0);CHECK(plain!=formatted);
  // More prepared MISS descriptors than the bounded direct-map table holds:
  // an overwritten entry must fall back by full identity, never borrow sRGB.
  for(int id=64;id<320;id++)mtl_reg_tex(id,texture,&srgb);
  mtl_reg_tex(descriptor,texture,NULL);
  vk_texture_prepare_binding(descriptor,0,VK_SHADER_STAGE_COMPUTE_BIT);
  for(int id=64;id<320;id++)vk_texture_prepare_binding(id,0,VK_SHADER_STAGE_COMPUTE_BIT);
  CHECK(vk_texture_view(descriptor,0)==plain);
  // A child scope and depth overflow cannot consume or overwrite a parent
  // snapshot. No prepare in the deepest scope must still use the registry.
  mtl_reg_tex(descriptor,texture,NULL);
  vk_texture_prepare_binding(descriptor,0,VK_SHADER_STAGE_COMPUTE_BIT);
  for(unsigned n=0;n<9;n++)vk_texture_binding_scope_begin();
  mtl_reg_tex(descriptor,texture,&srgb);
  CHECK(vk_texture_view(descriptor,0)==formatted);
  for(unsigned n=0;n<9;n++)vk_texture_binding_scope_end();
  CHECK(vk_texture_view(descriptor,0)==formatted);
  vk_texture_binding_scope_end();
  // Reusing the same stack slot without prepare must not see its old frame.
  mtl_reg_tex(descriptor,texture,NULL);
  vk_texture_binding_scope_begin();CHECK(vk_texture_view(descriptor,0)==plain);
  vk_texture_binding_scope_end();
  CHECK(vk_texture_view(descriptor,0)==plain); // Outside all scopes.
  mtl_set_tex_pool_n(64);
}
#ifndef DM_ANDROID_APP
extern unsigned vk_test_texture_registration_index(uint64_t,uint64_t *);
static void texture_registration_retirement_test(void) {
  // Real backend commits advance the existing grace; never edit a frontier.
  for(unsigned n=0;n<9;n++){mtl_sub_begin();CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));}
  tex_t a={.serial=UINT64_C(0x1000000000008000)},b=a,c=a,zero={0},snapshot;
  b.serial+=8192;c.serial+=17;uint64_t bytes_used,epoch;
  enum { FIRST=73,OTHER=74,HIDDEN=65535,REPLACE=333 };
  mtl_set_tex_pool_n(1048576);
  mtl_reg_tex(FIRST,&a,NULL);mtl_reg_tex(OTHER,&a,NULL);mtl_reg_tex(HIDDEN,&a,NULL);
  mtl_reg_tex(REPLACE,&a,NULL);mtl_reg_tex(REPLACE,&b,NULL); // same bucket, new serial
  mtl_reg_tex(REPLACE+1,&c,NULL);mtl_reg_tex(REPLACE+2,&zero,NULL);
  CHECK(vk_test_texture_registration_index(a.serial,&bytes_used)==3);
  CHECK(vk_test_texture_registration_index(b.serial,&bytes_used)==1);
  CHECK(bytes_used==65536+3*2048); // pages 0,1,255; snapshots did not grow
  mtl_reg_tex(REPLACE+1,&a,NULL);CHECK(vk_test_texture_registration_index(a.serial,&bytes_used)==4);
  mtl_reg_tex(REPLACE+1,&c,NULL);CHECK(vk_test_texture_registration_index(a.serial,&bytes_used)==3);
  mtl_reg_tex(REPLACE+2,&b,NULL);CHECK(vk_test_texture_registration_index(b.serial,&bytes_used)==2);
  mtl_reg_tex(REPLACE+2,&zero,NULL);CHECK(vk_test_texture_registration_index(b.serial,&bytes_used)==1);
  mtl_set_tex_pool_n(4); // hidden registrations still retire
  epoch=vk_texture_descriptor_epoch();mtl_tex_release(&a);vk_texture_end_submit();
  CHECK(vk_texture_descriptor_epoch()==epoch+3);
  mtl_set_tex_pool_n(1048576);
  CHECK(!vk_texture_descriptor(FIRST,&snapshot));CHECK(!vk_texture_descriptor(OTHER,&snapshot));
  CHECK(!vk_texture_descriptor(HIDDEN,&snapshot));CHECK(vk_texture_descriptor(REPLACE,&snapshot)&&snapshot.serial==b.serial);
  CHECK(vk_texture_descriptor(REPLACE+1,&snapshot)&&snapshot.serial==c.serial);
  CHECK(vk_texture_descriptor(REPLACE+2,&snapshot)&&!snapshot.serial);
  mtl_reg_tex(REPLACE+2,&zero,NULL); // singleton zero-serial re-registration
  CHECK(vk_test_texture_registration_index(b.serial,&bytes_used)==1);
  // Original retirement also republishes invalid historical slots. Keep that
  // exact epoch behavior while proving serial collision cannot invalidate b.
  epoch=vk_texture_descriptor_epoch();mtl_tex_release(&a);vk_texture_end_submit();
  CHECK(vk_texture_descriptor_epoch()==epoch+3);
  CHECK(vk_test_texture_registration_index(a.serial,&bytes_used)==3);
  mtl_reg_tex(FIRST,&b,NULL);mtl_reg_tex(HIDDEN,&a,NULL); // serial reuse and replacement
  CHECK(vk_test_texture_registration_index(a.serial,&bytes_used)==2);
  CHECK(vk_test_texture_registration_index(b.serial,&bytes_used)==2);
  vk_texture_lookup_scope_begin();CHECK(vk_texture_descriptor(HIDDEN,&snapshot));
  epoch=vk_texture_descriptor_epoch();mtl_tex_release(&a);vk_texture_end_submit();
  CHECK(vk_texture_descriptor_epoch()==epoch+2);CHECK(!vk_texture_descriptor(HIDDEN,&snapshot));
  CHECK(vk_texture_descriptor(FIRST,&snapshot)&&snapshot.serial==b.serial);
  vk_texture_lookup_scope_end();
  // Re-registration repeatedly unlinks head, middle and tail in one bucket.
  for(unsigned n=0;n<257;n++) {
    unsigned id=FIRST+n%3;mtl_reg_tex(id,n%2?&a:&b,NULL);
    CHECK(vk_test_texture_registration_index(a.serial,&bytes_used)>0);
  }
  mtl_set_tex_pool_n(4);
  LOG("[texture registration retirement] collisions, multi-ID, re-registration, hidden limits, invalid history, epoch/scope and sparse pages passed");
}
#endif
static void image_retirement_test(void) {
  if (!V.timeline_semaphore) {
    LOG("[vulkan image retirement fixture] skipped: timeline semaphore unavailable");
    return;
  }
  int saved_template_reference=vk_descriptor_template_reference_mode();
  // The same independently sampled image/sampler/buffer results must agree
  // with both Vulkan update APIs, including complete descriptor cache MISSes.
  for(unsigned mode=0;mode<4;mode++) {
  unsigned reference=mode&1;
  vk_descriptor_template_reference(mode>>1);
  vk_sampler_lookup_reference(reference);
  vk_texture_binding_reference(reference);
  // 真实调用底层提交接口推进宽限计数，不修改任何计数器或游戏内存状态。
  // Real backend submissions advance grace; no counters/game state are patched.
  for (unsigned i = 0; i < 9; i++) {
    mtl_sub_begin();
    CHECK(mtl_wait(mtl_sub_commit(), UINT64_MAX));
  }
  uint32_t images_before = vk_texture_count(), samplers_before = vk_sampler_count();
  enum { descriptor = 37 };
  mtl_set_tex_pool_n(64);
  NVNtextureBuilder builder;
  CALL(nvnTextureBuilderSetDefaults, &builder);
  CALL(nvnTextureBuilderSetDevice, &builder, &device);
  CALL(nvnTextureBuilderSetTarget, &builder, NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetFlags, &builder, NVN_TEXTURE_FLAGS_IMAGE);
  CALL(nvnTextureBuilderSetWidth, &builder, 4);
  CALL(nvnTextureBuilderSetHeight, &builder, 4);
  CALL(nvnTextureBuilderSetFormat, &builder, NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage, &builder, &memory, (3 << 20) + 589824);
  NVNtexture texture;
  CHECK(CALL(nvnTextureInitialize, &texture, &builder));
  mtl_reg_tex(descriptor, (const tex_t *)&texture, NULL);
  NVNsamplerBuilder sampler_builder;
  CALL(nvnSamplerBuilderSetDefaults, &sampler_builder);
  CALL(nvnSamplerBuilderSetDevice, &sampler_builder, &device);
  NVNsampler sampler;
  CHECK(CALL(nvnSamplerInitialize, &sampler, &sampler_builder));
  samp_par params = ((const samp_t *)&sampler)->p;
  mtl_reg_samp(descriptor, &params);
  uint8_t control[0x880]; memset(control, 23, sizeof control);
  NVNshaderData shader = {.control = control};
  NVNprogram program;
  CHECK(CALL(nvnProgramInitialize, &program, &device));
  CHECK(CALL(nvnProgramSetShaders, &program, 1, &shader));
  uint32_t *output = (void *)(bytes + (2 << 20) + 131072);
  memset(output, 0, 256);
  vk_state saved = S;
  uint64_t sampler_hits0,sampler_misses0;int sampler_reference;
  vk_sampler_lookup_stats(&sampler_hits0,&sampler_misses0,&sampler_reference);
  uint64_t view_hits0,prepare_hits0,view_misses0;int view_reference;
  vk_texture_binding_stats(&view_hits0,&prepare_hits0,&view_misses0,&view_reference);
  mtl_sub_begin();
  uint64_t use = vk_recording_serial();
  p_clear_tex clear = {.tex = (const tex_t *)&texture, .r = {0,0,0,4,4,1},
                       .mask = 15, .c.f = {.25f,.5f,.75f,1}};
  vk_texture_clear(&clear);
  texture_snapshot_scope_test(descriptor,(const tex_t *)&texture);
  S.program = (const prog_t *)&program;
  S.tex[5][0] = descriptor;
  S.samp[5][0] = (uint64_t)descriptor << 20;
  S.ssbo[5][0] = (p_bind_buf){.stage = 5, .index = 0,
                             .addr = (uintptr_t)output, .size = 256};
  vk_dispatch(&(p_dispatch){1,1,1});
  // Reuse metadata across actual accesses, then change physical storage and
  // descriptor format in the SAME submission. The earlier dispatch still
  // samples its original physical image behind the GPU gate below.
  vk_texture_prepare_binding(descriptor,0,VK_SHADER_STAGE_COMPUTE_BIT);
  VkImageView first_view=vk_texture_view(descriptor,0);
  CHECK(vk_texture_view(descriptor,0)==first_view);
  vk_texture_view(descriptor,1); // Deferred STORAGE promotion replaces storage.
  VkImageView promoted_view=vk_texture_view(descriptor,0);
  CHECK(promoted_view!=first_view && vk_texture_view(descriptor,0)==promoted_view);
  view_t srgb_view={.set_format=1,.format=NVN_FORMAT_RGBA8_SRGB};
  mtl_reg_tex(descriptor,(const tex_t *)&texture,&srgb_view);
  VkImageView reinterpreted_view=vk_texture_view(descriptor,0);
  CHECK(reinterpreted_view!=promoted_view);
  mtl_reg_tex(descriptor,(const tex_t *)&texture,NULL);
  VkImageView plain_view=vk_texture_view(descriptor,0);
  CHECK(plain_view!=reinterpreted_view);
  mtl_reg_tex(descriptor+1024,(const tex_t *)&texture,NULL); // Grow the registry.
  CHECK(vk_texture_view(descriptor,0)==plain_view);
  VkSampler first_sampler=vk_sampler(descriptor);
  CHECK(vk_sampler(descriptor)==first_sampler);
  mtl_reg_samp(descriptor,&params); // Identical registration preserves the handle.
  CHECK(vk_sampler(descriptor)==first_sampler);
  params.magf=!params.magf;mtl_reg_samp(descriptor,&params);
  VkSampler replacement=vk_sampler(descriptor);
  CHECK(replacement!=first_sampler && vk_sampler(descriptor)==replacement);
  // Registry reallocation and a direct-cache collision cannot leave a stale
  // registry pointer or return another id's handle. The GPU still uses first.
  mtl_reg_samp(descriptor+1024,&params);
  CHECK(vk_sampler(descriptor)==replacement);
  mtl_reg_samp(descriptor+64,&params);
  CHECK(vk_sampler(descriptor+64)!=replacement);
  CHECK(vk_sampler(descriptor)==replacement);
  CHECK(vk_pool_pending_writes((uintptr_t)output, 256));
  VkSemaphoreTypeCreateInfoKHR type = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                 .pNext = &type};
  VkSemaphore gate;
  VK_CHECK(vkCreateSemaphore(V.device, &create, NULL, &gate));
  uint64_t value = 1;
  VkTimelineSemaphoreSubmitInfoKHR timeline = {
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &value};
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo blocker = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline,
      .waitSemaphoreCount = 1, .pWaitSemaphores = &gate, .pWaitDstStageMask = &stage};
  pthread_mutex_lock(&V.queue_mu);
  VK_CHECK(vkQueueSubmit(V.queue, 1, &blocker, VK_NULL_HANDLE));
  pthread_mutex_unlock(&V.queue_mu);
  uint64_t fence = mtl_sub_commit();
  uint64_t sampler_hits,sampler_misses;
  uint64_t view_hits,prepare_hits,view_misses;
  vk_texture_binding_stats(&view_hits,&prepare_hits,&view_misses,&view_reference);
  CHECK(view_reference==(int)reference && view_misses>view_misses0);
  CHECK(reference?view_hits==view_hits0:view_hits>=view_hits0+2);
  CHECK(reference?prepare_hits==prepare_hits0:prepare_hits>prepare_hits0);
  vk_sampler_lookup_stats(&sampler_hits,&sampler_misses,&sampler_reference);
  CHECK(sampler_reference==(int)reference && sampler_misses>sampler_misses0);
  CHECK(reference?sampler_hits==sampler_hits0:sampler_hits>=sampler_hits0+2);
  S = saved;
  vk_pipeline_state_changed();
  CHECK(!mtl_wait(fence, 0));
  CHECK(vk_completed_serial() < use);
  CHECK(vk_pool_pending_writes((uintptr_t)output, 256));
  CHECK(!vk_pool_pending_writes((uintptr_t)output + 256, 1));
  CALL(nvnTextureFinalize, &texture);
  params.magf = !params.magf;
  mtl_reg_samp(descriptor, &params);
  mtl_reg_samp(descriptor+64,&params);
  CHECK(mtl_last_fence() >= nvn_submissions_enqueued() + 8);
  vk_texture_end_submit();
  CHECK(vk_texture_count() == images_before + 1);
  CHECK(vk_sampler_count() == samplers_before + 3);
  VkSemaphoreSignalInfoKHR signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
                                     .semaphore = gate, .value = 1};
  VK_CHECK(V.signal_timeline(V.device, &signal));
  CHECK(mtl_wait(fence, UINT64_MAX));
  CHECK(vk_completed_serial() >= use);
  CHECK(!vk_pool_pending_writes((uintptr_t)output, 256));
  for (unsigned i = 0; i < 64; i++) CHECK(output[i] == 64);
  vk_texture_end_submit();
  CHECK(vk_texture_count() == images_before);
  CHECK(vk_sampler_count() == samplers_before);
  vkDestroySemaphore(V.device, gate, NULL);
  mtl_set_tex_pool_n(4);
  CALL(nvnSamplerFinalize, &sampler);
  CALL(nvnProgramFinalize, &program);
  LOG("[vulkan image retirement fixture] passed: expired grace, pending GPU use, CPU-write metadata, sampled pixels and post-completion release");
  }
  vk_sampler_lookup_reference(0);
  vk_texture_binding_reference(0);
  vk_descriptor_template_reference(saved_template_reference);
  LOG("[vulkan texture binding fixture] cached/reference metadata, same-submit STORAGE/mutable promotion, descriptor format replacement, registry growth and true GPU retirement passed");
  LOG("[vulkan descriptor handoff fixture] prepare MISS, alternate registration, registry growth, >128 collisions, nested/depth overflow, unprepared/outside scopes and independent compute samples passed");
  LOG("[vulkan deferred descriptor payload fixture] combined image/sampler and buffer GPU output agrees for template/plain updates, cached/reference texture paths and pending retirement");
  LOG("[vulkan sampler lookup fixture] reference/cache, same-submit replacement, table growth, collisions and true GPU retirement passed");
}

// A read-only dispatch after a completed GPU clear can use optimal sampling.
// A later clear in the same submission must restore GENERAL without reusing
// the old descriptor; the following submission may use optimal again.
static void compute_sample_layout_test(void) {
  enum { descriptor=37 };mtl_set_tex_pool_n(64);
  NVNtextureBuilder b;NVNtexture texture;NVNsamplerBuilder sb;NVNsampler sampler;
  CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
  CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&b,4);CALL(nvnTextureBuilderSetHeight,&b,4);
  CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&b,&memory,(3<<20)+589824);
  CHECK(CALL(nvnTextureInitialize,&texture,&b));mtl_reg_tex(descriptor,(const tex_t *)&texture,NULL);
  CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_reg_samp(descriptor,&((const samp_t *)&sampler)->p);
  uint8_t control[0x880];memset(control,23,sizeof control);NVNshaderData shader={.control=control};
  NVNprogram program;CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  vk_state saved=S;uint32_t *output=(void *)(bytes+(2<<20)+131072);
  p_clear_tex clear={.tex=(const tex_t *)&texture,.r={0,0,0,4,4,1},.mask=15,.c.f={.25f,.5f,.75f,1}};
  mtl_sub_begin();vk_texture_clear(&clear);CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));mtl_finish();
  uint64_t samples_before,samples_after;int reference;
  vk_compute_layout_stats(&samples_before,&reference);
  for(unsigned pass=0;pass<6;pass++) {
    uint64_t hits_before,prepares_before,misses_before;int binding_reference;
    vk_texture_binding_stats(&hits_before,&prepares_before,&misses_before,&binding_reference);
    mtl_sub_begin();S.program=(const prog_t *)&program;S.tex[5][0]=descriptor;
    S.samp[5][0]=(uint64_t)descriptor<<20;
    S.ssbo[5][0]=(p_bind_buf){.stage=5,.index=0,.addr=(uintptr_t)output,.size=256};
    if(pass==1) { clear.c.f[0]=.5f;vk_texture_clear(&clear); }
    // Two real physical replacements separated by completion leave the
    // logical descriptor unchanged. A numerical VkImage handle match alone
    // must never resurrect a view from either retired allocation.
    if(pass==2) vk_texture_prepare_binding(descriptor,1,VK_SHADER_STAGE_COMPUTE_BIT);
    if(pass==4) {
      view_t srgb={.set_format=1,.format=NVN_FORMAT_RGBA8_SRGB};
      vk_texture_prepare_attachment((const tex_t *)&texture,&srgb);
    }
    vk_dispatch(&(p_dispatch){1,1,1});CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));mtl_finish();
    for(unsigned i=0;i<64;i++) CHECK(output[i]==(pass?128:64));
    uint64_t hits_after,prepares_after,misses_after;
    vk_texture_binding_stats(&hits_after,&prepares_after,&misses_after,&binding_reference);
    // Explicit Finish recycles the completed context. Its immutable view may
    // be reused across real submissions, including a GPU content/layout change;
    // the independent sampled output above must still observe the new clear.
    if(pass && pass!=2 && pass!=4 && !binding_reference) {
      CHECK(hits_after>hits_before);CHECK(misses_after==misses_before);
    }
    if(pass==2 || pass==4) CHECK(misses_after>misses_before);
  }
  vk_compute_layout_stats(&samples_after,&reference);
  uint64_t transitions,safe,fallback;int layout_reference;
  vk_image_layout_stats(&transitions,&safe,&fallback,&layout_reference);
  CHECK(samples_after>samples_before || reference || layout_reference);
  S=saved;vk_pipeline_state_changed();CALL(nvnTextureFinalize,&texture);
  CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);mtl_set_tex_pool_n(4);
  LOG("[vulkan compute sample layout fixture] optimal sampling, same-submission clear fallback and cross-submission immutable view reuse preserve GPU data");
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static void persistent_texel_owner_test(void) {
  mtl_init();
  extern VkBufferView vk_test_texel_view(vk_buffer *,VkFormat,VkDeviceSize,VkDeviceSize);
  extern unsigned vk_test_texel_view_count(const vk_buffer *);
  extern uint64_t vk_test_owned_texel_views_destroyed(void);
  CHECK(V.cmd==VK_NULL_HANDLE);
  int reference=getenv("DM_VULKAN_TEXEL_VIEW_REFERENCE") && !strcmp(getenv("DM_VULKAN_TEXEL_VIEW_REFERENCE"),"1");
  VkDeviceSize stride=V.props.limits.minTexelBufferOffsetAlignment;if(stride<256)stride=256;
  vk_buffer b={0};vk_buffer_create(&b,stride*128,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
  vk_texture_submission *owner=vk_texture_begin(NULL);
  VkBufferView first=vk_test_texel_view(&b,VK_FORMAT_R32_UINT,0,256);
  VkBufferView repeat=vk_test_texel_view(&b,VK_FORMAT_R32_UINT,0,256);
  CHECK(reference?first!=repeat:first==repeat);
  VkBufferView format=vk_test_texel_view(&b,VK_FORMAT_R32_SINT,0,256);
  VkBufferView range=vk_test_texel_view(&b,VK_FORMAT_R32_UINT,0,128);
  CHECK(first!=format && first!=range && format!=range);
  for(unsigned k=1;k<=61;k++)vk_test_texel_view(&b,VK_FORMAT_R32_UINT,k*stride,256);
  CHECK(vk_test_texel_view_count(&b)==(reference?0:64));
  VkBufferView overflow=vk_test_texel_view(&b,VK_FORMAT_R32_UINT,96*stride,256);
  VkBufferView overflow_again=vk_test_texel_view(&b,VK_FORMAT_R32_UINT,96*stride,256);
  CHECK(reference?overflow!=overflow_again:overflow==overflow_again);
  CHECK(vk_test_texel_view_count(&b)==(reference?0:64));
  VkBufferView external;
  VkBufferViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
    .buffer=b.handle,.format=VK_FORMAT_R32_UINT,.offset=100*stride,.range=256};
  VK_CHECK(vkCreateBufferView(V.device,&ci,NULL,&external));vk_texture_own_buffer_view(external);
  uint64_t destroyed=vk_test_owned_texel_views_destroyed();
  vk_texture_end(owner);vk_texture_recycle(owner);
  CHECK(vk_test_owned_texel_views_destroyed()==destroyed);
  owner=vk_texture_begin(owner);
  uint64_t hits,created,created_after;vk_texel_view_stats(&hits,&created);
  repeat=vk_test_texel_view(&b,VK_FORMAT_R32_UINT,0,256);
  vk_texel_view_stats(&hits,&created_after);
  CHECK(reference?created_after==created+1:(repeat==first && created_after==created));
  vk_texture_end(owner);vk_texture_destroy(owner);
  // The only production by-value move is a fresh whole-import Buffer (no views
  // yet); preserve ownership even if a fully constructed owner is moved once.
  vk_buffer moved=b;memset(&b,0,sizeof b);vk_buffer_destroy(&moved);
  CHECK(!moved.handle && !moved.texel_views);
  CHECK(vk_test_owned_texel_views_destroyed()==destroyed+(reference?0:64));
  vk_buffer_create(&b,stride*128,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
  CHECK(!b.texel_views);owner=vk_texture_begin(NULL);
  vk_texel_view_stats(&hits,&created);vk_test_texel_view(&b,VK_FORMAT_R32_UINT,0,256);
  vk_texel_view_stats(&hits,&created_after);CHECK(created_after==created+1);
  CHECK(vk_test_texel_view_count(&b)==(reference?0:1));
  vk_texture_end(owner);vk_texture_destroy(owner);vk_buffer_destroy(&b);
  CHECK(!b.handle && !b.texel_views && vk_validation_error_count()==0);
  LOG("[persistent texel owner fixture] reference=%d cross-submission identity, exact keys, cap64 transient fallback, external views, move/destroy/address reuse passed",reference);
}
#endif

// 经原 NVN 纹理描述符和绑定路径创建临时 texel view，再验真实在途独立回收。
// Create transient texel views through original NVN registration/binding, then
// exercise independent ownership with actual GPU submissions in flight.
static void texel_submission_test(void) {
  uint32_t *input = (void *)(bytes + (3 << 20) + 458752);
  uint32_t *output = (void *)(bytes + (2 << 20) + 65536);
  for (unsigned i = 0; i < 64; i++) { input[i] = i * 13 + 5; output[i] = 0; }
  NVNtextureBuilder builder;
  CALL(nvnTextureBuilderSetDefaults, &builder);
  CALL(nvnTextureBuilderSetDevice, &builder, &device);
  CALL(nvnTextureBuilderSetTarget, &builder, NVN_TEXTURE_TARGET_BUFFER);
  CALL(nvnTextureBuilderSetWidth, &builder, 64);
  CALL(nvnTextureBuilderSetFormat, &builder, NVN_FORMAT_R32UI);
  CALL(nvnTextureBuilderSetStorage, &builder, &memory, (uint8_t *)input - bytes);
  NVNtexture texture;
  CHECK(CALL(nvnTextureInitialize, &texture, &builder));
  NVNtexturePool pool;
  CHECK(CALL(nvnTexturePoolInitialize, &pool, &memory, (3 << 20) + 24576, 4));
  CALL(nvnTexturePoolRegisterTexture, &pool, 1, &texture, NULL);
  uint8_t control[0x880]; memset(control, 22, sizeof control);
  NVNshaderData shader = {.control = control};
  NVNprogram program;
  CHECK(CALL(nvnProgramInitialize, &program, &device));
  CHECK(CALL(nvnProgramSetShaders, &program, 1, &shader));
  CALL(nvnCommandBufferBeginRecording, &cmd);
  CALL(nvnCommandBufferSetTexturePool, &cmd, &pool);
  CALL(nvnCommandBufferBindProgram, &cmd, &program, NVN_SHADER_STAGE_COMPUTE_BIT);
  NVNseparateTextureHandle handle = {.value = 1};
  CALL(nvnCommandBufferBindSeparateTextures, &cmd, NVN_SHADER_STAGE_COMPUTE, 0, 1, &handle);
  CALL(nvnCommandBufferBindStorageBuffer, &cmd, NVN_SHADER_STAGE_COMPUTE, 0,
       (uintptr_t)output, 256);
  CALL(nvnCommandBufferDispatchCompute, &cmd, 1, 1, 1);
  submit(end());
  for (unsigned i = 0; i < 64; i++) CHECK(output[i] == input[i] * 3 + 7);
  descriptor_inflight_test((const prog_t *)&program, 1,6,0);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  descriptor_inflight_test((const prog_t *)&program,1,6,1);
#endif
  CALL(nvnProgramFinalize, &program);
  CALL(nvnTextureFinalize, &texture);
  CALL(nvnTexturePoolFinalize, &pool);
}
