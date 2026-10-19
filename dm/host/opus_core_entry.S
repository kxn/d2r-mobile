// Transparent AAPCS64 tail dispatch. In SDK/default/unowned cases the original
// function sees exactly x0-x8, v0-v7 and the original stack argument layout.
// No private TLS, x18 change, guest bytes or CPU emulation.
.text
.hidden dm_opus_original_math
.hidden dm_opus_core_dispatch_enabled
.hidden dm_opus_math_select
.macro OPUS_ENTRY name,kind
.globl \name
.hidden \name
.type \name,%function
.p2align 2
\name:
    .cfi_startproc
    hint #34
    adrp x16,dm_opus_core_dispatch_enabled
    add x16,x16,:lo12:dm_opus_core_dispatch_enabled
    ldar w17,[x16]
    cbnz w17,1f
    adrp x16,dm_opus_original_math
    add x16,x16,:lo12:dm_opus_original_math
    add x16,x16,#(\kind*8)
    ldar x17,[x16]
    cbz x17,1f
    br x17
1:  sub sp,sp,#208
    .cfi_def_cfa_offset 208
    stp x0,x1,[sp]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x30,[sp,#64]
    .cfi_offset x30,-136
    stp q0,q1,[sp,#80]
    stp q2,q3,[sp,#112]
    stp q4,q5,[sp,#144]
    stp q6,q7,[sp,#176]
    mov w0,#\kind
    mov x1,sp
    bl dm_opus_math_select
    mov x16,x0
    ldp q0,q1,[sp,#80]
    ldp q2,q3,[sp,#112]
    ldp q4,q5,[sp,#144]
    ldp q6,q7,[sp,#176]
    ldp x0,x1,[sp]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp x8,x30,[sp,#64]
    add sp,sp,#208
    .cfi_def_cfa_offset 0
    .cfi_restore x30
    br x16
    .cfi_endproc
.size \name,.-\name
.endm
OPUS_ENTRY dm_opus_init_entry,0
OPUS_ENTRY dm_opus_ctl_entry,1
OPUS_ENTRY dm_opus_decode_entry,2
.section .note.GNU-stack,"",%progbits
