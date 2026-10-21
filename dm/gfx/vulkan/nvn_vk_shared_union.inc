#ifdef DM_VULKAN_TEST_EMBED
// Owned handshake: proves a live CPU observation survives the rare union wait.
static void (*shared_union_test_observer_release)(void);
void vk_test_shared_union_observer_release(void (*release)(void)) {
  shared_union_test_observer_release=release;
}
#endif
// No migration copy: both imports name identical original memfd bytes.
// pool_policy_promote has closed recording, finished GPU+CPU effects and reset,
// and reclaimed old descriptor/typed-view contexts before entering here.
static int shared_pool_union_quiescent(uint64_t base,uint64_t identity,
    const vk_pool_requirement *trigger,uint64_t window) {
  if(shared_lazy_active)return 0; // immutable aliases never enter destructive union
  if(V.cmd || recording_resources || stream_recording || vk_render_active() ||
      vk_counter_active() || vk_shader_operation_active() || vk_texture_binding_scope_active() ||
      vk_texture_shader_collecting() || vk_mip_clear_pending() || !trigger)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(base,1);
  uint64_t begin,end;
  if(!p || (uintptr_t)p->guest!=base || p->identity!=identity || !pool_shared_window_policy(p) ||
      p->shared_closing || !pool_window_chain_valid(p) ||
      !pool_migration_bounds(p,trigger,window,&begin,&end)) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  // A CPU observation retains only this logical Pool/guest pages. It can
  // finish on an I/O thread after all GPU submissions have been reclaimed.
  // Pin identity across cond-wait; cond-wait releases resources. No queue or
  // dirty lock is held, and observer end never submits/needs this owner.
  p->references++;
  while(p->references>1 && !p->shared_closing) {
#ifdef DM_VULKAN_TEST_EMBED
    if(shared_union_test_observer_release)shared_union_test_observer_release();
#endif
    pthread_cond_wait(&pool_cv,&V.resources);
  }
  if(p->shared_closing || p!=pool_find(base,1)) {
    p->references--;pthread_cond_broadcast(&pool_cv);
    pthread_mutex_unlock(&V.resources);return 0;
  }
  if(p->backing_generation==UINT64_MAX ||
      atomic_load_explicit(&pool_registry_epoch,memory_order_relaxed)==UINT64_MAX ||
      V.buffer_epoch==UINT64_MAX)die("Shared union generation overflow");
  pool_backing_span *replacement=calloc(1,sizeof *replacement);
  VkResult allocated=replacement?VK_SUCCESS:VK_ERROR_OUT_OF_HOST_MEMORY;
#ifdef DM_VULKAN_TEST_EMBED
  if(allocated==VK_SUCCESS && pool_reserve_fail_after==0)allocated=VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
  if(allocated==VK_SUCCESS)allocated=shared_span_try_allocate(p,replacement,begin,end);
  if(allocated!=VK_SUCCESS) {
    if(replacement){vk_buffer_destroy(&replacement->buffer);free(replacement);}
    p->references--;pthread_cond_broadcast(&pool_cv);
    pthread_mutex_unlock(&V.resources);return 0; // every original node/data kept
  }
  pool_backing_span **at=&p->backings;
  while(*at && (*at)->end<=begin)at=&(*at)->next;
  pool_backing_span *old=*at,*after=old;
  while(after && after->begin<end)after=after->next;
  replacement->next=after;replacement->generation=++p->backing_generation;
  *at=replacement;p->locked_span_hint=NULL;p->access_serial=0;p->recording_access=NULL;
  // All fallible work precedes publication. Pool identity is unchanged and no
  // content/authority/dirty proof is created by changing the driver mapping.
  atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release);V.buffer_epoch++;
  while(old!=after) {
    pool_backing_span *next=old->next;vk_buffer_destroy(&old->buffer);
    if(old!=&p->whole)free(old);
    old=next;
  }
  shared_union_visibility_pending=1;
  p->references--;pthread_cond_broadcast(&pool_cv);
  pthread_mutex_unlock(&V.resources);return 1;
}

#ifdef DM_VULKAN_TEST_EMBED
int vk_test_shared_union(uint64_t base,uint64_t identity,const vk_pool_requirement *trigger,uint64_t window) {
  return shared_pool_union_quiescent(base,identity,trigger,window);
}
#endif
