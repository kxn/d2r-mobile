// Owned private-metadata oracle. Captures the real cached_view creation chain;
// it does not advertise unsupported hardware or execute fake Vulkan objects.
#ifdef DM_VULKAN_TEST_EMBED
static unsigned astc_test_views,astc_test_decode;
static VkResult VKAPI_CALL astc_test_create_view(VkDevice device,
    const VkImageViewCreateInfo *create,const VkAllocationCallbacks *allocator,VkImageView *view) {
  (void)device;(void)allocator;
  const VkBaseInStructure *next=create->pNext;
  astc_test_decode=0;
  if(next && next->sType==VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT) {
    const VkImageViewASTCDecodeModeEXT *decode=(const void *)next;
    if(decode->decodeMode!=VK_FORMAT_R8G8B8A8_UNORM)die("ASTC contract wrong decode mode");
    astc_test_decode=1;next=next->pNext;
  }
  if(!next || next->sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO || next->pNext)
    die("ASTC contract lost usage pNext");
  *view=(VkImageView)(uintptr_t)++astc_test_views;
  return VK_SUCCESS;
}
unsigned vk_texture_test_astc_contract(void) {
  PFN_vkCreateImageView original=vkCreateImageView;
  int supported=V.astc_decode_mode;
  vkCreateImageView=astc_test_create_view;V.astc_decode_mode=1;
  image_access access[3]={0};
  image i={.format=VK_FORMAT_ASTC_4x4_UNORM_BLOCK,.levels=1,.layers=1,
      .aspect=VK_IMAGE_ASPECT_COLOR_BIT,.access=access};
  VkImageViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .format=i.format,.viewType=VK_IMAGE_VIEW_TYPE_2D,
      .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  VkImageView ordinary=cached_view(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT);
  if(astc_test_decode)die("ASTC unknown content got decode8");
  uint64_t generation=image_recipe_generation;
  image_astc_proof(&i,1);
  if(generation==image_recipe_generation)die("ASTC proof failed recipe invalidation");
  VkImageView low=cached_view(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT);
  if(!astc_test_decode || low==ordinary || i.views->description.pNext)
    die("ASTC view key or pNext persistence");
  if(cached_view(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT)!=low)
    die("ASTC immutable view not reused");
  VkAccessFlags writers[]={VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_MEMORY_WRITE_BIT};
  for(unsigned n=0;n<sizeof writers/sizeof *writers;n++) {
    image_astc_proof(&i,1);generation=image_recipe_generation;
    image_record(&i,ci.subresourceRange,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,writers[n],0);
    if(i.astc_ldr_proven || generation==image_recipe_generation ||
        cached_view(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT)!=ordinary)
      die("ASTC GPU writer retained stale decode8");
  }
  image_astc_proof(&i,1);
  image_record(&i,ci.subresourceRange,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,1);
  if(!i.astc_ldr_proven)die("ASTC readonly access revoked proof");
  ci.format=VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
  if(image_astc_decode8(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT))die("ASTC sRGB mode");
  ci.format=i.format;
  if(image_astc_decode8(&i,&ci,VK_IMAGE_USAGE_STORAGE_BIT))die("ASTC storage mode");
  V.astc_decode_mode=0;
  if(cached_view(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT)!=ordinary)die("ASTC unsupported reused decode8");
  V.astc_decode_mode=1;
  image_unknown(&i);image_astc_proof(&i,1);
  if(i.astc_ldr_proven || image_astc_decode8(&i,&ci,VK_IMAGE_USAGE_SAMPLED_BIT))
    die("ASTC escaped image reacquired proof");
  // Both views remain alive across certificate changes. No GPU wait, resource
  // deletion or lifetime replacement is performed by the production toggle.
  unsigned count=0;
  while(i.views) {image_view *next=i.views->next;count++;free(i.views);i.views=next;}
  if(count!=2)die("ASTC unnecessary view recreation");
  vkCreateImageView=original;V.astc_decode_mode=supported;
  return 17;
}
#endif
