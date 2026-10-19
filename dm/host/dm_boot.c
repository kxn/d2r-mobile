// 与原 main.c 共用的游戏数据初始化；Android 不另抄一份启动逻辑。
// Shared original game-data startup, used by the Apple and Android hosts.
#include "dm.h"
#include "dm_boot.h"
// 存档落盘节流：游戏把脏了的存档容器写盘并提交的「非强制刷新」每拍调一次（main+0x2497ac → main+0xdff8a4，强制 = 0），
// 但写档入口 main+0xdfdf80 对同一容器 30 秒只放行一次（常数在 main+0x20cefa8；新容器的时间戳从建档那一刻起算）——
// 新建角色因此要约 30 秒后才第一次提交，这之前进程被结束（手机上切出去被系统回收是常态）角色就没了。改成 3 秒。
// 受影响的只有非强制写（新角色、键位、改名）；局内角色存盘、设置等本来就是强制写，不走节流。DM_SAVE_THROTTLE=秒 可改（30 = 原版）。
// Save flush throttling: the "non-forced flush" in which the game writes a dirty save container to disk and commits it is called once per tick (main+0x2497ac → main+0xdff8a4, forced = 0),
// but the save-write entry main+0xdfdf80 lets the same container through only once per 30 seconds (the constant is at main+0x20cefa8; a new container's timestamp counts from the moment the save is created) --
// so a newly created character is first committed only after about 30 seconds, and if the process is ended before that (being reclaimed by the system after switching away is normal on a phone) the character is gone. Changed to 3 seconds.
// Only non-forced writes are affected (new character, key bindings, rename); in-run character saves, settings etc. are forced writes anyway and do not go through the throttle. DM_SAVE_THROTTLE=seconds can change it (30 = the original game).
static void save_throttle_patch(void) {
    const char *v = getenv("DM_SAVE_THROTTLE"); int sec = v ? atoi(v) : 3;
    module_t *m = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) m = &g_mod[i];
    if (!m) return;
    int32_t *p = (int32_t *)(m->base + 0x20cefa8);
    if (*p != 30) { LOG("【红】存档落盘节流：main+0x20cefa8 原值 %d ≠ 30（构建号不符？），不改", *p); return; }
    if (sec == 30) return;
    *p = sec < 1 ? 1 : sec;
    LOG("存档落盘节流：main+0x20cefa8 30 → %d 秒", *p);
}
// DM_GAME_SEED=N：游戏自带的固定种子开关（main 数据段 0x20a0074，出厂 0xffffffff = 关）。
// 建局 0x5d0bd0 在 0x5d297c 处判它 ≠ -1 则以 {N, 666} 作种子（存 Game+0x1b8），否则取系统时钟。数据段直接写、不剪口；
// 放在各模块 init_array 之后、nnMain 之前，免得全局构造覆盖。写前断言原值，构建号不符就报红中止。
// DM_GAME_SEED=N: the game's own fixed-seed switch (main data segment 0x20a0074, factory value 0xffffffff = off).
// Game creation 0x5d0bd0 checks at 0x5d297c whether it ≠ -1 and then uses {N, 666} as the seed (stored at Game+0x1b8), otherwise takes the system clock. Written directly into the data segment, no cut;
// placed after each module's init_array and before nnMain, so that global constructors do not overwrite it. The original value is asserted before writing, and a build ID mismatch reports a failure and aborts.
static void game_seed_maybe(void) {
    const char *v = getenv("DM_GAME_SEED");
    if (!v) return;
    module_t *m = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) m = &g_mod[i];
    if (!m) die("DM_GAME_SEED：没有 main 模块");
    uint32_t *p = (uint32_t *)(m->base + 0x20a0074);
    if (*p != 0xffffffffu) die("DM_GAME_SEED：main+0x20a0074 原值 %#x ≠ 0xffffffff（构建号不符？）", *p);
    *p = (uint32_t)strtoull(v, NULL, 0);
    LOG("DM_GAME_SEED：main+0x20a0074 ← %#x", *p);
}

void dm_guest_boot_configure(void) { game_seed_maybe(); save_throttle_patch(); }
