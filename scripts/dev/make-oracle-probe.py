#!/usr/bin/env python3
"""Original homebrew fixture: five unit classes in the published D2R state layout."""
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def build():
    out = ROOT / '.local/build/oracle-probe'
    out.mkdir(parents=True, exist_ok=True)
    setup = []
    for kind, table in enumerate((0x2230, 0x2630, 0x2a30, 0x3230, 0x2e30)):
        setup.append(f'adr x2, unit{kind}\nmov x3, #{table}\nstr x2, [x21, x3]')
        if kind in (0, 1, 3):
            setup.append('adr x3, path\nstr x3, [x2, #0x38]')
        if kind in (0, 1, 4):
            setup.append('adr x3, stats\nstr x3, [x2, #0x88]')
        if kind == 4:
            setup.append('adr x3, item\nstr x3, [x2, #0x10]')
    assembly = '''.text
.global _start
_start: b entry
.org 0x80
entry:
    adr x21, game
    mov x20, #0
''' + '\n'.join(setup) + '''
    adr x2, stats
    adr x3, stats_array
    str x3, [x2, #0x30]
    mov w3, #2
    str w3, [x2, #0x38]
    mov x0, x21
    b tick
.org 0x300
tick:
    add x20, x20, #1
    str w20, [x21, #0x170]
    str w20, [x21, #0x1a8]
    adr x2, path
    strh w20, [x2, #2]
''' + '\n'.join(f'adr x2, unit{i}\nstr w20, [x2, #0x28]' for i in range(5)) + '''
    movz x0, #0x4240
    movk x0, #0xf, lsl #16
    svc #0xb
    mov x0, x21
    b tick
.org 0x3000
game:
.org 0x31ac
    .word 666
'''
    for kind in range(5):
        offset = 0x7000 + kind * 0x200
        assembly += f'''.org {offset}
unit{kind}:
    .word {kind}, {100 + kind}, {kind + 1}, 1
.org {offset + 0x18}
    .byte 2
.org {offset + 0x28}
    .word 0, 666
'''
    assembly += '''.org 0x8000
path: .hword 16, 0, 32, 80
.org 0x8100
stats:
.org 0x8200
stats_array: .word 6, 25600, 7, 51200
.org 0x8300
item: .word 4, 123, 666, 0, 456, 0, 17
.org 0x9000
'''
    (out / 'probe.S').write_text(assembly)
    subprocess.run(['clang', '--target=aarch64-none-elf', '-c', str(out / 'probe.S'), '-o', str(out / 'probe.o')], check=True)
    subprocess.run(['llvm-objcopy', '-O', 'binary', '--only-section=.text', str(out / 'probe.o'), str(out / 'probe.bin')], check=True)
    data = bytearray((out / 'probe.bin').read_bytes())
    struct.pack_into('<4sIII', data, 0x10, b'NRO0', 0, 0x9000, 0)
    struct.pack_into('<7I', data, 0x20, 0, 0x1000, 0x1000, 0x1000, 0x2000, 0x7000, 0x1000)
    data += struct.pack('<4sI6Q', b'ASET', 0, 0, 0, 0, 0, 0, 0)
    target = out / 'oracle-probe.nro'
    target.write_bytes(data)
    return target


if __name__ == '__main__':
    print(build())
