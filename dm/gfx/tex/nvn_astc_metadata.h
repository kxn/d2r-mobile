// Conservative LDR-content proof, using the existing Mesa ASTC parser.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Complete tightly packed 2D mip. Unknown, HDR, malformed or unsupported
// input returns false. No allocation or pixel decoding; scans at most size.
int nvn_astc_mip_is_ldr(const void *blocks, size_t size,
                      unsigned block_width, unsigned block_height);
#ifdef __cplusplus
}
#endif
