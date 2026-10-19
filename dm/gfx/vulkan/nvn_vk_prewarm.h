#pragma once
#include <stddef.h>
#include <stdint.h>

// Versioned, pointer-free wire data. Only successful, exactly identified
// pipelines are recorded; an unknown module always uses ordinary creation.
enum { VK_PREWARM_WORDS=512, VK_PREWARM_LIMIT=512 };
typedef struct {
  char key[20];
  uint8_t fingerprint[32];
  uint32_t module; // 0 absent, 1 base, 2 exact-word, 3 predicate
} vk_prewarm_stage;
typedef struct {
  uint32_t kind,used;
  vk_prewarm_stage stages[6];
  uint32_t words[VK_PREWARM_WORDS];
} vk_prewarm_recipe;
void vk_prewarm_record(const vk_prewarm_recipe *recipe);
void vk_prewarm_forget(const vk_prewarm_recipe *recipe);
size_t vk_prewarm_read(vk_prewarm_recipe *recipes,size_t capacity);
void vk_prewarm_store(void); // existing PSO persistence worker only
void vk_graphics_prewarm(void); // once, sole recording owner, before commands
uint64_t vk_prewarm_record_count(void);
uint64_t vk_prewarm_store_rejected(void);
void vk_prewarm_stats(uint64_t *recorded,uint64_t *warmed,uint64_t *rejected,uint64_t *live_hits);
void vk_prewarm_note_hit(void);
