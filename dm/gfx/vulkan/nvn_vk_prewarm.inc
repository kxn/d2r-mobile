// Exact Vulkan recipes, built only before the first command submission by the
// recording owner. No background compiler races the active scene or shader
// registry. Driver binary persistence remains on its existing I/O worker.
static _Atomic uint64_t prewarm_warmed,prewarm_rejected,prewarm_hits;
static unsigned prewarm_started;
void vk_prewarm_note_hit(void) { atomic_fetch_add(&prewarm_hits,1); }
void vk_prewarm_stats(uint64_t *recorded,uint64_t *warmed,uint64_t *rejected,uint64_t *hits) {
  *recorded=vk_prewarm_record_count();*warmed=atomic_load(&prewarm_warmed);
  *rejected=atomic_load(&prewarm_rejected)+vk_prewarm_store_rejected();*hits=atomic_load(&prewarm_hits);
}
static int recipe_word(uint32_t *words,unsigned *offset,void *field,int read) {
  if(*offset==VK_PREWARM_WORDS)return 0;
  if(read)memcpy(field,words+*offset,4);else memcpy(words+*offset,field,4);
  (*offset)++;return 1;
}
// Explicit scalar codec: never serialize VkShaderModule, pNext, pSampleMask,
// padding, VkPipelineLayout or any other process-specific representation.
static int pipeline_recipe_codec(pipeline_key *k,uint32_t *words,unsigned *used,int read) {
  unsigned n=0;
#define P(field) do { _Static_assert(sizeof(k->field)==4,"recipe scalar"); if(!recipe_word(words,&n,&k->field,read))return 0; } while(0)
  P(specialization_word);P(specialization_mask);P(dynamic_uniforms);
  for(unsigned i=0;i<9;i++)P(formats[i]);
  for(unsigned i=0;i<8;i++){P(colors[i].attachment);P(colors[i].layout);}
  P(depth.attachment);P(depth.layout);
  for(unsigned i=0;i<16;i++){P(streams[i].binding);P(streams[i].stride);P(streams[i].inputRate);P(divisors[i]);}
  for(unsigned i=0;i<16;i++){P(attributes[i].location);P(attributes[i].binding);P(attributes[i].format);P(attributes[i].offset);}
  P(raster.flags);P(raster.depthClampEnable);P(raster.rasterizerDiscardEnable);P(raster.polygonMode);
  P(raster.cullMode);P(raster.frontFace);P(raster.depthBiasEnable);P(raster.depthBiasConstantFactor);
  P(raster.depthBiasClamp);P(raster.depthBiasSlopeFactor);P(raster.lineWidth);
  P(multisample.flags);P(multisample.rasterizationSamples);P(multisample.sampleShadingEnable);
  P(multisample.minSampleShading);P(multisample.alphaToCoverageEnable);P(multisample.alphaToOneEnable);
  P(depth_stencil.flags);P(depth_stencil.depthTestEnable);P(depth_stencil.depthWriteEnable);
  P(depth_stencil.depthCompareOp);P(depth_stencil.depthBoundsTestEnable);P(depth_stencil.stencilTestEnable);
#define STENCIL(side) P(depth_stencil.side.failOp);P(depth_stencil.side.passOp);P(depth_stencil.side.depthFailOp);P(depth_stencil.side.compareOp);P(depth_stencil.side.compareMask);P(depth_stencil.side.writeMask);P(depth_stencil.side.reference)
  STENCIL(front);STENCIL(back);
#undef STENCIL
  P(depth_stencil.minDepthBounds);P(depth_stencil.maxDepthBounds);
  for(unsigned i=0;i<8;i++) {
    P(blends[i].blendEnable);P(blends[i].srcColorBlendFactor);P(blends[i].dstColorBlendFactor);
    P(blends[i].colorBlendOp);P(blends[i].srcAlphaBlendFactor);P(blends[i].dstAlphaBlendFactor);
    P(blends[i].alphaBlendOp);P(blends[i].colorWriteMask);
  }
  for(unsigned i=0;i<4;i++)P(blend_constants[i]);
  P(targets);P(attachments);P(streams_count);P(attributes_count);P(topology);P(has_depth);P(logic_enable);P(logic_op);
#undef P
  if(read) {
    if(*used!=n)return 0;
    for(unsigned i=n;i<VK_PREWARM_WORDS;i++)if(words[i])return 0;
  } else *used=n;
  return 1;
}
static int recipe_ref(VkAttachmentReference ref,unsigned count,int depth) {
  if(ref.attachment!=VK_ATTACHMENT_UNUSED && ref.attachment>=count)return 0;
  return (ref.attachment==VK_ATTACHMENT_UNUSED&&ref.layout==VK_IMAGE_LAYOUT_UNDEFINED) || ref.layout==VK_IMAGE_LAYOUT_GENERAL ||
    ref.layout==(depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) ||
    (depth&&ref.layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}
static int pipeline_recipe_valid(const pipeline_key *k) {
  if(k->targets>8 || k->attachments>9 || !k->attachments || k->has_depth>1 ||
     k->streams_count>16 || k->attributes_count>16 || k->dynamic_uniforms>2 ||
     k->topology>VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN || k->logic_enable>1 || k->logic_op>15 ||
     (k->logic_enable&&!V.features.logicOp))return 0;
  VkSampleCountFlags samples=k->multisample.rasterizationSamples;
  if(!samples || (samples&(samples-1)) || samples>VK_SAMPLE_COUNT_64_BIT ||
     (k->targets&&!(samples&V.props.limits.framebufferColorSampleCounts)) ||
     (k->has_depth&&!(samples&V.props.limits.framebufferDepthSampleCounts)))return 0;
  if(k->raster.flags || k->raster.depthClampEnable || k->raster.rasterizerDiscardEnable ||
     k->raster.polygonMode>VK_POLYGON_MODE_POINT || k->raster.cullMode>3 || k->raster.frontFace>1 ||
     k->raster.depthBiasEnable>1 || k->raster.lineWidth!=1 ||
     (k->raster.polygonMode!=VK_POLYGON_MODE_FILL&&!V.features.fillModeNonSolid) ||
     k->multisample.flags || k->multisample.sampleShadingEnable || k->multisample.alphaToOneEnable ||
     k->multisample.alphaToCoverageEnable>1)return 0;
  const VkPipelineDepthStencilStateCreateInfo *s=&k->depth_stencil;
  if(s->flags || s->depthTestEnable>1 || s->depthWriteEnable>1 || s->depthCompareOp>7 ||
     s->depthBoundsTestEnable || s->stencilTestEnable>1)return 0;
  const VkStencilOpState *stencil[2]={&s->front,&s->back};
  for(unsigned i=0;i<2;i++)if(stencil[i]->failOp>7 || stencil[i]->passOp>7 ||
    stencil[i]->depthFailOp>7 || stencil[i]->compareOp>7)return 0;
  for(unsigned i=0;i<k->attachments;i++)if(!k->formats[i] || k->formats[i]>VK_FORMAT_ASTC_12x12_SRGB_BLOCK)return 0;
  for(unsigned i=k->attachments;i<9;i++)if(k->formats[i])return 0;
  if(k->has_depth) { if(!recipe_ref(k->depth,k->attachments,1))return 0; }
  else if(k->depth.attachment || k->depth.layout)return 0; // original unused key is zero
  for(unsigned i=0;i<k->targets;i++) {
    const VkPipelineColorBlendAttachmentState *b=&k->blends[i];
    if(!recipe_ref(k->colors[i],k->attachments,0) || b->blendEnable>1 ||
       b->srcColorBlendFactor>VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA ||
       b->dstColorBlendFactor>VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA ||
       b->srcAlphaBlendFactor>VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA ||
       b->dstAlphaBlendFactor>VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA ||
       b->colorBlendOp>VK_BLEND_OP_MAX || b->alphaBlendOp>VK_BLEND_OP_MAX || b->colorWriteMask>15)return 0;
  }
  unsigned bindings=0,locations=0;
  for(unsigned i=0;i<k->streams_count;i++) {
    const VkVertexInputBindingDescription *b=&k->streams[i];
    if(b->binding>=16 || (bindings&(1u<<b->binding)) || b->inputRate>1 ||
       b->stride>V.props.limits.maxVertexInputBindingStride || k->divisors[b->binding]>V.max_vertex_divisor)return 0;
    bindings|=1u<<b->binding;
  }
  for(unsigned binding=0;binding<16;binding++)if(k->divisors[binding]) {
    int found=0;
    for(unsigned i=0;i<k->streams_count;i++)if(k->streams[i].binding==binding)
      found=k->streams[i].inputRate==VK_VERTEX_INPUT_RATE_INSTANCE;
    if(!found || k->divisors[binding]<2)return 0;
  }
  for(unsigned i=0;i<k->attributes_count;i++) {
    const VkVertexInputAttributeDescription *a=&k->attributes[i];
    if(a->location>=16 || (locations&(1u<<a->location)) || a->binding>=16 || !(bindings&(1u<<a->binding)) ||
       a->offset>V.props.limits.maxVertexInputAttributeOffset || !a->format || a->format>VK_FORMAT_ASTC_12x12_SRGB_BLOCK)return 0;
    VkFormatProperties props;vk_format_properties(a->format,&props);
    if(!(props.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))return 0;
    locations|=1u<<a->location;
  }
  return 1;
}
static void pipeline_recipe_record(const pipeline_key *key,vk_shader *const shaders_[6]) {
  if(key->multisample.pSampleMask || key->raster.pNext || key->multisample.pNext || key->depth_stencil.pNext)return;
  vk_prewarm_recipe recipe={.kind=1};
  pipeline_key copy=*key;
  if(!pipeline_recipe_codec(&copy,recipe.words,&recipe.used,0))return;
  for(unsigned i=0;i<5;i++)if(!vk_shader_prewarm_stage(shaders_[i],key->modules[i],&recipe.stages[i]))return;
  vk_prewarm_record(&recipe);
}
static int pipeline_recipe_build(const vk_prewarm_recipe *recipe) {
  pipeline_key key={0};
  uint32_t words[VK_PREWARM_WORDS];memcpy(words,recipe->words,sizeof words);unsigned used=recipe->used;
  if(!pipeline_recipe_codec(&key,words,&used,1) || !pipeline_recipe_valid(&key))return 0;
  vk_shader *list[6]={0};
  VkPipelineShaderStageCreateInfo stages[5];unsigned ns=0;
  const vk_prewarm_stage absent={0};
  if(memcmp(&recipe->stages[5],&absent,sizeof absent))return 0;
  for(unsigned i=0;i<5;i++) {
    const vk_prewarm_stage *stage=&recipe->stages[i];
    if(!stage->module) { if(memcmp(stage,&absent,sizeof absent))return 0;continue; }
    if(i>=2)return 0; // normal vk_draw does not implement geometry/tessellation
    list[i]=vk_shader_prewarm_load(stage,&key.modules[i]);
    if(!list[i] || list[i]->stage!=i || (i!=1&&stage->module!=1))return 0;
    stages[ns++]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage=vk_stage(i),.module=key.modules[i],.pName="main"};
  }
  if(!list[0])return 0;
  if(vk_shader_quad_readonly_stages_error(list))return 0;
  if(list[1] && list[1]->quad_transfer &&
     vk_shader_quad_transfer_error(list[1],key.multisample.rasterizationSamples,
       key.topology,key.raster.polygonMode))return 0;
  uint32_t values[2]={key.specialization_word,key.specialization_mask};
  VkSpecializationMapEntry entries[2]={{.constantID=list[1]?list[1]->specialization_id:0,.offset=0,.size=4},
    {.constantID=list[1]&&list[1]->predicate?list[1]->predicate->id:0,.offset=4,.size=4}};
  unsigned special=recipe->stages[1].module;
  if(special<2 && (values[0]||values[1]))return 0;
  if(special!=3 && values[1])return 0;
  VkSpecializationInfo spec={.mapEntryCount=special==3?2:1,.pMapEntries=entries,.dataSize=special==3?8:4,.pData=values};
  for(unsigned i=0;i<ns;i++)if(stages[i].stage==VK_SHADER_STAGE_FRAGMENT_BIT && special>=2)stages[i].pSpecializationInfo=&spec;
  unsigned bucket=pipeline_bucket(&key,0);
  for(cached_pipeline *p=pipelines[0][bucket];p;p=p->next[0])if(!memcmp(&key,&p->key,sizeof key))return 1;
  VkAttachmentDescription attachments[9];
  for(unsigned i=0;i<key.attachments;i++)attachments[i]=(VkAttachmentDescription){.format=key.formats[i],
    .samples=key.multisample.rasterizationSamples,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
  VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount=key.targets,.pColorAttachments=key.colors,.pDepthStencilAttachment=key.has_depth?&key.depth:NULL};
  VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=key.attachments,
    .pAttachments=attachments,.subpassCount=1,.pSubpasses=&subpass};
  VkRenderPass pass=VK_NULL_HANDLE;if(vkCreateRenderPass(V.device,&rp,NULL,&pass)!=VK_SUCCESS)return 0;
  VkVertexInputBindingDivisorDescriptionEXT divisors[16];unsigned nd=0;
  for(unsigned i=0;i<16;i++)if(key.divisors[i])divisors[nd++]=(VkVertexInputBindingDivisorDescriptionEXT){.binding=i,.divisor=key.divisors[i]};
  VkPipelineVertexInputDivisorStateCreateInfoEXT divisor={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,
    .vertexBindingDivisorCount=nd,.pVertexBindingDivisors=divisors};
  VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pNext=nd?&divisor:NULL,.vertexBindingDescriptionCount=key.streams_count,.pVertexBindingDescriptions=key.streams,
    .vertexAttributeDescriptionCount=key.attributes_count,.pVertexAttributeDescriptions=key.attributes};
  VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=key.topology};
  VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
  VkPipelineRasterizationStateCreateInfo raster=key.raster;raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  VkPipelineMultisampleStateCreateInfo ms=key.multisample;ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  VkPipelineDepthStencilStateCreateInfo ds=key.depth_stencil;ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.logicOpEnable=key.logic_enable,
    .logicOp=key.logic_op,.attachmentCount=key.targets,.pAttachments=key.blends};
  memcpy(blend.blendConstants,key.blend_constants,sizeof key.blend_constants);
  const VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS};
  VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=3,.pDynamicStates=states};
  VkPipelineLayout layout=vk_program_layout_mode(list,key.dynamic_uniforms);
  VkGraphicsPipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=ns,.pStages=stages,
    .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,
    .pDepthStencilState=&ds,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=layout,.renderPass=pass};
  VkPipeline pipeline=VK_NULL_HANDLE;
  vk_pipeline_cache_begin();VkResult result=vkCreateGraphicsPipelines(V.device,V.pipeline_cache,1,&ci,NULL,&pipeline);
  vk_pipeline_cache_end(result==VK_SUCCESS);vkDestroyRenderPass(V.device,pass,NULL);
  if(result!=VK_SUCCESS){if(pipeline)vkDestroyPipeline(V.device,pipeline,NULL);return 0;}
  cached_pipeline *cached=calloc(1,sizeof *cached);
  if(!cached){vkDestroyPipeline(V.device,pipeline,NULL);return 0;}
  cached->key=key;cached->pipeline=pipeline;cached->layout=layout;cached->warm_origin=1;
  for(unsigned mode=0;mode<2;mode++) { unsigned b=pipeline_bucket(&key,mode);cached->next[mode]=pipelines[mode][b];pipelines[mode][b]=cached; }
  pipeline_count++;return 1;
}
void vk_graphics_prewarm(void) {
  if(prewarm_started)return;
  prewarm_started=1;
  vk_prewarm_recipe *recipes=calloc(VK_PREWARM_LIMIT,sizeof *recipes);if(!recipes)return;
  size_t count=vk_prewarm_read(recipes,VK_PREWARM_LIMIT);
  int trace=vk_system_trace_begin("Vulkan startup PSO warmup");
  for(size_t i=0;i<count;i++) {
    const vk_prewarm_recipe *p=&recipes[i];int ok=0;
    if(p->kind==1)ok=pipeline_recipe_build(p);
    else if(p->kind==2 && !p->used) {
      const vk_prewarm_recipe canonical={.kind=2,.stages={[5]=p->stages[5]}};
      if(!memcmp(p,&canonical,sizeof canonical) && p->stages[5].module==1) {
        VkShaderModule module;vk_shader *s=vk_shader_prewarm_load(&p->stages[5],&module);
        if(s&&s->stage==5 && vk_compute_prewarm(s)){s->compute_warm=1;ok=1;}
      }
    }
    if(ok)atomic_fetch_add(&prewarm_warmed,1);
    else { atomic_fetch_add(&prewarm_rejected,1);vk_prewarm_forget(p); }
  }
  vk_system_trace_end(trace);free(recipes);
  LOG("[vulkan PSO warmup] loaded=%zu warmed=%llu rejected=%llu (startup owner; unseen recipes compile normally)",count,
    (unsigned long long)atomic_load(&prewarm_warmed),(unsigned long long)(atomic_load(&prewarm_rejected)+vk_prewarm_store_rejected()));
}
