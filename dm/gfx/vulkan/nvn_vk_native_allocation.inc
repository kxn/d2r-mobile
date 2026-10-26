// A native VkImage cannot be rebound when an ordinary Buffer window unions.
// Its own finite import names the SAME memfd pages, not a payload copy. The
// active Buffer spans still participate in all existing access/WB machinery.
typedef struct native_image_allocation {
  struct native_image_allocation *next;
  pool *owner;
  uint64_t identity,begin,end,address,bytes;
  vk_buffer storage;
  alias_range_node range;
  _Atomic int released;
} native_image_allocation;
_Static_assert(sizeof(native_image_allocation)==192,"Native import node: original184B + Buffer-owned view pointer");
static _Atomic uint64_t native_allocation_count,native_allocation_bytes;

// Real physical allocation index, not an authority/cache shortcut. All changes
// and queries hold resources. Existing max_end AVL prunes earlier envelopes;
// traversal visits only O(logN + actual overlaps), including equal starts.
static int native_allocation_alive(const alias_range_node *node,int unused) {
  (void)unused;native_image_allocation *a=node->owner;
  return !atomic_load_explicit(&a->released,memory_order_acquire);
}
static uint64_t native_allocation_cover_end(const alias_range_node *node,uint64_t at,uint64_t end) {
  if(!node || node->max_end<=at)return at;
  uint64_t covered=native_allocation_cover_end(node->left,at,end);
  if(node->start>=end)return covered;
  if(node->end>at && native_allocation_alive(node,0) && node->end>covered)covered=node->end;
  uint64_t right=native_allocation_cover_end(node->right,at,end);
  return right>covered?right:covered;
}
static int shared_pool_storage_covered_locked(pool *p,uint64_t offset,uint64_t bytes) {
  if(!bytes || offset>p->size || bytes>p->size-offset)return 0;
  uint64_t at=offset,end=offset+bytes;pool_backing_span *b=p->backings;
  if(p->shared_lazy_eligible) {
    while(at<end) {
      uint64_t next=native_allocation_cover_end(p->native_allocation_index,at,at+1);
      uint64_t buffer_end=shared_buffer_cover_end(p->shared_buffer_index,at);
      if(buffer_end>next)next=buffer_end;
      if(next==at)return 0;
      at=next;
    }
    return 1;
  }
  while(at<end) {
    // All native intervals containing this exact point may extend coverage;
    // then advance to the next boundary, never scan unrelated native records.
    uint64_t next=native_allocation_cover_end(p->native_allocation_index,at,at+1);
    while(b && b->begin<=at) {if(b->end>next)next=b->end;b=b->next;}
    if(next==at)return 0;
    at=next;
  }
  return 1;
}
static void native_allocation_collect_locked(pool *p) {
  // All live imports can remain resident for many submissions. No release
  // notification means no node can be reclaimable; avoid scanning them all.
  // Keep the original list/index consistency check even on this empty path.
  if(!atomic_load_explicit(&p->native_retire_pending,memory_order_acquire)) {
    if(!p->native_allocations && p->native_allocation_index)die("Native allocation index outlived imports");
    return;
  }
  native_image_allocation **at=&p->native_allocations;
  while(*at) {
    native_image_allocation *a=*at;
    if(atomic_load_explicit(&a->released,memory_order_acquire)!=2) {at=&a->next;continue;}
    *at=a->next;
    int removed=0;p->native_allocation_index=alias_range_remove(p->native_allocation_index,&a->range,&removed);
    if(!removed)die("Native allocation index retirement mismatch");
    // A publisher paused in released=1 keeps its notification. Never reset
    // a pending flag after a scan: released=2 may be published concurrently.
    if(!atomic_fetch_sub_explicit(&p->native_retire_pending,1,memory_order_acq_rel))
      die("Native allocation retirement notification underflow");
    atomic_fetch_sub(&native_allocation_count,1);
    atomic_fetch_sub(&native_allocation_bytes,a->end-a->begin);
    vk_buffer_destroy(&a->storage);free(a);
  }
  if(!p->native_allocations && p->native_allocation_index)die("Native allocation index outlived imports");
}
static int native_allocation_overlaps_locked(pool *p,uint64_t offset,uint64_t bytes) {
  return alias_range_overlap(p->native_allocation_index,offset,offset+bytes,native_allocation_alive,0);
}
static void native_allocation_cache_visit(const alias_range_node *node,uint64_t offset,uint64_t end,int invalidate) {
  if(!node || node->max_end<=offset)return;
  native_allocation_cache_visit(node->left,offset,end,invalidate);
  if(node->start>=end)return;
  native_image_allocation *a=node->owner;
  if(node->end>offset && native_allocation_alive(node,0)) {
    uint64_t first=offset>a->begin?offset:a->begin,last=end<a->end?end:a->end;
    shared_cache(&a->storage,first-a->begin,last-first,invalidate);
  }
  native_allocation_cache_visit(node->right,offset,end,invalidate);
}
static void native_allocation_cache_locked(pool *p,uint64_t offset,uint64_t bytes,int invalidate) {
  // Every live Buffer/native import in this Pool has exactly these memory
  // properties: both import publishers reject a mismatch while holding
  // resources. For coherent memory shared_cache() is a no-op for every
  // intersecting allocation, so avoid walking the allocation index at all.
  // This is only cache maintenance: the caller still validates the logical
  // range and proves complete physical coverage, and all access histories,
  // submission ordering and CPU publication remain unchanged.
  if(p->shared_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)return;
  // Every intersecting immutable VkMemory has its own required cache op;
  // one canonical Buffer's memory cannot replace this alias visibility.
  native_allocation_cache_visit(p->native_allocation_index,offset,offset+bytes,invalidate);
}
void vk_pool_native_image_collect(void) {
  pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next)native_allocation_collect_locked(p);
  pthread_mutex_unlock(&V.resources);
}
int vk_native_resources_active(void) {return native_resource_path!=NULL;}

int vk_pool_native_image_acquire_range(void *token,uint64_t address,uint64_t bytes,
                                     uint64_t *first,uint64_t *length) {
  native_image_allocation *a=token;
  if(!a || !first || !length)return 0;
  pthread_mutex_lock(&V.resources);pool *p=a->owner;
  int valid=!atomic_load(&a->released) && p && a->identity==p->identity &&
    !p->shared_closing && p->shared && address==a->address && bytes==a->bytes;
  if(valid) {
    // Registration alignment is not the image's binding range, but using the
    // whole immutable import envelope is a cheap stronger negative proof.
    // Optimal image vs linear Buffer aliasing additionally pads binding ranges
    // to bufferImageGranularity; neither it nor host import alignment is
    // universally bounded by the OS page size.
    uint64_t granularity=V.props.limits.bufferImageGranularity;
    uint64_t page=(uint64_t)getpagesize(),atom=V.props.limits.nonCoherentAtomSize;
    if(page>granularity)granularity=page;
    if(atom>granularity)granularity=atom;
    uint64_t base=(uintptr_t)p->guest,lo=base+a->begin,hi=base+a->end;
    valid=granularity && !(granularity&(granularity-1)) && hi>=lo &&
      hi<=UINT64_MAX-(granularity-1);
    if(valid) {*first=lo&~(granularity-1);*length=((hi+granularity-1)&~(granularity-1))-*first;}
  }
  pthread_mutex_unlock(&V.resources);return valid;
}

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

  if(shared_window_bytes) {
    // The operation retains the exact logical Pool. This immutable image
    // import supplies physical coverage; no VkBuffer consumer is implied.
    if(p->shared_lazy_eligible)shared_lazy_pin_locked(p);
    else if(!pool_operation_retains_locked(p))die("Native payload missing retained logical operation admission");
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
  if(result==VK_SUCCESS && p->shared_lazy_eligible &&
      !(a->storage.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
  if(result==VK_SUCCESS && shared_pool_has_imports_locked(p) &&
      a->storage.properties!=p->shared_properties)result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
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
  if(!shared_pool_has_imports_locked(p))p->shared_properties=a->storage.properties;
  a->range=(alias_range_node){.start=a->begin,.end=a->end,.max_end=a->end,
    .serial=a->address,.owner=a,.height=1};
  int inserted=0;p->native_allocation_index=alias_range_insert(p->native_allocation_index,&a->range,&inserted);
  if(!inserted)die("Native allocation index publication mismatch");
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
static int native_allocation_write_range_locked(pool *p,void *token,uint64_t address,uint64_t bytes) {
  native_image_allocation *a=token;
  return a && p==a->owner && a->identity==p->identity && p->shared && !p->shared_closing &&
      !atomic_load_explicit(&a->released,memory_order_acquire) && bytes && address>=a->address &&
      address-a->address<=a->bytes && bytes<=a->bytes-(address-a->address);
}
// The per-chunk native stage ledger may skip a repeated GPU-use record, but
// a NEW operation still needs its own Pool borrow across reentrant submits.
// This is only lifetime metadata: do not move stage/page visibility forward.
// Cold group construction only. The actual consumer retained this operation
// before capture; post-draw publication will have acquired its recording ref.
void *vk_pool_native_image_operation_owner(void *token) {
  native_image_allocation *a=token;
  pthread_mutex_lock(&V.resources);
  pool *p=a?a->owner:NULL;
  if(!a || atomic_load(&a->released) || !p || a->identity!=p->identity ||
      p->shared_closing || (shared_lazy_active && !pool_operation_retains_locked(p)))
    die("Native read group cold owner lacks operation lifetime");
  pthread_mutex_unlock(&V.resources);return p;
}
void vk_pool_native_image_operation_retain(void *token) {
  if(!shared_lazy_active || !pool_operation_depth)return;
  native_image_allocation *a=token;
  if(!a)die("Shared lazy native operation requires live allocation");
  pool *p=a->owner;
  pool_operation_plan *plan=pool_operation_plans[pool_operation_depth-1];
  for(size_t k=0;k<plan->pin_count;k++)if(plan->pins[k]==p)return;
  pthread_mutex_lock(&V.resources);
  if(atomic_load_explicit(&a->released,memory_order_acquire) ||
      p!=pool_find(a->address,a->bytes) || a->identity!=p->identity ||
      !p->shared_lazy_eligible || p->shared_closing)
    die("Shared lazy native operation identity/closing mismatch");
  shared_lazy_pin_locked(p);
  pthread_mutex_unlock(&V.resources);
}
void vk_pool_native_image_use(void *token,uint64_t address,uint64_t bytes,VkPipelineStageFlags stages) {
  native_image_allocation *a=token;
  pthread_mutex_lock(&V.resources);pool *p=a?a->owner:NULL;
  if(!a || atomic_load(&a->released) || p!=pool_find(address,bytes) ||
      a->identity!=p->identity || !p->shared || p->shared_closing || address!=a->address || bytes!=a->bytes)
    die("Native image retained allocation/Pool lifetime mismatch");
  // Token identity, exact immutable memory and retained logical Pool are the
  // physical proof. Real Buffer callers still require canonical span lookup.
  if(address<(uintptr_t)p->guest+a->begin || bytes>a->end-(address-(uintptr_t)p->guest))
    die("Native image use outside immutable import coverage");
  record_pool_use(p,address-(uintptr_t)p->guest,bytes,stages,1);
  pthread_mutex_unlock(&V.resources);
}
int vk_pool_native_image_queue_resident(void *token) {
  native_image_allocation *a=token;
  return native_resident_driver && a && !atomic_load(&a->released) &&
      (a->storage.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}
#ifdef DM_VULKAN_TEST_EMBED
// Explicit host oracle policy override, not a production driver identity claim.
// Coherent memory and real imported allocation lifetime remain mandatory.
int vk_test_native_resident_provider(int enabled) {
  int previous=native_resident_driver;native_resident_driver=enabled;return previous;
}
#endif
int vk_pool_native_image_write_ready(void *token) {
  native_image_allocation *a=token;
  if(!a || atomic_load(&a->released))return 0;
  return (a->storage.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || vk_pool_shared_gpu_gate_mode();
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Owned handshake only. Set with an idle recorder before starting a releaser;
// clear after it joins/completes. It is absent from every Android APP build.
static void (*native_retire_test_pause)(void *,void *);
static void *native_retire_test_context;
void vk_test_native_retire_pause(void (*pause)(void *,void *),void *context) {
  native_retire_test_pause=pause;native_retire_test_context=context;
}
void vk_test_native_retire_state(uint64_t address,uint64_t *pending,unsigned *live,unsigned *nodes) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  if(!p)die("Native retirement test Pool missing");
  *pending=atomic_load(&p->native_retire_pending);*live=atomic_load(&p->native_image_references);*nodes=0;
  for(native_image_allocation *a=p->native_allocations;a;a=a->next)(*nodes)++;
  pthread_mutex_unlock(&V.resources);
}
#endif
void vk_pool_native_image_release(void *token) {
  // Can be called while resources is held or by the completion worker. Never
  // acquire it here: original physical image/scope retirement has completed.
  native_image_allocation *a=token;
  if(!a || atomic_exchange_explicit(&a->released,1,memory_order_release))
    die("Native image allocation release mismatch");
  // Register before dropping the Pool's last live image reference and before
  // the final publication. The collector may free a immediately after 2; no
  // access to a or its Pool is permitted after that final store.
  pool *p=a->owner;
  if(atomic_fetch_add_explicit(&p->native_retire_pending,1,memory_order_release)==UINT64_MAX)
    die("Native allocation retirement notification overflow");
  if(!atomic_fetch_sub(&p->native_image_references,1))die("Native image Pool reference underflow");
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(native_retire_test_pause)native_retire_test_pause(a,native_retire_test_context);
#endif
  atomic_store_explicit(&a->released,2,memory_order_release);
  // Last access above publishes reclaimability; collector cannot free midway.
  // Owner maintenance/Pool close collects the released token. No extra waits.
}
void vk_native_allocation_stats(uint64_t *count,uint64_t *bytes,uint64_t *metadata_bytes) {
  *count=atomic_load(&native_allocation_count);*bytes=atomic_load(&native_allocation_bytes);
  *metadata_bytes=*count*sizeof(native_image_allocation);
}
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_test_native_import_index(void) {
  // Exact set oracle, independent of image recipes or CPU contents. Actual
  // GPU imports/pixels and later Buffer unions are tested by native_lane.
  enum {N=32};native_image_allocation a[N]={0};pool p={.size=4096};unsigned checks=0;
  for(unsigned k=0;k<N;k++) {
    unsigned n=(k*13)%N;uint64_t first=(n%8)*32,length=(n/8+1)*24;
    a[n].begin=first;a[n].end=first+length;
    a[n].range=(alias_range_node){.start=first,.end=first+length,.max_end=first+length,.serial=n,.owner=&a[n],.height=1};
    int inserted=0;p.native_allocation_index=alias_range_insert(p.native_allocation_index,&a[n].range,&inserted);
    if(!inserted)die("Native allocation test index duplicate");checks++;
  }
  for(unsigned pass=0;pass<3;pass++) {
    for(uint64_t first=0;first<384;first+=3)for(uint64_t length=1;length<65;length+=7) {
      uint64_t next=first;int overlaps=0;
      for(unsigned n=0;n<N;n++)if(!atomic_load(&a[n].released) && a[n].begin<first+1 && a[n].end>first) {
        if(a[n].end>next)next=a[n].end;
      }
      for(unsigned n=0;n<N;n++)if(!atomic_load(&a[n].released) && a[n].begin<first+length && a[n].end>first)overlaps=1;
      if(native_allocation_cover_end(p.native_allocation_index,first,first+1)!=next ||
          native_allocation_overlaps_locked(&p,first,length)!=overlaps)die("Native allocation indexed point/overlap mismatch");checks++;
      uint64_t at=first;
      while(at<first+length) {
        uint64_t end=at;
        for(unsigned n=0;n<N;n++)if(!atomic_load(&a[n].released) && a[n].begin<=at && a[n].end>end)end=a[n].end;
        if(end==at)break;
        at=end;
      }
      if(shared_pool_storage_covered_locked(&p,first,length)!=(at>=first+length))die("Native allocation union coverage mismatch");checks++;
    }
    if(pass==0)for(unsigned n=0;n<N;n+=2)atomic_store(&a[n].released,2);
    if(pass==1)for(unsigned k=0;k<N/2;k++) {
      unsigned n=(k*5)%16*2;int removed=0;
      p.native_allocation_index=alias_range_remove(p.native_allocation_index,&a[n].range,&removed);
      if(!removed)die("Native allocation test remove mismatch");checks++;
    }
  }
  for(unsigned n=1;n<N;n+=2) {
    atomic_store(&a[n].released,2);int removed=0;
    p.native_allocation_index=alias_range_remove(p.native_allocation_index,&a[n].range,&removed);
    if(!removed)die("Native allocation final retirement mismatch");checks++;
  }
  if(p.native_allocation_index || shared_pool_storage_covered_locked(&p,0,1))die("Native allocation index remained after release");checks++;
  return checks;
}
#if !defined(DM_ANDROID_APP)
unsigned vk_test_shared_buffer_coverage_index(void) {
  // Exact set oracle, independent of image recipes or CPU contents. Actual
  // GPU imports/pixels and later Buffer unions are tested by native_lane.
  enum {N=32,B=8};native_image_allocation a[N]={0};pool p={.size=4096};unsigned checks=0;
  pool_backing_span buffers[B]={0};
  const uint64_t bounds[B][2]={{0,32},{96,128},{192,256},{480,512},
    {32,128},{100,224},{512,544},{192,240}};
  for(unsigned k=0;k<N;k++) {
    unsigned n=(k*13)%N;uint64_t first=(n%8)*32,length=(n/8+1)*24;
    a[n].begin=first;a[n].end=first+length;
    a[n].range=(alias_range_node){.start=first,.end=first+length,.max_end=first+length,.serial=n,.owner=&a[n],.height=1};
    int inserted=0;p.native_allocation_index=alias_range_insert(p.native_allocation_index,&a[n].range,&inserted);
    if(!inserted)die("Native allocation test index duplicate");checks++;
  }
  for(unsigned pass=0;pass<3;pass++) {
    for(uint64_t first=0;first<608;first+=3)for(uint64_t length=1;length<65;length+=7) {
      uint64_t next=first;int overlaps=0;
      for(unsigned n=0;n<N;n++)if(!atomic_load(&a[n].released) && a[n].begin<first+1 && a[n].end>first) {
        if(a[n].end>next)next=a[n].end;
      }
      for(unsigned n=0;n<N;n++)if(!atomic_load(&a[n].released) && a[n].begin<first+length && a[n].end>first)overlaps=1;
      if(native_allocation_cover_end(p.native_allocation_index,first,first+1)!=next ||
          native_allocation_overlaps_locked(&p,first,length)!=overlaps)die("Native allocation indexed point/overlap mismatch");checks++;
      uint64_t at=first;
      while(at<first+length) {
        uint64_t end=at;
        for(unsigned n=0;n<N;n++)if(!atomic_load(&a[n].released) && a[n].begin<=at && a[n].end>end)end=a[n].end;
        for(unsigned chain=0;chain<2;chain++)
          for(pool_backing_span *b=chain?p.shared_covers:p.backings;b;b=b->next)
            if(b->begin<=at && b->end>end)end=b->end;
        if(end==at)break;
        at=end;
      }
      if(shared_pool_storage_covered_locked(&p,first,length)!=(at>=first+length))die("Native allocation union coverage mismatch");checks++;
    }
    if(pass==0) {
      // Same typed index/query as successful real Buffer imports. Synthetic
      // ranges prove holes, adjacency and nested/duplicate-start cover unions;
      // actual Buffer handles and bidirectional bytes stay in the GPU fixture.
      p.shared_lazy_eligible=1;
      for(unsigned n=0;n<B;n++) {
        pool_backing_span *b=&buffers[n];b->begin=bounds[n][0];b->end=bounds[n][1];b->generation=n+1;
        pool_backing_span **chain=n<4?&p.backings:&p.shared_covers;
        b->next=*chain;*chain=b;shared_buffer_index_add_locked(&p,b,n>=4);checks++;
      }
      for(unsigned n=0;n<N;n+=2)atomic_store(&a[n].released,n%4?2:1);
    }
    if(pass==1)for(unsigned k=0;k<N/2;k++) {
      unsigned n=(k*5)%16*2;int removed=0;
      p.native_allocation_index=alias_range_remove(p.native_allocation_index,&a[n].range,&removed);
      if(!removed)die("Native allocation test remove mismatch");checks++;
    }
  }
  for(unsigned n=1;n<N;n+=2) {
    atomic_store(&a[n].released,2);int removed=0;
    p.native_allocation_index=alias_range_remove(p.native_allocation_index,&a[n].range,&removed);
    if(!removed)die("Native allocation final retirement mismatch");checks++;
  }
  // Removal from this separate tree cannot revive a released native interval.
  for(unsigned n=0;n<B;n++) {
    int removed=0;p.shared_buffer_index=alias_range_remove(p.shared_buffer_index,&buffers[n].shared_coverage,&removed);
    if(!removed)die("Shared Buffer coverage test removal mismatch");checks++;
  }
  if(p.native_allocation_index || p.shared_buffer_index || shared_pool_storage_covered_locked(&p,0,1))die("Native/Buffer allocation index remained after release");checks++;
  return checks;
}
#endif
int vk_test_native_import_coverage(uint64_t address,uint64_t bytes) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  int valid=p && p->shared && shared_pool_storage_covered_locked(p,address-(uintptr_t)p->guest,bytes);
  pthread_mutex_unlock(&V.resources);return valid;
}
void vk_test_native_allocation_binding(void *token,VkDeviceMemory *memory,uint64_t *offset) {
  native_image_allocation *a=token;
  if(!a || atomic_load(&a->released))die("Native binding test outside lifetime");
  *memory=a->storage.memory;*offset=a->address-(uintptr_t)a->owner->guest-a->begin;
}
void vk_test_native_allocation_stats(uint64_t *count,uint64_t *bytes) {
  *count=atomic_load(&native_allocation_count);*bytes=atomic_load(&native_allocation_bytes);
}
#endif
