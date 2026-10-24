// Diagnostic-only private control-flow observations. Single recording owner,
// no timing/locks/atomics and no additional resource/key traversal. Cumulative
// counts describe first failed decisions, not CPU cost or executed GPU draws.
#ifdef DM_VULKAN_RETAINED_REASONS
#define RETAINED_REASON_FIELDS(X) \
  X(attempts) X(equal) X(owner_unavailable) X(count) X(pass) X(framebuffer) X(area) \
  X(pipeline) X(layout) X(vertex_mask) X(index_bound) X(stage_mask) X(descriptor_mode) \
  X(bias_enabled) X(drawargs) X(state_reference) X(viewport) X(scissor) X(depth_bias) \
  X(vertex) X(index) X(stage_set) X(db_base) X(db_offset) X(descriptor_set) X(dynamic_offsets) X(bias) \
  X(private_captures) X(published_captures) X(published_replays) \
  X(forget_epoch) X(forget_parameter) X(forget_both) X(forget_trim) \
  X(cleared_epoch) X(cleared_parameter) X(cleared_both) X(cleared_trim) \
  X(discard_epoch) X(discard_db_product) X(discard_uab_product) X(discard_test)
#define RR_DECLARE(n) uint64_t n;
static struct { RETAINED_REASON_FIELDS(RR_DECLARE) } retained_reasons;
#undef RR_DECLARE
#define RR_INC(n) (retained_reasons.n++)
#define RR_FAIL(n) do { RR_INC(n); return 0; } while(0)
void vk_retained_diagnostic_publish(uint64_t guest_serial) {
  if(guest_serial%60)return;
  char text[2048];size_t used=0;
#define RR_PRINT(n) do { int count=snprintf(text+used,sizeof text-used," " #n "=%llu",(unsigned long long)retained_reasons.n); if(count<0 || (size_t)count>=sizeof text-used)die("Retained diagnostic log bound");used+=(size_t)count; }while(0);
  RETAINED_REASON_FIELDS(RR_PRINT)
#undef RR_PRINT
  LOG("[retained private reasons] guest=%llu cumulative=1%s",(unsigned long long)guest_serial,text);
}
#else
#define RR_INC(n) ((void)0)
#define RR_FAIL(n) return 0
#endif
