from pathlib import Path
import subprocess,hashlib,json,argparse
parser=argparse.ArgumentParser();parser.add_argument('--work-dir',type=Path,default=Path('/home/dl/d2r-mobile/.local/build/turnip-native-astc-whole-linear-v1'));args=parser.parse_args();B=args.work_dir.resolve();S=B/'mesa-26.2.5';src=S/'src/freedreno/vulkan/tu_image.cc';s=src.read_text();a=s.index('\nbool\ntu_host_image_format_supported(');z=s.index('\nstatic VkResult\ntu_image_bind',a);profile=s[a:z];a=s.index('   const bool native_astc_linear =');z=s.index('   if (native_astc_linear)',a);selection=s[a:z]
prefix=r'''#include <vulkan/vulkan_core.h>
#include <libdrm/drm_fourcc.h>
#include <cstdio>
#include <cstdlib>
struct tu_physical_device {bool has_host_ptr_import;unsigned mask;};
static unsigned tu_host_ptr_memory_types(const tu_physical_device*p){return p->mask;}
struct tu_device {tu_physical_device*physical_device;};
struct image_info {VkExternalMemoryHandleTypeFlags external_handle_types;VkFormat format;VkImageType image_type;VkImageTiling tiling;VkImageUsageFlags2KHR usage;VkImageCreateFlags2KHR create_flags;unsigned array_layers;VkExtent3D extent;unsigned mip_levels;VkSampleCountFlagBits samples;};
struct tu_image {image_info vk;};
'''
func='\nstatic bool actual_selection(tu_device*device,tu_image*image,uint64_t modifier,const void*plane_layouts) {\n'+selection+'return native_astc_linear;\n}\n'
checks=r'''
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);abort();}}while(0)
int main(){tu_physical_device physical={true,1};tu_device device={&physical};tu_image original={};auto&x=original.vk;x.external_handle_types=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;x.format=VK_FORMAT_ASTC_4x4_UNORM_BLOCK;x.image_type=VK_IMAGE_TYPE_2D;x.tiling=VK_IMAGE_TILING_OPTIMAL;x.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;x.array_layers=1;x.extent={256,256,1};x.mip_levels=9;x.samples=VK_SAMPLE_COUNT_1_BIT;
#define TRY(field,value) {tu_image v=original;v.vk.field=value;CHECK(!actual_selection(&device,&v,DRM_FORMAT_MOD_INVALID,nullptr));}
CHECK(actual_selection(&device,&original,DRM_FORMAT_MOD_INVALID,nullptr));
for(auto f:{VK_FORMAT_ASTC_4x4_UNORM_BLOCK,VK_FORMAT_ASTC_4x4_SRGB_BLOCK,VK_FORMAT_ASTC_6x6_UNORM_BLOCK,VK_FORMAT_ASTC_6x6_SRGB_BLOCK}){tu_image v=original;v.vk.format=f;CHECK(actual_selection(&device,&v,DRM_FORMAT_MOD_INVALID,nullptr));}
TRY(external_handle_types,0);TRY(external_handle_types,VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);TRY(external_handle_types,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT|VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
for(auto f:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_BC3_UNORM_BLOCK,VK_FORMAT_ASTC_8x8_UNORM_BLOCK})TRY(format,f);
TRY(image_type,VK_IMAGE_TYPE_1D);TRY(image_type,VK_IMAGE_TYPE_3D);TRY(tiling,VK_IMAGE_TILING_LINEAR);TRY(array_layers,2);TRY(extent.depth,2);TRY(mip_levels,0);TRY(mip_levels,17);TRY(samples,VK_SAMPLE_COUNT_2_BIT);
for(auto flag:{VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,VK_IMAGE_CREATE_SPARSE_BINDING_BIT,VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT})TRY(create_flags,flag);
TRY(usage,VK_IMAGE_USAGE_SAMPLED_BIT);TRY(usage,VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT);TRY(usage,x.usage|VK_IMAGE_USAGE_STORAGE_BIT);TRY(usage,x.usage|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);TRY(usage,x.usage|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);TRY(usage,x.usage|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
CHECK(!actual_selection(&device,&original,DRM_FORMAT_MOD_LINEAR,nullptr));int external_layout;CHECK(!actual_selection(&device,&original,DRM_FORMAT_MOD_INVALID,&external_layout));
physical.has_host_ptr_import=false;CHECK(!actual_selection(&device,&original,DRM_FORMAT_MOD_INVALID,nullptr));physical.has_host_ptr_import=true;physical.mask=0;CHECK(!actual_selection(&device,&original,DRM_FORMAT_MOD_INVALID,nullptr));
printf("{\"passed\":true,\"actual_source_selection_checks\":%u,\"private_RT_nonASTC_sparse_array_MSAA_explicit_old_paths_excluded\":true,\"GPU_used\":false}\n",checks);}
'''
# initializer_list only supplies finite test vectors, not runtime functionality.
code=prefix+'#include <initializer_list>\n'+profile+func+checks;target=B/'policy-source-test.cc';target.write_text(code);cmd=['c++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-I'+str(S/'include'),str(target),'-o',str(B/'policy-source-test')]
with(B/'policy-source-build.txt').open('w')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
with(B/'policy-source-result.json').open('w')as f,(B/'policy-source-stderr.txt').open('w')as err:p=subprocess.run([str(B/'policy-source-test')],stdout=f,stderr=err)
assert p.returncode==0,p.returncode
(B/'policy-source-provenance.json').write_text(json.dumps({'source':str(src),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'actual_profile_function_sha256':hashlib.sha256(profile.encode()).hexdigest(),'actual_selection_expression_sha256':hashlib.sha256(selection.encode()).hexdigest(),'actual_source_extracted_not_rewritten':True,'host_stubs_scope':'physical host capability/memory-type mask and image fields only, no Vulkan calls or device','compile_argv':cmd},indent=2)+'\n');print((B/'policy-source-result.json').read_text())
