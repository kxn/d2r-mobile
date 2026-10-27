/* Functional Android WSI exercise, no timing or performance conclusions. */
#define VK_USE_PLATFORM_ANDROID_KHR
#include "volk.h"
#include <android/native_window_jni.h>
#include <jni.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "adrenotools/driver.h"

static atomic_int stopping, selected_color;
#define CHECK(x) do { VkResult result = (x); if (result != VK_SUCCESS) { \
    fprintf(stderr, "WSI_FAILURE %s result=%d\n", #x, result); abort(); } } while (0)
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "WSI_FAILURE %s\n", #x); abort(); } } while (0)

JNIEXPORT void JNICALL Java_org_kxn_wsiubwc_ProbeActivity_prepare(JNIEnv *env, jclass c) {
    (void)env; (void)c; atomic_store(&stopping, 0);
}
JNIEXPORT void JNICALL Java_org_kxn_wsiubwc_ProbeActivity_stop(JNIEnv *env, jclass c) {
    (void)env; (void)c; atomic_store(&stopping, 1);
}
JNIEXPORT void JNICALL Java_org_kxn_wsiubwc_ProbeActivity_color(JNIEnv *env, jclass c, jint value) {
    (void)env; (void)c; atomic_store(&selected_color, value & 3);
}
JNIEXPORT void JNICALL Java_org_kxn_wsiubwc_ProbeActivity_render(
    JNIEnv *env, jclass klass, jobject surface, jstring directory, jstring hooks,
    jboolean ubwc, jint color) {
    (void)klass;
    const char *dir = (*env)->GetStringUTFChars(env, directory, NULL);
    const char *hook = (*env)->GetStringUTFChars(env, hooks, NULL);
    REQUIRE(dir && hook);
    char path[4096], driver_root[4096], hook_root[4096];
    REQUIRE(snprintf(path, sizeof path, "%s/wsi.log", dir) < (int)sizeof path);
    REQUIRE(freopen(path, "a", stderr)); setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "WSI_BEGIN pid=%d ubwc=%d game_modules_executed=0\n", getpid(), !!ubwc);
    REQUIRE(!setenv("TU_UBWC_SWAPCHAIN", ubwc ? "1" : "0", 1));
    REQUIRE(!setenv("TU_DEBUG", "startup,layout", 1));
    REQUIRE(snprintf(driver_root, sizeof driver_root, "%s/", dir) < (int)sizeof driver_root);
    REQUIRE(snprintf(hook_root, sizeof hook_root, "%s/", hook) < (int)sizeof hook_root);
    REQUIRE(snprintf(path, sizeof path, "%s/libadrenotools.so", hook) < (int)sizeof path);
    void *tools = dlopen(path, RTLD_NOW | RTLD_LOCAL); REQUIRE(tools);
    typedef void *(*open_fn)(int, int, const char *, const char *, const char *, const char *, const char *, void **);
    open_fn open_driver = (open_fn)dlsym(tools, "adrenotools_open_libvulkan"); REQUIRE(open_driver);
    void *loader = open_driver(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, NULL,
        hook_root, driver_root, "libvulkan_freedreno.so", NULL, NULL); REQUIRE(loader);
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr"); REQUIRE(get);
    volkInitializeCustom(get);
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo application = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="WSI correctness fixture", .apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo=&application, .enabledExtensionCount=2, .ppEnabledExtensionNames=extensions};
    VkInstance instance; CHECK(vkCreateInstance(&instance_info, NULL, &instance)); volkLoadInstance(instance);
    uint32_t physical_count = 1; VkPhysicalDevice physical; CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    REQUIRE(physical_count == 1);
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface); REQUIRE(window);
    VkAndroidSurfaceCreateInfoKHR surface_info = {.sType=VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window=window};
    VkSurfaceKHR target; CHECK(vkCreateAndroidSurfaceKHR(instance, &surface_info, NULL, &target));
    uint32_t queue_count=0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, NULL);
    VkQueueFamilyProperties *queues = calloc(queue_count, sizeof *queues); REQUIRE(queues);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues);
    uint32_t family=UINT32_MAX;
    for (uint32_t n=0; n<queue_count; n++) {
        VkBool32 supported=0; CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical,n,target,&supported));
        if (supported && (queues[n].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family=n; break; }
    }
    free(queues); REQUIRE(family != UINT32_MAX);
    float priority=1;
    VkDeviceQueueCreateInfo queue_info = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority};
    const char *swap_extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_info = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1,
        .pQueueCreateInfos=&queue_info, .enabledExtensionCount=1, .ppEnabledExtensionNames=&swap_extension};
    VkDevice device; CHECK(vkCreateDevice(physical, &device_info, NULL, &device)); volkLoadDevice(device);
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkSurfaceCapabilitiesKHR cap; CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,target,&cap));
    uint32_t format_count=0; CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,target,&format_count,NULL));
    VkSurfaceFormatKHR *formats=calloc(format_count,sizeof *formats); REQUIRE(formats);
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,target,&format_count,formats));
    VkSurfaceFormatKHR format={0};
    for (uint32_t n=0;n<format_count;n++) if (formats[n].format==VK_FORMAT_R8G8B8A8_UNORM) {format=formats[n];break;}
    free(formats); REQUIRE(format.format==VK_FORMAT_R8G8B8A8_UNORM);
    REQUIRE(cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    uint32_t count=cap.minImageCount>3?cap.minImageCount:3;
    if(cap.maxImageCount && count>cap.maxImageCount) count=cap.maxImageCount;
    VkExtent2D extent=cap.currentExtent;
    if (extent.width==UINT32_MAX) extent=(VkExtent2D){ANativeWindow_getWidth(window),ANativeWindow_getHeight(window)};
    VkSwapchainCreateInfoKHR swap_info={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=target,
        .minImageCount=count,.imageFormat=format.format,.imageColorSpace=format.colorSpace,
        .imageExtent=extent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=cap.currentTransform,
        .compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
    VkSwapchainKHR swap; CHECK(vkCreateSwapchainKHR(device,&swap_info,NULL,&swap));
    CHECK(vkGetSwapchainImagesKHR(device,swap,&count,NULL)); VkImage *images=calloc(count,sizeof *images); REQUIRE(images);
    CHECK(vkGetSwapchainImagesKHR(device,swap,&count,images));
    fprintf(stderr,"WSI_SWAPCHAIN width=%u height=%u count=%u\n",extent.width,extent.height,count);
    VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool pool; CHECK(vkCreateCommandPool(device,&pool_info,NULL,&pool));
    VkCommandBufferAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device,&alloc,&command));
    VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
    CHECK(vkCreateFence(device,&fence_info,NULL,&fence));
    atomic_store(&selected_color,color&3); int last=-1; unsigned frames=0;
    while (!atomic_load(&stopping)) {
        uint32_t index; VkResult acquired=vkAcquireNextImageKHR(device,swap,UINT64_MAX,VK_NULL_HANDLE,fence,&index);
        REQUIRE(acquired==VK_SUCCESS || acquired==VK_SUBOPTIMAL_KHR);
        CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX)); CHECK(vkResetFences(device,1,&fence));
        CHECK(vkResetCommandPool(device,pool,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(command,&begin));
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=images[index],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        int selected=atomic_load(&selected_color)&3;
        VkClearColorValue value={.float32={selected==0 || selected==3,selected==1 || selected==3,selected==2 || selected==3,1}};
        vkCmdClearColorImage(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&barrier.subresourceRange);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=0;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barrier);
        CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
        CHECK(vkQueueSubmit(queue,1,&submit,fence)); CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));
        CHECK(vkResetFences(device,1,&fence));
        VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.swapchainCount=1,.pSwapchains=&swap,.pImageIndices=&index};
        VkResult result=vkQueuePresentKHR(queue,&present); REQUIRE(result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR);
        CHECK(vkQueueWaitIdle(queue)); frames++;
        if (selected!=last) { fprintf(stderr,"WSI_COLOR value=%d frames=%u\n",selected,frames);last=selected; }
        usleep(16000); /* Fixture pacing only; never used to calculate GPU time or FPS. */
    }
    CHECK(vkDeviceWaitIdle(device)); vkDestroyFence(device,fence,NULL); vkDestroyCommandPool(device,pool,NULL);
    vkDestroySwapchainKHR(device,swap,NULL); free(images); vkDestroyDevice(device,NULL);
    vkDestroySurfaceKHR(instance,target,NULL); vkDestroyInstance(instance,NULL); ANativeWindow_release(window);
    fprintf(stderr,"WSI_TERMINAL frames=%u game_modules_executed=0\n",frames);
    (*env)->ReleaseStringUTFChars(env,directory,dir); (*env)->ReleaseStringUTFChars(env,hooks,hook);
    /* Adreno-tools namespaces remain loaded until the fixture process exits. */
}
