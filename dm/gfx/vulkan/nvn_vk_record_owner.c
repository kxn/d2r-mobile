#include "nvn_vk_record_owner.h"
#include <stdatomic.h>
static atomic_int record_owners_enabled;
static atomic_uint_fast64_t record_object_epoch=1;
void vk_record_object_tracking_enable(void) {
  atomic_store_explicit(&record_owners_enabled,1,memory_order_release);
}
void vk_record_object_retired(void) {
  if(!atomic_load_explicit(&record_owners_enabled,memory_order_relaxed))return;
  uint64_t before=atomic_fetch_add_explicit(&record_object_epoch,1,memory_order_release);
  if(before==UINT64_MAX)die("Vulkan retained recording object epoch overflow");
}
uint64_t vk_record_object_epoch(void) {
  return atomic_load_explicit(&record_object_epoch,memory_order_acquire);
}
typedef struct {
  VkCommandBuffer command;
  uint64_t epoch;
  int recording,executable;
} record_slot;
struct vk_record_owner {
  VkCommandPool pool;
  uint64_t pending_serial,reclaimed_serial;
  unsigned count,open_slot; // 0 none, otherwise the sole recording slot + 1
  record_slot slots[];
};
vk_record_owner *vk_record_owner_create(unsigned slots) {
  if(!slots || slots>VK_RECORD_OWNER_MAX_SLOTS)die("Vulkan retained recording slot bound");
  vk_record_owner *owner=calloc(1,sizeof *owner+slots*sizeof *owner->slots);
  if(!owner)die("Vulkan retained recording owner allocation");
  VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=V.queue_family};
  VK_CHECK(vkCreateCommandPool(V.device,&ci,NULL,&owner->pool));owner->count=slots;
  vk_record_object_tracking_enable();return owner;
}
static record_slot *slot_at(vk_record_owner *owner,unsigned slot) {
  if(!owner || slot>=owner->count)die("Vulkan retained recording slot outside owner");
  return &owner->slots[slot];
}
int vk_record_owner_available(const vk_record_owner *owner) {return owner && !owner->pending_serial;}
int vk_record_owner_forget(vk_record_owner *owner) {
  if(!vk_record_owner_available(owner))return 0;
  if(owner->open_slot)return 0;
  VK_CHECK(vkResetCommandPool(V.device,owner->pool,0));
  for(unsigned i=0;i<owner->count;i++) {
    owner->slots[i].epoch=0;owner->slots[i].executable=0;
  }
  return 1;
}
VkCommandBuffer vk_record_owner_begin(vk_record_owner *owner,unsigned slot,
    const VkCommandBufferInheritanceInfo *inherit) {
  record_slot *s=slot_at(owner,slot);
  if(!vk_record_owner_available(owner))return VK_NULL_HANDLE;
  // Queries and multi-subpass/local rendering retain their original path.
  if(!inherit || inherit->sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO ||
      inherit->pNext || !inherit->renderPass || inherit->subpass ||
      inherit->occlusionQueryEnable || inherit->queryFlags || inherit->pipelineStatistics)
    return VK_NULL_HANDLE;
  if(owner->open_slot)die("Vulkan nested retained recording");
  if(!s->command) {
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool=owner->pool,.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY,.commandBufferCount=1};
    VK_CHECK(vkAllocateCommandBuffers(V.device,&ai,&s->command));
  } else VK_CHECK(vkResetCommandBuffer(s->command,0));
  VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags=VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,.pInheritanceInfo=inherit};
  s->executable=0;s->recording=1;s->epoch=vk_record_object_epoch();owner->open_slot=slot+1;
  VK_CHECK(vkBeginCommandBuffer(s->command,&bi));return s->command;
}
int vk_record_owner_end(vk_record_owner *owner,unsigned slot) {
  record_slot *s=slot_at(owner,slot);
  if(!s->recording || owner->pending_serial || owner->open_slot!=slot+1)die("Vulkan retained recording end ownership");
  VK_CHECK(vkEndCommandBuffer(s->command));s->recording=0;owner->open_slot=0;
  s->executable=s->epoch==vk_record_object_epoch();return s->executable;
}
VkCommandBuffer vk_record_owner_cached(vk_record_owner *owner,unsigned slot) {
  record_slot *s=slot_at(owner,slot);
  return !owner->pending_serial && s->executable && !s->recording &&
      s->epoch==vk_record_object_epoch()?s->command:VK_NULL_HANDLE;
}
void vk_record_owner_pending(vk_record_owner *owner,uint64_t queue_serial) {
  if(!owner || owner->pending_serial || !queue_serial || queue_serial<=owner->reclaimed_serial)
    die("Vulkan retained recording pending serial ownership");
  if(owner->open_slot)die("Vulkan retained recording submitted while open");
  owner->pending_serial=queue_serial;
}
void vk_record_owner_reclaim(vk_record_owner *owner,uint64_t queue_serial) {
  if(!owner || !owner->pending_serial || queue_serial!=owner->pending_serial)
    die("Vulkan retained recording completion ownership");
  owner->reclaimed_serial=queue_serial;owner->pending_serial=0;
}
void vk_record_owner_destroy(vk_record_owner *owner) {
  if(!owner)return;
  if(owner->pending_serial)die("Vulkan retained recording destroy while pending");
  if(owner->open_slot)die("Vulkan retained recording destroy while recording");
  vkDestroyCommandPool(V.device,owner->pool,NULL);free(owner);
}
