// A native VkImage cannot be rebound when an ordinary Buffer window unions.
// Its own finite import names the SAME memfd pages, not a payload copy. The
// active Buffer spans still participate in all existing access/WB machinery.
typedef struct native_image_allocation {
  struct native_image_allocation *next;
  pool *owner;
  uint64_t identity,begin,end,address,bytes;
  vk_buffer storage;
  _Atomic int released;
} native_image_allocation;
static _Atomic uint64_t native_allocation_count,native_allocation_bytes;

static void native_allocation_collect_locked(pool *p) {
  native_image_allocation **at=&p->native_allocations;
  while(*at) {
    native_image_allocation *a=*at;
    if(atomic_load_explicit(&a->released,memory_order_acquire)!=2) {at=&a->next;continue;}
    *at=a->next;
    atomic_fetch_sub(&native_allocation_count,1);
    atomic_fetch_sub(&native_allocation_bytes,a->end-a->begin);
    vk_buffer_destroy(&a->storage);free(a);
  }
}
static int native_allocation_overlaps_locked(pool *p,uint64_t offset,uint64_t bytes) {
  for(native_image_allocation *a=p->native_allocations;a;a=a->next)
    if(!atomic_load_explicit(&a->released,memory_order_acquire) && a->begin<offset+bytes && a->end>offset)return 1;
  return 0;
}
static void native_allocation_cache_locked(pool *p,uint64_t offset,uint64_t bytes,int invalidate) {
  // Cache each actual imported allocation. A replacement Buffer memory object
  // is not a substitute for the immutable memory bound to the VkImage.
  for(native_image_allocation *a=p->native_allocations;a;a=a->next) {
    if(atomic_load_explicit(&a->released,memory_order_acquire) || a->begin>=offset+bytes || a->end<=offset)continue;
    uint64_t first=offset>a->begin?offset:a->begin,last=offset+bytes<a->end?offset+bytes:a->end;
    shared_cache(&a->storage,first-a->begin,last-first,invalidate);
  }
}
void vk_pool_native_image_collect(void) {
  pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next)native_allocation_collect_locked(p);
  pthread_mutex_unlock(&V.resources);
}
int vk_native_resources_active(void) {return native_resource_path!=NULL;}

#ifdef DM_VULKAN_TEST_EMBED
static int native_allocation_test_fail;
void vk_test_native_allocation_fail(int fail) {native_allocation_test_fail=fail;}
#endif
static void *native_image_try_bind(uint64_t address,uint64_t bytes,VkImage image,
    const VkMemoryRequirements *needs,VkDeviceMemory *memory,VkResult *failure) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  if(!p || !p->shared || p->shared_closing || !bytes || !needs || needs->size!=bytes ||
      !needs->alignment || (needs->alignment&(needs->alignment-1)) ||
      !needs->memoryTypeBits || !image || !memory)
    die("Native image requires live finite shared storage and exact requirements");
  uint64_t logical=address-(uintptr_t)p->guest;
  pool_span_piece current;
  if(shared_window_bytes) {
    if(!pool_span_resolve_locked(p,logical,bytes,&current))
      die("Native payload missing complete operation admission");
  } else shared_pool_materialize(p);
  uint64_t alignment=shared_alignment;
  if(needs->alignment>alignment)alignment=needs->alignment;
  uint64_t atom=V.props.limits.nonCoherentAtomSize;
  if(!atom || (atom&(atom-1)))die("Native allocation cache atom");
  if(atom>alignment)alignment=atom;
  if(address%needs->alignment || address>UINT64_MAX-bytes ||
      address+bytes>UINT64_MAX-(alignment-1))die("Native image allocation address/alignment");
  uint64_t first=address&~(alignment-1),last=(address+bytes+alignment-1)&~(alignment-1);
  if(first<(uintptr_t)p->guest || last-(uintptr_t)p->guest>p->size || last<=first)
    die("Native image import envelope outside Pool");
  void *alias=shared_import_host_pointer(first,last-first);
  if(!alias)die("Native image lacks committed RW memfd import alias");
  native_allocation_collect_locked(p);
  native_image_allocation *a=calloc(1,sizeof *a);
  VkResult result=a?VK_SUCCESS:VK_ERROR_OUT_OF_HOST_MEMORY;
  if(result==VK_SUCCESS)result=shared_buffer_create_types(alias,last-first,&a->storage,needs->memoryTypeBits);
#ifdef DM_VULKAN_TEST_EMBED
  if(result==VK_SUCCESS && native_allocation_test_fail)result=VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
  if(result==VK_SUCCESS && a->storage.properties!=p->shared_properties)result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
  if(result==VK_SUCCESS)result=vkBindImageMemory(V.device,image,a->storage.memory,address-first);
  if(result!=VK_SUCCESS) {
    if(a){vk_buffer_destroy(&a->storage);free(a);}
    *failure=result;pthread_mutex_unlock(&V.resources);return NULL;
  }
  // The common import helper uses a temporary VkBuffer for its exact memory
  // type/size contract. The image has its own successful binding now; retain
  // only that memory/map, not one unused VkBuffer object per texture.
  vkDestroyBuffer(V.device,a->storage.handle,NULL);a->storage.handle=VK_NULL_HANDLE;
  a->owner=p;a->identity=p->identity;a->begin=first-(uintptr_t)p->guest;a->end=last-(uintptr_t)p->guest;
  a->address=address;a->bytes=bytes;a->storage.map=p->guest+a->begin;
  a->next=p->native_allocations;p->native_allocations=a;
  atomic_fetch_add(&p->native_image_references,1);
  atomic_fetch_add(&native_allocation_count,1);atomic_fetch_add(&native_allocation_bytes,last-first);
  *memory=a->storage.memory;
  pthread_mutex_unlock(&V.resources);return a;
}
void *vk_pool_native_image_bind(uint64_t address,uint64_t bytes,VkImage image,
    const VkMemoryRequirements *needs,VkDeviceMemory *memory) {
  VkResult failure=VK_SUCCESS;
  void *token=native_image_try_bind(address,bytes,image,needs,memory,&failure);
  if(!token)die("Native finite import/bind failed: %d; no whole import fallback",failure);
  return token;
}
#ifdef DM_VULKAN_TEST_EMBED
void *vk_test_native_image_try_bind(uint64_t address,uint64_t bytes,VkImage image,
    const VkMemoryRequirements *needs,VkDeviceMemory *memory,VkResult *failure) {
  return native_image_try_bind(address,bytes,image,needs,memory,failure);
}
#endif
void vk_pool_native_image_use(void *token,uint64_t address,uint64_t bytes,VkPipelineStageFlags stages) {
  native_image_allocation *a=token;
  pthread_mutex_lock(&V.resources);pool *p=a?a->owner:NULL;
  if(!a || atomic_load(&a->released) || p!=pool_find(address,bytes) ||
      a->identity!=p->identity || !p->shared || p->shared_closing || address!=a->address || bytes!=a->bytes)
    die("Native image retained allocation/Pool lifetime mismatch");
  pool_span_piece active;
  if(!pool_span_resolve_locked(p,address-(uintptr_t)p->guest,bytes,&active))
    die("Native image use missing active Buffer cache/access coverage");
  record_pool_use(p,address-(uintptr_t)p->guest,bytes,stages,1);
  pthread_mutex_unlock(&V.resources);
}
int vk_pool_native_image_write_ready(void *token) {
  native_image_allocation *a=token;
  if(!a || atomic_load(&a->released))return 0;
  return (a->storage.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || vk_pool_shared_gpu_gate_mode();
}
void vk_pool_native_image_release(void *token) {
  // Can be called while resources is held or by the completion worker. Never
  // acquire it here: original physical image/scope retirement has completed.
  native_image_allocation *a=token;
  if(!a || atomic_exchange_explicit(&a->released,1,memory_order_release))
    die("Native image allocation release mismatch");
  if(!atomic_fetch_sub(&a->owner->native_image_references,1))die("Native image Pool reference underflow");
  atomic_store_explicit(&a->released,2,memory_order_release);
  // Last access above publishes reclaimability; collector cannot free midway.
  // Owner maintenance/Pool close collects the released token. No extra waits.
}
void vk_native_allocation_stats(uint64_t *count,uint64_t *bytes,uint64_t *metadata_bytes) {
  *count=atomic_load(&native_allocation_count);*bytes=atomic_load(&native_allocation_bytes);
  *metadata_bytes=*count*sizeof(native_image_allocation);
}
#ifdef DM_VULKAN_TEST_EMBED
void vk_test_native_allocation_binding(void *token,VkDeviceMemory *memory,uint64_t *offset) {
  native_image_allocation *a=token;
  if(!a || atomic_load(&a->released))die("Native binding test outside lifetime");
  *memory=a->storage.memory;*offset=a->address-(uintptr_t)a->owner->guest-a->begin;
}
void vk_test_native_allocation_stats(uint64_t *count,uint64_t *bytes) {
  *count=atomic_load(&native_allocation_count);*bytes=atomic_load(&native_allocation_bytes);
}
#endif
