// This registry contains no Vulkan pointers or handles. Recording owns the
// recipes; only the existing cache persistence worker performs disk writes.
_Static_assert(sizeof(vk_prewarm_stage)==56,"prewarm wire stage ABI");
_Static_assert(sizeof(vk_prewarm_recipe)==2392,"prewarm wire record ABI");
_Static_assert(__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__,"prewarm wire little endian");
static pthread_mutex_t recipe_mutex=PTHREAD_MUTEX_INITIALIZER;
static vk_prewarm_recipe recipe_registry[VK_PREWARM_LIMIT];
static unsigned recipe_count,recipe_initialized;
static uint64_t recipe_generation,recipe_written;
static _Atomic uint64_t recipe_count_published;
static _Atomic uint64_t recipe_bad_files;
static void recipe_header(uint8_t h[88],const void *data,size_t bytes,unsigned count) {
  memset(h,0,88);memcpy(h,"DMVKPR1",7);put32(h+8,1);
  put32(h+12,sizeof(vk_prewarm_recipe));put32(h+16,count);put32(h+20,0x01020304);
  put32(h+24,V.props.vendorID);put32(h+28,V.props.deviceID);
  put32(h+32,V.props.driverVersion);put32(h+36,V.props.apiVersion);
  memcpy(h+40,V.props.pipelineCacheUUID,16);CC_SHA256(data,bytes,h+56);
}
static int recipe_path(char *path,size_t capacity) {
  return *cache_path && snprintf(path,capacity,"%s.recipes-v1",cache_path)<(int)capacity;
}
static void recipe_load(void) {
  if(recipe_initialized)return;
  recipe_initialized=1;
  char path[sizeof cache_path+24];if(!recipe_path(path,sizeof path))return;
  int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return;
  struct stat st;uint8_t header[88],expected[88];
  int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=88&&
    st.st_size<=(off_t)(88+sizeof recipe_registry)&&read_all(fd,header,88);
  unsigned count=ok?u32(header+16):0;
  size_t bytes=(size_t)count*sizeof(vk_prewarm_recipe);
  ok=ok&&count<=VK_PREWARM_LIMIT&&st.st_size==(off_t)(88+bytes)&&
    read_all(fd,recipe_registry,bytes);
  close(fd);
  if(ok) { recipe_header(expected,recipe_registry,bytes,count);ok=!memcmp(header,expected,88); }
  if(ok)recipe_count=count;
  else { atomic_fetch_add(&recipe_bad_files,1);memset(recipe_registry,0,sizeof recipe_registry);LOG("[vulkan PSO warmup] ignored incompatible/damaged registry"); }
  atomic_store(&recipe_count_published,recipe_count);
}
uint64_t vk_prewarm_record_count(void) { return atomic_load(&recipe_count_published); }
uint64_t vk_prewarm_store_rejected(void) { return atomic_load(&recipe_bad_files); }
size_t vk_prewarm_read(vk_prewarm_recipe *recipes,size_t capacity) {
  pthread_mutex_lock(&recipe_mutex);recipe_load();
  size_t n=recipe_count<capacity?recipe_count:capacity;
  memcpy(recipes,recipe_registry,n*sizeof *recipes);
  pthread_mutex_unlock(&recipe_mutex);return n;
}
void vk_prewarm_record(const vk_prewarm_recipe *recipe) {
  pthread_mutex_lock(&recipe_mutex);recipe_load();
  for(unsigned i=0;i<recipe_count;i++)if(!memcmp(recipe,&recipe_registry[i],sizeof *recipe)) {
    pthread_mutex_unlock(&recipe_mutex);return;
  }
  if(recipe_count==VK_PREWARM_LIMIT){pthread_mutex_unlock(&recipe_mutex);return;}
  recipe_registry[recipe_count++]=*recipe;recipe_generation++;
  atomic_store(&recipe_count_published,recipe_count);
  pthread_mutex_unlock(&recipe_mutex);vk_pipeline_cache_changed();
}
void vk_prewarm_forget(const vk_prewarm_recipe *recipe) {
  int changed=0;pthread_mutex_lock(&recipe_mutex);
  for(unsigned i=0;i<recipe_count;i++)if(!memcmp(recipe,&recipe_registry[i],sizeof *recipe)) {
    memmove(recipe_registry+i,recipe_registry+i+1,(recipe_count-i-1)*sizeof *recipe);
    memset(recipe_registry+--recipe_count,0,sizeof *recipe);recipe_generation++;changed=1;
    atomic_store(&recipe_count_published,recipe_count);break;
  }
  pthread_mutex_unlock(&recipe_mutex);if(changed)vk_pipeline_cache_changed();
}
void vk_prewarm_store(void) {
  pthread_mutex_lock(&recipe_mutex);
  if(!recipe_initialized||recipe_generation==recipe_written){pthread_mutex_unlock(&recipe_mutex);return;}
  unsigned count=recipe_count;uint64_t generation=recipe_generation;
  size_t bytes=(size_t)count*sizeof(vk_prewarm_recipe);void *copy=malloc(bytes);
  if(copy)memcpy(copy,recipe_registry,bytes);
  pthread_mutex_unlock(&recipe_mutex);if(!copy)return;
  char path[sizeof cache_path+24],temp[sizeof cache_path+48];uint8_t header[88];
  int ok=recipe_path(path,sizeof path);int fd=-1;
  if(ok) { recipe_header(header,copy,bytes,count);snprintf(temp,sizeof temp,"%s.tmp-XXXXXX",path);fd=mkstemp(temp); }
  ok=fd>=0&&write_all(fd,header,sizeof header)&&write_all(fd,copy,bytes)&&!fsync(fd);
  if(fd>=0&&close(fd))ok=0;
  if(ok&&rename(temp,path))ok=0;
  if(!ok&&fd>=0)unlink(temp);
  free(copy);
  if(ok) { pthread_mutex_lock(&recipe_mutex);recipe_written=generation;pthread_mutex_unlock(&recipe_mutex); }
}
