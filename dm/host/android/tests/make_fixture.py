#!/usr/bin/env python3
"""原创 NSO：真机验证原改写器；无 CPU 模拟。 | Original NSO for hardware rewriter tests; no CPU emulation."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
assembly = '''.text
.global nnMain,native_increment,fixture_override,native_count
.org 0x100
nnMain:
stp x29,x30,[sp,#-64]!
stp x19,x20,[sp,#16]
stp x21,x22,[sp,#32]
mov x18,#0x2345
add x18,x18,#1
mov x9,#0x2346
cmp x18,x9
b.ne failure
mov x18,sp
mov x9,sp
cmp x18,x9
b.ne failure
stp x18,x19,[sp,#-16]!
mov x18,#0
ldp x18,x19,[sp],#16
cmp x18,sp
b.ne failure
mov x18,#0
cbz x18,1f
b failure
1:
adr x18,native_count
adr x9,native_count
cmp x18,x9
b.ne failure
mrs x20,tpidr_el0
mov x0,#0x123
mov x1,#0x456
movi v31.16b,#0xa5
cmp xzr,xzr
mrs x18,tpidr_el0
b.ne failure
cmp x0,#0x123
b.ne failure
cmp x1,#0x456
b.ne failure
umov w9,v31.b[15]
cmp w9,#0xa5
b.ne failure
mrs x0,tpidr_el0
cmp x0,x20
b.ne failure
mrs x1,tpidr_el0
cmp x1,x20
b.ne failure
mrs x30,tpidr_el0
cmp x30,x20
b.ne failure
mov x9,#0xcafe
msr tpidr_el0,x9
mrs x21,tpidr_el0
cmp x9,x21
b.ne failure
mrs x18,tpidr_el0
cmp x18,x9
b.ne failure
msr tpidr_el0,x20
mrs x18,tpidrro_el0
ldr x9,[x18,#0x1f8]
cbz x9,failure
bl native_gate_checks
cbnz x0,failure
bl native_group_checks
cbnz x0,failure
svc #0x1e
cbz x0,failure
mov x19,x0
mrs x9,cntpct_el0
cmp x9,x19
b.lo failure
adr x16,override_slot
ldr x16,[x16]
blr x16
cmp x0,#42
b.ne failure
b finish
failure:
mov x0,#0xdead
finish:
ldp x21,x22,[sp,#32]
ldp x19,x20,[sp,#16]
ldp x29,x30,[sp],#64
ret
.org 0x600
native_increment:
adr x9,native_count
mov x18,#0
add x18,x18,#1
add x18,x18,#1
cmp x18,#2
b.ne increment_bad
2:
ldaxr x18,[x9]
add x18,x18,#1
stlxr w10,x18,[x9]
cbnz w10,2b
increment_bad:
ret
.org 0x700
fixture_override:
mov x0,#0xdead
ret
.org 0x1000
native_gate_checks:
stp x29,x30,[sp,#-96]!
mov x29,sp
stp x19,x20,[sp,#16]
stp x21,x22,[sp,#32]
mrs x9,fpcr
mrs x10,fpsr
stp x9,x10,[sp,#48]
mov x9,#0x1000000
msr fpcr,x9
mov x9,#0x10
msr fpsr,x9
movi v0.16b,#0x3c
movi v31.16b,#0xa5
mov x1,#0x456
cmp xzr,xzr
svc #0x1e
b.ne gate_failure
cbz x0,gate_failure
cmp x1,#0x456
b.ne gate_failure
mov x20,x0
mov x0,#0x123
cmp xzr,xzr
mrs x21,cntpct_el0
b.ne gate_failure
mrs x9,nzcv
mov x10,#0x60000000
cmp x9,x10
b.ne gate_failure
cmp x21,x20
b.lo gate_failure
cmp x0,#0x123
b.ne gate_failure
cmp x1,#0x456
b.ne gate_failure
cmp xzr,xzr
mrs x9,cntpct_el0
b.ne gate_failure
cmp x9,x21
b.lo gate_failure
mrs x9,fpcr
mov x10,#0x1000000
cmp x9,x10
b.ne gate_failure
mrs x9,fpsr
cmp x9,#0x10
b.ne gate_failure
umov w9,v0.b[0]
cmp w9,#0x3c
b.ne gate_failure
umov w9,v31.b[15]
cmp w9,#0xa5
b.ne gate_failure
mov x9,sp
cmp x29,x9
b.ne gate_failure
mov x19,#0
gate_jump_loop:
mov x0,x19
mov x16,#0x1616
mov x17,#0x1717
bl native_jump_probe
cmp x0,x19
b.ne gate_failure
mov x9,#0x1616
cmp x16,x9
b.ne gate_failure
mov x9,#0x1717
cmp x17,x9
b.ne gate_failure
add x19,x19,#1
cmp x19,#256
b.ne gate_jump_loop
mov x0,#0
b gate_restore
gate_failure:
mov x0,#0xdead
gate_restore:
ldp x9,x10,[sp,#48]
msr fpcr,x9
msr fpsr,x10
ldp x21,x22,[sp,#32]
ldp x19,x20,[sp,#16]
ldp x29,x30,[sp],#96
ret
.org 0x1800
native_jump_probe:
sub sp,sp,#16
strb w0,[sp]
ldrb w10,[sp]
add sp,sp,#16
adr x18,native_jump_table
add x18,x18,x10,lsl #2
br x18
native_jump_table:
DIRECT_GATE_JUMP_TABLE
DIRECT_GATE_JUMP_CASES
.org 0x2600
native_group_checks:
stp x29,x30,[sp,#-32]!
mov x0,#0x123
mov x1,#0x456
mov x16,#0x1616
mov x17,#0x1717
bl group_first
mov x9,#0x40000
cmp x18,x9
b.ne group_failure
// Unknown computed entry directly into the second original instruction.
mov x18,#2
adr x12,group_second
blr x12
cmp x18,#12
b.ne group_failure
// And into the third, with its own independently resolved thread state.
mov x18,#3
adr x12,group_third
blr x12
cmp x18,#12
b.ne group_failure
mov x18,#0xffffffff
bl group_flags
mrs x9,nzcv
mov x10,#0x60000000
cmp x9,x10
b.ne group_failure
cmp x18,#1
b.ne group_failure
cmp x0,#0x123
b.ne group_failure
cmp x1,#0x456
b.ne group_failure
mov x9,#0x1616
cmp x16,x9
b.ne group_failure
mov x9,#0x1717
cmp x17,x9
b.ne group_failure
mov x2,#0x222
mov x3,#0x333
bl group_inputs
mov x9,#0xaf2
cmp x18,x9
b.ne group_failure
cmp x2,#0x222
b.ne group_failure
cmp x3,#0x333
b.ne group_failure
mov x9,sp
bl group_sp
sub x9,x9,#16
cmp x18,x9
b.ne group_failure
mov x0,#0
ldp x29,x30,[sp],#32
ret
group_failure:
mov x0,#0xdead
ldp x29,x30,[sp],#32
ret
group_first:
mov x18,#0xffff
group_second:
add w18,w18,#1
group_third:
lsl x18,x18,#2
ret
group_flags:
adds w18,w18,#1
adc x18,x18,xzr
csel x18,x18,xzr,eq
ret
group_sp:
add x18,sp,#0
sub x18,x18,#16
ret
group_inputs:
add x18,x0,x1
add x18,x18,x0
add x18,x18,x1
ret
.org 0x8000
native_count: .quad 0
override_slot: .quad 0
.org 0xc000
'''
# CMP 不接受第二操作数 sp；以原 sp 副本核对写回。 | CMP cannot take sp as its second operand; compare against the saved original sp.
assembly=assembly.replace('cmp x18,sp','cmp x18,x9')
assembly=assembly.replace('DIRECT_GATE_JUMP_TABLE', '\n'.join(f'b native_jump_case_{i}' for i in range(256)))
assembly=assembly.replace('DIRECT_GATE_JUMP_CASES', '\n'.join(f'native_jump_case_{i}:\nmov x0,#{i}\nret' for i in range(256)))
(out/'fixture.S').write_text(assembly)
subprocess.run(['clang','--target=aarch64-none-elf','-c',str(out/'fixture.S'),'-o',str(out/'fixture.o')],check=True)
subprocess.run(['ld.lld','-Ttext=0','--entry=nnMain','--build-id=none',str(out/'fixture.o'),'-o',str(out/'fixture.elf')],check=True)
subprocess.run(['llvm-objcopy','-O','binary','--only-section=.text',str(out/'fixture.elf'),str(out/'fixture.bin')],check=True)
raw=bytearray((out/'fixture.bin').read_bytes())
exports={'nnMain':0x100,'native_increment':0x600,'fixture_override':0x700,'native_count':0x8000}
forces=['nndiagStartup','_ZN2nn2oe10InitializeEv','_ZN2nn3aoc10InitializeEv',
'_ZN2nn3err26InitializeContextWriterApiEv','_ZN2nn2fs24InitializeForApplicationEv',
'_ZN2nn4pctl10InitializeEv','_ZN2nn4diag34InitializeApplicationAbortObserverEv',
'_ZN2nn3mem17StandardAllocator8AllocateEmm']
names=list(exports)+forces
strings=b'\0';syms=[bytes(24)]
for name in names:
    syms.append(struct.pack('<IBBHQQ',len(strings),0x12,0,1 if name in exports else 0,exports.get(name,0),0))
    strings+=name.encode()+b'\0'
symtab=b''.join(syms);strtab=0x4200+len(symtab)
raw[0x4200:strtab]=symtab;raw[strtab:strtab+len(strings)]=strings
rels=[struct.pack('<QQq',0x8008,((names.index('fixture_override')+1)<<32)|1026,0)]
for i,name in enumerate(forces):
    rels.append(struct.pack('<QQq',0x8020+i*8,((names.index(name)+1)<<32)|1026,0))
rel=b''.join(rels);raw[0x5d00:0x5d00+len(rel)]=rel
for i,(tag,value) in enumerate([(6,0x4200),(5,strtab),(7,0x5d00),(8,len(rel)),(0,0)]):
    struct.pack_into('<qQ',raw,0x4000+16*i,tag,value)
struct.pack_into('<I',raw,4,0x80)
struct.pack_into('<4si',raw,0x80,b'MOD0',0x4000-0x80)
struct.pack_into('<i',raw,0x90,0x5c00-0x80)
struct.pack_into('<BBBBiI',raw,0x5c00,1,0x1b,3,0x3b,0,3)
starts=[0x100,0x600,0x700,0x1000,0x1800,0x2600]
struct.pack_into('<I',raw,0x5c08,len(starts))
for i,start in enumerate(starts):struct.pack_into('<ii',raw,0x5c0c+i*8,start-0x5c00,0)
h=bytearray(256);h[:4]=b'NSO0';struct.pack_into('<I',h,12,0x38)
h[0x40:0x60]=hashlib.sha256(b'original native-only rewrite fixture').digest()
for i in range(3):
    struct.pack_into('<III',h,0x10+16*i,256+16384*i,16384*i,16384)
    struct.pack_into('<I',h,0x60+4*i,16384)
    h[0xa0+32*i:0xc0+32*i]=hashlib.sha256(raw[16384*i:16384*(i+1)]).digest()
struct.pack_into('<I',h,0x3c,16384)
(out/'fixture.nso').write_bytes(h+raw)
print(out/'fixture.nso')
