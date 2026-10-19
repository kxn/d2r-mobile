.text
.hidden dm_opus_tail_expected_sp
.hidden dm_opus_tail_expected_x18
.globl dm_opus_tail_register_test
.type dm_opus_tail_register_test,%function
.p2align 2
dm_opus_tail_register_test:
    .cfi_startproc
    hint #34
    sub sp,sp,#64
    .cfi_def_cfa_offset 64
    stp x29,x30,[sp,#32]
    .cfi_offset x29,-32
    .cfi_offset x30,-24
    add x29,sp,#32
    mov x9,sp
    adrp x10,dm_opus_tail_expected_sp
    str x9,[x10,:lo12:dm_opus_tail_expected_sp]
    adrp x10,dm_opus_tail_expected_x18
    str x18,[x10,:lo12:dm_opus_tail_expected_x18]
    mov x9,#0x800
    mov x10,#0x900
    stp x9,x10,[sp]
    mov x9,#0xa00
    mov x10,#0xb00
    stp x9,x10,[sp,#16]
    mov x0,#0x100
    mov x1,#0x101
    mov x2,#0x102
    mov x3,#0x103
    mov x4,#0x104
    mov x5,#0x105
    mov x6,#0x106
    mov x7,#0x107
    mov x8,#0x108
    movi v0.16b,#0x10
    movi v1.16b,#0x11
    movi v2.16b,#0x12
    movi v3.16b,#0x13
    movi v4.16b,#0x14
    movi v5.16b,#0x15
    movi v6.16b,#0x16
    movi v7.16b,#0x17
    bl dm_opus_ctl_entry
    ldp x29,x30,[sp,#32]
    add sp,sp,#64
    .cfi_def_cfa_offset 0
    .cfi_restore x29
    .cfi_restore x30
    ret
    .cfi_endproc
.size dm_opus_tail_register_test,.-dm_opus_tail_register_test
.globl dm_opus_tail_oracle
.type dm_opus_tail_oracle,%function
.p2align 2
dm_opus_tail_oracle:
    hint #34
    cmp x0,#0x100
    b.ne 1f
    cmp x1,#0x101
    b.ne 1f
    cmp x2,#0x102
    b.ne 1f
    cmp x3,#0x103
    b.ne 1f
    cmp x4,#0x104
    b.ne 1f
    cmp x5,#0x105
    b.ne 1f
    cmp x6,#0x106
    b.ne 1f
    cmp x7,#0x107
    b.ne 1f
    cmp x8,#0x108
    b.ne 1f
    mov x9,sp
    adrp x10,dm_opus_tail_expected_sp
    ldr x10,[x10,:lo12:dm_opus_tail_expected_sp]
    cmp x9,x10
    b.ne 1f
    adrp x10,dm_opus_tail_expected_x18
    ldr x10,[x10,:lo12:dm_opus_tail_expected_x18]
    cmp x18,x10
    b.ne 1f
    ldr x9,[sp]
    cmp x9,#0x800
    b.ne 1f
    ldr x9,[sp,#8]
    cmp x9,#0x900
    b.ne 1f
    ldr x9,[sp,#16]
    cmp x9,#0xa00
    b.ne 1f
    ldr x9,[sp,#24]
    cmp x9,#0xb00
    b.ne 1f
.macro CHECKV reg,value
    movi v16.16b,#\value
    cmeq v17.16b,\reg\().16b,v16.16b
    uminv b17,v17.16b
    umov w9,v17.b[0]
    cmp w9,#255
    b.ne 1f
.endm
    CHECKV v0,0x10
    CHECKV v1,0x11
    CHECKV v2,0x12
    CHECKV v3,0x13
    CHECKV v4,0x14
    CHECKV v5,0x15
    CHECKV v6,0x16
    CHECKV v7,0x17
    mov w0,#1
    ret
1:  mov w0,#0
    ret
.size dm_opus_tail_oracle,.-dm_opus_tail_oracle
.section .note.GNU-stack,"",%progbits
