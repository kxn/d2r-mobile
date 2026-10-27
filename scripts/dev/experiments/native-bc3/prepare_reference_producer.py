#!/usr/bin/env python3
"""Extend the same owned fixture with NPOT BC3 and an ordinary-image oracle."""
import argparse,difflib,hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();base=a.baseline.resolve();out=a.output.resolve()
if out.exists():raise ValueError('New output required; never mutate the passed v1 probe')
old=json.loads((base/'source-ready.json').read_text());out.mkdir();source=out/'source'
shutil.copytree(base/'source',source,ignore=lambda path,names:['.local'] if '.local' in names else [])
prefix=Path('scripts/dev/native-image-payload')
def change(relative,before,after):
 f=source/relative;s=f.read_text()
 if s.count(before)!=1:raise ValueError('Source anchor not unique: '+before[:60])
 f.write_text(s.replace(before,after))
change(prefix/'CMakeLists.txt','contract-profile:turnip-coherent-host-bc3-probe-v1','contract-profile:turnip-coherent-host-bc3-npot-reference-v2')
change(prefix/'fixture.c','#include <fcntl.h>','#include <fcntl.h>\n#include <math.h>')
change(prefix/'fixture.c','#ifdef DM_NATIVE_TURNIP_HOST_ASTC\nenum { FIXTURE_WIDTH=8, FIXTURE_HEIGHT=8 };',
'''#ifdef DM_NATIVE_TURNIP_HOST_BC3
enum { FIXTURE_WIDTH=13, FIXTURE_HEIGHT=9 };
#elif defined(DM_NATIVE_TURNIP_HOST_ASTC)
enum { FIXTURE_WIDTH=8, FIXTURE_HEIGHT=8 };''')
change(prefix/'fixture.c','  rgba[0] = n == 0 ? 255 : 0;',
'''#ifdef DM_NATIVE_TURNIP_HOST_BC3
  if (format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK) {
    const uint8_t encoded[3] = {n == 0 ? 123 : 0, n == 1 ? 125 : 0, n == 2 ? 123 : 0};
    for (unsigned c = 0; c < 3; c++) {
      float v = encoded[c] / 255.0f;
      if (format == VK_FORMAT_BC3_SRGB_BLOCK)
        v = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
      rgba[c] = (uint8_t)lroundf(v * 255.0f);
    }
    rgba[3] = 255;
    return;
  }
#endif
  rgba[0] = n == 0 ? 255 : 0;''')
change(prefix/'fixture.c','        uint16_t color = rgba[0] ? 0xf800 : rgba[1] ? 0x07e0 : 0x001f;',
'''#ifdef DM_NATIVE_TURNIP_HOST_BC3
        uint16_t color = rgba[0] ? 0x7800 : rgba[1] ? 0x03e0 : 0x000f;
#else
        uint16_t color = rgba[0] ? 0xf800 : rgba[1] ? 0x07e0 : 0x001f;
#endif''')
change(prefix/'fixture.c','static void sample_all(image *i, VkFormat format) {',
'''static uint8_t sample_output_sha256[32];
static void sample_all(image *i, VkFormat format) {
  mbedtls_sha256_context samples;
  mbedtls_sha256_init(&samples);
  mbedtls_sha256_starts(&samples, 0);''')
change(prefix/'fixture.c','          CHECK(!memcmp((uint8_t *)mapped + (y * push[0] + x) * 4, rgba, 4));',
'''#ifdef DM_NATIVE_TURNIP_HOST_BC3
          if (format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK) {
            for (unsigned c = 0; c < 4; c++)
              CHECK(abs((int)((uint8_t *)mapped)[(y * push[0] + x) * 4 + c] - rgba[c]) <= (c == 3 ? 0 : 1));
          } else
#endif
          CHECK(!memcmp((uint8_t *)mapped + (y * push[0] + x) * 4, rgba, 4));''')
change(prefix/'fixture.c','      for (uint32_t n = push[0] * push[1]; n < FIXTURE_WIDTH * FIXTURE_HEIGHT; n++)',
'''      mbedtls_sha256_update(&samples, mapped, (size_t)push[0] * push[1] * 4);
      for (uint32_t n = push[0] * push[1]; n < FIXTURE_WIDTH * FIXTURE_HEIGHT; n++)''')
change(prefix/'fixture.c','  vkDestroyFence(device, fence, NULL);\n  vkFreeCommandBuffers(device, commands, 1, &command);',
'''  mbedtls_sha256_finish(&samples, sample_output_sha256);
  mbedtls_sha256_free(&samples);
  vkDestroyFence(device, fence, NULL);
  vkFreeCommandBuffers(device, commands, 1, &command);''')
anchor='static void restore(const char *directory, VkFormat format) {'
reference='''#ifdef DM_NATIVE_TURNIP_HOST_BC3
static void ordinary_sample_reference(VkFormat format, const uint8_t native_digest[32]) {
  int saved = native_mode;
  native_mode = 0;
  image reference = create_image(format); /* No external handle, normal optimal image. */
  native_mode = saved;
  transition(&reference, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
  for (unsigned level = 0; level < LEVELS; level++) {
    size_t bytes;
    uint8_t *linear = linear_source(format, level, 0, &bytes);
    VkMemoryToImageCopyEXT copy = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
        .pHostPointer = linear,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
        .imageExtent = {mip(FIXTURE_WIDTH, level), mip(FIXTURE_HEIGHT, level), 1}};
    VkCopyMemoryToImageInfoEXT upload = {
        .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
        .dstImage = reference.image, .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1, .pRegions = &copy};
    VK(vkCopyMemoryToImageEXT(device, &upload));
    free(linear);
  }
  transition(&reference, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  sample_all(&reference, format);
  CHECK(!memcmp(native_digest, sample_output_sha256, 32));
  printf("{\\"ordinary_BC3_reference\\":true,\\"format\\":%u,\\"width\\":%u,\\"height\\":%u,\\"mips\\":%u,\\"nonexternal_optimal\\":true,\\"native_sample_bytes_exact\\":true,\\"nonendpoint_RGB565\\":true,\\"NPOT\\":true}\\n", format, FIXTURE_WIDTH, FIXTURE_HEIGHT, LEVELS);
  destroy_image(&reference);
}
#endif
'''
change(prefix/'fixture.c',anchor,reference+anchor)
change(prefix/'native_import.inc','      sample_all(&i, format);',
'''      sample_all(&i, format);
#ifdef DM_NATIVE_TURNIP_HOST_BC3
      uint8_t imported_samples[32];
      memcpy(imported_samples, sample_output_sha256, 32);
      ordinary_sample_reference(format, imported_samples);
#endif''')
change(prefix/'fixture.c','      if (native_mode &&\n#ifdef DM_NATIVE_TURNIP_HOST_BC3',
'''#ifdef DM_NATIVE_TURNIP_HOST_BC3
      if (native_mode && formats[n] != VK_FORMAT_BC3_UNORM_BLOCK && formats[n] != VK_FORMAT_BC3_SRGB_BLOCK)
        continue;
#endif
      if (native_mode &&
#ifdef DM_NATIVE_TURNIP_HOST_BC3''')
files=['CMakeLists.txt','fixture.c','native_import.inc','resource_bake.inc','resource_batch.inc','resource_format.h','sample.comp'];inputs={n:hashlib.sha256((source/prefix/n).read_bytes()).hexdigest() for n in files};profile='turnip-coherent-host-bc3-npot-reference-v2';identity=hashlib.sha256((''.join(k+':'+v+'\n' for k,v in inputs.items())+'contract-profile:'+profile+'\n').encode()).hexdigest()
patch=[]
for name in ['CMakeLists.txt','fixture.c','native_import.inc','resource_bake.inc']:
 relative=str(prefix/name);original=subprocess.check_output(['git','show',old['base_revision']+':'+relative],cwd=ROOT,text=True);current=(source/relative).read_text();patch.extend(difflib.unified_diff(original.splitlines(True),current.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
(out/'producer.patch').write_text(''.join(patch));shutil.copy2(base/'app-entry.patch',out/'app-entry.patch')
local=source/'.local';local.mkdir();(local/'android-sdk').symlink_to(ROOT/'.local/android-sdk',target_is_directory=True);(local/'venv').symlink_to(ROOT/'.local/venv',target_is_directory=True);keys=local/'build/android-apk';keys.mkdir(parents=True);shutil.copy2(base/'source/.local/build/android-apk/debug.keystore',keys/'debug.keystore')
ledger={str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(source.rglob('*')) if f.is_file() and '.local' not in f.relative_to(source).parts}
ready={'schema':1,'complete':True,'base_revision':old['base_revision'],'profile':profile,'source':str(source),'baseline_probe_ready_sha256':hashlib.sha256((base/'source-ready.json').read_bytes()).hexdigest(),'producer_source_sha256':identity,'CMake_inputs':inputs,'source_ledger':ledger,'patch_sha256':hashlib.sha256((out/'producer.patch').read_bytes()).hexdigest(),'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'device_executed':False,'coherent_qualification':'NOT_GRANTED'}
(out/'source-ready.json').write_text(json.dumps(ready,indent=2)+'\n');(out/'app-source-ready.json').write_text(json.dumps({'base_revision':old['base_revision'],'producer_source_sha256':identity,'app_entry_patch_sha256':hashlib.sha256((out/'app-entry.patch').read_bytes()).hexdigest(),'source_ledger':ledger},indent=2)+'\n');print(identity)
