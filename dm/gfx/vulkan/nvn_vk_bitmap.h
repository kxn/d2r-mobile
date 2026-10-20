#ifndef NVN_VK_BITMAP_H
#define NVN_VK_BITMAP_H
#include <stdint.h>

// Consume the lowest consecutive one-bit run from a NONZERO word. Unsigned
// carry clears that run; AND with the original word removes the carried bit.
// No shift by 64, ctz(0), bitmap write or retained state is needed. The caller
// owns the local word and still joins adjacent runs across word boundaries.
static inline unsigned vk_bitmap_take_run(uint64_t *word, unsigned *first) {
  uint64_t bits=*word;
  *first=(unsigned)__builtin_ctzll(bits);
  uint64_t tail=bits>>*first;
  unsigned length=tail==UINT64_MAX?64:(unsigned)__builtin_ctzll(~tail);
  *word=bits & (bits+(bits & (UINT64_C(0)-bits)));
  return length;
}
#endif
