// 离线反射的小端二进制 ABI；运行时对象字段变化不应使着色器缓存失效。
// Little-endian offline reflection ABI; runtime object fields must not
// invalidate the shader cache.
#pragma once
#include <stdint.h>
#define VK_META_V1 UINT32_C(0x314b5644)
#define VK_META_V2 UINT32_C(0x324b5644)
#define VK_META_V3 UINT32_C(0x334b5644)
// Runtime-only alternative; never reinterpret its texture slot as an NVN SSBO.
#define VK_BINDING_TEXEL_STORAGE 10
typedef struct {
  int32_t kind, binding, reg, sampler, flags, count, bank, type;
} vk_binding;
// These are different offline reflection enums. Texture bit0 describes
// resolution scaling, not writes; ImageStore is bit2.
static inline int vk_binding_writable(const vk_binding *b) {
  return (b->kind==2 || b->kind==8)?!!(b->flags&1):
      (b->kind==4 || b->kind==9)?!!(b->flags&4):0;
}
