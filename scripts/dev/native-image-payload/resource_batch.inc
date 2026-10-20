// Bounded cold orchestration only. Original DMRSRC1/DMNRES1 and driver bake
// remain authoritative; no texture layout or runtime protocol is introduced.
enum { RESOURCE_BATCH_LIMIT = 128, RESOURCE_BATCH_ROW = 195 };
typedef struct {
  char id[65];
  uint8_t input[32], declaration[32];
} resource_batch_entry;
typedef struct {
  unsigned count;
  uint8_t digest[32];
  resource_batch_entry entries[RESOURCE_BATCH_LIMIT];
} resource_batch_list;
static unsigned resource_hex_digit(char c) {
  CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10);
}
static void resource_unhex(const uint8_t *text, uint8_t out[32]) {
  for (unsigned n = 0; n < 32; n++)
    out[n] = resource_hex_digit(text[n*2])*16 + resource_hex_digit(text[n*2+1]);
}
static resource_batch_list *resource_batch_open(const char *directory) {
  const char magic[] = "DM_RESOURCE_BATCH_V1\n";
  size_t bytes;
  uint8_t *data = resource_read(directory, "resource-batch.tsv", &bytes);
  CHECK(bytes > sizeof magic-1 && !memcmp(data, magic, sizeof magic-1));
  size_t body = bytes-(sizeof magic-1);
  CHECK(body % RESOURCE_BATCH_ROW == 0 &&
        body / RESOURCE_BATCH_ROW <= RESOURCE_BATCH_LIMIT);
  resource_batch_list *list = calloc(1, sizeof *list);
  CHECK(list);
  list->count = body/RESOURCE_BATCH_ROW;
  native_sha(data, bytes, list->digest);
  for (unsigned n = 0; n < list->count; n++) {
    const uint8_t *row = data+sizeof magic-1+n*RESOURCE_BATCH_ROW;
    CHECK(row[64]=='\t' && row[129]=='\t' && row[194]=='\n');
    for (unsigned b = 0; b < 64; b++) (void)resource_hex_digit(row[b]);
    resource_batch_entry *e = &list->entries[n];
    memcpy(e->id, row, 64);
    resource_unhex(row+65, e->input);
    resource_unhex(row+130, e->declaration);
    for (unsigned previous=0; previous<n; previous++)
      CHECK(strcmp(e->id, list->entries[previous].id));
  }
  free(data);
  return list;
}
static unsigned resource_batch(const char *directory, int bake) {
  resource_batch_list *list = resource_batch_open(directory);
  FILE *receipt = NULL;
  int receipt_fd = -1;
  if (bake) {
    char path[4096];
    resource_path(path, directory, "resource-batch-receipts.jsonl");
    receipt_fd = open(path, O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC, 0600);
    CHECK(receipt_fd >= 0);
    receipt = fdopen(receipt_fd, "w");
    CHECK(receipt);
  }
  uint64_t total = 0;
  for (unsigned n=0; n<list->count; n++) {
    const resource_batch_entry *e = &list->entries[n];
    char child[4096];
    CHECK(snprintf(child, sizeof child, "%s/items/%s", directory, e->id) < (int)sizeof child);
    if (!bake) {
      resource_input r = resource_open(child);
      uint8_t actual[32];
      native_sha(r.data, r.bytes, actual);
      CHECK(!memcmp(actual,e->input,32) && !memcmp(r.header->declaration_sha256,e->declaration,32));
      resource_close(&r);
      continue;
    }
    // At most ONE original per-resource input, image and opaque output live.
    // resource_bake frees all three before returning this small value record.
    dm_resource_native wire = resource_bake(child,e->input,e->declaration);
    uint8_t layout_sha[32];
    native_sha(&wire,sizeof wire,layout_sha);
    CHECK(UINT64_MAX-total >= wire.image_size);
    total += wire.image_size;
    fprintf(receipt,"{\"ordinal\":%u,\"entry_id\":\"%s\",\"complete\":true,\"input_sha256\":",n,e->id);
    resource_hex(receipt,wire.input_sha256,32);
    fprintf(receipt,",\"declaration_sha256\":"); resource_hex(receipt,wire.declaration_sha256,32);
    fprintf(receipt,",\"native_payload_sha256\":"); resource_hex(receipt,wire.payload_sha256,32);
    fprintf(receipt,",\"native_metadata_sha256\":"); resource_hex(receipt,wire.metadata_sha256,32);
    fprintf(receipt,",\"layout_record_sha256\":"); resource_hex(receipt,layout_sha,32);
    fprintf(receipt,",\"layout_UUID\":"); resource_hex(receipt,wire.layout_uuid,16);
    fprintf(receipt,",\"device_UUID\":"); resource_hex(receipt,wire.device_uuid,16);
    fprintf(receipt,",\"driver_UUID\":"); resource_hex(receipt,wire.driver_uuid,16);
    fprintf(receipt,",\"producer_source_sha256\":\"%s\",\"physical_image_bytes\":%llu,\"alignment\":%llu,\"format\":%u,\"width\":%u,\"height\":%u,\"levels\":%u,\"original_driver_roundtrip_compressed_blocks_exact\":true}\n",
            DM_NATIVE_RESOURCE_PRODUCER_SHA,(unsigned long long)wire.image_size,(unsigned long long)wire.image_alignment,
            wire.format,wire.width,wire.height,wire.levels);
    CHECK(!fflush(receipt) && !fsync(receipt_fd));
  }
  if (bake) {
    CHECK(!fclose(receipt));
    // Published only after every original per-entry bake and receipt succeeded.
    char completed[2048];
    int len = snprintf(completed,sizeof completed,
      "{\"schema\":1,\"complete\":true,\"entries\":%u,\"physical_image_bytes\":%llu,\"producer_source_sha256\":\"%s\",\"manifest_sha256\":\"",
      list->count,(unsigned long long)total,DM_NATIVE_RESOURCE_PRODUCER_SHA);
    CHECK(len>0 && len<(int)sizeof completed-68);
    for (unsigned n=0;n<32;n++) len += snprintf(completed+len,sizeof completed-len,"%02x",list->digest[n]);
    len += snprintf(completed+len,sizeof completed-len,"\",\"GPU_sampling_executed\":false,\"game_modules_executed\":false}\n");
    CHECK(len>0 && len<(int)sizeof completed);
    resource_write(directory,"resource-batch-complete.json",completed,len);
  }
  unsigned count = list->count;
  free(list);
  return count;
}
