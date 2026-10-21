// Keep complete allocation / construction phases outside the consuming loop.
// Reference builds inline the SAME function bodies; no second runtime cache,
// skipped dependency or production measurement is introduced.
#pragma once
#ifndef DM_VULKAN_COLD_PHASE_REFERENCE
#define DM_VULKAN_COLD_PHASE_REFERENCE 0
#endif
#if DM_VULKAN_COLD_PHASE_REFERENCE
#define VK_COLD_PHASE __attribute__((always_inline)) inline
#else
#define VK_COLD_PHASE __attribute__((noinline))
#endif
