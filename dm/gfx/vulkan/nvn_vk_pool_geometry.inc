// Operation-owned physical geometry only. No dirty/ready/visibility decision,
// submission ledger, temporary or recording owner is retained in these rows.
enum { POOL_OPERATION_DEPTH=8,POOL_GEOMETRY_SLOTS=256 };
typedef struct {
  pool_backing_span *backing;
  uint64_t offset,generation;
  uint16_t owner_index;
  uint16_t reserved16;
  uint32_t reserved32;
} pool_operation_geometry;
_Static_assert(sizeof(pool_operation_geometry)==32,"operation geometry row size");
_Static_assert(VK_POOL_RESERVE_MAX_REQUIREMENTS<=UINT16_MAX,"operation geometry row index width");
typedef struct {
  vk_pool_requirement ranges[VK_POOL_RESERVE_MAX_REQUIREMENTS];
  pool *pins[VK_POOL_RESERVE_MAX_REQUIREMENTS];
  size_t count,pin_count;
  pool_operation_geometry *geometry;
  size_t geometry_capacity;
  uint64_t geometry_registry_epoch;
  uint32_t geometry_stamp;
  struct {uint32_t stamp;uint16_t row,reserved;} geometry_slots[POOL_GEOMETRY_SLOTS];
} pool_operation_plan;
static pool_operation_plan *pool_operation_plans[POOL_OPERATION_DEPTH];
static unsigned pool_geometry_slot(uint64_t address,uint64_t size) {
  return ((address>>4)^(address>>20)^(size*UINT64_C(0x9e3779b1)))&(POOL_GEOMETRY_SLOTS-1);
}
static void pool_operation_geometry_begin(pool_operation_plan *plan) {
  if(plan->count>plan->geometry_capacity) {
    size_t capacity=plan->geometry_capacity?plan->geometry_capacity:16;
    while(capacity<plan->count)capacity*=2;
    if(capacity>VK_POOL_RESERVE_MAX_REQUIREMENTS)capacity=VK_POOL_RESERVE_MAX_REQUIREMENTS;
    pool_operation_geometry *p=realloc(plan->geometry,capacity*sizeof *p);
    if(!p)die("Vulkan operation geometry scratch allocation");
    plan->geometry=p;plan->geometry_capacity=capacity;
  }
  // No per-draw table clear. A wrapped 32-bit stamp alone needs a cold reset.
  if(!++plan->geometry_stamp) {
    memset(plan->geometry_slots,0,sizeof plan->geometry_slots);plan->geometry_stamp=1;
  }
  plan->geometry_registry_epoch=0;
}
static void pool_operation_geometry_remember(pool_operation_plan *plan,size_t row,
    size_t owner_index,const pool_span_piece *span) {
  if(row>=plan->count || owner_index>=plan->pin_count || row>UINT16_MAX)
    die("Vulkan operation geometry bounds");
  // Sole caller: pool_operation_pin, immediately after a successful
  // pool_span_resolve_operation while holding the SAME V.resources lock.
  // The owner was already checked by pool_find or the pinned-owner bounds;
  // every resolver branch checks handle/generation, Pool and VkBuffer extent
  // and constructs {backing, requested logical, logical-begin, requested size}.
  // No callback or mutation occurs before this copy. Repeating those exact
  // checks here proves nothing further; retain the table-index guard above.
  // This geometry still carries no CPU readiness or GPU access/visibility.
  pool_backing_span *b=span->backing;
  plan->geometry[row]=(pool_operation_geometry){.backing=b,
    .offset=span->offset,.generation=b->generation,.owner_index=(uint16_t)owner_index};
  unsigned slot=pool_geometry_slot(plan->ranges[row].address,plan->ranges[row].size);
  plan->geometry_slots[slot].stamp=plan->geometry_stamp;
  plan->geometry_slots[slot].row=(uint16_t)row;
}
// Called only after all reentrant mip/alias checks, on the recording owner.
// Every pointer is protected by THIS operation's independent Pool pins, not
// by a previous submission that completion could release after a reopen.
static int pool_operation_geometry_find(uint64_t address,uint64_t size,uint64_t epoch,
    pool **owner,pool_span_piece *span) {
  if(!pool_operation_depth || !size)return 0;
  pool_operation_plan *plan=pool_operation_plans[pool_operation_depth-1];
  if(!epoch || plan->geometry_registry_epoch!=epoch)return 0;
  unsigned slot=pool_geometry_slot(address,size);
  if(plan->geometry_slots[slot].stamp!=plan->geometry_stamp)return 0;
  unsigned row=plan->geometry_slots[slot].row;
  if(row>=plan->count || plan->ranges[row].address!=address || plan->ranges[row].size!=size)return 0;
  const pool_operation_geometry *g=&plan->geometry[row];
  if(g->owner_index>=plan->pin_count)return 0;
  pool *p=plan->pins[g->owner_index];pool_backing_span *b=g->backing;
  // The exact key selects a resolution already checked at admission. The
  // operation's independent pin prevents Pool destruction and backing
  // replacement (including across completion/reopen); unchanged registry
  // epoch + generation retain that proof. guest/size/begin/end/buffer extent
  // and the saved offset cannot change under this pin, so do not redo their
  // arithmetic here. This does NOT retain any ready/dirty/hazard proof.
  if(b->generation!=g->generation)return 0;
  *owner=p;*span=(pool_span_piece){b,address-(uintptr_t)p->guest,g->offset,size};return 1;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t pool_operation_geometry_uses;
int vk_test_pool_operation_geometry(uint64_t address,uint64_t size,vk_pool_span_info *out) {
  pool *p;pool_span_piece span;
  int found=pool_operation_geometry_find(address,size,
      atomic_load_explicit(&pool_registry_epoch,memory_order_acquire),&p,&span);
  if(found && out)*out=(vk_pool_span_info){.pool_identity=p->identity,.pool_base=(uintptr_t)p->guest,
    .pool_size=p->size,.logical_begin=span.backing->begin,.logical_end=span.backing->end,
    .backing_generation=span.backing->generation,.local_offset=span.offset,.size=size,.handle=span.backing->buffer.handle};
  return found;
}
uint64_t vk_test_pool_operation_geometry_uses(void) {return pool_operation_geometry_uses;}
#endif
