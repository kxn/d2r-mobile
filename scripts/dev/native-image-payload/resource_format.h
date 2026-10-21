// Fixed little-endian transfer declaration. No pointers or platform handles.
#pragma once
#include <stddef.h>
#include <stdint.h>
enum { DM_RESOURCE_MIPS = 16 };
typedef struct {
  uint32_t width, height;
  uint64_t offset, size;
  uint8_t sha256[32];
} dm_resource_mip;
typedef struct {
  uint8_t magic[8];
  uint32_t version, header_bytes;
  uint64_t file_bytes;
  uint32_t width, height, levels, vk_format, block, game_format, nvn_format,
      reserved;
  uint8_t texture_sha256[32], descriptor_sha256[32], declaration_sha256[32];
  dm_resource_mip mips[DM_RESOURCE_MIPS];
  uint8_t digest[32];
} dm_resource_input;
_Static_assert(sizeof(dm_resource_mip) == 56, "fixed mip wire format");
_Static_assert(offsetof(dm_resource_input, mips) == 152, "fixed header prefix");
_Static_assert(sizeof(dm_resource_input) == 1080, "fixed resource wire format");

// Producer/consumer transfer record, not a new game packaged-layout format.
typedef struct {
  uint64_t offset, size, row_pitch, array_pitch, depth_pitch;
} dm_resource_native_mip;
typedef struct {
  uint8_t magic[8];
  uint32_t version, header_bytes;
  uint64_t image_size, image_alignment;
  uint32_t format, width, height, depth, levels, layers, image_type, samples,
      tiling, usage, flags, sharing, initial_layout, external_handle,
      producer_layout, producer_family, vendor, device_id, driver_version,
      driver_id;
  uint8_t layout_uuid[16], device_uuid[16], driver_uuid[16];
  uint8_t input_sha256[32], payload_sha256[32], declaration_sha256[32],
      metadata_sha256[32], producer_source_sha256[32];
  dm_resource_native_mip mips[DM_RESOURCE_MIPS];
  uint8_t digest[32];
} dm_resource_native;
_Static_assert(sizeof(dm_resource_native_mip) == 40, "fixed native mip ABI");

_Static_assert(sizeof(dm_resource_native) == 992, "fixed native record ABI");
