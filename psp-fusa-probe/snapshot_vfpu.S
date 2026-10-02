/* SPDX-License-Identifier: MIT
 * Progressive 480x272 snapshot. Only the VFPU-owning worker calls these.
 * a0 = aligned RAM destination, a1 = source, a2 = source stride in pixels.
 * No interlaced offsets or game-side register/context assumptions.
 */
    .text
    .set noreorder
    .global fs_copy16_vfpu
    .ent fs_copy16_vfpu
fs_copy16_vfpu:
    sll $a2,$a2,1
    addiu $a2,$a2,-960
    li $t0,272
1:
    li $t1,15
2:
    lv.q C000,0($a1)
    lv.q C010,16($a1)
    lv.q C020,32($a1)
    lv.q C030,48($a1)
    sv.q C000,0($a0)
    sv.q C010,16($a0)
    sv.q C020,32($a0)
    sv.q C030,48($a0)
    vnop
    addiu $a0,$a0,64
    addiu $a1,$a1,64
    addiu $t1,$t1,-1
    bnez $t1,2b
    nop
    addu $a1,$a1,$a2
    addiu $t0,$t0,-1
    bnez $t0,1b
    nop
    jr $ra
    nop
    .end fs_copy16_vfpu

    .global fs_copy32_vfpu
    .ent fs_copy32_vfpu
fs_copy32_vfpu:
    sll $a2,$a2,2
    addiu $a2,$a2,-1920
    li $t0,272
3:
    li $t1,30
4:
    lv.q C000,0($a1)
    lv.q C010,16($a1)
    lv.q C020,32($a1)
    lv.q C030,48($a1)
    vt5650.q C100,C000
    vt5650.q C102,C010
    vt5650.q C110,C020
    vt5650.q C112,C030
    sv.q C100,0($a0)
    sv.q C110,16($a0)
    vnop
    addiu $a0,$a0,32
    addiu $a1,$a1,64
    addiu $t1,$t1,-1
    bnez $t1,4b
    nop
    addu $a1,$a1,$a2
    addiu $t0,$t0,-1
    bnez $t0,3b
    nop
    jr $ra
    nop
    .end fs_copy32_vfpu
