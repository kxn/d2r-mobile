// Standalone owned correctness experiment. No game/runtime code, CPU emulation,
// custom performance timer, sampler, driver modification or production patch.
#include "provenance.inc"
#include "shaders.inc"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include "dm/gfx/vulkan/nvn_vk_loader.h"
static void check(VkResult r, const char *what) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(what) +
                             " VkResult=" + std::to_string(r));
}
#define VK(x) check((x), #x)
static void require(bool b, const char *why) {
  if (!b)
    throw std::runtime_error(why);
}
static std::string quoted(const char *s) {
  std::string r = "\"";
  for (; *s; s++) {
    if (*s == '"' || *s == '\\')
      r += '\\';
    if ((unsigned char)*s < 32)
      r += '?';
    else
      r += *s;
  }
  return r + '"';
}
static std::string hex(const uint8_t *p, size_t n) {
  static const char *h = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += h[p[i] >> 4];
    s += h[p[i] & 15];
  }
  return s;
}
static uint64_t decimal(const char *s) {
  char *e = nullptr;
  require(s && *s && *s != '-', "expected nonnegative integer");
  auto n = strtoull(s, &e, 10);
  require(e && !*e, "invalid integer argument");
  return n;
}
struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize allocation = 0;
  VkMemoryPropertyFlags flags = 0;
  void *mapped = nullptr;
};
struct App {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceIDProperties id{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceMemoryProperties mem{};
  VkPhysicalDeviceFeatures features{};
  uint32_t family = 0;
  Buffer input, stats;
  VkImage output = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkDeviceSize image_allocation = 0;
  VkImageView output_view = VK_NULL_HANDLE;
  VkBufferView input_view = VK_NULL_HANDLE;
  VkRenderPass pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  VkPipeline generate = VK_NULL_HANDLE, compare = VK_NULL_HANDLE,
             reduce = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  uint32_t width = 0, height = 0, count = 0;
  bool initialized_image = false;
  bool submitted = false;
  std::vector<VkShaderModule> modules;
  ~App() {
    if (device) {
      // Normal/error paths with completed GPU work are explicitly released. A
      // hung fence takes the process-fatal branch, never an unbounded retry.
      if (submitted)
        vkDeviceWaitIdle(device);
      if (fence)
        vkDestroyFence(device, fence, nullptr);
      if (command_pool)
        vkDestroyCommandPool(device, command_pool, nullptr);
      if (generate)
        vkDestroyPipeline(device, generate, nullptr);
      if (compare)
        vkDestroyPipeline(device, compare, nullptr);
      if (reduce)
        vkDestroyPipeline(device, reduce, nullptr);
      for (auto m : modules)
        vkDestroyShaderModule(device, m, nullptr);
      if (descriptor_pool)
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
      if (layout)
        vkDestroyPipelineLayout(device, layout, nullptr);
      if (descriptor_layout)
        vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
      if (framebuffer)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
      if (pass)
        vkDestroyRenderPass(device, pass, nullptr);
      if (output_view)
        vkDestroyImageView(device, output_view, nullptr);
      if (input_view)
        vkDestroyBufferView(device, input_view, nullptr);
      if (output)
        vkDestroyImage(device, output, nullptr);
      if (image_memory)
        vkFreeMemory(device, image_memory, nullptr);
      for (Buffer *b : {&input, &stats}) {
        if (b->mapped)
          vkUnmapMemory(device, b->memory);
        if (b->handle)
          vkDestroyBuffer(device, b->handle, nullptr);
        if (b->memory)
          vkFreeMemory(device, b->memory, nullptr);
      }
      vkDestroyDevice(device, nullptr);
    }
    if (instance)
      vkDestroyInstance(instance, nullptr);
  }
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags needed,
                       VkMemoryPropertyFlags prefer) {
    int best = -1, score = -1;
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
      if ((bits & (1u << i)) &&
          (mem.memoryTypes[i].propertyFlags & needed) == needed) {
        int s = __builtin_popcount(mem.memoryTypes[i].propertyFlags & prefer);
        if (s > score) {
          best = int(i);
          score = s;
        }
      }
    require(best >= 0, "required memory type absent");
    return uint32_t(best);
  }
  void buffer(Buffer &b, VkDeviceSize bytes, VkBufferUsageFlags usage,
              VkMemoryPropertyFlags needed, VkMemoryPropertyFlags prefer) {
    VkBufferCreateInfo ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                          .size = bytes,
                          .usage = usage,
                          .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VK(vkCreateBuffer(device, &ci, nullptr, &b.handle));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b.handle, &req);
    uint32_t type = memory_type(req.memoryTypeBits, needed, prefer);
    b.flags = mem.memoryTypes[type].propertyFlags;
    b.allocation = req.size;
    VkMemoryAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                            .allocationSize = req.size,
                            .memoryTypeIndex = type};
    VK(vkAllocateMemory(device, &ai, nullptr, &b.memory));
    VK(vkBindBufferMemory(device, b.handle, b.memory, 0));
  }
  template <size_t N> VkShaderModule module(const uint32_t (&words)[N]) {
    VkShaderModuleCreateInfo ci{.sType =
                                    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                .codeSize = sizeof words,
                                .pCode = words};
    VkShaderModule m;
    VK(vkCreateShaderModule(device, &ci, nullptr, &m));
    modules.push_back(m);
    return m;
  }
  void compute(VkPipeline &pipeline, VkShaderModule shader) {
    VkComputePipelineCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = shader,
                  .pName = "main"},
        .layout = layout};
    VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                &pipeline));
  }
  void init(uint32_t pow, uint32_t conversion, uint32_t inject,
            bool allow_software) {
    count = 1u << pow;
    width = 1u << (pow < 10 ? pow : 10);
    height = count / width;
    VkApplicationInfo app{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                          .pApplicationName = "owned-sfloat-input-domain",
                          .applicationVersion = 1,
                          .pEngineName = "standalone-correctness-only",
                          .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo = &app};
    vk_loader_initialize();
    VK(vkCreateInstance(&ici, nullptr, &instance));
    volkLoadInstance(instance);
    uint32_t n = 0;
    VK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    require(n != 0, "no physical Vulkan device");
    std::vector<VkPhysicalDevice> devices(n);
    VK(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    physical = devices[0];
    VkPhysicalDeviceProperties2 p2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &id};
    vkGetPhysicalDeviceProperties2(physical, &p2);
    props = p2.properties;
    require(props.apiVersion >= VK_API_VERSION_1_1,
            "physical Vulkan1.1 required");
    require(allow_software || props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU,
            "software Vulkan requires explicit --allow-software; never report "
            "as phone evidence");
    vkGetPhysicalDeviceMemoryProperties(physical, &mem);
    vkGetPhysicalDeviceFeatures(physical, &features);
    require(features.fragmentStoresAndAtomics,
            "fragmentStoresAndAtomics required for first-failure evidence");
    require(features.robustBufferAccess,
            "robustBufferAccess required to match recorded context");
    require(props.limits.maxStorageBufferRange >= uint64_t(count) * 16 &&
                props.limits.maxTexelBufferElements >= count,
            "chunk exceeds buffer limits");
    require(props.limits.maxImageDimension2D >= width &&
                props.limits.maxImageDimension2D >= height &&
                props.limits.maxFramebufferWidth >= width &&
                props.limits.maxFramebufferHeight >= height,
            "chunk exceeds image/framebuffer limits");
    require(props.limits.maxComputeWorkGroupInvocations >= 128 &&
                props.limits.maxComputeWorkGroupSize[0] >= 128 &&
                props.limits.maxComputeWorkGroupCount[0] >= count / 128,
            "compute workgroup limits insufficient");
    require(props.limits.maxComputeSharedMemorySize >= 128 * 5 * 4,
            "compute shared memory insufficient");
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R32G32B32A32_SFLOAT,
                                        &fp);
    require(fp.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT,
            "RGBA32_SFLOAT uniform texel buffer unsupported");
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R32_UINT, &fp);
    require(
        (fp.optimalTilingFeatures & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                     VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) ==
            (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT),
        "R32_UINT color+storage unsupported");
    VkImageFormatProperties ifp;
    VK(vkGetPhysicalDeviceImageFormatProperties(
        physical, VK_FORMAT_R32_UINT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0,
        &ifp));
    require(ifp.maxExtent.width >= width && ifp.maxExtent.height >= height &&
                (ifp.sampleCounts & VK_SAMPLE_COUNT_1_BIT),
            "R32_UINT combined usage extent unsupported");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qs(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, qs.data());
    for (family = 0; family < n; family++)
      if ((qs[family].queueFlags &
           (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        break;
    require(family < n, "combined graphics/compute queue required");
    float priority = 1;
    VkDeviceQueueCreateInfo qi{.sType =
                                   VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                               .queueFamilyIndex = family,
                               .queueCount = 1,
                               .pQueuePriorities = &priority};
    VkPhysicalDeviceFeatures enabled{};
    enabled.fragmentStoresAndAtomics = VK_TRUE;
    enabled.robustBufferAccess = VK_TRUE;
    VkDeviceCreateInfo di{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                          .queueCreateInfoCount = 1,
                          .pQueueCreateInfos = &qi,
                          .pEnabledFeatures = &enabled};
    VK(vkCreateDevice(physical, &di, nullptr, &device));
    volkLoadDevice(device);
    vkGetDeviceQueue(device, family, 0, &queue);
    buffer(input, uint64_t(count) * 16,
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
           0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    buffer(stats, 128,
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
           VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkMapMemory(device, stats.memory, 0, VK_WHOLE_SIZE, 0, &stats.mapped));
    VkBufferViewCreateInfo bv{.sType =
                                  VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                              .buffer = input.handle,
                              .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                              .offset = 0,
                              .range = uint64_t(count) * 16};
    VK(vkCreateBufferView(device, &bv, nullptr, &input_view));
    VkImageCreateInfo image{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R32_UINT,
                            .extent = {width, height, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_STORAGE_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VK(vkCreateImage(device, &image, nullptr, &output));
    VkMemoryRequirements ir;
    vkGetImageMemoryRequirements(device, output, &ir);
    image_allocation = ir.size;
    VkMemoryAllocateInfo ia{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = ir.size,
        .memoryTypeIndex = memory_type(ir.memoryTypeBits, 0,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK(vkAllocateMemory(device, &ia, nullptr, &image_memory));
    VK(vkBindImageMemory(device, output, image_memory, 0));
    VkImageViewCreateInfo iv{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = output,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32_UINT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VK(vkCreateImageView(device, &iv, nullptr, &output_view));
    VkAttachmentDescription attachment{
        .format = VK_FORMAT_R32_UINT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL};
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription sub{.pipelineBindPoint =
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                             .colorAttachmentCount = 1,
                             .pColorAttachments = &color};
    VkRenderPassCreateInfo ri{.sType =
                                  VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                              .attachmentCount = 1,
                              .pAttachments = &attachment,
                              .subpassCount = 1,
                              .pSubpasses = &sub};
    VK(vkCreateRenderPass(device, &ri, nullptr, &pass));
    VkFramebufferCreateInfo fi{.sType =
                                   VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                               .renderPass = pass,
                               .attachmentCount = 1,
                               .pAttachments = &output_view,
                               .width = width,
                               .height = height,
                               .layers = 1};
    VK(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer));
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1,
         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
         nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo li{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings = bindings};
    VK(vkCreateDescriptorSetLayout(device, &li, nullptr, &descriptor_layout));
    VkPushConstantRange push{
        VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pl{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push};
    VK(vkCreatePipelineLayout(device, &pl, nullptr, &layout));
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo dpi{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 3,
        .pPoolSizes = sizes};
    VK(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptor_pool));
    VkDescriptorSetAllocateInfo da{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_layout};
    VK(vkAllocateDescriptorSets(device, &da, &set));
    VkDescriptorBufferInfo ib{input.handle, 0, uint64_t(count) * 16},
        sb{stats.handle, 0, 128};
    VkDescriptorImageInfo ii{VK_NULL_HANDLE, output_view,
                             VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &ib},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
         .pTexelBufferView = &input_view},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .pImageInfo = &ii},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 3,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &sb}};
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    compute(generate, module(generate_spv));
    compute(reduce, module(reduce_spv));
    VkShaderModule vs = module(triangle_spv), fs = module(compare_spv);
    uint32_t spec_data[] = {conversion, inject};
    VkSpecializationMapEntry spec_entries[] = {{0, 0, 4}, {1, 4, 4}};
    VkSpecializationInfo spec{2, spec_entries, sizeof spec_data, spec_data};
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vs,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fs,
         .pName = "main",
         .pSpecializationInfo = &spec}};
    VkPipelineVertexInputStateCreateInfo vi{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ai{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport{0, 0, float(width), float(height), 0, 1};
    VkRect2D scissor{{0, 0}, {width, height}};
    VkPipelineViewportStateCreateInfo vp{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba{.colorWriteMask =
                                               VK_COLOR_COMPONENT_R_BIT};
    VkPipelineColorBlendStateCreateInfo blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &ba};
    VkGraphicsPipelineCreateInfo gp{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ai,
        .pViewportState = &vp,
        .pRasterizationState = &raster,
        .pMultisampleState = &ms,
        .pColorBlendState = &blend,
        .layout = layout,
        .renderPass = pass};
    VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr,
                                 &compare));
    VkCommandPoolCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = family};
    VK(vkCreateCommandPool(device, &ci, nullptr, &command_pool));
    VkCommandBufferAllocateInfo ca{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VK(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkFenceCreateInfo fc{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK(vkCreateFence(device, &fc, nullptr, &fence));
  }
  std::array<uint32_t, 32> run(uint32_t base) {
    VK(vkResetCommandBuffer(cmd, 0));
    VK(vkResetFences(device, 1, &fence));
    VkCommandBufferBeginInfo bi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK(vkBeginCommandBuffer(cmd, &bi));
    VkBufferMemoryBarrier init[] = {
        {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask =
             initialized_image ? VkAccessFlags(VK_ACCESS_SHADER_READ_BIT) : 0u,
         .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = input.handle,
         .offset = 0,
         .size = VK_WHOLE_SIZE},
        {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask = initialized_image ? (VK_ACCESS_SHADER_WRITE_BIT |
                                               VK_ACCESS_HOST_READ_BIT)
                                            : 0u,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = stats.handle,
         .offset = 0,
         .size = VK_WHOLE_SIZE}};
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 2, init, 0, nullptr);
    uint32_t initial_stats[32]{};
    initial_stats[5] = 0xffffffffu;
    vkCmdUpdateBuffer(cmd, stats.handle, 0, sizeof initial_stats,
                      initial_stats);
    VkBufferMemoryBarrier ready{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = stats.handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &ready, 0, nullptr);
    uint32_t params[] = {base, count, width, 0};
    vkCmdPushConstants(
        cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof params, params);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, generate);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1,
                            &set, 0, nullptr);
    vkCmdDispatch(cmd, count / 128, 1, 1);
    VkBufferMemoryBarrier generated{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = input.handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         1, &generated, 0, nullptr);
    VkImageMemoryBarrier to_color{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask =
            initialized_image ? VkAccessFlags(VK_ACCESS_SHADER_READ_BIT) : 0u,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = initialized_image ? VK_IMAGE_LAYOUT_GENERAL
                                       : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = output,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd,
                         initialized_image
                             ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &to_color);
    VkClearValue clear{};
    clear.color.uint32[0] = 0x80000000u;
    VkRenderPassBeginInfo rb{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                             .renderPass = pass,
                             .framebuffer = framebuffer,
                             .renderArea = {{0, 0}, {width, height}},
                             .clearValueCount = 1,
                             .pClearValues = &clear};
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compare);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    VkImageMemoryBarrier to_compute{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = output,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkBufferMemoryBarrier fs_done{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = stats.handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                         &fs_done, 1, &to_compute);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, reduce);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1,
                            &set, 0, nullptr);
    vkCmdDispatch(cmd, count / 128, 1, 1);
    VkBufferMemoryBarrier readback{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = stats.handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &readback, 0, nullptr);
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &cmd};
    VK(vkQueueSubmit(queue, 1, &si, fence));
    submitted = true;
    VkResult wait = vkWaitForFences(device, 1, &fence, VK_TRUE, 60000000000ull);
    if (wait == VK_TIMEOUT) {
      puts("{\"event\":\"fatal\",\"reason\":\"60s fence timeout; no "
           "retry\",\"full_domain_completed\":false}");
      fflush(stdout);
      std::quick_exit(4);
    }
    VK(wait);
    submitted = false;
    initialized_image = true;
    if (!(stats.flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range{.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                .memory = stats.memory,
                                .offset = 0,
                                .size = VK_WHOLE_SIZE};
      VK(vkInvalidateMappedMemoryRanges(device, 1, &range));
    }
    std::array<uint32_t, 32> r{};
    memcpy(r.data(), stats.mapped, 128);
    return r;
  }
};
int owned_sfloat_probe_main(int argc, char **argv) {
  setbuf(stdout, nullptr);
  try {
    uint32_t pow = 20, conversion = 1, inject = 0;
    uint64_t chunks = 1, start = 0;
    bool full = false, keep = false, software = false;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      auto next = [&]() {
        require(i + 1 < argc, "option requires value");
        return argv[++i];
      };
      if (a == "--chunk-pow")
        pow = uint32_t(decimal(next()));
      else if (a == "--chunks")
        chunks = decimal(next());
      else if (a == "--start-chunk")
        start = decimal(next());
      else if (a == "--conversion") {
        std::string v = next();
        require(v == "raw" || v == "quiet", "conversion must be raw or quiet");
        conversion = v == "quiet";
      } else if (a == "--full")
        full = true;
      else if (a == "--continue-on-mismatch")
        keep = true;
      else if (a == "--inject-error")
        inject = 1;
      else if (a == "--allow-software")
        software = true;
      else
        throw std::runtime_error("unknown option " + a);
    }
    require(pow >= 7 && pow <= 20, "chunk-pow must be7..20");
    uint64_t all_chunks = uint64_t(1) << (32 - pow);
    if (full) {
      require(start == 0, "--full requires start0");
      chunks = all_chunks;
    }
    require(chunks && start < all_chunks && chunks <= all_chunks - start,
            "chunk range outside 32bit domain");
    App app;
    app.init(pow, conversion, inject, software);
    FILE *maps=fopen("/proc/self/maps","r");require(maps,"driver maps unavailable");
    char line[8192];unsigned driver_maps=0;
    while(fgets(line,sizeof line,maps))if(strstr(line,"libvulkan_freedreno.so")) {
      ++driver_maps;printf("{\"event\":\"actual_driver_map\",\"line\":%s}\n",quoted(line).c_str());
    }
    fclose(maps);require(driver_maps,"expected app-local Turnip not actually mapped; stock fallback is not qualified");
    printf("{\"event\":\"context\",\"device\":%s,\"device_type\":%u,"
           "\"apiVersion\":%u,\"driverVersion\":%u,\"deviceUUID\":\"%s\","
           "\"driverUUID\":\"%s\",\"pipelineCacheUUID\":\"%s\",\"source_"
           "fingerprint\":\"%s\",\"chunk_elements\":%u,\"width\":%u,\"height\":"
           "%u,\"requested_chunks\":%llu,\"start_chunk\":%llu,\"conversion\":"
           "\"%s\",\"inject_error\":%s,\"input_memory_flags\":%u,\"readback_"
           "memory_flags\":%u,\"format\":\"R32G32B32A32_SFLOAT\",\"comparison_"
           "stage\":\"fragment\",\"explicit_float_controls\":false,"
           "\"robustBufferAccess\":true,\"fragmentStoresAndAtomics\":true,"
           "\"production_raw_still_blocked\":true}\n",
           quoted(app.props.deviceName).c_str(), app.props.deviceType,
           app.props.apiVersion, app.props.driverVersion,
           hex(app.id.deviceUUID, VK_UUID_SIZE).c_str(),
           hex(app.id.driverUUID, VK_UUID_SIZE).c_str(),
           hex(app.props.pipelineCacheUUID, VK_UUID_SIZE).c_str(),
           source_fingerprint, app.count, app.width, app.height,
           (unsigned long long)chunks, (unsigned long long)start,
           conversion ? "quiet-hypothesis" : "raw", inject ? "true" : "false",
           app.input.flags, app.stats.flags);
    printf("{\"event\":\"resources\",\"input_logical_bytes\":%llu,\"mask_"
           "logical_bytes\":%llu,\"input_allocation_bytes\":%llu,\"mask_"
           "allocation_bytes\":%llu,\"stats_allocation_bytes\":%llu,\"kernel_"
           "and_driver_allocations_included\":false}\n",
           (unsigned long long)(uint64_t(app.count) * 16),
           (unsigned long long)(uint64_t(app.count) * 4),
           (unsigned long long)app.input.allocation,
           (unsigned long long)app.image_allocation,
           (unsigned long long)app.stats.allocation);
    uint64_t tested = 0, mismatch = 0, components = 0, source_errors = 0,
             coverage_errors = 0, done = 0;
    bool first = false;
    for (uint64_t chunk = 0; chunk < chunks; chunk++) {
      uint64_t base = (start + chunk) * app.count;
      auto s = app.run(uint32_t(base));
      tested += s[0];
      mismatch += s[1];
      components += s[2];
      source_errors += s[3];
      coverage_errors += s[4];
      done++;
      require(s[0] + s[4] == app.count,
              "reduction coverage count inconsistent");
      require(s[1] <= s[0] && uint64_t(s[2]) <= uint64_t(s[1]) * 4 &&
                  s[3] <= s[0],
              "GPU counters invalid");
      if ((s[1] || s[3]) && !first) {
        first = true;
        require(s[5] < app.count, "first-failure diagnostics missing");
        printf("{\"event\":\"first_mismatch\",\"global_pattern\":%llu,\"local_"
               "index\":%u,\"mask\":%u,\"raw\":[%u,%u,%u,%u],\"typed\":[%u,%u,%"
               "u,%u],\"predicted\":[%u,%u,%u,%u]}\n",
               (unsigned long long)(base + s[5]), s[5], s[6], s[7], s[8], s[9],
               s[10], s[11], s[12], s[13], s[14], s[15], s[16], s[17], s[18]);
      }
      if (s[4] || s[3] || ((s[1] != 0) && !keep))
        break;
      if ((done % 64) == 0)
        printf("{\"event\":\"progress\",\"completed_chunks\":%llu,\"validated_"
               "texels\":%llu,\"mismatch_texels\":%llu}\n",
               (unsigned long long)done, (unsigned long long)tested,
               (unsigned long long)mismatch);
    }
    bool complete = done == chunks;
    bool domain = complete && start == 0 && chunks == all_chunks &&
                  tested == (uint64_t(1) << 32) && !source_errors &&
                  !coverage_errors;
    bool pass = complete && !mismatch && !source_errors && !coverage_errors &&
                tested == chunks * app.count;
    printf(
        "{\"event\":\"result\",\"status\":\"%s\",\"completed_chunks\":%llu,"
        "\"requested_chunks\":%llu,\"tested_texels\":%llu,\"tested_"
        "components\":%llu,\"mismatch_texels\":%llu,\"mismatched_components\":%"
        "llu,\"source_mismatch_texels\":%llu,\"coverage_errors\":%llu,\"full_"
        "domain_completed\":%s,\"interval_begin\":%llu,\"interval_end_"
        "exclusive\":%llu,\"proof_scope\":\"this compiled fragment pipeline, "
        "driver and four per-lane bijections; not all RGBA128 combinations or "
        "all shaders\",\"production_raw_still_blocked\":true}\n",
        pass ? "PASS" : "FAIL", (unsigned long long)done,
        (unsigned long long)chunks, (unsigned long long)tested,
        (unsigned long long)(tested * 4), (unsigned long long)mismatch,
        (unsigned long long)components, (unsigned long long)source_errors,
        (unsigned long long)coverage_errors, domain ? "true" : "false",
        (unsigned long long)(start * app.count),
        (unsigned long long)((start + done) * app.count));
    return pass ? 0 : 3;
  } catch (const std::exception &e) {
    fprintf(stderr, "owned sfloat probe: %s\n", e.what());
    printf("{\"event\":\"error\",\"message\":%s,\"full_domain_completed\":"
           "false,\"production_raw_still_blocked\":true}\n",
           quoted(e.what()).c_str());
    return 2;
  }
}
