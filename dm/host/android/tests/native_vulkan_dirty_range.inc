// Owned GPU oracle; no game modules and no timing/profiling instrumentation.
typedef struct { uint8_t *data; NVNmemoryPool pool; NVNtexture tex[2]; } dirty_range_fixture;
static void dirty_range_fill(uint8_t *dst, size_t size, const uint8_t value[4]) {
  for (size_t b=0;b<size;b+=4) memcpy(dst+b,value,4);
}
static void dirty_range_create(dirty_range_fixture *f) {
  CHECK(!posix_memalign((void **)&f->data,65536,1<<20));memset(f->data,0,1<<20);
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,f->data,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&f->pool,&builder));
  for(unsigned t=0;t<2;t++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
    CALL(nvnTextureBuilderSetStorage,&b,&f->pool,t*65536);CHECK(CALL(nvnTextureInitialize,&f->tex[t],&b));
    CHECK(((tex_t *)&f->tex[t])->L.size<=4096);
  }
}
static void dirty_range_pixels(NVNtexture *texture,const uint8_t value[4]) {
  mtl_sub_begin();vk_buffer *result=vk_temp_buffer(1024);
  VkImage image=vk_texture_native((tex_t *)texture);
  VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
  vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,result->handle,1,&copy);
  vk_internal_wait(vk_internal_commit());
  for(unsigned p=0;p<256;p++) CHECK(!memcmp(result->map+p*4,value,4));
}
static void dirty_range_destroy(dirty_range_fixture *f) {
  for(unsigned t=0;t<2;t++) CALL(nvnTextureFinalize,&f->tex[t]);
  CALL(nvnMemoryPoolFinalize,&f->pool);free(f->data);
}
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
typedef struct {
  uint8_t *target; _Atomic int reserved, release; uint64_t generation; uint8_t value[4];
} dirty_range_order;
static void dirty_range_pause(const void *addr,uint64_t generation,void *context) {
  dirty_range_order *o=context;if(addr!=o->target)return;
  o->generation=generation;atomic_store_explicit(&o->reserved,1,memory_order_release);
  while(!atomic_load_explicit(&o->release,memory_order_acquire)) sched_yield();
}
static void *dirty_range_writer(void *context) {
  dirty_range_order *o=context;dm_guest_write_prepare(o->target,4096);
  dirty_range_fill(o->target,4096,o->value);return NULL;
}
#endif
static void texture_range_dirty_proof_test(void) {
  dirty_range_fixture a,b;dirty_range_create(&a);dirty_range_create(&b);
  const uint8_t red[4]={255,0,0,255},green[4]={0,255,0,255},blue[4]={0,0,255,255};
  dirty_range_fill(a.data,4096,red);dirty_range_fill(a.data+65536,4096,green);
  dirty_range_fill(b.data,4096,blue);
  dirty_range_pixels(&a.tex[0],red);dirty_range_pixels(&a.tex[1],green);dirty_range_pixels(&b.tex[0],blue);
  dirty_range_pixels(&a.tex[0],red); // acquire negative range proof after initial upload
  nvn_dirty_range_proof before,after;CHECK(nvn_dirty_range_observe(a.data,4096,&before));
  uint64_t hits,dirty_checks,hits2,checks2;vk_texture_dirty_stats(&hits,&dirty_checks);
  dm_guest_write_prepare(b.data,4096);dirty_range_fill(b.data,4096,green);
  CHECK(nvn_dirty_range_refresh(&before,&after));CHECK(after.changes==before.changes);
  dirty_range_pixels(&a.tex[0],red);vk_texture_dirty_stats(&hits2,&checks2);
  const char *ref=getenv("DM_VULKAN_TEXTURE_DIRTY_PROOF_REFERENCE");int reference=ref&&!strcmp(ref,"1");
  LOG("[range dirty counters] ref=%d checks=%llu->%llu hits=%llu->%llu",reference,(unsigned long long)dirty_checks,(unsigned long long)checks2,(unsigned long long)hits,(unsigned long long)hits2);
  CHECK(reference?checks2>dirty_checks:checks2==dirty_checks); // existing counter, no new production instrumentation
  dirty_range_pixels(&b.tex[0],green);
  dm_guest_write_prepare(a.data+65536,4096);dirty_range_fill(a.data+65536,4096,blue);
  CHECK(nvn_dirty_range_refresh(&before,&after));CHECK(after.changes!=before.changes);
  vk_texture_dirty_stats(&hits,&dirty_checks);dirty_range_pixels(&a.tex[0],red);
  vk_texture_dirty_stats(&hits2,&checks2);CHECK(checks2>dirty_checks);
  dirty_range_pixels(&a.tex[1],blue);
  dirty_range_fill(a.data,4096,green); // real write fault, original protected page behavior
  dirty_range_pixels(&a.tex[0],green);dirty_range_pixels(&a.tex[0],green);
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
  dirty_range_order order={.target=a.data,.value={57,101,203,255}};
  nvn_dirty_test_publication_hook(dirty_range_pause,&order);
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,dirty_range_writer,&order));
  unsigned spins=0;while(!atomic_load_explicit(&order.reserved,memory_order_acquire)&&++spins<10000000) sched_yield();
  CHECK(atomic_load_explicit(&order.reserved,memory_order_acquire));
  // A reserved N, B publishes N+1 first. Keep the original A upload stamp.
  dm_guest_write_prepare(a.data+65536,4096);dirty_range_fill(a.data+65536,4096,red);
  CHECK(nvn_dirty_now()>order.generation);
  CHECK(nvn_dirty_range_observe(a.data,4096,&before));
  dirty_range_pixels(&a.tex[0],green); // negative scan while A's reserved generation is not published
  atomic_store_explicit(&order.release,1,memory_order_release);
  CHECK(!pthread_join(thread,NULL));nvn_dirty_test_publication_hook(NULL,NULL);
  CHECK(nvn_dirty_range_refresh(&before,&after));CHECK(after.changes!=before.changes);
  dirty_range_pixels(&a.tex[0],order.value);dirty_range_pixels(&a.tex[1],red);
  nvn_dirty_test_lock_slots(1);CHECK(nvn_dirty_range_refresh(&before,&after));CHECK(!nvn_dirty_range_observe(a.data,4096,&after));nvn_dirty_test_lock_slots(0);
  // Owned-only exclusion contract. No Vulkan import or GPU reference is
  // created for the excluded bytes: before their FIRST image access the
  // fixture explicitly releases at a known quiescent boundary. This tests
  // proof invalidation/publication, not PanVK/COW or an automatic admission.
  dirty_range_fixture owned;dirty_range_create(&owned);
  nvn_dirty_owned_exclusion excluded={0};
  CHECK(nvn_dirty_owned_exclude(owned.data,4096,&excluded));
  CHECK(nvn_dirty_change_epoch()==0);
  CHECK(!nvn_dirty_range_observe(owned.data,4096,&after));
  CHECK(nvn_dirty_since(owned.data,4096,UINT64_MAX));
  nvn_dirty_protect(owned.data,4096);dirty_range_fill(owned.data,4096,blue);
  // An unrelated image must not reuse its old global-epoch shortcut while
  // any exclusion is active. Its normal dirty/producer semantics remain live.
  dm_guest_write_prepare(a.data,4096);dirty_range_fill(a.data,4096,red);
  dirty_range_pixels(&a.tex[0],red);
  CHECK(nvn_dirty_owned_release(&excluded));
  CHECK(nvn_dirty_change_epoch()!=0);
  dirty_range_pixels(&owned.tex[0],blue);
  dirty_range_pixels(&owned.tex[0],blue);
  dm_guest_write_prepare(owned.data,4096);dirty_range_fill(owned.data,4096,green);
  dirty_range_pixels(&owned.tex[0],green);dirty_range_destroy(&owned);
  CHECK(!nvn_dirty_range_observe((void *)(UINTPTR_MAX-7),16,&after));
  CHECK(!nvn_dirty_range_observe(a.data+(1<<20)-8,16,&after));
  CHECK(!nvn_dirty_range_observe(a.data,0,&after));
  CHECK(nvn_dirty_range_observe(a.data,4096,&before));
  nvn_dirty_remove_range(a.data);CHECK(!nvn_dirty_range_refresh(&before,&after));
  nvn_dirty_add_range(a.data,1<<20);CHECK(!nvn_dirty_range_refresh(&before,&after));
  CHECK(nvn_dirty_range_observe(a.data,4096,&after));CHECK(before.slot==after.slot);CHECK(before.lifetime!=after.lifetime);
  nvn_dirty_protect(a.data,4096);dirty_range_fill(a.data,4096,blue);dirty_range_pixels(&a.tex[0],blue);
  nvn_dirty_test_poison_range(a.data,0);CHECK(!nvn_dirty_range_observe(a.data,4096,&after));
  dm_guest_write_prepare(a.data,4096);dirty_range_fill(a.data,4096,red);dirty_range_pixels(&a.tex[0],red);
  nvn_dirty_test_poison_range(b.data,1);CHECK(!nvn_dirty_range_observe(b.data,4096,&after));
  dm_guest_write_prepare(b.data,4096);dirty_range_fill(b.data,4096,blue);dirty_range_pixels(&b.tex[0],blue);
  uint64_t saved=nvn_dirty_test_global_epoch(UINT64_MAX-1);
  dm_guest_write_prepare(b.data,4096);dirty_range_fill(b.data,4096,red);
  CHECK(nvn_dirty_change_epoch()==0);dirty_range_pixels(&b.tex[0],red);dirty_range_pixels(&b.tex[0],red);
  dm_guest_write_prepare(b.data,4096);dirty_range_fill(b.data,4096,green);
  CHECK(nvn_dirty_change_epoch()==0);dirty_range_pixels(&b.tex[0],green);
  // Host-only overflow fault injection ends at a quiescent boundary. Advance
  // the old monotonic key so no pre-injection negative proof can compare equal.
  nvn_dirty_test_global_epoch(saved+1);
#endif
  nvn_dirty_remove_range(a.data);dirty_range_fill(a.data,4096,green);dirty_range_pixels(&a.tex[0],green);
  nvn_dirty_protect(a.data,4096);dirty_range_fill(a.data,4096,blue);dirty_range_pixels(&a.tex[0],blue);
  dirty_range_destroy(&a);dirty_range_destroy(&b);
  LOG("[vulkan texture range dirty proof fixture] cross-pool skip, same-pool invalidation, faults, ordered completion, lifecycle, untracked and overflow GPU bytes passed");
}
