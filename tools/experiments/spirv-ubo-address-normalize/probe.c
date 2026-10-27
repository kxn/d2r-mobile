/* Independent graphics-pipeline compilation only. No draw, queue submit,
 * sampler, timer, benchmark or game module. Compiler text is Mesa's own output. */
#include "volk.h"
#include "adrenotools/driver.h"
#include "stderr_redirect.h"
#include <jni.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(x) do {if(!(x)){fprintf(stderr,"UBOCOMPILE_FAILURE line=%d %s\n",__LINE__,#x);abort();}}while(0)
#define CHECK(x) do {VkResult result_=(x);if(result_!=VK_SUCCESS){fprintf(stderr,"UBOCOMPILE_FAILURE line=%d %s result=%d\n",__LINE__,#x,result_);abort();}}while(0)
static atomic_flag invoked=ATOMIC_FLAG_INIT;
#include "cases.h"

JNIEXPORT void JNICALL Java_org_kxn_ubocompile_ProbeActivity_prepare(JNIEnv *e,jclass c){(void)e;(void)c;}
JNIEXPORT void JNICALL Java_org_kxn_ubocompile_ProbeActivity_stop(JNIEnv *e,jclass c){(void)e;(void)c;}
JNIEXPORT void JNICALL Java_org_kxn_ubocompile_ProbeActivity_color(JNIEnv *e,jclass c,jint n){(void)e;(void)c;(void)n;}

static VkShaderModule module(VkDevice device,const char *directory,const char *leaf){
  char path[4096];REQUIRE(snprintf(path,sizeof path,"%s/%s",directory,leaf)<(int)sizeof path);
  FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long bytes=ftell(f);REQUIRE(bytes>=20&&bytes<1048576&&!(bytes%4));rewind(f);
  uint32_t *words=malloc((size_t)bytes);REQUIRE(words&&fread(words,1,(size_t)bytes,f)==(size_t)bytes);REQUIRE(!fclose(f));REQUIRE(words[0]==0x07230203);
  VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)bytes,.pCode=words};VkShaderModule result;CHECK(vkCreateShaderModule(device,&info,NULL,&result));free(words);return result;
}

JNIEXPORT void JNICALL Java_org_kxn_ubocompile_ProbeActivity_render(JNIEnv *env,jclass klass,jobject surface,jstring directory,jstring hooks,jboolean query_only,jint color){
  (void)klass;(void)surface;(void)query_only;(void)color;
  if(atomic_flag_test_and_set(&invoked))return;
  const char *dir=(*env)->GetStringUTFChars(env,directory,NULL),*hook=(*env)->GetStringUTFChars(env,hooks,NULL);REQUIRE(dir&&hook);
  char path[4096],driver_root[4096],hook_root[4096];
  REQUIRE(snprintf(path,sizeof path,"%s/compile.log",dir)<(int)sizeof path);REQUIRE(!dm_android_redirect_stderr(path));
  fprintf(stderr,"UBOCOMPILE_BEGIN pid=%d uid=%d game_modules_executed=0 queue_submits=0 no_timing=1\n",getpid(),getuid());
  REQUIRE(snprintf(path,sizeof path,"%s/ir3.log",dir)<(int)sizeof path);
  REQUIRE(!setenv("IR3_SHADER_DEBUG","vs,fs",1));REQUIRE(!setenv("MESA_SHADER_CACHE_DISABLE","true",1));
  REQUIRE(!setenv("MESA_LOG","file",1));REQUIRE(!setenv("MESA_LOG_LEVEL","info",1));REQUIRE(!setenv("MESA_LOG_FILE",path,1));
  /* Exact existing adrenotools/Volk loader invocation, extracted and source-
   * hashed by the builder from the already qualified independent fixture. */
#include "loader.inc"
  VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="UBO address compiler qualification",.apiVersion=VK_API_VERSION_1_1};
  VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;CHECK(vkCreateInstance(&ii,NULL,&instance));volkLoadInstance(instance);
  VkPhysicalDevice physical;uint32_t count=1;CHECK(vkEnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);
  VkPhysicalDeviceIDProperties id={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&id};vkGetPhysicalDeviceProperties2(physical,&properties);
  fprintf(stderr,"UBOCOMPILE_DEVICE name=%s deviceUUID=",properties.properties.deviceName);for(unsigned n=0;n<16;n++)fprintf(stderr,"%02x",id.deviceUUID[n]);fprintf(stderr," driverUUID=");for(unsigned n=0;n<16;n++)fprintf(stderr,"%02x",id.driverUUID[n]);fprintf(stderr,"\n");
  VkPhysicalDeviceShaderDrawParametersFeatures draw={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&draw};vkGetPhysicalDeviceFeatures2(physical,&features);REQUIRE(features.features.robustBufferAccess&&draw.shaderDrawParameters);
  uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);VkQueueFamilyProperties *qp=calloc(families,sizeof *qp);REQUIRE(qp);vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,qp);uint32_t family=UINT32_MAX;for(uint32_t n=0;n<families;n++)if(qp[n].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=n;break;}free(qp);REQUIRE(family!=UINT32_MAX);
  float priority=1;VkDeviceQueueCreateInfo qc={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&draw,.pEnabledFeatures=&features.features,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc};VkDevice device;CHECK(vkCreateDevice(physical,&dc,NULL,&device));volkLoadDevice(device);
  fprintf(stderr,"UBOCOMPILE_FEATURE robustBufferAccess=%u shaderDrawParameters=%u pipeline_cache=NULL isolated_UID=1\n",features.features.robustBufferAccess,draw.shaderDrawParameters);
  VkDescriptorSetLayout sets[2];
  for(unsigned set=0;set<2;set++){
    VkDescriptorSetLayoutBinding bindings[4];unsigned first=set?0:1,n=set?3:3;
    for(unsigned k=0;k<n;k++)bindings[k]=(VkDescriptorSetLayoutBinding){.binding=first+k,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=set?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=n,.pBindings=bindings};CHECK(vkCreateDescriptorSetLayout(device,&info,NULL,&sets[set]));
  }
  VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=sets};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&li,NULL,&layout));
  VkAttachmentDescription depth={.format=VK_FORMAT_D16_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkAttachmentReference depth_ref={0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.pDepthStencilAttachment=&depth_ref};VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&depth,.subpassCount=1,.pSubpasses=&sub};VkRenderPass pass;CHECK(vkCreateRenderPass(device,&rp,NULL,&pass));
  VkShaderModule fs=module(device,dir,"shadow.spv");
  for(unsigned n=0;n<4;n++){
    const struct compile_case *c=&cases[n];fprintf(stderr,"UBOCOMPILE_CASE index=%u label=%s\n",n,c->label);fflush(stderr);
    VkShaderModule vs=module(device,dir,c->file);
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkVertexInputBindingDescription binding={.binding=0,.stride=c->stride,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,.vertexAttributeDescriptionCount=c->count,.pVertexAttributeDescriptions=c->attributes};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkViewport viewport={0,0,1,1,0,1};VkRect2D scissor={{0,0},{1,1}};VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,.pDepthStencilState=&ds,.pColorBlendState=&blend,.layout=layout,.renderPass=pass};VkPipeline pipeline;CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&pipeline));fprintf(stderr,"UBOCOMPILE_CREATED index=%u label=%s\n",n,c->label);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,vs,NULL);
  }
  vkDestroyShaderModule(device,fs,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyPipelineLayout(device,layout,NULL);for(unsigned n=0;n<2;n++)vkDestroyDescriptorSetLayout(device,sets[n],NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
  fprintf(stderr,"UBOCOMPILE_TERMINAL pipelines=4 queue_submits=0 game_modules_executed=0 no_timing=1\n");
  (*env)->ReleaseStringUTFChars(env,directory,dir);(*env)->ReleaseStringUTFChars(env,hooks,hook);
}
