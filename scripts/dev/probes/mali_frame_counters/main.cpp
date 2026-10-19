// Independent profiling helper. Arm SDK libraries/names remain unmodified.
// No game writes, GPU layers, synthetic GPU timing or clock requests.
#include <device/handle.hpp>
#include <device/instance.hpp>
#include <device/product_id.hpp>
#include <device/hwcnt/sampler/manual.hpp>
#include <device/hwcnt/sampler/configuration.hpp>
#include <device/hwcnt/reader.hpp>
#include <device/hwcnt/sample.hpp>
#include <mali_userspace/MaliHwCntrNamesGenerated.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cinttypes>
#include <ctime>
namespace dev=hwcpipe::device;
static uint64_t now(clockid_t clock) { timespec t;clock_gettime(clock,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec; }
int main() {
  setvbuf(stdout,nullptr,_IOLBF,0);
  auto h=dev::handle::create();if(!h) { perror("open Mali");return 1; }
  std::error_code ec;auto inst=dev::instance::create(*h,ec);
  if(!inst) { fprintf(stderr,"instance: %s\n",ec.message().c_str());return 2; }
  auto c=inst->get_constants();auto product=dev::product_id_from_raw_gpu_id(c.gpu_id);
  auto ext=inst->get_hwcnt_block_extents();
  if(product.first||product.second!=dev::product_id::g710||ext.counters_per_block()!=64) {
    fprintf(stderr,"unsupported product/counter layout\n");return 3;
  }
  printf("{\"metadata\":true,\"gpu_id\":%" PRIu64 ",\"shader_cores\":%" PRIu64 ",\"core_mask\":%" PRIu64 ",\"l2_slices\":%" PRIu64 ",\"axi_bus_width_bits\":%" PRIu64 ",\"warp_width\":%" PRIu64 ",\"counter_words\":%u,\"word_bits\":%u}\n",c.gpu_id,c.num_shader_cores,c.shader_core_mask,c.num_l2_slices,c.axi_bus_width,c.warp_width,ext.counters_per_block(),ext.values_type()==dev::hwcnt::sample_values_type::uint32?32:64);
  dev::hwcnt::sampler::configuration::enable_map_type mask;mask.set();
  dev::hwcnt::sampler::configuration cfg[]={
    {dev::hwcnt::block_type::fe,dev::hwcnt::prfcnt_set::primary,mask},
    {dev::hwcnt::block_type::geometry,dev::hwcnt::prfcnt_set::primary,mask},
    {dev::hwcnt::block_type::memory,dev::hwcnt::prfcnt_set::primary,mask},
    {dev::hwcnt::block_type::core,dev::hwcnt::prfcnt_set::primary,mask}};
  dev::hwcnt::sampler::manual sampler(*inst,cfg,4);
  if(!sampler) { fprintf(stderr,"sampler unavailable\n");return 4; }
  auto features=sampler.get_reader().get_features();
  ec=sampler.accumulation_start();if(ec) { fprintf(stderr,"start: %s\n",ec.message().c_str());return 5; }
  for(unsigned i=0;i<40;i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto mono_before=now(CLOCK_MONOTONIC),raw_before=now(CLOCK_MONOTONIC_RAW);
    ec=sampler.request_sample(i);if(ec) { fprintf(stderr,"request: %s\n",ec.message().c_str());return 6; }
    {
      dev::hwcnt::sample sample(sampler.get_reader(),ec);
      if(!sample) { fprintf(stderr,"read: %s\n",ec.message().c_str());return 7; }
      auto raw_after=now(CLOCK_MONOTONIC_RAW),mono_after=now(CLOCK_MONOTONIC);
      const auto &m=sample.get_metadata();
      printf("{\"sample\":%u,\"user_data\":%" PRIu64 ",\"begin_ns\":%" PRIu64 ",\"end_ns\":%" PRIu64 ",\"mono_before_ns\":%" PRIu64 ",\"mono_after_ns\":%" PRIu64 ",\"raw_before_ns\":%" PRIu64 ",\"raw_after_ns\":%" PRIu64 ",\"error\":%u,\"stretched\":%s,\"blocks\":[",i,m.user_data,m.timestamp_ns_begin,m.timestamp_ns_end,mono_before,mono_after,raw_before,raw_after,m.flags.error,features.has_stretched_flag?(m.flags.stretched?"true":"false"):"null");
      bool first=true;
      for(const auto &block:sample.blocks()) {
        int group=-1;
        switch(block.type) {
          case dev::hwcnt::block_type::fe:group=0;break;
          case dev::hwcnt::block_type::geometry:group=1;break;
          case dev::hwcnt::block_type::core:group=2;break;
          case dev::hwcnt::block_type::memory:group=3;break;
          default:continue;
        }
        printf("%s{\"group\":%d,\"index\":%u,\"counters\":{",first?"":",",group,block.index);first=false;
        bool first_counter=true;
        for(unsigned n=0;n<64;n++) {
          const char *name=mali_userspace::hardware_counters_mali_g710[group*64+n];if(!*name)continue;
          uint64_t value=ext.values_type()==dev::hwcnt::sample_values_type::uint32?static_cast<const uint32_t *>(block.values)[n]:static_cast<const uint64_t *>(block.values)[n];
          printf("%s\"%s\":%" PRIu64,first_counter?"":",",name,value);first_counter=false;
        }
        printf("}}");
      }
      puts("]}");
    }
    if(ec) { fprintf(stderr,"release: %s\n",ec.message().c_str());return 8; }
  }
  ec=sampler.accumulation_stop(40);if(ec) { fprintf(stderr,"stop: %s\n",ec.message().c_str());return 9; }
}
