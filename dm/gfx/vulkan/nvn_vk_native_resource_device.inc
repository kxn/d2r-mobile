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
  // Same audited HOST_ALLOCATION/coherent contract, independently validated
  // whole-linear ASTC layout. Unknown driver builds keep FOREIGN handoffs.
  static const uint8_t coherent_turnip_linear[32]={
    0xf8,0x30,0x28,0x5f,0xe6,0x8f,0x0a,0x74,0x47,0x92,0x26,0x9d,0xa5,0xf1,0x60,0xed,
    0x86,0x36,0xc1,0x72,0xb1,0xf6,0x51,0x1d,0xb9,0xd9,0x94,0x78,0xe1,0xa2,0xac,0xce};
  // Isolated Android WSI/AHB usage candidate. Its complete frozen source
  // comparison retains the f830 coherent host-import and ASTC layout code;
  // regenerated producer declarations must still authenticate this exact ELF.
  // UBWC requests are cold/default-OFF and do not touch native ASTC images.
  static const uint8_t coherent_turnip_wsi[32]={
    0xb3,0xfc,0xe5,0xab,0x29,0xc6,0x61,0x7b,0xd2,0xcf,0x33,0x4b,0x3a,0x56,0xc6,0x79,
    0x51,0xde,0x27,0x1b,0xd8,0x70,0x87,0x8c,0x9a,0x31,0x21,0xff,0x60,0x6b,0x1b,0x9f};
  // BC3 admission-only derivative of B3. Complete source comparison retains
  // host import, coherency and layout code; ordinary-UID original NVN tests
  // cover all mips, tail, GPU alias writers, CPU Flush and retirement.
  // Runtime identity and live shared startup admission remain mandatory.
  static const uint8_t coherent_turnip_bc3[32]={
    0x0e,0x53,0xff,0xa2,0xbb,0xbf,0xf0,0xa4,0xf5,0x20,0xeb,0x08,0x00,0x39,0xa8,0x48,
    0xf0,0x85,0x32,0x76,0xec,0xe5,0x9c,0xaa,0x8d,0xf9,0xa0,0x29,0xcf,0x2b,0x40,0x24};
  // Same host allocation/coherent contract, with BC3 returned to the original
  // FDL tiling path. Its distinct layout UUID and freshly baked payloads passed
  // real ordinary-UID NVN mip/baseMip, tail and writer regressions. No old
  // whole-linear payload is accepted under this ELF identity.
  static const uint8_t coherent_turnip_bc3_tiled[32]={
    0x3e,0x93,0x76,0xed,0x37,0x59,0xaf,0x2c,0xd0,0x15,0xcf,0x56,0x08,0x27,0x4f,0x66,
    0x1b,0xa3,0xb3,0x3d,0xf6,0xe9,0x5c,0xb6,0xfa,0x25,0x42,0x66,0x6f,0x34,0xee,0x50};
  // Exact 0e53 derivative: only the Android allowlist for the implemented KHR
  // internally-synchronized-queues extension changed. Full source and generated
  // table comparison retain the host-import/coherent/image contract. This is
  // not permission to load an old table under a different driver SHA: the
  // strict table installer above still requires the actual consumer identity.
  static const uint8_t coherent_turnip_bc3_queue_sync[32]={
    0x12,0xb8,0xb5,0x4a,0x25,0x14,0xff,0x1c,0xdf,0x8d,0xa4,0x22,0xc7,0x1d,0xb7,0x9a,
    0x68,0x3d,0x45,0x17,0xeb,0x9a,0xec,0xfb,0xb2,0x0a,0x41,0x25,0x52,0x25,0x59,0xf5};
  // Exact compiler-only derivative of 12b8: host-import/coherent/image sources
  // are unchanged, actual UUIDs match, and readonly buffer data tests passed.
  // This does not authorize old table headers or bypass live shared startup.
  static const uint8_t coherent_turnip_readonly_vec4[32]={
    0x82,0xf2,0x9f,0xeb,0xf9,0x53,0xf9,0xe3,0xc4,0x89,0x1c,0xe3,0xd7,0x93,0xc1,0x71,
    0x36,0x6a,0xf9,0x65,0xef,0x1b,0x25,0xa2,0xfb,0xdb,0x17,0x38,0xd7,0x25,0xf3,0xcb};
  native_resident_driver=!memcmp(identity.driver_sha256,coherent_turnip,32) ||
      !memcmp(identity.driver_sha256,coherent_turnip_linear,32) ||
      !memcmp(identity.driver_sha256,coherent_turnip_wsi,32) ||
      !memcmp(identity.driver_sha256,coherent_turnip_bc3,32) ||
      !memcmp(identity.driver_sha256,coherent_turnip_bc3_tiled,32) ||
      !memcmp(identity.driver_sha256,coherent_turnip_bc3_queue_sync,32) ||
      !memcmp(identity.driver_sha256,coherent_turnip_readonly_vec4,32);
  // One cold identity record for the existing explicit resource contract.
  printf("{\"native_resource_identity\":true,\"layout_UUID\":\"");
  for(unsigned n=0;n<16;n++)printf("%02x",identity.layout[n]);
  printf("\",\"device_UUID\":\"");for(unsigned n=0;n<16;n++)printf("%02x",identity.device[n]);
  printf("\",\"driver_UUID\":\"");for(unsigned n=0;n<16;n++)printf("%02x",identity.driver[n]);
  printf("\",\"driver_sha256\":\"");for(unsigned n=0;n<32;n++)printf("%02x",identity.driver_sha256[n]);
  printf("\"}\n");
  LOG("[native resources] fixed table installed, %zu metadata bytes; mutable NVN synchronization remains required",bytes);
}
