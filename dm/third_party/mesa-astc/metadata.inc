/*
 * Copyright 2015 Philip Taylor <philip@zaynar.co.uk>
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// Metadata-only extraction from Mesa 26.2.5 src/util/texcompress_astc.cpp.
// Original parser sections below are verbatim, see provenance.json.
// No pixel decompression, endpoint unpacking, infill, or codec is implemented.
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define UNREACHABLE(message) abort()
static const bool VERBOSE_DECODE = false;
namespace dm_mesa_astc {
class decode_error
{
public:
   enum type {
      ok,
      unsupported_hdr_void_extent,
      reserved_block_mode_1,
      reserved_block_mode_2,
      dual_plane_and_too_many_partitions,
      invalid_range_in_void_extent,
      weight_grid_exceeds_block_size,
      invalid_colour_endpoints_size,
      invalid_colour_endpoints_count,
      invalid_weight_bits,
      invalid_num_weights,
   };
};



struct cem_range {
   uint8_t max;
   uint8_t t, q, b;
};

/* Based on the Color Unquantization Parameters table,
 * plus the bit-only representations, sorted by increasing size
 */
static cem_range cem_ranges[] = {
   { 5, 1, 0, 1 },
   { 7, 0, 0, 3 },
   { 9, 0, 1, 1 },
   { 11, 1, 0, 2 },
   { 15, 0, 0, 4 },
   { 19, 0, 1, 2 },
   { 23, 1, 0, 3 },
   { 31, 0, 0, 5 },
   { 39, 0, 1, 3 },
   { 47, 1, 0, 4 },
   { 63, 0, 0, 6 },
   { 79, 0, 1, 4 },
   { 95, 1, 0, 5 },
   { 127, 0, 0, 7 },
   { 159, 0, 1, 5 },
   { 191, 1, 0, 6 },
   { 255, 0, 0, 8 },
};


struct uint8x4_t
{
   uint8_t v[4];

   uint8x4_t() { }

   uint8x4_t(int a, int b, int c, int d)
   {
      assert(0 <= a && a <= 255);
      assert(0 <= b && b <= 255);
      assert(0 <= c && c <= 255);
      assert(0 <= d && d <= 255);
      v[0] = a;
      v[1] = b;
      v[2] = c;
      v[3] = d;
   }

   static uint8x4_t clamped(int a, int b, int c, int d)
   {
      uint8x4_t r;
      r.v[0] = MAX2(0, MIN2(255, a));
      r.v[1] = MAX2(0, MIN2(255, b));
      r.v[2] = MAX2(0, MIN2(255, c));
      r.v[3] = MAX2(0, MIN2(255, d));
      return r;
   }
};


struct InputBitVector
{
   uint32_t data[4];

   void printf_bits(int offset, int count, const char *fmt = "", ...)
   {
      char out[129];
      memset(out, '.', 128);
      out[128] = '\0';
      int idx = offset;
      for (int i = 0; i < count; ++i) {
         out[127 - idx] = ((data[idx >> 5] >> (idx & 31)) & 1) ? '1' : '0';
         ++idx;
      }
      printf("%s ", out);
      va_list ap;
      va_start(ap, fmt);
      vprintf(fmt, ap);
      va_end(ap);
      printf("\n");
   }

   uint32_t get_bits(int offset, int count)
   {
      assert(count >= 0 && count < 32);

      uint32_t out = 0;
      if (offset < 32)
         out |= data[0] >> offset;

      if (0 < offset && offset <= 32)
         out |= data[1] << (32 - offset);
      if (32 < offset && offset < 64)
         out |= data[1] >> (offset - 32);

      if (32 < offset && offset <= 64)
         out |= data[2] << (64 - offset);
      if (64 < offset && offset < 96)
         out |= data[2] >> (offset - 64);

      if (64 < offset && offset <= 96)
         out |= data[3] << (96 - offset);
      if (96 < offset && offset < 128)
         out |= data[3] >> (offset - 96);

      out &= (1 << count) - 1;
      return out;
   }

   uint64_t get_bits64(int offset, int count)
   {
      assert(count >= 0 && count < 64);

      uint64_t out = 0;
      if (offset < 32)
         out |= data[0] >> offset;

      if (offset <= 32)
         out |= (uint64_t)data[1] << (32 - offset);
      if (32 < offset && offset < 64)
         out |= data[1] >> (offset - 32);

      if (0 < offset && offset <= 64)
         out |= (uint64_t)data[2] << (64 - offset);
      if (64 < offset && offset < 96)
         out |= data[2] >> (offset - 64);

      if (32 < offset && offset <= 96)
         out |= (uint64_t)data[3] << (96 - offset);
      if (96 < offset && offset < 128)
         out |= data[3] >> (offset - 96);

      out &= ((uint64_t)1 << count) - 1;
      return out;
   }

   uint32_t get_bits_rev(int offset, int count)
   {
      assert(offset >= count);
      uint32_t tmp = get_bits(offset - count, count);
      uint32_t out = 0;
      for (int i = 0; i < count; ++i)
         out |= ((tmp >> i) & 1) << (count - 1 - i);
      return out;
   }
};


class Decoder
{
public:
   Decoder(int block_w, int block_h, int block_d, bool srgb, bool output_unorm8)
      : block_w(block_w), block_h(block_h), block_d(block_d), srgb(srgb),
        output_unorm8(output_unorm8) {}

   decode_error::type decode(const uint8_t *in, uint16_t *output) const;

   int block_w, block_h, block_d;
   bool srgb, output_unorm8;
};

struct Block
{
   bool is_error;
   bool bogus_colour_endpoints;
   bool bogus_weights;

   int high_prec;
   int dual_plane;
   int colour_component_selector;
   int wt_range;
   int wt_w, wt_h, wt_d;
   int num_parts;
   int partition_index;

   bool is_void_extent;
   int void_extent_d;
   int void_extent_min_s;
   int void_extent_max_s;
   int void_extent_min_t;
   int void_extent_max_t;
   uint16_t void_extent_colour_r;
   uint16_t void_extent_colour_g;
   uint16_t void_extent_colour_b;
   uint16_t void_extent_colour_a;

   bool is_multi_cem;
   int num_extra_cem_bits;
   int colour_endpoint_data_offset;
   int extra_cem_bits;
   int cem_base_class;
   int cems[4];

   int num_cem_values;

   /* Calculated by unpack_weights(): */
   uint8_t weights_quant[64 + 4]; /* max 64 values, plus padding for overflows in trit parsing */

   /* Calculated by unquantise_weights(): */
   uint8_t weights[64 + 18]; /* max 64 values, plus padding for the infill interpolation */

   /* Calculated by unpack_colour_endpoints(): */
   uint8_t colour_endpoints_quant[18 + 4]; /* max 18 values, plus padding for overflows in trit parsing */

   /* Calculated by unquantise_colour_endpoints(): */
   uint8_t colour_endpoints[18];

   /* Calculated by calculate_from_weights(): */
   int wt_trits;
   int wt_quints;
   int wt_bits;
   int wt_max;
   int num_weights;
   int weight_bits;

   /* Calculated by calculate_remaining_bits(): */
   int remaining_bits;

   /* Calculated by calculate_colour_endpoints_size(): */
   int colour_endpoint_bits;
   int ce_max;
   int ce_trits;
   int ce_quints;
   int ce_bits;

   /* Calculated by compute_infill_weights(); */
   uint8_t infill_weights[2][216]; /* large enough for 6x6x6 */

   /* Calculated by decode_colour_endpoints(); */
   uint8x4_t endpoints_decoded[2][4];

   void calculate_from_weights();
   void calculate_remaining_bits();
   decode_error::type calculate_colour_endpoints_size();

   void unquantise_weights();
   void unquantise_colour_endpoints();

   decode_error::type decode(const Decoder &decoder, InputBitVector in);

   decode_error::type decode_block_mode(InputBitVector in);
   decode_error::type decode_void_extent(InputBitVector in);
   void decode_cem(InputBitVector in);
   void unpack_colour_endpoints(InputBitVector in);
   void decode_colour_endpoints();
   void unpack_weights(InputBitVector in);
   void compute_infill_weights(int block_w, int block_h, int block_d);

   void write_decoded(const Decoder &decoder, uint16_t *output);
};



decode_error::type Block::decode_void_extent(InputBitVector block)
{
   /* TODO: 3D */

   is_void_extent = true;
   void_extent_d = block.get_bits(9, 1);
   void_extent_min_s = block.get_bits(12, 13);
   void_extent_max_s = block.get_bits(25, 13);
   void_extent_min_t = block.get_bits(38, 13);
   void_extent_max_t = block.get_bits(51, 13);
   void_extent_colour_r = block.get_bits(64, 16);
   void_extent_colour_g = block.get_bits(80, 16);
   void_extent_colour_b = block.get_bits(96, 16);
   void_extent_colour_a = block.get_bits(112, 16);

   /* TODO: maybe we should do something useful with the extent coordinates? */

   if (void_extent_d) {
      return decode_error::unsupported_hdr_void_extent;
   }

   if (void_extent_min_s == 0x1fff && void_extent_max_s == 0x1fff
       && void_extent_min_t == 0x1fff && void_extent_max_t == 0x1fff) {

      /* No extents */

   } else {

      /* Check for illegal encoding */
      if (void_extent_min_s >= void_extent_max_s || void_extent_min_t >= void_extent_max_t) {
         return decode_error::invalid_range_in_void_extent;
      }
   }

   return decode_error::ok;
}

decode_error::type Block::decode_block_mode(InputBitVector in)
{
   dual_plane = in.get_bits(10, 1);
   high_prec = in.get_bits(9, 1);

   if (in.get_bits(0, 2) != 0x0) {
      wt_range = (in.get_bits(0, 2) << 1) | in.get_bits(4, 1);
      int a = in.get_bits(5, 2);
      int b = in.get_bits(7, 2);
      switch (in.get_bits(2, 2)) {
      case 0x0:
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "DHBBAAR00RR");
         wt_w = b + 4;
         wt_h = a + 2;
         break;
      case 0x1:
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "DHBBAAR01RR");
         wt_w = b + 8;
         wt_h = a + 2;
         break;
      case 0x2:
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "DHBBAAR10RR");
         wt_w = a + 2;
         wt_h = b + 8;
         break;
      case 0x3:
         if ((b & 0x2) == 0) {
            if (VERBOSE_DECODE)
               in.printf_bits(0, 11, "DH0BAAR11RR");
            wt_w = a + 2;
            wt_h = b + 6;
         } else {
            if (VERBOSE_DECODE)
               in.printf_bits(0, 11, "DH1BAAR11RR");
            wt_w = (b & 0x1) + 2;
            wt_h = a + 2;
         }
         break;
      }
   } else {
      if (in.get_bits(6, 3) == 0x7) {
         if (in.get_bits(0, 9) == 0x1fc) {
            if (VERBOSE_DECODE)
               in.printf_bits(0, 11, "xx111111100 (void extent)");
            return decode_void_extent(in);
         } else {
            if (VERBOSE_DECODE)
               in.printf_bits(0, 11, "xx111xxxx00");
            return decode_error::reserved_block_mode_1;
         }
      }
      if (in.get_bits(0, 4) == 0x0) {
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "xxxxxxx0000");
         return decode_error::reserved_block_mode_2;
      }

      wt_range = in.get_bits(1, 3) | in.get_bits(4, 1);
      int a = in.get_bits(5, 2);
      int b;

      switch (in.get_bits(7, 2)) {
      case 0x0:
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "DH00AARRR00");
         wt_w = 12;
         wt_h = a + 2;
         break;
      case 0x1:
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "DH01AARRR00");
         wt_w = a + 2;
         wt_h = 12;
         break;
      case 0x3:
         if (in.get_bits(5, 1) == 0) {
            if (VERBOSE_DECODE)
               in.printf_bits(0, 11, "DH1100RRR00");
            wt_w = 6;
            wt_h = 10;
         } else {
            if (VERBOSE_DECODE)
               in.printf_bits(0, 11, "DH1101RRR00");
            wt_w = 10;
            wt_h = 6;
         }
         break;
      case 0x2:
         if (VERBOSE_DECODE)
            in.printf_bits(0, 11, "BB10AARRR00");
         b = in.get_bits(9, 2);
         wt_w = a + 6;
         wt_h = b + 6;
         dual_plane = 0;
         high_prec = 0;
         break;
      }
   }
   return decode_error::ok;
}

void Block::decode_cem(InputBitVector in)
{
   cems[0] = cems[1] = cems[2] = cems[3] = -1;

   num_extra_cem_bits = 0;
   extra_cem_bits = 0;

   if (num_parts > 1) {

      partition_index = in.get_bits(13, 10);
      if (VERBOSE_DECODE)
         in.printf_bits(13, 10, "partition ID (%d)", partition_index);

      uint32_t cem = in.get_bits(23, 6);

      if ((cem & 0x3) == 0x0) {
         cem >>= 2;
         cem_base_class = cem >> 2;
         is_multi_cem = false;

         for (int i = 0; i < num_parts; ++i)
            cems[i] = cem;

         if (VERBOSE_DECODE)
            in.printf_bits(23, 6, "CEM (single, %d)", cem);
      } else {

         cem_base_class = (cem & 0x3) - 1;
         is_multi_cem = true;

         if (VERBOSE_DECODE)
            in.printf_bits(23, 6, "CEM (multi, base class %d)", cem_base_class);

         int offset = 128 - weight_bits;

         if (num_parts == 2) {
            if (VERBOSE_DECODE) {
               in.printf_bits(25, 4, "M0M0 C1 C0");
               in.printf_bits(offset - 2, 2, "M1M1");
            }

            uint32_t c0 = in.get_bits(25, 1);
            uint32_t c1 = in.get_bits(26, 1);

            extra_cem_bits = c0 + c1;

            num_extra_cem_bits = 2;

            uint32_t m0 = in.get_bits(27, 2);
            uint32_t m1 = in.get_bits(offset - 2, 2);

            cems[0] = ((cem_base_class + c0) << 2) | m0;
            cems[1] = ((cem_base_class + c1) << 2) | m1;

         } else if (num_parts == 3) {
            if (VERBOSE_DECODE) {
               in.printf_bits(25, 4, "M0 C2 C1 C0");
               in.printf_bits(offset - 5, 5, "M2M2 M1M1 M0");
            }

            uint32_t c0 = in.get_bits(25, 1);
            uint32_t c1 = in.get_bits(26, 1);
            uint32_t c2 = in.get_bits(27, 1);

            extra_cem_bits = c0 + c1 + c2;

            num_extra_cem_bits = 5;

            uint32_t m0 = in.get_bits(28, 1) | (in.get_bits(128 - weight_bits - 5, 1) << 1);
            uint32_t m1 = in.get_bits(offset - 4, 2);
            uint32_t m2 = in.get_bits(offset - 2, 2);

            cems[0] = ((cem_base_class + c0) << 2) | m0;
            cems[1] = ((cem_base_class + c1) << 2) | m1;
            cems[2] = ((cem_base_class + c2) << 2) | m2;

         } else if (num_parts == 4) {
            if (VERBOSE_DECODE) {
               in.printf_bits(25, 4, "C3 C2 C1 C0");
               in.printf_bits(offset - 8, 8, "M3M3 M2M2 M1M1 M0M0");
            }

            uint32_t c0 = in.get_bits(25, 1);
            uint32_t c1 = in.get_bits(26, 1);
            uint32_t c2 = in.get_bits(27, 1);
            uint32_t c3 = in.get_bits(28, 1);

            extra_cem_bits = c0 + c1 + c2 + c3;

            num_extra_cem_bits = 8;

            uint32_t m0 = in.get_bits(offset - 8, 2);
            uint32_t m1 = in.get_bits(offset - 6, 2);
            uint32_t m2 = in.get_bits(offset - 4, 2);
            uint32_t m3 = in.get_bits(offset - 2, 2);

            cems[0] = ((cem_base_class + c0) << 2) | m0;
            cems[1] = ((cem_base_class + c1) << 2) | m1;
            cems[2] = ((cem_base_class + c2) << 2) | m2;
            cems[3] = ((cem_base_class + c3) << 2) | m3;
         } else {
            UNREACHABLE("");
         }
      }

      colour_endpoint_data_offset = 29;

   } else {
      uint32_t cem = in.get_bits(13, 4);

      cem_base_class = cem >> 2;
      is_multi_cem = false;

      cems[0] = cem;

      partition_index = -1;

      if (VERBOSE_DECODE)
         in.printf_bits(13, 4, "CEM = %d (class %d)", cem, cem_base_class);

      colour_endpoint_data_offset = 17;
   }
}

void Block::calculate_from_weights()
{
   wt_trits = 0;
   wt_quints = 0;
   wt_bits = 0;
   switch (high_prec) {
   case 0:
      switch (wt_range) {
      case 0x2: wt_max = 1; wt_bits = 1; break;
      case 0x3: wt_max = 2; wt_trits = 1; break;
      case 0x4: wt_max = 3; wt_bits = 2; break;
      case 0x5: wt_max = 4; wt_quints = 1; break;
      case 0x6: wt_max = 5; wt_trits = 1; wt_bits = 1; break;
      case 0x7: wt_max = 7; wt_bits = 3; break;
      default: abort();
      }
      break;
   case 1:
      switch (wt_range) {
      case 0x2: wt_max = 9; wt_quints = 1; wt_bits = 1; break;
      case 0x3: wt_max = 11; wt_trits = 1; wt_bits = 2; break;
      case 0x4: wt_max = 15; wt_bits = 4; break;
      case 0x5: wt_max = 19; wt_quints = 1; wt_bits = 2; break;
      case 0x6: wt_max = 23; wt_trits = 1; wt_bits = 3; break;
      case 0x7: wt_max = 31; wt_bits = 5; break;
      default: abort();
      }
      break;
   }

   assert(wt_trits || wt_quints || wt_bits);

   num_weights = wt_w * wt_h * wt_d;

   if (dual_plane)
      num_weights *= 2;

   weight_bits =
         (num_weights * 8 * wt_trits + 4) / 5
         + (num_weights * 7 * wt_quints + 2) / 3
         +  num_weights * wt_bits;
}

void Block::calculate_remaining_bits()
{
   int config_bits;
   if (num_parts > 1) {
      if (!is_multi_cem)
         config_bits = 29;
      else
         config_bits = 25 + 3 * num_parts;
   } else {
      config_bits = 17;
   }

   if (dual_plane)
      config_bits += 2;

   remaining_bits = 128 - config_bits - weight_bits;
}

decode_error::type Block::calculate_colour_endpoints_size()
{
   /* Specified as illegal */
   if (remaining_bits < (13 * num_cem_values + 4) / 5) {
      colour_endpoint_bits = ce_max = ce_trits = ce_quints = ce_bits = 0;
      return decode_error::invalid_colour_endpoints_size;
   }

   /* Find the largest cem_ranges that fits within remaining_bits */
   for (int i = ARRAY_SIZE(cem_ranges)-1; i >= 0; --i) {
      int cem_bits;
      cem_bits = (num_cem_values * 8 * cem_ranges[i].t + 4) / 5
                 + (num_cem_values * 7 * cem_ranges[i].q + 2) / 3
                 +  num_cem_values * cem_ranges[i].b;

      if (cem_bits <= remaining_bits)
      {
         colour_endpoint_bits = cem_bits;
         ce_max = cem_ranges[i].max;
         ce_trits = cem_ranges[i].t;
         ce_quints = cem_ranges[i].q;
         ce_bits = cem_ranges[i].b;
         return decode_error::ok;
      }
   }

   assert(0);
   return decode_error::invalid_colour_endpoints_size;
}

} // namespace dm_mesa_astc
