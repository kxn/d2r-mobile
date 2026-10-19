// Byte-range CPU-shadow authority, not a timing/dirty-page profiler. Only
// startup-window pools (or the conservative legacy migration fallback) use it.
// Unknown ranges carry the latest RECORDING generation. Completion clears
// only generations at/before its actual snapshot; a later writer survives.
enum { POOL_CPU_UNKNOWN_LIMIT=256 };
typedef struct {uint64_t begin,end,generation;} pool_cpu_unknown;
struct pool_cpu_authority {
  unsigned count;
  pool_cpu_unknown ranges[POOL_CPU_UNKNOWN_LIMIT];
};
// This lock never nests any other backend lock. Callers may already hold
// resources or publication_mu, so completion/CPU uploads cannot invert them.
static pthread_mutex_t pool_cpu_authority_mu=PTHREAD_MUTEX_INITIALIZER;
static uint64_t pool_cpu_generation;
// Recording-owner-only: CPU uploads and relocation copies retain known data;
// their ordinary transfer dependency bookkeeping is NOT an unknown GPU value.
static unsigned pool_cpu_preserving;
static uint64_t pool_cpu_generation_next(void) {
  if(pool_cpu_generation==UINT64_MAX)die("Vulkan CPU authority generation overflow");
  return ++pool_cpu_generation;
}
static pool_cpu_authority *pool_cpu_state(pool *p) {
  if(!p->cpu_authority) {
    p->cpu_authority=calloc(1,sizeof *p->cpu_authority);
    if(!p->cpu_authority) {p->cpu_copy_requires_gpu=1;return NULL;}
    if(p->cpu_copy_requires_gpu) {
      p->cpu_authority->count=1;
      p->cpu_authority->ranges[0]=(pool_cpu_unknown){0,p->size,pool_cpu_generation_next()};
    }
  }
  return p->cpu_authority;
}
static uint64_t pool_cpu_cutoff(void) {
  pthread_mutex_lock(&pool_cpu_authority_mu);uint64_t value=pool_cpu_generation;
  pthread_mutex_unlock(&pool_cpu_authority_mu);return value;
}
static void pool_cpu_written(pool *p,uint64_t offset,uint64_t size) {
  if(!size || pool_cpu_preserving || (!p->cpu_authority_tracking&&!p->cpu_copy_requires_gpu))return;
  if(offset>p->size || size>p->size-offset)die("Vulkan CPU authority writer bounds");
  pthread_mutex_lock(&pool_cpu_authority_mu);
  pool_cpu_authority *state=pool_cpu_state(p);
  if(state) {
    pool_cpu_unknown value={offset,offset+size,pool_cpu_generation_next()};
    pool_cpu_unknown next[POOL_CPU_UNKNOWN_LIMIT+2];unsigned count=0;int inserted=0;
    for(unsigned k=0;k<state->count;k++) {
      pool_cpu_unknown r=state->ranges[k];
      if(r.end<=value.begin)next[count++]=r;
      else if(r.begin>=value.end) {
        if(!inserted) {next[count++]=value;inserted=1;}
        next[count++]=r;
      } else {
        // Only the written bytes acquire the new recording generation. Older
        // snapshots may still prove untouched neighbors; raising their stamp
        // would needlessly lose exact publication/CPU-upload recovery.
        if(r.begin<value.begin)next[count++]=(pool_cpu_unknown){r.begin,value.begin,r.generation};
        if(!inserted) {next[count++]=value;inserted=1;}
        if(r.end>value.end)next[count++]=(pool_cpu_unknown){value.end,r.end,r.generation};
      }
    }
    if(!inserted)next[count++]=value;
    if(count>POOL_CPU_UNKNOWN_LIMIT) {
      // Drop precision, never an unknown byte. Newer generation prevents an
      // older in-flight publication from accidentally clearing this collapse.
      state->count=1;state->ranges[0]=(pool_cpu_unknown){0,p->size,value.generation};
    } else {
      memcpy(state->ranges,next,count*sizeof *next);state->count=count;
    }
  }
  pthread_mutex_unlock(&pool_cpu_authority_mu);
}
static void pool_cpu_synced(pool *p,uint64_t offset,uint64_t size,uint64_t cutoff) {
  if(!size || (!p->cpu_authority_tracking&&!p->cpu_copy_requires_gpu))return;
  if(offset>p->size || size>p->size-offset)die("Vulkan CPU authority publication bounds");
  pthread_mutex_lock(&pool_cpu_authority_mu);
  // A pool with no unknown writers has no map to edit/allocate. Metadata OOM
  // or untracked legacy migration instead starts conservatively whole-unknown.
  pool_cpu_authority *state=p->cpu_authority;
  if(!state && p->cpu_copy_requires_gpu)state=pool_cpu_state(p);
  if(state && state->count) {
    uint64_t end=offset+size;pool_cpu_unknown next[POOL_CPU_UNKNOWN_LIMIT+1];unsigned count=0;
    for(unsigned k=0;k<state->count;k++) {
      pool_cpu_unknown r=state->ranges[k];
      if(r.generation>cutoff || r.end<=offset || r.begin>=end)next[count++]=r;
      else {
        if(r.begin<offset)next[count++]=(pool_cpu_unknown){r.begin,offset,r.generation};
        if(r.end>end)next[count++]=(pool_cpu_unknown){end,r.end,r.generation};
      }
    }
    // A split may need one extra entry. If full, retain the old conservative
    // proof rather than truncate unknown bytes or allocate an unbounded tree.
    if(count<=POOL_CPU_UNKNOWN_LIMIT) {
      memcpy(state->ranges,next,count*sizeof *next);state->count=count;
    }
  }
  pthread_mutex_unlock(&pool_cpu_authority_mu);
}
// One actual sparse CPU publication can contain many disjoint changed runs.
// They share the page's retained pool and recording cutoff. Subtract their
// union once, without making unchanged GPU bytes or newer writers CPU-known.
// The caller has already copied every run to shadow and its physical mirror
// (or recorded the existing immutable queued upload). No callbacks occur here.
static void pool_cpu_synced_runs(pool *p,const VkBufferCopy *runs,unsigned n,uint64_t cutoff) {
  if(!n || (!p->cpu_authority_tracking&&!p->cpu_copy_requires_gpu))return;
  uint64_t end=0;
  for(unsigned k=0;k<n;k++) {
    uint64_t begin=runs[k].dstOffset,size=runs[k].size;
    if(!size || begin>p->size || size>p->size-begin || (k && begin<end))
      die("Vulkan CPU authority sparse publication bounds");
    end=begin+size;
  }
  pthread_mutex_lock(&pool_cpu_authority_mu);
  pool_cpu_authority *state=p->cpu_authority;
  if(!state && p->cpu_copy_requires_gpu)state=pool_cpu_state(p);
  if(state && state->count) {
    pool_cpu_unknown next[POOL_CPU_UNKNOWN_LIMIT+1];unsigned count=0,first=0;
    for(unsigned k=0;k<state->count;k++) {
      pool_cpu_unknown r=state->ranges[k];uint64_t at=r.begin;
      while(first<n && runs[first].dstOffset+runs[first].size<=at)first++;
      if(r.generation<=cutoff)for(unsigned j=first;j<n && runs[j].dstOffset<r.end;j++) {
        uint64_t lo=runs[j].dstOffset,hi=lo+runs[j].size;
        if(lo>at) {
          next[count++]=(pool_cpu_unknown){at,lo,r.generation};
          if(count>POOL_CPU_UNKNOWN_LIMIT)goto scalar;
        }
        at=hi<r.end?hi:r.end;
        if(at==r.end)break;
      }
      if(at<r.end) {
        next[count++]=(pool_cpu_unknown){at,r.end,r.generation};
        if(count>POOL_CPU_UNKNOWN_LIMIT)goto scalar;
      }
    }
    // All remaining bytes and recording generations retain their exact value.
    // No partial result is published. An overflow below retains exactly the
    // original ordered scalar clear behavior, including its useful prefix.
    memcpy(state->ranges,next,count*sizeof *next);state->count=count;
  }
  pthread_mutex_unlock(&pool_cpu_authority_mu);return;
scalar:
  // Keep the old knowledge/resource behavior at the bounded precision limit.
  // Caller still holds publication_mu and this exact pool's lifetime. Never
  // nest this mutex recursively or advance the original recording cutoff.
  pthread_mutex_unlock(&pool_cpu_authority_mu);
  for(unsigned k=0;k<n;k++)pool_cpu_synced(p,runs[k].dstOffset,runs[k].size,cutoff);
}
static int pool_cpu_copy_proven(pool *p,uint64_t offset,uint64_t size) {
  if(!p->cpu_authority_tracking&&!p->cpu_copy_requires_gpu)return 1;
  if(!size || offset>p->size || size>p->size-offset)return 0;
  int known=1;pthread_mutex_lock(&pool_cpu_authority_mu);
  pool_cpu_authority *state=p->cpu_authority;
  if(!state && p->cpu_copy_requires_gpu)known=0;
  if(state)for(unsigned k=0;k<state->count;k++) {
    if(state->ranges[k].begin>=offset+size)break;
    if(state->ranges[k].end>offset) {known=0;break;}
  }
  pthread_mutex_unlock(&pool_cpu_authority_mu);return known;
}
static void pool_cpu_migrated(pool *p) {
  pthread_mutex_lock(&pool_cpu_authority_mu);
  if(!p->cpu_authority_tracking) {
    p->cpu_copy_requires_gpu=1;
    (void)pool_cpu_state(p); // no prior writer tracking => conservative legacy
  }
  p->cpu_authority_tracking=1;
  pthread_mutex_unlock(&pool_cpu_authority_mu);
}
