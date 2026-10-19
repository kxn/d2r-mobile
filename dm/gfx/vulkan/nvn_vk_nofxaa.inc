// Optional counterpart of upstream DM_NVN_NOFXAA. Default OFF, as in Metal.
// Preserve the original draw/UV/attachments and replace only its edge filter
// with the same center sample. No texture-ID forwarding or game binary patch.
#include "nvn_vk_nofxaa_spv.h"
static atomic_int no_fxaa_requested=-1,no_fxaa_active;
static atomic_uint_fast64_t no_fxaa_replacements;
static VkShaderModule no_fxaa_module;
static char no_fxaa_fixture_key[17];
void vk_nofxaa_enabled(int enabled) { atomic_store(&no_fxaa_requested,!!enabled); }
void vk_nofxaa_stats(uint64_t *replacements,int *enabled) {
  *replacements=atomic_load(&no_fxaa_replacements);*enabled=atomic_load(&no_fxaa_active);
}
void vk_nofxaa_test_fact(const char *key) {
  if(key && strlen(key)!=16) die("Vulkan no-FXAA fixture key");
  snprintf(no_fxaa_fixture_key,sizeof no_fxaa_fixture_key,"%s",key?key:"");
}
static VkShaderModule no_fxaa_fragment(vk_shader *fragment) {
  if(!fragment || (strcmp(fragment->key,"ebcfcd1fa0a77dbc") &&
      (!no_fxaa_fixture_key[0] || strcmp(fragment->key,no_fxaa_fixture_key))))
    return fragment?fragment->module:VK_NULL_HANDLE;
  int wanted=atomic_load(&no_fxaa_requested);
  if(wanted<0) {
    const char *v=getenv("DM_NVN_NOFXAA");
    wanted=v && strcmp(v,"0");atomic_store(&no_fxaa_requested,wanted);
  }
  if(wanted!=atomic_load(&no_fxaa_active)) {
    atomic_store(&no_fxaa_active,wanted);
    // Only the recording thread updates the prepared-pipeline epoch. UI
    // requests cannot race on S or switch a module in an already recorded draw.
    vk_pipeline_state_changed();
  }
  if(!wanted) return fragment->module;
  unsigned inputs=0;
  for(unsigned k=0;k<fragment->nb;k++) {
    const vk_binding *b=&fragment->bindings[k];
    if(vk_binding_writable(b) || b->kind==4 || b->kind==9) return fragment->module;
    if(b->kind==3) {
      if(b->binding!=1 || b->count!=1 || (b->type&255)!=3) return fragment->module;
      inputs++;
    } else if(b->kind==5 || b->kind==6 || b->kind==7) return fragment->module;
  }
  if(inputs!=1) return fragment->module;
  if(!no_fxaa_module) {
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=sizeof dm_nofxaa_spv,.pCode=dm_nofxaa_spv};
    VK_CHECK(vkCreateShaderModule(V.device,&info,NULL,&no_fxaa_module));
  }
  atomic_fetch_add(&no_fxaa_replacements,1);
  return no_fxaa_module;
}
