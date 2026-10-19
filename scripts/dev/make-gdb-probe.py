#!/usr/bin/env python3
"""Build our own tiny Switch NRO to test the debugger without any game files."""
from pathlib import Path
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.local/build/gdb-probe'
out.mkdir(parents=True, exist_ok=True)
assembly = out / 'probe.S'
assembly.write_text('''
.text
.global _start
_start:
    b entry
.org 0x80
entry:
    adr x19, counter
    mov x20, #0
tick:
    add x20, x20, #1
    str x20, [x19]
    movz x0, #0x4240
    movk x0, #0xf, lsl #16
    svc #0xb
    b tick
.org 0x2000
counter:
    .quad 0
    .quad 0x44325250524f4245
.org 0x3000
''')
subprocess.run(['clang', '--target=aarch64-none-elf', '-c', str(assembly), '-o', str(out / 'probe.o')], check=True)
subprocess.run(['llvm-objcopy', '-O', 'binary', '--only-section=.text', str(out / 'probe.o'), str(out / 'probe.bin')], check=True)
data = bytearray((out / 'probe.bin').read_bytes())
struct.pack_into('<4sIII', data, 0x10, b'NRO0', 0, 0x3000, 0)
struct.pack_into('<7I', data, 0x20, 0, 0x1000, 0x1000, 0x1000, 0x2000, 0x1000, 0x1000)
# Empty homebrew asset sections: no Nintendo firmware, keys or game content.
data += struct.pack('<4sI6Q', b'ASET', 0, 0, 0, 0, 0, 0, 0)
target = out / 'd2r-debug-probe.nro'
target.write_bytes(data)
print(target)
