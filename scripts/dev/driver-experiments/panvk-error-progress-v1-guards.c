#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>
#define KBASE_RINGBUF_SIZE 65536u
static int dump_enabled, calls, skips;
static uint64_t actual_addr, actual_size;
#define PANVK_DEBUG(x) dump_enabled
#define mesa_loge(...) do { skips++; } while (0)
struct panvk_device { struct { void *decode_ctx; } debug; };
struct panvk_subqueue { struct { uint64_t ringbuf_dev; } kbase; };
static void pandecode_cs_binary(void *ctx,uint64_t addr,uint64_t size,uint32_t gpu) { assert(ctx);calls++;actual_addr=addr;actual_size=size;(void)gpu; }
static void
kbase_dump_ring_diagnostic(struct panvk_device *dev,
                           const struct panvk_subqueue *subq,
                           uint64_t offset, uint64_t bytes,
                           uint32_t gpu_id, const char *reason)
{
   if (!PANVK_DEBUG(DUMP) || !dev->debug.decode_ctx) {
      mesa_loge("kbase: %s ring decode skipped: DUMP disabled or no decoder",
                reason);
      return;
   }

   if (offset >= KBASE_RINGBUF_SIZE || !bytes ||
       bytes > KBASE_RINGBUF_SIZE - offset) {
      mesa_loge("kbase: %s ring decode skipped: offset %" PRIu64
                ", size %" PRIu64 ", ring %u (outside contiguous ring range)",
                reason, offset, bytes, KBASE_RINGBUF_SIZE);
      return;
   }

   pandecode_cs_binary(dev->debug.decode_ctx, subq->kbase.ringbuf_dev + offset,
                        bytes, gpu_id);
}


int main(void) {
 struct panvk_device d = {.debug={.decode_ctx=(void*)1}};
 struct panvk_subqueue q = {.kbase={.ringbuf_dev=0x5ffffdf000ull}};
 dump_enabled=1;
 kbase_dump_ring_diagnostic(&d,&q,226184,376,1,"actual event");assert(calls==0 && skips==1);
 kbase_dump_ring_diagnostic(&d,&q,65520,32,1,"cross boundary");assert(calls==0 && skips==2);
 kbase_dump_ring_diagnostic(&d,&q,0,0,1,"zero");assert(calls==0 && skips==3);
 kbase_dump_ring_diagnostic(&d,&q,65536,8,1,"end");assert(calls==0 && skips==4);
 kbase_dump_ring_diagnostic(&d,&q,29576,376,1,"valid");assert(calls==1 && actual_addr==0x5ffffe6388ull && actual_size==376);
 kbase_dump_ring_diagnostic(&d,&q,65528,8,1,"last qword");assert(calls==2);
 dump_enabled=0;kbase_dump_ring_diagnostic(&d,&q,0,8,1,"off");assert(calls==2 && skips==5);
 dump_enabled=1;d.debug.decode_ctx=0;kbase_dump_ring_diagnostic(&d,&q,0,8,1,"missing ctx");assert(calls==2 && skips==6);
 puts("8 extracted-helper guards passed; actual monotonic 226184 rejected before decoder");
}
