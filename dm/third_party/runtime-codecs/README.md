# NSO decoding dependencies

Unmodified `lz4.c` / `lz4.h` and the standalone mbed TLS 2.6.1 SHA-256, SHA-1 and MD5
implementation, copied from SciresM/hactool commit
`1d64a83450e025622f3468c28fc4164dad2c5ef6`.
Source: https://github.com/SciresM/hactool/tree/1d64a83450e025622f3468c28fc4164dad2c5ef6

LZ4 retains its BSD 2-Clause license at the top of both source files.
SHA-256 retains its Apache-2.0 notices; full license: LICENSE.mbedtls.
LZ4 is retained from the retired prototype; the native host uses SHA-256 and MD5 for content diagnostics and SHA-1 for the original UI string guards. No key handling, encryption,
console services or game data are included. dm_sha_config.h is our build config.
