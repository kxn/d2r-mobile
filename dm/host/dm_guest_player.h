// 从原 diag.c 抽出受控玩家与位置读取，Apple/Android 使用同一口径。
// Controlled-player and location reads extracted from original diag.c, shared
// by Apple and Android. The caller supplies its platform's safe reader.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int (*dm_guest_read_fn)(uint64_t, void *, size_t);
static inline uint32_t dm_guest_local_player_id(uint64_t main,
                                                dm_guest_read_fn read) {
  uint32_t slot = 8, id = UINT32_MAX;
  if (main && read(main + 0x2d20e4c, &slot, 4) && slot < 8)
    read(main + 0x2d60380 + 4 * slot, &id, 4);
  return id;
}
static inline uint64_t dm_guest_player_unit(uint64_t main, uint64_t game,
                                            dm_guest_read_fn read) {
  if (!game)
    return 0;
  uint32_t local = dm_guest_local_player_id(main, read);
  for (int bucket = 0; bucket < 128; bucket++) {
    uint64_t unit = 0;
    if (!read(game + 0x2230 + 8 * bucket, &unit, 8))
      continue;
    for (int n = 0; unit && n < 4096; n++) {
      uint32_t mode = 0, id = 0;
      uint64_t path = 0;
      uint16_t position[4] = {0};
      if (read(unit + 0xc, &mode, 4) && mode && read(unit + 0x38, &path, 8) &&
          path && read(path, position, 8) && (position[1] || position[3]) &&
          (local == UINT32_MAX || (read(unit + 8, &id, 4) && id == local)))
        return unit;
      if (!read(unit + 0x158, &unit, 8))
        break;
    }
  }
  return 0;
}
static inline int dm_guest_player_location(uint64_t player,
                                           dm_guest_read_fn read, uint32_t *x,
                                           uint32_t *y, uint32_t *area,
                                           uint64_t *path) {
  uint64_t room = 0, area_room = 0, level = 0;
  uint16_t position[4] = {0};
  *x = *y = *area = 0;
  *path = 0;
  if (!player || !read(player + 0x38, path, 8) || !*path ||
      !read(*path, position, 8))
    return 0;
  *x = position[1];
  *y = position[3];
  uint32_t number = 0;
  if (read(*path + 0x20, &room, 8) && room &&
      read(room + 0x18, &area_room, 8) && area_room &&
      read(area_room + 0x90, &level, 8) && level &&
      read(level + 0x1f8, &number, 4) && number < 200)
    *area = number;
  return 1;
}
