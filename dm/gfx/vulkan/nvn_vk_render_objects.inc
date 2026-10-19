// Immutable render objects can outlive one submission. Jobs hold references
// until their real GPU completion; the cache itself never keeps an image view
// alive. Retiring a view removes every matching framebuffer from lookup before
// that view can be destroyed/reused. The completion thread can retire views,
// so cache ownership is protected independently from the recording state.
typedef struct {
  VkAttachmentDescription attachments[9];
  VkAttachmentReference colors[8],depth;
  unsigned attachment_count,color_count,has_depth;
} render_pass_key;
typedef struct render_pass_object {
  struct render_pass_object *next;
  struct render_pass_object *hash_next;
  render_pass_key key;
  VkRenderPass handle;
  uint64_t age;
  unsigned references;
  unsigned bucket;
  int cached;
} render_pass_object;
typedef struct framebuffer_object {
  struct framebuffer_object *next;
  struct framebuffer_object *hash_next;
  render_pass_object *pass;
  VkImageView views[9];
  VkFramebuffer handle;
  uint32_t count,width,height;
  uint64_t age;
  unsigned references;
  unsigned bucket;
  int cached;
} framebuffer_object;
static pthread_mutex_t render_object_mu=PTHREAD_MUTEX_INITIALIZER;
static render_pass_object *render_pass_objects;
static framebuffer_object *framebuffer_objects;
static render_pass_object *render_pass_buckets[128];
static framebuffer_object *framebuffer_buckets[512];
static unsigned render_pass_object_count,framebuffer_object_count;
static uint64_t render_object_age;
enum { RENDER_PASS_OBJECT_LIMIT=256,FRAMEBUFFER_OBJECT_LIMIT=512 };
static uint64_t render_object_hash(const void *bytes,size_t size,uint64_t hash) {
  const uint8_t *p=bytes;
  for(size_t k=0;k<size;k++) hash=(hash^p[k])*1099511628211ull;
  return hash;
}
static void framebuffer_unindex(framebuffer_object *f) {
  framebuffer_object **at=&framebuffer_buckets[f->bucket];
  while(*at && *at!=f) at=&(*at)->hash_next;
  if(!*at) die("Vulkan framebuffer cache index lost");
  *at=f->hash_next;
}
static void render_pass_release_locked(render_pass_object *p) {
  if(!p->references) die("Vulkan render pass reference underflow");
  if(!--p->references && !p->cached) {
    vkDestroyRenderPass(V.device,p->handle,NULL);free(p);
  }
}
static void framebuffer_destroy_locked(framebuffer_object *f) {
  vkDestroyFramebuffer(V.device,f->handle,NULL);
  render_pass_release_locked(f->pass);free(f);
}
static void framebuffer_release(framebuffer_object *f) {
  if(!f) return;
  pthread_mutex_lock(&render_object_mu);
  if(!f->references) die("Vulkan framebuffer reference underflow");
  if(!--f->references && !f->cached) framebuffer_destroy_locked(f);
  pthread_mutex_unlock(&render_object_mu);
}
static void render_pass_release(render_pass_object *p) {
  if(!p) return;
  pthread_mutex_lock(&render_object_mu);
  render_pass_release_locked(p);
  pthread_mutex_unlock(&render_object_mu);
}
static render_pass_object *render_pass_acquire(const VkRenderPassCreateInfo *ci) {
  if(ci->pNext || ci->flags || ci->subpassCount!=1 || ci->dependencyCount ||
      ci->attachmentCount>9) die("Vulkan render pass cache unsupported description");
  const VkSubpassDescription *s=ci->pSubpasses;
  if(s->flags || s->pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS ||
      s->inputAttachmentCount || s->pResolveAttachments ||
      s->preserveAttachmentCount || s->colorAttachmentCount>8)
    die("Vulkan render pass cache unsupported subpass");
  render_pass_key key={.attachment_count=ci->attachmentCount,
    .color_count=s->colorAttachmentCount,.has_depth=s->pDepthStencilAttachment!=NULL};
  // Assign the scalar fields, avoiding padding/uninitialized trailing slots.
  for(unsigned k=0;k<ci->attachmentCount;k++) {
    const VkAttachmentDescription *a=&ci->pAttachments[k];
    key.attachments[k]=(VkAttachmentDescription){a->flags,a->format,a->samples,
      a->loadOp,a->storeOp,a->stencilLoadOp,a->stencilStoreOp,a->initialLayout,a->finalLayout};
  }
  for(unsigned k=0;k<s->colorAttachmentCount;k++)
    key.colors[k]=(VkAttachmentReference){s->pColorAttachments[k].attachment,s->pColorAttachments[k].layout};
  if(key.has_depth) key.depth=(VkAttachmentReference){s->pDepthStencilAttachment->attachment,
                                                    s->pDepthStencilAttachment->layout};
  unsigned bucket=render_object_hash(&key,sizeof key,14695981039346656037ull)%128;
  pthread_mutex_lock(&render_object_mu);
  for(render_pass_object *p=render_pass_buckets[bucket];p;p=p->hash_next)
    if(!memcmp(&p->key,&key,sizeof key)) {
      p->references++;p->age=++render_object_age;
      pthread_mutex_unlock(&render_object_mu);return p;
    }
  if(render_pass_object_count>=RENDER_PASS_OBJECT_LIMIT) {
    render_pass_object **oldest=NULL;
    for(render_pass_object **at=&render_pass_objects;*at;at=&(*at)->next)
      if(!(*at)->references && (!oldest || (*at)->age<(*oldest)->age)) oldest=at;
    if(oldest) {
      render_pass_object *p=*oldest;*oldest=p->next;render_pass_object_count--;
      render_pass_object **at=&render_pass_buckets[p->bucket];
      while(*at && *at!=p) at=&(*at)->hash_next;
      if(!*at) die("Vulkan render pass cache index lost");
      *at=p->hash_next;
      vkDestroyRenderPass(V.device,p->handle,NULL);free(p);
    }
  }
  render_pass_object *p=calloc(1,sizeof *p);
  if(!p) die("Vulkan render pass cache allocation");
  p->key=key;p->references=1;p->age=++render_object_age;
  VK_CHECK(vkCreateRenderPass(V.device,ci,NULL,&p->handle));
  if(render_pass_object_count<RENDER_PASS_OBJECT_LIMIT) {
    p->cached=1;p->next=render_pass_objects;render_pass_objects=p;render_pass_object_count++;
    p->bucket=bucket;p->hash_next=render_pass_buckets[bucket];render_pass_buckets[bucket]=p;
  }
  pthread_mutex_unlock(&render_object_mu);return p;
}
static framebuffer_object *framebuffer_acquire(render_pass_object *pass,
                                               const VkFramebufferCreateInfo *ci) {
  if(ci->pNext || ci->flags || ci->layers!=1 || ci->attachmentCount>9 ||
      ci->renderPass!=pass->handle) die("Vulkan framebuffer cache unsupported description");
  uint64_t hash=render_object_hash(&pass,sizeof pass,14695981039346656037ull);
  const uint32_t shape[]={ci->attachmentCount,ci->width,ci->height};
  hash=render_object_hash(shape,sizeof shape,hash);
  unsigned bucket=render_object_hash(ci->pAttachments,ci->attachmentCount*sizeof *ci->pAttachments,hash)%512;
  pthread_mutex_lock(&render_object_mu);
  for(framebuffer_object *f=framebuffer_buckets[bucket];f;f=f->hash_next)
    if(f->pass==pass && f->count==ci->attachmentCount && f->width==ci->width &&
        f->height==ci->height && !memcmp(f->views,ci->pAttachments,f->count*sizeof *f->views)) {
      f->references++;f->age=++render_object_age;
      pthread_mutex_unlock(&render_object_mu);return f;
    }
  if(framebuffer_object_count>=FRAMEBUFFER_OBJECT_LIMIT) {
    framebuffer_object **oldest=NULL;
    for(framebuffer_object **at=&framebuffer_objects;*at;at=&(*at)->next)
      if(!(*at)->references && (!oldest || (*at)->age<(*oldest)->age)) oldest=at;
    if(oldest) {
      framebuffer_object *f=*oldest;*oldest=f->next;framebuffer_object_count--;
      framebuffer_unindex(f);
      framebuffer_destroy_locked(f);
    }
  }
  framebuffer_object *f=calloc(1,sizeof *f);
  if(!f) die("Vulkan framebuffer cache allocation");
  f->pass=pass;pass->references++;f->count=ci->attachmentCount;
  memcpy(f->views,ci->pAttachments,f->count*sizeof *f->views);
  f->width=ci->width;f->height=ci->height;f->references=1;f->age=++render_object_age;
  VK_CHECK(vkCreateFramebuffer(V.device,ci,NULL,&f->handle));
  if(framebuffer_object_count<FRAMEBUFFER_OBJECT_LIMIT) {
    f->cached=1;f->next=framebuffer_objects;framebuffer_objects=f;framebuffer_object_count++;
    f->bucket=bucket;f->hash_next=framebuffer_buckets[bucket];framebuffer_buckets[bucket]=f;
  }
  pthread_mutex_unlock(&render_object_mu);return f;
}
void vk_render_view_retired(VkImageView view) {
  pthread_mutex_lock(&render_object_mu);
  for(framebuffer_object **at=&framebuffer_objects;*at;) {
    framebuffer_object *f=*at;unsigned k=0;
    while(k<f->count && f->views[k]!=view) k++;
    if(k==f->count) { at=&f->next;continue; }
    *at=f->next;framebuffer_object_count--;f->cached=0;
    framebuffer_unindex(f);
    // Jobs may still retain metadata for a completed command buffer. Removing
    // lookup ownership prevents any new recording from reusing a retired view.
    if(!f->references) framebuffer_destroy_locked(f);
  }
  pthread_mutex_unlock(&render_object_mu);
}
