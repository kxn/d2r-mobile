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
// Keep the common one-subpass key and its hashing cost unchanged. Only the
// pixel-local multi-subpass path allocates/compares the additional topology.
enum { RENDER_PASS_SUBPASSES=2,RENDER_PASS_DEPENDENCIES=4 };
typedef struct {
  VkAttachmentReference inputs[2][8],second_colors[8],second_depth;
  uint32_t preserve[2][9];
  VkSubpassDependency dependencies[RENDER_PASS_DEPENDENCIES];
  uint32_t subpasses,dependency_count,input_count[2],preserve_count[2];
  uint32_t second_colors_count,second_has_depth;
} render_pass_extra_key;
typedef struct render_pass_object {
  struct render_pass_object *next;
  struct render_pass_object *hash_next;
  render_pass_key key;
  render_pass_extra_key *extra;
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
    vk_record_object_retired();
    vkDestroyRenderPass(V.device,p->handle,NULL);free(p->extra);free(p);
  }
}
static void framebuffer_destroy_locked(framebuffer_object *f) {
  vk_record_object_retired();
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
static int render_pass_reference_key(VkAttachmentReference *out,
    const VkAttachmentReference *in,uint32_t attachments) {
  if(in->attachment!=VK_ATTACHMENT_UNUSED && in->attachment>=attachments)return 0;
  *out=(VkAttachmentReference){in->attachment,in->layout};return 1;
}
static int render_pass_description_key(const VkRenderPassCreateInfo *ci,
    render_pass_key *key,render_pass_extra_key *extra,int *extended) {
  if(!ci || ci->pNext || ci->flags || !ci->subpassCount || ci->subpassCount>RENDER_PASS_SUBPASSES ||
      ci->dependencyCount>RENDER_PASS_DEPENDENCIES || ci->attachmentCount>9 || !ci->pSubpasses ||
      (ci->attachmentCount&&!ci->pAttachments) || (ci->dependencyCount&&!ci->pDependencies))return 0;
  *key=(render_pass_key){.attachment_count=ci->attachmentCount};
  *extended=ci->subpassCount!=1 || ci->dependencyCount!=0 ||
    ci->pSubpasses[0].inputAttachmentCount || ci->pSubpasses[0].preserveAttachmentCount;
  if(*extended)*extra=(render_pass_extra_key){.subpasses=ci->subpassCount,.dependency_count=ci->dependencyCount};
  // Assign the scalar fields, avoiding padding/uninitialized trailing slots.
  for(unsigned k=0;k<ci->attachmentCount;k++) {
    const VkAttachmentDescription *a=&ci->pAttachments[k];
    key->attachments[k]=(VkAttachmentDescription){a->flags,a->format,a->samples,
      a->loadOp,a->storeOp,a->stencilLoadOp,a->stencilStoreOp,a->initialLayout,a->finalLayout};
  }
  for(unsigned sub=0;sub<ci->subpassCount;sub++) {
    const VkSubpassDescription *s=&ci->pSubpasses[sub];
    if(s->flags || s->pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS || s->pResolveAttachments ||
        s->inputAttachmentCount>8 || s->colorAttachmentCount>8 || s->preserveAttachmentCount>9 ||
        (s->inputAttachmentCount&&!s->pInputAttachments) ||
        (s->colorAttachmentCount&&!s->pColorAttachments) ||
        (s->preserveAttachmentCount&&!s->pPreserveAttachments))return 0;
    extra->input_count[sub]=s->inputAttachmentCount;extra->preserve_count[sub]=s->preserveAttachmentCount;
    if(s->inputAttachmentCount || s->preserveAttachmentCount)*extended=1;
    if(!sub) { key->color_count=s->colorAttachmentCount;key->has_depth=s->pDepthStencilAttachment!=NULL; }
    else { extra->second_colors_count=s->colorAttachmentCount;extra->second_has_depth=s->pDepthStencilAttachment!=NULL; }
    for(unsigned k=0;k<s->colorAttachmentCount;k++)
      if(!render_pass_reference_key(sub?&extra->second_colors[k]:&key->colors[k],
          &s->pColorAttachments[k],ci->attachmentCount))return 0;
    if(s->pDepthStencilAttachment && !render_pass_reference_key(sub?&extra->second_depth:&key->depth,
        s->pDepthStencilAttachment,ci->attachmentCount))return 0;
    for(unsigned k=0;k<s->inputAttachmentCount;k++)
      if(!render_pass_reference_key(&extra->inputs[sub][k],&s->pInputAttachments[k],ci->attachmentCount))return 0;
    for(unsigned k=0;k<s->preserveAttachmentCount;k++) {
      if(s->pPreserveAttachments[k]>=ci->attachmentCount)return 0;
      extra->preserve[sub][k]=s->pPreserveAttachments[k];
    }
  }
  for(unsigned k=0;k<ci->dependencyCount;k++) {
    const VkSubpassDependency *d=&ci->pDependencies[k];
    if((d->srcSubpass!=VK_SUBPASS_EXTERNAL && d->srcSubpass>=ci->subpassCount) ||
       (d->dstSubpass!=VK_SUBPASS_EXTERNAL && d->dstSubpass>=ci->subpassCount))return 0;
    extra->dependencies[k]=(VkSubpassDependency){d->srcSubpass,d->dstSubpass,d->srcStageMask,
      d->dstStageMask,d->srcAccessMask,d->dstAccessMask,d->dependencyFlags};
  }
  return 1;
}
static render_pass_object *render_pass_acquire(const VkRenderPassCreateInfo *ci) {
  render_pass_key key;render_pass_extra_key extra;int extended;
  if(!render_pass_description_key(ci,&key,&extra,&extended))
    die("Vulkan render pass cache unsupported description");
  uint64_t hash=render_object_hash(&key,sizeof key,14695981039346656037ull);
  if(extended)hash=render_object_hash(&extra,sizeof extra,hash);
  unsigned bucket=hash%128;
  pthread_mutex_lock(&render_object_mu);
  for(render_pass_object *p=render_pass_buckets[bucket];p;p=p->hash_next)
    if(!!p->extra==extended && !memcmp(&p->key,&key,sizeof key) &&
        (!extended || !memcmp(p->extra,&extra,sizeof extra))) {
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
      vk_record_object_retired();
      vkDestroyRenderPass(V.device,p->handle,NULL);free(p->extra);free(p);
    }
  }
  render_pass_object *p=calloc(1,sizeof *p);
  if(!p) die("Vulkan render pass cache allocation");
  p->key=key;p->references=1;p->age=++render_object_age;
  if(extended) {
    p->extra=malloc(sizeof extra);if(!p->extra)die("Vulkan render pass topology allocation");
    *p->extra=extra;
  }
  VK_CHECK(vkCreateRenderPass(V.device,ci,NULL,&p->handle));
  if(render_pass_object_count<RENDER_PASS_OBJECT_LIMIT) {
    p->cached=1;p->next=render_pass_objects;render_pass_objects=p;render_pass_object_count++;
    p->bucket=bucket;p->hash_next=render_pass_buckets[bucket];render_pass_buckets[bucket]=p;
  }
  pthread_mutex_unlock(&render_object_mu);return p;
}
#ifdef DM_VULKAN_TEST_EMBED
void *vk_test_render_pass_acquire(const VkRenderPassCreateInfo *ci,VkRenderPass *handle) {
  render_pass_object *p=render_pass_acquire(ci);*handle=p->handle;return p;
}
void vk_test_render_pass_release(void *pass) { render_pass_release(pass); }
int vk_test_render_pass_supported(const VkRenderPassCreateInfo *ci) {
  render_pass_key key;render_pass_extra_key extra;int extended;
  return render_pass_description_key(ci,&key,&extra,&extended);
}
#endif
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
#ifdef DM_VULKAN_TEST_EMBED
void *vk_test_framebuffer_acquire(void *pass,const VkFramebufferCreateInfo *ci,VkFramebuffer *handle) {
  framebuffer_object *f=framebuffer_acquire(pass,ci);*handle=f->handle;return f;
}
void vk_test_framebuffer_release(void *framebuffer) { framebuffer_release(framebuffer); }
#endif
void vk_render_view_retired(VkImageView view) {
  vk_record_object_retired();
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
