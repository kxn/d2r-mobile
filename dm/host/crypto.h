// 原宿主摘要接口的平台适配；算法来自固定版本的 mbed TLS，不包含游戏内容。
// Platform adaptation of original host digest APIs; algorithms come from pinned
// mbed TLS, without game content.
#pragma once
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha1.h"
#define CC_SHA1_DIGEST_LENGTH 20
typedef mbedtls_sha256_context CC_SHA256_CTX;
typedef unsigned CC_LONG;
static inline unsigned char *CC_SHA1(const void *p, CC_LONG n,
                                    unsigned char *out) {
  mbedtls_sha1(p, n, out);
  return out;
}
#define CC_SHA256_Init(c) mbedtls_sha256_starts((c), 0)
#define CC_SHA256_Update(c, p, n)                                              \
  mbedtls_sha256_update((c), (const unsigned char *)(p), (n))
#define CC_SHA256_Final(p, c) mbedtls_sha256_finish((c), (p))
static inline unsigned char *CC_SHA256(const void *p, CC_LONG n,
                                      unsigned char *out) {
  mbedtls_sha256(p, n, out, 0);
  return out;
}
static inline unsigned char *CC_MD5(const void *p, CC_LONG n,
                                    unsigned char *out) {
  mbedtls_md5(p, n, out);
  return out;
}
#endif
