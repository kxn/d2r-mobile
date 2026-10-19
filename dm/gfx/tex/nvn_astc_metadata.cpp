#include "nvn_astc_metadata.h"
#include "../../third_party/mesa-astc/metadata.inc"

static bool supported_2d(unsigned w, unsigned h) {
  static const unsigned footprints[][2] = {
    {4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},
    {8,8},{10,5},{10,6},{10,8},{10,10},{12,10},{12,12}};
  for (const auto &p : footprints) if (w == p[0] && h == p[1]) return true;
  return false;
}

extern "C" int nvn_astc_mip_is_ldr(const void *data, size_t size,
                                   unsigned w, unsigned h) {
  using namespace dm_mesa_astc;
  if (!data || !size || size % 16 || !supported_2d(w,h)) return 0;
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t offset = 0; offset < size; offset += 16) {
    InputBitVector bits = {};
    // ASTC storage is little endian, independently of host endianness.
    for (unsigned word = 0; word < 4; word++)
      for (unsigned b = 0; b < 4; b++)
        bits.data[word] |= uint32_t(bytes[offset + word*4 + b]) << (b*8);
    // Do not clear the decoder's unused pixel/weight arrays per block. Every
    // metadata field consumed below is initialized by these original parser
    // methods; only the mode's void flag and dimensionality are caller state.
    Block block;
    block.is_void_extent = false;
    block.wt_d = 1;
    if (block.decode_block_mode(bits) != decode_error::ok) return 0;
    if (block.is_void_extent) {
      // Mesa rejects FP16 constants; also conservatively reject the reserved
      // bits of the constant encoding, rather than certifying an error block.
      if (block.void_extent_d || bits.get_bits(10,2) != 3) return 0;
      continue;
    }
    // Bound the original parser before its variable-offset CEM reads. These
    // checks precede calculate_from_weights' assertion/abort on reserved mode.
    if (block.wt_range < 2 || block.wt_range > 7) return 0;
    block.calculate_from_weights();
    if (block.wt_w > int(w) || block.wt_h > int(h) ||
        block.num_weights > 64 || block.weight_bits < 24 || block.weight_bits > 96)
      return 0;
    block.num_parts = bits.get_bits(11,2) + 1;
    if (block.dual_plane && block.num_parts > 3) return 0;
    block.decode_cem(bits);
    for (int p = 0; p < block.num_parts; p++) {
      switch (block.cems[p]) {
      case 0: case 1: case 4: case 5: case 6:
      case 8: case 9: case 10: case 12: case 13: break;
      default: return 0; // HDR CEM 2,3,7,11,14,15, or unknown.
      }
    }
    block.num_cem_values = ((block.cem_base_class+1)*block.num_parts +
                            block.extra_cem_bits)*2;
    if (block.num_cem_values > 18) return 0;
    block.calculate_remaining_bits();
    if (block.calculate_colour_endpoints_size() != decode_error::ok) return 0;
  }
  return 1;
}
