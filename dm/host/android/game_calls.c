// 原改写器的调用点剪口仍须执行原游戏函数；不能把未接前端的诊断门当成功返回。
// Call-site cuts from the original rewriter must still execute original game
// functions, rather than return success when frontend diagnostics are absent.
#include "hle.h"

static uint8_t *main_base(void) {
  for (int i = 0; i < g_nmod; i++)
    if (!strcmp(g_mod[i].name, "main"))
      return g_mod[i].base;
  die("Original game call requires main");
}
int dm_native_game_call(int number, uint64_t *regs) {
  switch (number) {
  case SVC_HOST_PRIVATE_SKILL_AIM: {
    void svc_private_skill_aim(uint64_t *);
    svc_private_skill_aim(regs);
    return 1;
  }
  case SVC_HOST_C2S_DELIVER:
    ((void (*)(uint64_t, uint64_t, uint64_t, uint64_t))(
        main_base() + 0x632510))(regs[0], regs[1], regs[2], regs[3]);
    return 1;
  case SVC_HOST_C2S_SYS:
    ((void (*)(uint64_t, uint64_t))(main_base() + 0x6310b0))(regs[0], regs[1]);
    return 1;
  case SVC_HOST_JOIN_SPAWN:
  case SVC_HOST_JOIN_CORPSE: {
    void svc_join_spawn(uint64_t *, int);
    svc_join_spawn(regs, number == SVC_HOST_JOIN_CORPSE);
    return 1;
  }
  case SVC_HOST_GROUND_ASSET: {
    // 与原 dm_dropfilter.c 相同的 x8
    // 字符串返回约定。当前不装掉落光覆盖层，路径保持原版。 Same x8
    // string-result convention as original dm_dropfilter.c. Keep the original
    // asset path until the drop-light overlay is installed.
    uint8_t *base = main_base();
    register uint64_t x0 __asm__("x0") = regs[0], x1 __asm__("x1") = regs[1];
    register uint64_t x8 __asm__("x8") = regs[8];
    register uint64_t x16 __asm__("x16") = (uint64_t)(base + 0xa14dc4);
    __asm__ volatile("blr x16"
                     : "+r"(x0), "+r"(x1), "+r"(x8), "+r"(x16)
                     :
                     : "x2", "x3", "x4", "x5", "x6", "x7", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x17", "x30", "v0", "v1",
                       "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18",
                       "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26",
                       "v27", "v28", "v29", "v30", "v31", "cc", "memory");
    return 1;
  }
  case SVC_HOST_DROP_SOUND:
    regs[0] = ((uint32_t (*)(uint64_t, uint64_t))(main_base() + 0x470c20))(
        regs[0], regs[1]);
    return 1;
  default:
    return 0;
  }
}
