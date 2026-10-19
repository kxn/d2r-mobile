// 同一套 NVN 跳板使用 Mach-O 或 ELF 符号与重定位。
// The same NVN thunks use Mach-O or ELF symbols and relocations.
#ifdef __APPLE__
#define NVN_C(x) _##x
#define NVN_ADDR(reg, sym) adrp reg, NVN_C(sym)@PAGE; add reg, reg, NVN_C(sym)@PAGEOFF
#else
#define NVN_C(x) x
#define NVN_ADDR(reg, sym) adrp reg, sym; add reg, reg, :lo12:sym
#endif
