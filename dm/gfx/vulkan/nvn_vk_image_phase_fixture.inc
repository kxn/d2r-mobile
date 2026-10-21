// Owned finite history-equivalence oracle. No GPU timing/counters and no APP
// code. The reference is the former complete algorithm, not a new renderer.
static void image_phase_record_reference(void) {
  image_record_raster=0;
  for(size_t k=0;k<image_binding_count;k++) {
    image_request *q=&image_bindings[k];
    if(image_writes(q->access))image_record_normalized(q->owner,q->range,0,q->access,0);
  }
  image_record_merging=1;
  for(unsigned writing=0;writing<2;writing++)for(size_t k=0;k<image_binding_count;k++) {
    image_request *q=&image_bindings[k];
    if(!!image_writes(q->access)==writing) {
      image_record_raster=q->attachment && vk_render_active()?V.passes:0;
      image_record_normalized(q->owner,q->range,q->stages,q->access,1);
    }
  }
  image_binding_count=0;image_binding_active--;image_record_merging=0;image_record_raster=0;
}
unsigned vk_test_image_phase_artifact(void) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(vk_pool_shared_active() || vk_legacy_astc_copy_mode())die("Owned image phase oracle requires ordinary mode");
#endif
  if(image_binding_active || image_record_merging || image_record_preserve_readers)
    die("Owned image phase oracle needs idle batches");
  enum {N=6,CELLS=18};unsigned checks=0,fast=0,fallback=0;
  uint64_t saved_clock=image_write_clock;
  image owners[2],expected[2];image_access cells[2][CELLS],expected_cells[2][CELLS];
  for(unsigned topology=0;topology<4;topology++)for(unsigned mask=0;mask<(1u<<N);mask++) {
    uint64_t expected_clock=0;int expected_partition=1,seen_write=0;
    for(unsigned n=0;n<N;n++) {
      if(mask&(1u<<n))seen_write=1;else if(seen_write)expected_partition=0;
    }
    for(unsigned reference=0;reference<2;reference++) {
      memset(owners,0,sizeof owners);memset(cells,0,sizeof cells);
      for(unsigned o=0;o<2;o++) {
        owners[o].levels=3;owners[o].layers=2;owners[o].aspect=VK_IMAGE_ASPECT_COLOR_BIT;
        owners[o].tex.d.target=NVN_TEXTURE_TARGET_2D_ARRAY;owners[o].access=cells[o];
        owners[o].access_generation=7;owners[o].zero_mips=7;
        for(unsigned c=0;c<CELLS;c++)cells[o][c]=(image_access){
          .writers=VK_PIPELINE_STAGE_TRANSFER_BIT,.readers=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          .writes=VK_ACCESS_TRANSFER_WRITE_BIT,.visible=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
      }
      image_write_clock=saved_clock;vk_texture_shader_begin();
      for(unsigned n=0;n<N;n++) {
        int writing=!!(mask&(1u<<n));
        image *i=&owners[topology&1?n%2:0];
        VkImageSubresourceRange r={VK_IMAGE_ASPECT_COLOR_BIT,
          topology&2?n%3:0,topology&2?1:3,topology&1?n%2:0,topology&1?1:2};
        VkPipelineStageFlags stage=(n%3==0)?VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:
          (n%3==1)?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        image_request_add_normalized(i,r,stage,VK_ACCESS_SHADER_READ_BIT|(writing?VK_ACCESS_SHADER_WRITE_BIT:0));
        image_bindings[n].attachment=writing && !(n&1);
      }
      if(image_batch_current.writer_suffix!=expected_partition)die("Owned image phase classification mismatch");
      checks++;
      if(!reference) {
        if(expected_partition)fast++;else fallback++;
        vk_texture_shader_record();memcpy(expected,owners,sizeof expected);
        memcpy(expected_cells,cells,sizeof expected_cells);expected_clock=image_write_clock;
      } else {
        image_phase_record_reference();
        if(memcmp(owners,expected,sizeof owners) || memcmp(cells,expected_cells,sizeof cells) ||
            expected_clock!=image_write_clock)die("Owned image phase history differs from complete original record");
        checks++;
      }
      if(image_binding_active || image_binding_count || image_record_merging || image_record_raster)
        die("Owned image phase failed to close original scope");
      checks++;
    }
  }
  // Nested/abandoned batches may not change an outer partition proof.
  vk_texture_shader_begin();image_batch_current.writer_begin=3;image_batch_current.writer_suffix=0;
  vk_texture_shader_begin();if(image_batch_current.writer_begin!=SIZE_MAX || !image_batch_current.writer_suffix)
    die("Owned nested image phase not fresh");
  vk_texture_shader_abandon();if(image_batch_current.writer_begin!=3 || image_batch_current.writer_suffix)
    die("Owned nested image phase overwrote parent");
  vk_texture_shader_abandon();checks+=3;image_write_clock=saved_clock;
  if(fast!=28 || fallback!=228)die("Owned image phase fast/fallback coverage missing");
  return checks;
}
