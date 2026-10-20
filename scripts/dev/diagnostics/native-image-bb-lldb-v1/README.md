# bb4 native-image official LLDB hook

This hook owns only one debugger server/client/forward. The calling game
transaction owns its first PID, install/resource rollback, original save2 and
stop. It must deploy the exact NDK28.2 ARM64 `lldb-server` to its owned
`files/runtime/temp/d2r-lldb-server` before launch. No device action is performed
by importing the scripts or by the offline tests.

Call immediately after the transaction claims its first ordinary-UID PID:

```sh
python3 run-debugger.py FIRST_PID EXISTING_UNIQUE_OUTPUT_DIRECTORY
```

The hook requires exact bb4 APK/ELF hashes, the frozen Q735 serial, and a free
owned port/socket. It reuses official LLDB19 and the original bounded tool timeout.
It installs two one-shot breakpoints, derived from the exact bb4 ELF:

- `vk_pool_native_image_bind` at `0x32a954`: untouched entry ABI X0 address,
  X1 bytes, X2 VkImage, X3 requirements pointer, X4 output-memory pointer.
- `vk_pool_native_image_use` at `0x32afac`: immediately after `record_pool_use`
  returned and every lifetime/identity/span guard passed, before unlocking and
  callee-saved register restoration. X23 token, X22 owner, X21 address, X20
  bytes, W19 stages. Its immutable memory and typed caller `image` serial/native
  entry are read without executing target functions or expressions.

`native-binding-stops.json` preserves raw registers, typed fields and bounded
backtraces. `debugger-result.json` provides `native_binding_accepted` only for a
matched full135168-byte bind/use and caller serial/entry with matching memory,
identity, alignment, import type and complete allocation envelope. The separate
`owned_debugger_cleanup_complete` must also be true. Missing stops or optimized
caller metadata are inconclusive, never proof of runtime failure. The caller
must always collect logs and save2/stop/restore even when the hook exits nonzero.

This proves one actual game native-image binding/use, not all texture adoption,
frame cost, memory savings, timing or shader correctness. Attaching after launch
can miss the import. It is not a reason to add counters or rerun automatically.

Offline tests: `python3 test_guards.py`. Official LLDB target/type/disassembly
verification is recorded separately in the source-ready report; synthetic test
metadata does not represent an actual device result.
