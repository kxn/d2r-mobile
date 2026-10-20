// Included only by the explicit native-resource build. Everything here is a
// startup operation; no environment/file/hash operation is on a draw path.
#include "../nvn/nvn_native_layout.h"
#include <fcntl.h>
#include <sys/stat.h>
static const char *native_resource_path;
// This first residency experiment is limited to the exact coherent Turnip
// provider whose native payload/cache contract was audited. Cold identity only.
static int native_resident_driver;
static uint8_t native_pack_digest[32],native_producer_digest[32];
static VkPhysicalDeviceHostImageCopyFeaturesEXT native_host_copy={
  .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT};
static void native_digest_parse(const char *hex,uint8_t out[32]) {
  if(!hex || strlen(hex)!=64)die("Native resource requires a complete SHA256");
  for(unsigned n=0;n<32;n++) {
    unsigned value=0;
    for(unsigned k=0;k<2;k++) {char c=hex[n*2+k];unsigned x=c>='0'&&c<='9'?c-'0':
      c>='a'&&c<='f'?c-'a'+10:16;
      if(x==16)die("Native resource digest must be lowercase hexadecimal");value=(value<<4)|x;}
    out[n]=(uint8_t)value;
  }
}
static void native_resource_options(const VkExtensionProperties *available,uint32_t count) {
  native_resource_path=getenv("DM_VULKAN_NATIVE_RESOURCE_TABLE");
  if(!native_resource_path || !*native_resource_path) {native_resource_path=NULL;return;}
  native_resource_path=strdup(native_resource_path);
  if(!native_resource_path)die("Native resource path snapshot allocation");
  if(native_resource_path[0]!='/' || strlen(native_resource_path)>=4096 || !shared_requested)die("Native resource table requires absolute path and fixed shared mode");
  native_digest_parse(getenv("DM_VULKAN_NATIVE_PACK_SHA256"),native_pack_digest);
  native_digest_parse(getenv("DM_VULKAN_NATIVE_PRODUCER_SHA256"),native_producer_digest);
  const char *required[]={VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,
    VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
  for(unsigned r=0;r<sizeof required/sizeof *required;r++) {
    int found=0;for(uint32_t n=0;n<count;n++)if(!strcmp(required[r],available[n].extensionName))found=1;
    if(!found)die("Native resource missing device extension: %s",required[r]);
  }
  VkPhysicalDeviceFeatures2 feature={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&native_host_copy};
  vkGetPhysicalDeviceFeatures2(V.physical,&feature);
  if(!native_host_copy.hostImageCopy)die("Native resource hostImageCopy unavailable");
}
static void native_resource_install(void) {
  if(!native_resource_path)return;
  if(!shared_active || !vkGetImageSubresourceLayout2EXT)die("Native resource shared mode/layout API unavailable");
  VkPhysicalDeviceIDProperties ids={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceHostImageCopyPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT,.pNext=&ids};
  VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&host};
  vkGetPhysicalDeviceProperties2(V.physical,&props);
  nvn_native_identity identity={0};memcpy(identity.layout,host.optimalTilingLayoutUUID,16);
  memcpy(identity.device,ids.deviceUUID,16);memcpy(identity.driver,ids.driverUUID,16);
  memcpy(identity.producer_sha256,native_producer_digest,32);
  if(!vk_loader_native_driver_digest(identity.driver_sha256))die("Native resource active driver ELF is not authenticated");
  int fd=open(native_resource_path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
  if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<(off_t)sizeof(nvn_native_table) ||
     st.st_size>(off_t)(sizeof(nvn_native_table)+(size_t)NVN_NATIVE_MAX_ENTRIES*(sizeof(nvn_native_entry)+sizeof(nvn_native_family))))
    die("Native resource table file bounds");
  size_t bytes=(size_t)st.st_size,done=0;uint8_t *data=malloc(bytes);
  if(!data)die("Native resource table allocation");
  while(done<bytes) {ssize_t n=read(fd,data+done,bytes-done);if(n<0 && errno==EINTR)continue;
    if(n<=0)die("Native resource table truncated");done+=(size_t)n;}
  uint8_t extra;if(read(fd,&extra,1)!=0 || close(fd))die("Native resource table changed during read");
  int installed=nvn_native_table_install(data,bytes,native_pack_digest,&identity);free(data);
  if(!installed)die("Native resource table identity/producer/pack/recipe rejected");
  static const uint8_t coherent_turnip[32]={
    0xaa,0x18,0x53,0x74,0xef,0x6c,0x11,0x35,0xdd,0xcb,0xff,0x08,0x50,0x6a,0xa4,0xdc,
    0x72,0x0e,0x29,0xb8,0x2e,0x45,0xa9,0xae,0x9f,0x5c,0xe7,0x67,0xf5,0x45,0x64,0xdb};
  native_resident_driver=!memcmp(identity.driver_sha256,coherent_turnip,32);
  // One cold identity record for the existing explicit resource contract.
  printf("{\"native_resource_identity\":true,\"layout_UUID\":\"");
  for(unsigned n=0;n<16;n++)printf("%02x",identity.layout[n]);
  printf("\",\"device_UUID\":\"");for(unsigned n=0;n<16;n++)printf("%02x",identity.device[n]);
  printf("\",\"driver_UUID\":\"");for(unsigned n=0;n<16;n++)printf("%02x",identity.driver[n]);
  printf("\",\"driver_sha256\":\"");for(unsigned n=0;n<32;n++)printf("%02x",identity.driver_sha256[n]);
  printf("\"}\n");
  LOG("[native resources] fixed table installed, %zu metadata bytes; mutable NVN synchronization remains required",bytes);
}
