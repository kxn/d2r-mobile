// 手写会话/尸体/断链样本，验证原玩家识别规则的跨平台提取。
// Handwritten session/corpse/broken-chain fixtures for shared original reads.
#include "dm_guest_player.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t slot, ids[8];
static uint8_t game[0x2630], corpse[0x160], player[0x160], path[0x28],
    room[0x20], area_room[0x98], level[0x200];
static const uint64_t main_address = 0x10000000, game_address = 0x20000000,
                      corpse_address = 0x30000000, player_address = 0x40000000,
                      path_address = 0x50000000;
static unsigned reads;
static int read_guest(uint64_t address, void *out, size_t size) {
  const struct {
    uint64_t address;
    const void *data;
    size_t size;
  } regions[] = {{0x12d20e4c, &slot, sizeof slot},
                 {0x12d60380, ids, sizeof ids},
                 {0x20000000, game, sizeof game},
                 {0x30000000, corpse, sizeof corpse},
                 {0x40000000, player, sizeof player},
                 {0x50000000, path, sizeof path},
                 {0x60000000, room, sizeof room},
                 {0x70000000, area_room, sizeof area_room},
                 {0x80000000, level, sizeof level}};
  reads++;
  for (unsigned i = 0; i < sizeof regions / sizeof *regions; i++)
    if (address >= regions[i].address &&
        address - regions[i].address <= regions[i].size &&
        size <= regions[i].size - (address - regions[i].address)) {
      memcpy(out,
             (const uint8_t *)regions[i].data + address - regions[i].address,
             size);
      return 1;
    }
  return 0;
}
static void u32(void *base, size_t offset, uint32_t value) {
  memcpy((uint8_t *)base + offset, &value, 4);
}
static void u64(void *base, size_t offset, uint64_t value) {
  memcpy((uint8_t *)base + offset, &value, 8);
}
int main(void) {
  slot = 1;
  ids[1] = 42;
  u64(game, 0x2230, corpse_address);
  u32(corpse, 8, 41);
  u32(corpse, 0xc, 17);
  u64(corpse, 0x38, path_address);
  u64(corpse, 0x158, player_address);
  u32(player, 8, 42);
  u32(player, 0xc, 5);
  u64(player, 0x38, path_address);
  u32(path, 0, 123u << 16);
  u32(path, 4, 456u << 16);
  assert(dm_guest_local_player_id(main_address, read_guest) == 42);
  assert(dm_guest_player_unit(main_address, game_address, read_guest) ==
         player_address);
  // 正在死亡不能错误选中自己的旧尸体。 | Dying must not select the old corpse.
  u32(player, 0xc, 0);
  assert(!dm_guest_player_unit(main_address, game_address, read_guest));
  u32(player, 0xc, 17);
  assert(dm_guest_player_unit(main_address, game_address, read_guest) ==
         player_address);
  u64(player, 0x38, 0xdeadbeef);
  assert(!dm_guest_player_unit(main_address, game_address, read_guest));
  u64(player, 0x38, path_address);
  assert(!dm_guest_player_unit(main_address, 0, read_guest));
  uint32_t x, y, area;
  uint64_t found_path;
  u64(path, 0x20, 0x60000000);
  u64(room, 0x18, 0x70000000);
  u64(area_room, 0x90, 0x80000000);
  u32(level, 0x1f8, 2);
  assert(dm_guest_player_location(player_address, read_guest, &x, &y, &area,
                                  &found_path));
  assert(x == 123 && y == 456 && area == 2 && found_path == path_address);
  u32(level, 0x1f8, 200);
  assert(dm_guest_player_location(player_address, read_guest, &x, &y, &area,
                                  &found_path) &&
         !area);
  slot = 8;
  assert(dm_guest_local_player_id(main_address, read_guest) == UINT32_MAX);
  assert(dm_guest_player_unit(main_address, game_address, read_guest) ==
         corpse_address);
  slot = 1;
  u64(corpse, 0x158, corpse_address);
  reads = 0;
  assert(!dm_guest_player_unit(main_address, game_address, read_guest));
  assert(reads < 40000);
  puts("t_guest_player: passed (identity, corpse, death, broken paths, area, "
       "bounded cycle)");
}
