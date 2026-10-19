#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "probe_spv.h"
#ifdef __ANDROID__
#include <jni.h>
#if DM_RENDERDOC_PROBE
#include <dlfcn.h>
#include "renderdoc_app.h"
static RENDERDOC_API_1_6_0 *rdoc;
static const char *capture_template;
static uint32_t capture_started, capture_ended, capture_count;
static char capture_file[2048];
static RENDERDOC_DevicePointer capture_device;
static void capture_begin(VkInstance instance) {
    if (!capture_template) return;
    // Android's injected layer, not another copy of RenderDoc or a remote GUI.
    void *library = dlopen("libVkLayer_GLES_RenderDoc.so", RTLD_NOW | RTLD_NOLOAD);
    pRENDERDOC_GetAPI get_api = library ? (pRENDERDOC_GetAPI)dlsym(library, "RENDERDOC_GetAPI") : NULL;
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_6_0, (void **)&rdoc)) return;
    rdoc->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 0);
    rdoc->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, 0);
    rdoc->SetCaptureOptionU32(eRENDERDOC_Option_SoftMemoryLimit, 128);
    rdoc->SetCaptureFilePathTemplate(capture_template);
    capture_device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance);
    rdoc->StartFrameCapture(capture_device, NULL);
    capture_started = rdoc->IsFrameCapturing();
}
static void capture_end(void) {
    if (!rdoc || !capture_started) return;
    capture_ended = rdoc->EndFrameCapture(capture_device, NULL);
    capture_count = rdoc->GetNumCaptures();
    uint32_t size = sizeof capture_file;
    if (capture_count) rdoc->GetCapture(capture_count - 1, capture_file, &size, NULL);
}
#endif
#endif

/* This is a capability/driver probe, not a D2R renderer or a performance estimate. */
static int executable_memory(int *error) {
    long size = sysconf(_SC_PAGESIZE);
    void *memory = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) { *error = errno; return -1; }
#if defined(__aarch64__)
    const uint32_t instructions[] = { 0x52800540, 0xd65f03c0 }; /* mov w0,42; ret */
#elif defined(__x86_64__)
    const uint8_t instructions[] = { 0xb8, 42, 0, 0, 0, 0xc3 };
#else
    munmap(memory, (size_t)size); *error = ENOTSUP; return -1;
#endif
    memcpy(memory, instructions, sizeof(instructions));
    __builtin___clear_cache(memory, (char *)memory + sizeof(instructions));
    if (mprotect(memory, (size_t)size, PROT_READ | PROT_EXEC) != 0) {
        *error = errno; munmap(memory, (size_t)size); return -1;
    }
    int result = ((int (*)(void))memory)();
    munmap(memory, (size_t)size);
    return result;
}

static VkResult compute_probe(VkInstance instance, VkPhysicalDevice physical, uint32_t *verified,
                               const char **stage) {
#if !defined(__ANDROID__) || !DM_RENDERDOC_PROBE
    (void)instance;
#endif
    VkResult result = VK_SUCCESS;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkCommandPool commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, NULL);
    VkQueueFamilyProperties *queues = calloc(queue_count, sizeof(*queues));
    if (!queues) return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < queue_count; ++i)
        if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) { family = i; break; }
    free(queues);
    if (family == UINT32_MAX) { *stage = "compute_queue"; return VK_ERROR_FEATURE_NOT_PRESENT; }
#define CHECK(label, expression) do { *stage = label; result = (expression); if (result != VK_SUCCESS) goto cleanup; } while (0)
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info };
    CHECK("create_device", vkCreateDevice(physical, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 512 * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    CHECK("create_buffer", vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            memory_type = i; break;
        }
    if (memory_type == UINT32_MAX) { *stage = "host_visible_memory"; result = VK_ERROR_FEATURE_NOT_PRESENT; goto cleanup; }
    VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type };
    CHECK("allocate_memory", vkAllocateMemory(device, &allocation, NULL, &memory));
    CHECK("bind_buffer", vkBindBufferMemory(device, buffer, memory, 0));
    CHECK("map_memory", vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    memset(mapped, 0, 512 * sizeof(uint32_t));
    VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory, .size = VK_WHOLE_SIZE };
    CHECK("flush_memory", vkFlushMappedMemoryRanges(device, 1, &range));
    VkShaderModuleCreateInfo shader_info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(probe_spv), .pCode = probe_spv };
    CHECK("create_shader", vkCreateShaderModule(device, &shader_info, NULL, &shader));
    VkDescriptorSetLayoutBinding binding = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo set_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding };
    CHECK("descriptor_layout", vkCreateDescriptorSetLayout(device, &set_info, NULL, &set_layout));
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &set_layout };
    CHECK("pipeline_layout", vkCreatePipelineLayout(device, &layout_info, NULL, &pipeline_layout));
    VkComputePipelineCreateInfo pipeline_info = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main" },
        .layout = pipeline_layout };
    CHECK("compute_pipeline", vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
    VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
    CHECK("descriptor_pool", vkCreateDescriptorPool(device, &pool_info, NULL, &descriptors));
    VkDescriptorSetAllocateInfo set_allocate = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptors, .descriptorSetCount = 1, .pSetLayouts = &set_layout };
    VkDescriptorSet set;
    CHECK("descriptor_set", vkAllocateDescriptorSets(device, &set_allocate, &set));
    VkDescriptorBufferInfo descriptor_buffer = { .buffer = buffer, .range = 512 * sizeof(uint32_t) };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &descriptor_buffer };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    VkCommandPoolCreateInfo command_pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family };
    CHECK("command_pool", vkCreateCommandPool(device, &command_pool_info, NULL, &commands));
    VkCommandBufferAllocateInfo command_allocate = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commands, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command;
    CHECK("command_buffer", vkAllocateCommandBuffers(device, &command_allocate, &command));
#if defined(__ANDROID__) && DM_RENDERDOC_PROBE
    capture_begin(instance);
#endif
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK("begin_commands", vkBeginCommandBuffer(command, &begin));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(command, 8, 1, 1);
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK("end_commands", vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CHECK("create_fence", vkCreateFence(device, &fence_info, NULL, &fence));
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command };
    CHECK("submit", vkQueueSubmit(queue, 1, &submit, fence));
    *stage = "wait_fence";
    result = vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ULL);
    /* Do not destroy in-flight resources on timeout. The one-shot process owns them
       until exit; returning a failure is preferable to an unbounded DeviceWaitIdle. */
    if (result == VK_TIMEOUT) return result;
    if (result != VK_SUCCESS) goto cleanup;
#if defined(__ANDROID__) && DM_RENDERDOC_PROBE
    capture_end();
#endif
    CHECK("invalidate_memory", vkInvalidateMappedMemoryRanges(device, 1, &range));
    *verified = 0;
    for (uint32_t i = 0; i < 512; ++i)
        if (((uint32_t *)mapped)[i] == i * 3u + 7u) ++*verified;
    *stage = *verified == 512 ? "verified" : "readback_mismatch";
    if (*verified != 512) result = VK_ERROR_UNKNOWN;
cleanup:
#if defined(__ANDROID__) && DM_RENDERDOC_PROBE
    if (rdoc && rdoc->IsFrameCapturing()) rdoc->DiscardFrameCapture(capture_device, NULL);
#endif
    if (device) {
        if (mapped) vkUnmapMemory(device, memory);
        if (fence) vkDestroyFence(device, fence, NULL);
        if (commands) vkDestroyCommandPool(device, commands, NULL);
        if (descriptors) vkDestroyDescriptorPool(device, descriptors, NULL);
        if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
        if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, NULL);
        if (shader) vkDestroyShaderModule(device, shader, NULL);
        if (buffer) vkDestroyBuffer(device, buffer, NULL);
        if (memory) vkFreeMemory(device, memory, NULL);
        vkDestroyDevice(device, NULL);
    }
    return result;
#undef CHECK
}

static void json_escape(const char *source, char *target, size_t capacity) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)source; *p && n + 7 < capacity; ++p) {
        if (*p < 32 || *p == '"' || *p == '\\')
            n += (size_t)snprintf(target + n, capacity - n, "\\u%04x", *p);
        else target[n++] = (char)*p;
    }
    target[n] = 0;
}

static char *run_probe(int *passed) {
    struct utsname system_info;
    memset(&system_info, 0, sizeof(system_info));
    uname(&system_info);
    int memory_error = 0;
    int memory_result = executable_memory(&memory_error);
    VkApplicationInfo application = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "D2R development probe", .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo create = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&create, NULL, &instance);
    uint32_t count = 0, verified = 0;
    const char *stage = "create_instance";
    VkPhysicalDeviceProperties properties = {0};
    VkPhysicalDeviceFeatures features = {0};
    VkPhysicalDeviceMemoryProperties memory = {0};
    if (result == VK_SUCCESS) {
        stage = "enumerate_devices";
        result = vkEnumeratePhysicalDevices(instance, &count, NULL);
        if (result == VK_SUCCESS && count == 0) result = VK_ERROR_INITIALIZATION_FAILED;
        if (result == VK_SUCCESS) {
            VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
            if (!devices) result = VK_ERROR_OUT_OF_HOST_MEMORY;
            else {
                result = vkEnumeratePhysicalDevices(instance, &count, devices);
                if (result == VK_SUCCESS) {
                    vkGetPhysicalDeviceProperties(devices[0], &properties);
                    vkGetPhysicalDeviceFeatures(devices[0], &features);
                    vkGetPhysicalDeviceMemoryProperties(devices[0], &memory);
                    result = compute_probe(instance, devices[0], &verified, &stage);
                }
                free(devices);
            }
        }
    }
    uint64_t device_bytes = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) device_bytes += memory.memoryHeaps[i].size;
    /* A timeout leaves a live device owned by this process; its instance must stay too. */
    if (instance && result != VK_TIMEOUT) vkDestroyInstance(instance, NULL);
    char name[1600], architecture[400];
    json_escape(properties.deviceName, name, sizeof(name));
    json_escape(system_info.machine, architecture, sizeof(architecture));
    *passed = memory_result == 42 && result == VK_SUCCESS && verified == 512;
    char *report = calloc(8192, 1);
    if (!report) return NULL;
    snprintf(report, 8192,
        "{\n  \"schema\":1,\n  \"probe_only\":true,\n  \"passed\":%s,\n"
        "  \"architecture\":\"%s\",\n  \"page_bytes\":%ld,\n"
        "  \"rw_to_rx_return\":%d,\n  \"rw_to_rx_errno\":%d,\n"
        "  \"vulkan_device\":\"%s\",\n  \"device_type\":%u,\n  \"device_count\":%u,\n"
        "  \"api_version\":\"%u.%u.%u\",\n  \"driver_version_raw\":%u,\n"
        "  \"vendor_id\":%u,\n  \"device_id\":%u,\n"
        "  \"device_local_heap_bytes\":%llu,\n  \"max_image_2d\":%u,\n"
        "  \"shader_int64\":%s,\n  \"shader_float64\":%s,\n  \"geometry_shader\":%s,\n"
        "  \"compute_stage\":\"%s\",\n  \"vk_result\":%d,\n  \"verified_words\":%u\n}\n",
        *passed ? "true" : "false", architecture, sysconf(_SC_PAGESIZE), memory_result, memory_error,
        name, properties.deviceType, count, VK_VERSION_MAJOR(properties.apiVersion),
        VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion),
        properties.driverVersion, properties.vendorID, properties.deviceID,
        (unsigned long long)device_bytes, properties.limits.maxImageDimension2D,
        features.shaderInt64 ? "true" : "false", features.shaderFloat64 ? "true" : "false",
        features.geometryShader ? "true" : "false", stage, result, verified);
    return report;
}

#ifdef __ANDROID__
JNIEXPORT jstring JNICALL Java_com_d2rmobile_devprobe_ProbeActivity_nativeProbe(JNIEnv *env, jclass clazz, jstring capture_path) {
    (void)clazz;
#if DM_RENDERDOC_PROBE
    capture_template = capture_path ? (*env)->GetStringUTFChars(env, capture_path, NULL) : NULL;
#else
    (void)capture_path;
#endif
    int passed;
    char *report = run_probe(&passed);
#if DM_RENDERDOC_PROBE
    if (report && capture_template) {
        char filename[4096]; json_escape(capture_file, filename, sizeof filename);
        size_t end = strlen(report);
        while (end && report[end-1] != '}') --end;
        if (end) snprintf(report+end-1, 8192-end+1,
            ",\n  \"renderdoc\":{\"available\":%s,\"explicit_vulkan_instance\":true,\"started\":%u,\"ended\":%u,\"captures\":%u,\"file\":\"%s\"}\n}\n",
            rdoc ? "true" : "false", capture_started, capture_ended, capture_count, filename);
    }
    if (capture_template) (*env)->ReleaseStringUTFChars(env, capture_path, capture_template);
#endif
    jstring text = (*env)->NewStringUTF(env, report ? report : "{\"error\":\"out of memory\"}");
    free(report);
    return text;
}
#else
int main(void) {
    if (!getenv("VK_DRIVER_FILES") && !getenv("VK_ICD_FILENAMES")) {
        fputs("Select a Vulkan ICD explicitly; use scripts/dev/run-device-probe.sh.\n", stderr);
        return 2;
    }
    int passed = 0;
    char *report = run_probe(&passed);
    if (!report) return 2;
    puts(report);
    free(report);
    return passed ? 0 : 1;
}
#endif
