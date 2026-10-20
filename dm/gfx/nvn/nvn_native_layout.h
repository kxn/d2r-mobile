// Experimental host-packaged texture ABI. Metadata is immutable, image bytes
// are not: neither this marker nor a table lookup is a CPU/GPU ownership proof.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "nvn_tex.h"

_Static_assert(__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__,"native resource wire ABI is little endian");

enum { NVN_NATIVE_LEVELS = 16, NVN_NATIVE_MAX_ENTRIES = 65536 };
typedef struct { uint64_t offset,size,row_pitch,array_pitch,depth_pitch; } nvn_native_mip;
// Byte-identical to the independently validated native producer v1 record.
typedef struct {
  uint8_t magic[8]; uint32_t version,header_bytes;
  uint64_t image_size,image_alignment;
  uint32_t format,width,height,depth,levels,layers,image_type,samples,
    tiling,usage,flags,sharing,initial_layout,external_handle,
    producer_layout,producer_family,vendor,device_id,driver_version,driver_id;
  uint8_t layout_uuid[16],device_uuid[16],driver_uuid[16];
  uint8_t input_sha256[32],payload_sha256[32],declaration_sha256[32],
    metadata_sha256[32],producer_source_sha256[32];
  nvn_native_mip mips[NVN_NATIVE_LEVELS]; uint8_t digest[32];
} nvn_native_recipe;
typedef struct {
  uint32_t id,reserved; nvn_texdesc description; nvn_native_recipe recipe;
} nvn_native_entry;
typedef struct {
  uint8_t magic[8]; uint32_t version,header_bytes,count,entry_bytes;
  uint64_t file_bytes; uint8_t pack_sha256[32],driver_sha256[32],producer_sha256[32],digest[32];
} nvn_native_table;
// v2: one record after the entries, by family ID. Only the explicitly declared
// thumbnail-tail shape may use the original source encoding; never infer it
// from a small image size or reinterpret linear bytes as native opaque data.
typedef struct {uint8_t source_layout[8];uint32_t thumbnail_mip,reserved;} nvn_native_family;
_Static_assert(sizeof(nvn_native_family)==16,"native family metadata ABI");
typedef struct { uint8_t layout[16],device[16],driver[16];
  uint8_t driver_sha256[32],producer_sha256[32]; } nvn_native_identity;
_Static_assert(sizeof(nvn_native_recipe)==992,"native producer ABI");
_Static_assert(sizeof(nvn_native_entry)==1040,"native entry ABI");
_Static_assert(sizeof(nvn_native_table)==160,"native table ABI");

// Startup only, before publishing any NVN texture. Caller explicitly selects
// this pack and obtains identity from the active Vulkan driver. One successful
// install owns a process-lifetime metadata copy; it cannot be replaced online.
// Failed validation leaves the default (no native pack) completely unchanged.
int nvn_native_table_install(const void *,size_t,const uint8_t pack_sha256[32],
                             const nvn_native_identity *);
static inline int nvn_native_layout_is_marker(const uint8_t p[8]) {
  return p && p[0]=='D' && p[1]=='M' && p[2]=='N';
}
const nvn_native_entry *nvn_native_layout_resolve(const uint8_t layout[8],
                                                const nvn_texdesc *);
int nvn_native_layout_storage(const uint8_t layout[8],const nvn_texdesc *,
                              nvn_texlayout *);

// 1: exact opaque full image, 2: exact declared thumbnail tail in the original
// NVN source encoding, 0: unknown/incompatible. Outputs are only set for tail.
// This does not certify immutable bytes or GPU initialization.
int nvn_native_layout_classify(const uint8_t layout[8],const nvn_texdesc *,
                              nvn_texdesc *source,uint8_t source_layout[8]);
