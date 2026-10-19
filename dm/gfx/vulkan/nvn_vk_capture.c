// 原截图请求接口：出图线程读回已完成的 Vulkan 纹理，再写 PNG。
// Original screenshot API: read the completed Vulkan texture on the present
// thread, then write PNG. Requests never read GPU memory on the caller thread.
#include "nvn_vk_internal.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#include "../../third_party/flip/tool/stb_image_write.h"

enum { CAPTURE_MAX = 16 };
static pthread_mutex_t capture_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { uint64_t frame; char path[1024]; } captures[CAPTURE_MAX];
static unsigned capture_count;
static uint64_t capture_frame;
extern void dm_shot_written(const char *, int) __attribute__((weak));

void dm_gfx_shot_at(long delay, const char *path) {
  pthread_mutex_lock(&capture_mu);
  if (capture_count == CAPTURE_MAX || !path ||
      strlen(path) >= sizeof captures[0].path) {
    pthread_mutex_unlock(&capture_mu);
    LOG("Vulkan screenshot request rejected: queue full or invalid path");
    if (path && dm_shot_written) dm_shot_written(path, 0);
    return;
  }
  captures[capture_count].frame = capture_frame + (delay > 0 ? delay : 0);
  strlcpy(captures[capture_count++].path, path, sizeof captures[0].path);
  pthread_mutex_unlock(&capture_mu);
}
typedef struct { FILE *file; int failed; } capture_output;
static void capture_write(void *context, void *data, int size) {
  capture_output *out = context;
  if (fwrite(data, 1, size, out->file) != (size_t)size) out->failed = 1;
}
void vk_capture_present(const tex_t *texture, const int crop[4]) {
  char paths[CAPTURE_MAX][1024];
  unsigned count = 0;
  pthread_mutex_lock(&capture_mu);
  ++capture_frame;
  for (unsigned i = 0; i < capture_count;) {
    if (captures[i].frame > capture_frame) { ++i; continue; }
    strlcpy(paths[count++], captures[i].path, sizeof paths[0]);
    memmove(captures + i, captures + i + 1,
            (--capture_count - i) * sizeof captures[0]);
  }
  pthread_mutex_unlock(&capture_mu);
  if (!count) return;
  int x = crop ? crop[0] : 0, y = crop ? crop[1] : 0;
  int w = crop && crop[2] > 0 ? crop[2] : texture->d.width;
  int h = crop && crop[3] > 0 ? crop[3] : texture->d.height;
  int valid = (texture->d.format == NVN_FORMAT_RGBA8 ||
               texture->d.format == NVN_FORMAT_RGBA8_SRGB) &&
              x >= 0 && y >= 0 && w > 0 && h > 0 &&
              (uint64_t)x + w <= texture->d.width &&
              (uint64_t)y + h <= texture->d.height;
  uint8_t *pixels = valid ? malloc((size_t)w * h * 4) : NULL;
  if (pixels) {
    mtl_finish();
    NVNcopyRegion region = {x, y, 0, w, h, 1};
    mtl_read_texels(texture, 0, 0, &region, pixels, (size_t)w * 4,
                    (size_t)w * h * 4);
  }
  for (unsigned i = 0; i < count; ++i) {
    capture_output out = {0};
    int ok = 0;
    if (pixels && (out.file = fopen(paths[i], "wb"))) {
      ok = stbi_write_png_to_func(capture_write, &out, w, h, 4, pixels, w * 4);
      if (fclose(out.file) || out.failed) ok = 0;
    }
    LOG("Vulkan screenshot frame=%llu %s: %s", (unsigned long long)capture_frame,
        ok ? "written" : "failed", paths[i]);
    if (dm_shot_written) dm_shot_written(paths[i], ok);
  }
  free(pixels);
}
