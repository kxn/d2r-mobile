#!/usr/bin/env python3
"""Host contract checks of Android loader selection, using inert fixture DSOs.

No Vulkan instance, GPU workload, device, or game is created by these tests.
"""
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'dm/gfx/vulkan/nvn_vk_loader.h'
DRIVER = r'''
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>
__attribute__((constructor)) static void check_cache_at_load(void) {
  const char *expected=getenv("FIXTURE_EXPECT_CACHE_DIR");
  const char *actual=getenv("MESA_SHADER_CACHE_DIR");
  if(expected && (!actual || strcmp(expected,actual))) _Exit(82);
}
static void inert(void) {}
static void properties(VkPhysicalDevice physical,VkPhysicalDeviceProperties2 *out) {
  (void)physical;
  VkPhysicalDeviceDriverProperties *driver=out->pNext;
  driver->driverID=getenv("FIXTURE_WRONG_DRIVER")?VK_DRIVER_ID_ARM_PROPRIETARY:VK_DRIVER_ID_MESA_PANVK;
  strcpy(driver->driverName,"inert loader fixture");
  strcpy(driver->driverInfo,"no GPU execution");
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {
  (void)instance;
  if(getenv("FIXTURE_MISSING_GLOBAL") && !strcmp(name,"vkCreateInstance")) return NULL;
  if(getenv("FIXTURE_MISSING_SURFACE") && !strcmp(name,"vkCreateAndroidSurfaceKHR")) return NULL;
  if(!strcmp(name,"vkGetPhysicalDeviceProperties2")) return (PFN_vkVoidFunction)properties;
  return inert;
}
#ifndef FIXTURE_NO_ICD_ALIAS
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance,const char *name) {
  return vkGetInstanceProcAddr(instance,name);
}
#endif
'''
TOOLS = r'''
#include <dlfcn.h>
#include <stdio.h>
void *adrenotools_open_libvulkan(int mode,int flags,const char *tmp,const char *hooks,
    const char *directory,const char *name,const char *redirect,void **mapping) {
  (void)tmp;(void)hooks;(void)redirect;(void)mapping;
  if(flags!=1) return NULL;
  char path[4096];snprintf(path,sizeof path,"%s%s",directory,name);
  puts("ADRENO_HOOK");fflush(stdout);
  return dlopen(path,mode);
}
'''
RUNNER = r'''
#include "nvn_vk_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
VkResult volkInitialize(void) { puts("SYSTEM");fflush(stdout);return VK_SUCCESS; }
void volkInitializeCustom(PFN_vkGetInstanceProcAddr proc) {
  puts("CUSTOM");fflush(stdout);vkGetInstanceProcAddr=proc;
  vkGetPhysicalDeviceProperties2=(PFN_vkGetPhysicalDeviceProperties2)proc((VkInstance)1,"vkGetPhysicalDeviceProperties2");
}
int main(int argc,char **argv) {
  if(argc!=5) return 2;
  if(vk_loader_is_initialized())return 85;
  vk_loader_set_driver(strcmp(argv[1],"legacy")?argv[1]:NULL,
      strcmp(argv[2],"-")?argv[2]:NULL,strcmp(argv[3],"-")?argv[3]:NULL);
  vk_loader_initialize();
  if(!vk_loader_is_initialized())return 86;
  if(getenv("FIXTURE_ASSERT_NO_CACHE") && getenv("MESA_SHADER_CACHE_DIR")) return 83;
  if(!strcmp(argv[4],"surface")) vk_loader_check_instance((VkInstance)1,1);
  if(!strcmp(argv[4],"identity")) vk_loader_check_device((VkPhysicalDevice)1,1);
  setenv("DM_VULKAN_DRIVER_KIND","invalid-after-initialize",1);
  if(getenv("FIXTURE_CHANGE_CACHE")) setenv("MESA_SHADER_CACHE_DIR","after-initialize",1);
  vk_loader_set_driver("invalid-after-initialize",NULL,NULL);
  vk_loader_initialize();
  if(getenv("FIXTURE_CHANGE_CACHE") && strcmp(getenv("MESA_SHADER_CACHE_DIR"),"after-initialize")) return 84;
  puts("STARTUP_HELD");
  return 0;
}
'''


class VulkanLoaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='d2r-loader-contract-')
        cls.base = Path(cls.temp.name)
        cls.driver = cls.base / 'driver'; cls.driver.mkdir()
        cls.hooks = cls.base / 'hooks'; cls.hooks.mkdir()
        cls.runner = cls.base / 'loader-contract'
        cls.cc = shutil.which('cc')
        if not cls.cc:
            raise unittest.SkipTest('native host C compiler required')
        for name, source in [('driver', DRIVER), ('tools', TOOLS), ('runner', RUNNER)]:
            (cls.base / (name + '.c')).write_text(source)
        common = [cls.cc, '-D_GNU_SOURCE', '-D__ANDROID__', '-I', str(HEADER.parent)]
        subprocess.run([*common, '-shared', '-fPIC', cls.base / 'driver.c', '-o',
                        cls.driver / 'libvulkan_panfrost.so'], check=True)
        shutil.copy2(cls.driver / 'libvulkan_panfrost.so', cls.driver / 'libvulkan_freedreno.so')
        subprocess.run([*common, '-DFIXTURE_NO_ICD_ALIAS', '-shared', '-fPIC', cls.base / 'driver.c',
                        '-o', cls.base / 'alias.so'], check=True)
        subprocess.run([cls.cc, '-shared', '-fPIC', cls.base / 'tools.c', '-ldl', '-o',
                        cls.hooks / 'libadrenotools.so'], check=True)
        subprocess.run([*common, ROOT / 'dm/gfx/vulkan/nvn_vk_loader.c', cls.base / 'runner.c',
                        '-pthread', '-ldl', '-o', cls.runner], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_loader(self, provider='panvk', directory=None, hooks='-', stage='-', env=None):
        values = {k: v for k, v in os.environ.items()
                  if not k.startswith('DM_VULKAN_') and k != 'MESA_SHADER_CACHE_DIR'}
        values.update(env or {})
        result = subprocess.run([str(self.runner), provider, str(directory or self.driver),
                                 str(hooks), stage], env=values, text=True, capture_output=True,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        return result

    def test_system_is_explicit_and_startup_selection_is_immutable(self):
        result = self.run_loader('system', env={'DM_VULKAN_DRIVER_ROOT': '/missing'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ['SYSTEM', 'STARTUP_HELD'])
        environment = self.run_loader('legacy', directory='-', env={
            'DM_VULKAN_DRIVER_KIND': 'system', 'DM_VULKAN_DRIVER_ROOT': 'ignored-relative'})
        self.assertEqual(environment.returncode, 0, environment.stderr)
        self.assertEqual(environment.stdout.splitlines(), ['SYSTEM', 'STARTUP_HELD'])

    def test_default_and_legacy_adreno_keep_their_dispatch(self):
        default = self.run_loader('legacy', directory='-')
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertIn('SYSTEM', default.stdout)
        for provider in ('adreno', 'legacy'):
            result = self.run_loader(provider, hooks=self.hooks)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('ADRENO_HOOK', result.stdout)
            self.assertNotIn('SYSTEM', result.stdout)

    def test_panvk_has_no_hook_dependency_and_checks_surface_and_identity(self):
        for stage in ('surface', 'identity'):
            result = self.run_loader(stage=stage)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('SYSTEM', result.stdout)
            self.assertNotIn('ADRENO_HOOK', result.stdout)
            self.assertIn('STARTUP_HELD', result.stdout)
            self.assertIn('provider=panvk', result.stderr)

    def test_unknown_missing_wrong_dispatch_and_layer_requests_fail_closed(self):
        cases = [
            ({'provider': 'unknown'}, 'unknown driver provider'),
            ({'directory': 'relative'}, 'absolute directory'),
            ({'directory': '/missing'}, 'directory unavailable'),
            ({'env': {'FIXTURE_MISSING_GLOBAL': '1'}}, 'global dispatch incomplete'),
            ({'stage': 'surface', 'env': {'FIXTURE_MISSING_SURFACE': '1'}}, 'surface dispatch incomplete'),
            ({'stage': 'identity', 'env': {'FIXTURE_WRONG_DRIVER': '1'}}, 'not Mesa PanVK'),
            ({'env': {'DM_VULKAN_VALIDATE': '1'}}, 'does not compose loader validation layers'),
            ({'provider': 'adreno'}, 'hook directory missing'),
        ]
        for options, diagnostic in cases:
            with self.subTest(options=options):
                result = self.run_loader(**options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn('SYSTEM', result.stdout)

    def test_fixed_filename_and_no_symlink_escape(self):
        directory = self.base / 'bad'; directory.mkdir()
        target = directory / 'libvulkan_panfrost.so'
        try:
            shutil.copy2(self.driver / target.name, directory / 'different-name.so')
            absent = self.run_loader(directory=directory)
            self.assertNotEqual(absent.returncode, 0)
            self.assertNotIn('SYSTEM', absent.stdout)
            target.symlink_to(self.driver / target.name)
            symlink = self.run_loader(directory=directory)
            self.assertNotEqual(symlink.returncode, 0)
            self.assertIn('regular library', symlink.stderr)
        finally:
            shutil.rmtree(directory)

    def test_documented_gipa_alias_is_supported(self):
        directory = self.base / 'alias'; directory.mkdir()
        shutil.copy2(self.base / 'alias.so', directory / 'libvulkan_panfrost.so')
        result = self.run_loader(directory=directory, stage='surface')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('SYSTEM', result.stdout)

    def test_panvk_cache_is_private_before_dlopen_and_not_reconfigured(self):
        directory = self.base / 'cache-default'; directory.mkdir()
        shutil.copy2(self.driver / 'libvulkan_panfrost.so', directory / 'libvulkan_panfrost.so')
        cache = directory / 'shader-cache'
        result = self.run_loader(directory=directory, env={
            'FIXTURE_EXPECT_CACHE_DIR': str(cache), 'FIXTURE_CHANGE_CACHE': '1'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(cache.is_dir())
        self.assertEqual(cache.stat().st_mode & 0o777, 0o700)
        self.assertEqual(result.stderr.count('PanVK shader cache'), 1)

    def test_panvk_cache_respects_explicit_value_including_empty(self):
        for index, value in enumerate((str(self.base / 'explicit-cache'), '')):
            directory = self.base / ('cache-explicit-' + str(index)); directory.mkdir()
            shutil.copy2(self.driver / 'libvulkan_panfrost.so', directory / 'libvulkan_panfrost.so')
            result = self.run_loader(directory=directory, env={
                'MESA_SHADER_CACHE_DIR': value, 'FIXTURE_EXPECT_CACHE_DIR': value})
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((directory / 'shader-cache').exists())
            self.assertIn('PanVK shader cache explicit=' + value, result.stderr)

    def test_other_providers_do_not_set_mesa_cache_directory(self):
        for provider, directory, hooks in (('system', '-', '-'),
                                            ('adreno', self.driver, self.hooks)):
            result = self.run_loader(provider, directory=directory, hooks=hooks,
                                     env={'FIXTURE_ASSERT_NO_CACHE': '1'})
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('PanVK shader cache', result.stderr)


if __name__ == '__main__':
    unittest.main()
