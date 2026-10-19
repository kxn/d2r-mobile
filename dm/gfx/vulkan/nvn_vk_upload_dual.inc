// Experimental direct upload only. One source read feeds two contiguous
// destination stores; no cache/generation/timing policy is hidden in this copy.
// Caller retains publication exclusion and proves no prior GPU uses the map.
static int upload_dual_enabled;
static void upload_copy_dual(uint8_t *map,uint8_t *shadow,const uint8_t *guest,size_t size) {
  for(;size>=64;guest+=64,map+=64,shadow+=64,size-=64) {
#if defined(__aarch64__)
    uint8x16_t a=vld1q_u8(guest),b=vld1q_u8(guest+16);
    uint8x16_t c=vld1q_u8(guest+32),d=vld1q_u8(guest+48);
    vst1q_u8(map,a);vst1q_u8(map+16,b);vst1q_u8(map+32,c);vst1q_u8(map+48,d);
    vst1q_u8(shadow,a);vst1q_u8(shadow+16,b);vst1q_u8(shadow+32,c);vst1q_u8(shadow+48,d);
#else
    uint8_t bytes[64];memcpy(bytes,guest,64);memcpy(map,bytes,64);memcpy(shadow,bytes,64);
#endif
  }
  for(;size>=16;guest+=16,map+=16,shadow+=16,size-=16) {
#if defined(__aarch64__)
    uint8x16_t value=vld1q_u8(guest);vst1q_u8(map,value);vst1q_u8(shadow,value);
#else
    uint8_t bytes[16];memcpy(bytes,guest,16);memcpy(map,bytes,16);memcpy(shadow,bytes,16);
#endif
  }
  if(size&8) {
    uint64_t value;memcpy(&value,guest,8);memcpy(map,&value,8);memcpy(shadow,&value,8);
    guest+=8;map+=8;shadow+=8;
  }
  if(size&4) {
    uint32_t value;memcpy(&value,guest,4);memcpy(map,&value,4);memcpy(shadow,&value,4);
    guest+=4;map+=4;shadow+=4;
  }
  if(size&2) {
    uint16_t value;memcpy(&value,guest,2);memcpy(map,&value,2);memcpy(shadow,&value,2);
    guest+=2;map+=2;shadow+=2;
  }
  if(size&1) {uint8_t value=*guest;*map=value;*shadow=value;}
}
static void pool_spans_copy_dual(pool *p,uint64_t offset,uint64_t size) {
  pool_span_iter it;if(!pool_span_iter_begin(p,offset,size,&it))die("Vulkan dual upload range");
  pool_span_piece piece;int next;
  while((next=pool_span_iter_next(&it,&piece))==1) {
    if(!piece.backing->buffer.map)die("Vulkan dual upload backing not host mapped");
    upload_copy_dual(piece.backing->buffer.map+piece.offset,p->shadow+piece.logical,
        p->guest+piece.logical,piece.size);
  }
  if(next<0)die("Vulkan dual upload across unbacked hole");
}
#ifdef DM_VULKAN_TEST_EMBED
int vk_test_upload_dual_enable(int enabled) {int previous=upload_dual_enabled;upload_dual_enabled=!!enabled;return previous;}
void vk_test_upload_copy_dual(uint8_t *map,uint8_t *shadow,const uint8_t *guest,size_t size) {
  upload_copy_dual(map,shadow,guest,size);
}
#endif
