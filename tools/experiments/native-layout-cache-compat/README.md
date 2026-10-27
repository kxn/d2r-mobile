# Explicit host-layout cache compatibility receipt

This tool preserves the original0e53 BC3-linear bake and payload provenance. It does not call a producer, encode/retile textures, modify game modules, weaken the runtime parser, or overwrite the old pack.

The applicable standard contract is [`optimalTilingLayoutUUID`](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceHostImageCopyProperties.html): it identifies compatible opaque layouts for `VK_HOST_IMAGE_COPY_MEMCPY_BIT` caches. This experiment applies stricter conditions: actual layout/device/driver UUIDs must all match, the producer fingerprint must match, and a complete frozen source comparison must prove that12b8 differs from0e53 only in the single Android extension whitelist gate. Layout UUID equality is never inferred from an ELF BuildID or from an intended source change.

```sh
python3 tools/experiments/native-layout-cache-compat/rebind.py \
  --consumer-query .local/logs/native-bc3-three-arm-game-v1/internal-queue-producer-probe/bc3-unorm/native-resource.json \
  --consumer-query-receipt .local/logs/native-bc3-three-arm-game-v1/internal-queue-producer-probe/result.json \
  --output .local/build/native-bc3-linear-consumer-12b8-v1
```

Without a real query/authenticated receipt, accepted output is refused. `--pending` with no query can create an explicit `accepted:false` record; it cannot create a consumer table. The accepted path authenticates the actual producer controller, its bound12b8 driver/build, ordinary app UID, queried properties, actual mapped driver and hashed pulled query artifact. The controller's existing driver-SHA assertion occurs immediately after reading maps; the tool retains that controller fingerprint and map evidence rather than fabricating an inode receipt absent from the original result.

It verifies all6217 original source declarations, inputs, source textures, baked wire recipes, metadata and payload hashes against their49 original batch receipts. It reuses `pack-native-texture.py::load_entry` for recipe/source/container checks and compares the exact original table entries/families, packaged textures, descriptor files, full descriptor cache and pack-content identity. Only one entry's working bytes are rebuilt in memory at a time; no new texture file or bake record is published.

The new `consumer-native-layouts.bin` changes only the consumer driver SHA field and table digest. Its recipe/family body is byte-for-byte identical. The original pack-content SHA remains a reference to the original pack and original0e bake provenance; it is not relabeled as a freshly12b8-generated pack. `compatibility.json` explicitly records the distinct producer and consumer roles, all source fingerprints, full entry provenance and remaining runtime gates.

The unchanged production `nvn_native_layout.c` is compiled with ASan/UBSan and exercised on the full table. The new consumer identity must accept it; the old driver identity must reject it; the new identity must reject the old table, corrupted table digest and mismatched UUID. No parser override is used. Process-lifetime table allocation is intentional, so leak reporting is disabled for this short-lived parser fixture; address and undefined-behavior checks remain active.

```sh
python3 tools/experiments/native-layout-cache-compat/test_contract.py
```

These tests include missing-query refusal, explicit pending behavior, each UUID mismatch, producer mismatch, altered source/build fingerprint and corrupt file/table digests. A compatibility receipt and a host parser pass do not prove the game or GPU has used the reused payload. Actual new-driver NVN functional checks and game qualification remain separate.
