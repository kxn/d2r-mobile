#ifdef DM_VULKAN_SHARED_POOLS
// Shared sources are not snapshotted into an extra whole-Pool mirror. Prepare
// exactly the original upload staging, then keep the short CPU observation
// phase across its deswizzle/decode. No renderer/driver calls while it is held.
static void image_upload_observed_contents(image *i) {
  const tex_t *t=&i->tex;
  if(t->d.samples>1 || i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)
    die("Vulkan packaged depth/MS image unsupported");
  i->checked_dirty_epoch=0;i->checked_dirty_range=(nvn_dirty_range_proof){0};
  // Render/pass replay is finished before the CPU phase. Later GPU commands
  // use the original physical image and staging lifetimes after phase end.
  image_general_transfer(i);
  image_upload_plan plan=image_upload_plan_create(i);
  int ldr=V.astc_decode_mode && !i->external_access && !i->decoded && astc_unorm_format(i->format) &&
    i->description.imageType==VK_IMAGE_TYPE_2D && i->layers==1 && i->extent.depth==1;
  image_astc_proof(i,0);
  vk_shared_cpu_observation observation;
  if(!vk_pool_cpu_observe_begin((uintptr_t)t->mem,t->L.size,&observation))
    die("Shared CPU conversion missing page-level ordered preflight");
  uint64_t stamp=observation.stamp;
  ldr=image_upload_plan_convert(i,&plan,ldr);
  // Pages are still RO. End releases waiting kernel/user writes; all of them
  // receive a generation AFTER captured stamp. Never protect again afterwards
  // or assign a later global-now stamp which could swallow those new writes.
  vk_pool_cpu_observe_end(&observation);
  image_upload_plan_record(i,&plan);
  image_upload_plan_free(&plan);
  if(i->initialized)reuploads++;else uploads++;
  i->stamp=stamp;i->initialized=1;image_rebuildable_set(i,!i->external_access);
  if(i->cpu_rebuildable)image_shared_alias_remove(i);
  image_astc_proof(i,ldr);
}
#endif
