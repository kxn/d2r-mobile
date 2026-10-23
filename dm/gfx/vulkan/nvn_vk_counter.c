// Vulkan timestamps and occlusion queries. Legacy destinations publish after
// the true fence; registered fixed-shared destinations publish in GPU order.
#include "nvn_vk_internal.h"
#include <stdatomic.h>
static void completion_add(void (*fn)(void *), void *context, void *dst, size_t size) {
  vk_effect *e = calloc(1, sizeof *e);
  if (!e)
    die("Vulkan completion allocation");
  e->complete = fn;
  e->context = context;
  e->dst=dst;e->size=size;
  *V.effect_tail = e;
  V.effect_tail = &e->next;
}
void vk_completion(void (*fn)(void *), void *context) {
  completion_add(fn,context,NULL,0);
}
void vk_completion_write(void (*fn)(void *), void *context, void *dst, size_t size) {
  completion_add(fn,context,dst,size);
}
typedef struct sample_block {
  struct sample_block *next;
  VkQueryPool handle;
  VkQueryType type;
  _Atomic unsigned refs;
  unsigned used;
  vk_buffer *readback;
  int copied;
} sample_block;
static pthread_mutex_t free_blocks_mu=PTHREAD_MUTEX_INITIALIZER;
static sample_block *free_blocks[2];
static unsigned free_block_count[2];
static _Atomic uint64_t query_pool_reuses,query_pool_creates;
void vk_query_pool_stats(uint64_t *reuses,uint64_t *creates) {
  *reuses=atomic_load(&query_pool_reuses);*creates=atomic_load(&query_pool_creates);
}
static int pool_reference(void) {
  static int reference=-1;
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_QUERY_POOL_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1")) die("DM_VULKAN_QUERY_POOL_REFERENCE expects 0 or 1");
    reference=v && !strcmp(v,"1");
  }
  return reference;
}
static sample_block *block_acquire(VkQueryType type) {
  unsigned kind=type==VK_QUERY_TYPE_TIMESTAMP;sample_block *block=NULL;
  if(!pool_reference()) {
    pthread_mutex_lock(&free_blocks_mu);
    block=free_blocks[kind];
    if(block) { free_blocks[kind]=block->next;free_block_count[kind]--; }
    pthread_mutex_unlock(&free_blocks_mu);
  }
  if(block) {
    block->next=NULL;block->used=0;block->readback=NULL;block->copied=0;
    atomic_store(&block->refs,1);atomic_fetch_add(&query_pool_reuses,1);
  } else {
    block=calloc(1,sizeof *block);
    if(!block) die("Vulkan query block allocation");
    block->type=type;atomic_init(&block->refs,1);
    VkQueryPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType=type,.queryCount=kind?2048:256};
    VK_CHECK(vkCreateQueryPool(V.device,&ci,NULL,&block->handle));
    atomic_fetch_add(&query_pool_creates,1);
  }
  return block;
}
typedef struct {
  sample_block *block;
  unsigned index;
  _Atomic unsigned refs;
} sample_query;
static sample_block *current_block;
static sample_block *timestamp_block;
// 对应 Metal 的每通道可见性槽；报告/重置保留精确的累计前缀边界。
// Match Metal's per-pass visibility slots; reports/resets retain exact prefixes.
static _Atomic int span_requested = -1, span_active;
static _Atomic uint64_t query_begins, query_ends;
static vk_sample_query active_query;
int vk_counter_active(void) { return active_query.pool!=VK_NULL_HANDLE; }
void vk_counter_span_mode(int enabled) { atomic_store(&span_requested, !!enabled); }
void vk_counter_query_stats(uint64_t *begins, uint64_t *ends, int *span) {
  *begins=atomic_load(&query_begins);*ends=atomic_load(&query_ends);
  *span=atomic_load(&span_active);
}
void vk_counter_pass_end(void) {
  if (!active_query.pool) return;
  vk_draw_flush_pending();
  vkCmdEndQuery(V.cmd,active_query.pool,active_query.index);
  atomic_fetch_add(&query_ends,1);
  active_query=(vk_sample_query){0};
}
void vk_counter_draw_begin(vk_sample_query query) {
  if (!query.pool) return;
  vk_draw_flush_pending();
  if (active_query.pool) die("Vulkan overlapping occlusion query");
  vkCmdBeginQuery(V.cmd,query.pool,query.index,
      V.features.occlusionQueryPrecise?VK_QUERY_CONTROL_PRECISE_BIT:0);
  atomic_fetch_add(&query_begins,1);
  if (atomic_load(&span_active)) active_query=query;
}
void vk_counter_draw_end(vk_sample_query query) {
  if (!query.pool || atomic_load(&span_active)) return;
  vk_draw_flush_pending();
  vkCmdEndQuery(V.cmd,query.pool,query.index);
  atomic_fetch_add(&query_ends,1);
}
static void block_release(sample_block *block) {
  if (atomic_fetch_sub(&block->refs, 1) == 1) {
    // Reuse at exactly the former destruction point: current recording,
    // query-prefix snapshots and true-fence completion consumers all released.
    unsigned kind=block->type==VK_QUERY_TYPE_TIMESTAMP;
    if(!pool_reference()) {
      pthread_mutex_lock(&free_blocks_mu);
      if(free_block_count[kind]<32) {
        block->next=free_blocks[kind];free_blocks[kind]=block;free_block_count[kind]++;
        pthread_mutex_unlock(&free_blocks_mu);return;
      }
      pthread_mutex_unlock(&free_blocks_mu);
    }
    vkDestroyQueryPool(V.device, block->handle, NULL);
    free(block);
  }
}
void vk_counter_begin(void) {
  if (active_query.pool) die("Vulkan occlusion query escaped render pass");
  int mode=atomic_load(&span_requested);
  if (mode<0) {
    const char *setting=getenv("DM_VULKAN_QUERY_SPAN");
    if (setting&&strcmp(setting,"0")&&strcmp(setting,"1"))
      die("DM_VULKAN_QUERY_SPAN expects 0 or 1");
    mode=!setting||!strcmp(setting,"1");atomic_store(&span_requested,mode);
  }
  // 调试切换只在提交边界生效，不改变已打开查询的生命周期。
  // Apply debug switches only at submit boundaries, never during an open query.
  atomic_store(&span_active,mode);
  if (current_block)
    block_release(current_block);
  current_block = NULL;
  if (timestamp_block)
    block_release(timestamp_block);
  timestamp_block = NULL;
}
static sample_query **samples;
static unsigned sample_count, sample_capacity;
static int counting;
int vk_counter_counting(void) {return counting;}
static void sample_release(void *opaque) {
  sample_query *q = opaque;
  if (atomic_fetch_sub(&q->refs, 1) == 1) {
    block_release(q->block);
    free(q);
  }
}
vk_sample_query vk_counter_draw_prepare(void) {
  if (!counting)
    return (vk_sample_query){0};
  if (active_query.pool)
    return (vk_sample_query){0};
  vk_draw_flush_pending();
  if (sample_count == sample_capacity) {
    unsigned n = sample_capacity ? sample_capacity * 2 : 128;
    sample_query **p = realloc(samples, n * sizeof *p);
    if (!p)
      die("Vulkan occlusion list allocation");
    samples = p;
    sample_capacity = n;
  }
  sample_query *q = calloc(1, sizeof *q);
  if (!q)
    die("Vulkan occlusion query allocation");
  atomic_init(&q->refs, 2);
  // 每块容纳多次绘制，仍为每次绘制保存独立结果及引用计数。
  // Batch allocations while retaining independent per-draw results and
  // lifetimes.
  if (!current_block || current_block->used == 256) {
    vk_render_end();
    if (current_block)
      block_release(current_block);
    current_block=block_acquire(VK_QUERY_TYPE_OCCLUSION);
    vkCmdResetQueryPool(V.cmd, current_block->handle, 0, 256);
  }
  q->block = current_block;
  atomic_fetch_add(&current_block->refs, 1);
  q->index = current_block->used++;
  samples[sample_count++] = q;
  vk_completion(sample_release, q);
  return (vk_sample_query){q->block->handle, q->index};
}
typedef struct report {
  struct report *next;
  sample_block *timestamp;
  unsigned timestamp_index;
  uint64_t address;
  uint32_t type, count;
  sample_query *samples[];
} report;
static report *pending_reports;
void vk_counter_flush(void) {
  if (!pending_reports)
    return;
  vk_render_end();
  // 时间戳仍在原命令位置写入；结果只须在完成后可见，统一放到提交尾拷贝。
  // Keep timestamp writes at their original command positions. Results are
  // visible after completion, so copy them together at the end of the submit.
  for (report *r = pending_reports; r; r = r->next) {
    sample_block *block = r->timestamp;
    if (!block->copied) {
      vkCmdCopyQueryPoolResults(
          V.cmd, block->handle, 0, block->used, block->readback->handle, 0, 8,
          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
      // Query readback is consumed only by the completion worker after fence;
      // it is not an input to later GPU vertex/indirect work.
      block->copied = 1;
    }
  }
  pending_reports = NULL;
}
static void report_done(void *opaque) {
  report *r = opaque;
  uint64_t raw;
  memcpy(&raw, r->timestamp->readback->map + r->timestamp_index * 8, 8);
  uint64_t ticks = (uint64_t)((long double)raw *
                              V.props.limits.timestampPeriod * 0.0192L),
           sum = 0;
  for (unsigned i = 0; i < r->count; i++) {
    uint64_t value;
    VK_CHECK(vkGetQueryPoolResults(
        V.device, r->samples[i]->block->handle, r->samples[i]->index, 1,
        sizeof value, &value, sizeof value, VK_QUERY_RESULT_64_BIT));
    sum += value;
    sample_release(r->samples[i]);
  }
  uint64_t value[2] = {r->type == NVN_COUNTER_TYPE_SAMPLES_PASSED ? sum : ticks,
                       ticks};
  dm_guest_write_prepare((void *)(uintptr_t)r->address, sizeof value);
  memcpy((void *)(uintptr_t)r->address, value, sizeof value);
  block_release(r->timestamp);
  free(r);
}
#ifdef DM_VULKAN_SHARED_POOLS
#ifdef DM_VULKAN_TEST_EMBED
static void (*native_test_observe)(uint64_t,uint32_t,uint64_t,uint64_t);
void vk_counter_native_test_observer(void (*observe)(uint64_t,uint32_t,uint64_t,uint64_t)) {
  if(V.cmd)die("Native query observer must be selected outside recording");
  native_test_observe=observe;
}
#endif
static void report_gpu_cleanup(void *opaque) {
  report *r=opaque;
#ifdef DM_VULKAN_TEST_EMBED
  // Bounded functional oracle only. Normal shared execution never reads query
  // results on the CPU and never stores counter output from this callback.
  if(native_test_observe) {
    uint64_t raw,sum=0;
    VK_CHECK(vkGetQueryPoolResults(V.device,r->timestamp->handle,r->timestamp_index,1,
        sizeof raw,&raw,sizeof raw,VK_QUERY_RESULT_64_BIT));
    for(unsigned k=0;k<r->count;k++) {uint64_t v;
      VK_CHECK(vkGetQueryPoolResults(V.device,r->samples[k]->block->handle,r->samples[k]->index,1,
          sizeof v,&v,sizeof v,VK_QUERY_RESULT_64_BIT));sum+=v;}
    native_test_observe(r->address,r->type,raw,sum);
  }
#endif
  for(unsigned k=0;k<r->count;k++)sample_release(r->samples[k]);
  block_release(r->timestamp);free(r);
}
#endif
void vk_counter_reset(uint32_t type) {
  vk_draw_flush_pending();
  if (type != NVN_COUNTER_TYPE_TIMESTAMP &&
      type != NVN_COUNTER_TYPE_TIMESTAMP_TOP &&
      type != NVN_COUNTER_TYPE_SAMPLES_PASSED)
    die("Vulkan counter reset not implemented: %u", type);
  if (type == NVN_COUNTER_TYPE_SAMPLES_PASSED) {
    vk_counter_pass_end();
    for (unsigned i = 0; i < sample_count; i++)
      sample_release(samples[i]);
    sample_count = 0;
    counting = 1;
  }
}
void mtl_sub_counter(uint64_t address, uint32_t type) {
  vk_draw_flush_pending();
  if(V.options.pool_defer_publication)vk_pool_cpu_prepare(address,2*sizeof(uint64_t));
  // TOP 时间戳必须保留此前 NVN 顺序请求；BOTTOM 与采样结果已有命令位置保证。
  // A TOP timestamp must honor preceding NVN ordering. BOTTOM timestamps and
  // sample snapshots already retain their command-position semantics.
  if (V.nvn_barrier && type == NVN_COUNTER_TYPE_TIMESTAMP_TOP)
    vk_barrier();
  if (type != NVN_COUNTER_TYPE_TIMESTAMP &&
      type != NVN_COUNTER_TYPE_TIMESTAMP_TOP &&
      type != NVN_COUNTER_TYPE_SAMPLES_PASSED)
    die("Vulkan counter report not implemented: %u", type);
  if (type == NVN_COUNTER_TYPE_SAMPLES_PASSED) vk_counter_pass_end();
  unsigned count = type == NVN_COUNTER_TYPE_SAMPLES_PASSED ? sample_count : 0;
  report *r = calloc(1, sizeof *r + count * sizeof *r->samples);
  if (!r)
    die("Vulkan query allocation");
  r->address = address;
  r->type = type;
  r->count = count;
  for (unsigned i = 0; i < count; i++) {
    r->samples[i] = samples[i];
    atomic_fetch_add(&samples[i]->refs, 1);
  }
  if (!timestamp_block || timestamp_block->used == 2048) {
    vk_render_end();
    if (timestamp_block)
      block_release(timestamp_block);
    timestamp_block=block_acquire(VK_QUERY_TYPE_TIMESTAMP);
    timestamp_block->readback = vk_temp_buffer(2048 * 8);
    vkCmdResetQueryPool(V.cmd, timestamp_block->handle, 0, 2048);
  }
  r->timestamp = timestamp_block;
  r->timestamp_index = timestamp_block->used++;
  atomic_fetch_add(&timestamp_block->refs, 1);
  vkCmdWriteTimestamp(V.cmd,
                      type == NVN_COUNTER_TYPE_TIMESTAMP_TOP
                          ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                          : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      r->timestamp->handle, r->timestamp_index);
#ifdef DM_VULKAN_SHARED_POOLS
  int shared=vk_pool_shared_destination(address,2*sizeof(uint64_t));
  if(shared<0)die("Native query destination overlaps an invalid/closing shared Pool range");
  if(shared>0) {
    vk_query_native_source *prefix=count?malloc((size_t)count*sizeof *prefix):NULL;
    if(count&&!prefix)die("Native query prefix allocation");
    for(unsigned k=0;k<count;k++)prefix[k]=(vk_query_native_source){r->samples[k]->block->handle,r->samples[k]->index};
    // These report references survive any lookup/deferred callback and hold
    // original exact query slots until the actual consuming GPU fence.
    vk_query_native_publish(address,r->timestamp->handle,r->timestamp_index,prefix,count,
        type==NVN_COUNTER_TYPE_SAMPLES_PASSED);free(prefix);
    vk_completion(report_gpu_cleanup,r);return;
  }
#endif
  r->next = pending_reports;
  pending_reports = r;
  vk_completion_write(report_done,r,(void *)(uintptr_t)address,2*sizeof(uint64_t));
}
