# Copies bytes using separate aligned and unaligned word paths.
# Each bulk iteration moves twelve words; the tail handles the last three bytes.
.set noreorder
.set noat
.text
.align 4
.globl cdvdstm_copy
.ent cdvdstm_copy
cdvdstm_copy:
  srl	$7,$6,0x2
  beqz	$7,.Lcopy_2e60
  or	$7,$4,$5
  andi	$7,$7,0x3
  bnez	$7,.Lcopy_2b5c
  nop
  srl	$7,$6,0x2
  li	$1,12
  div	$0,$7,$1
  mflo	$7
  mfhi	$3
  beqz	$3,.Lcopy_2ae4
  nop
.Lcopy_2ac4:
  lw	$2,0($5)
  addiu	$3,$3,-1
  sw	$2,0($4)
  addiu	$5,$5,4
  bnez	$3,.Lcopy_2ac4
  addiu	$4,$4,4
  beqz	$7,.Lcopy_2e60
  nop
.Lcopy_2ae4:
  lw	$2,0($5)
  lw	$3,4($5)
  lw	$8,8($5)
  lw	$9,12($5)
  lw	$10,16($5)
  lw	$11,20($5)
  lw	$12,24($5)
  lw	$13,28($5)
  lw	$14,32($5)
  lw	$15,36($5)
  lw	$24,40($5)
  lw	$25,44($5)
  addiu	$7,$7,-1
  sw	$2,0($4)
  sw	$3,4($4)
  sw	$8,8($4)
  sw	$9,12($4)
  sw	$10,16($4)
  sw	$11,20($4)
  sw	$12,24($4)
  sw	$13,28($4)
  sw	$14,32($4)
  sw	$15,36($4)
  sw	$24,40($4)
  sw	$25,44($4)
  addiu	$5,$5,48
  bnez	$7,.Lcopy_2ae4
  addiu	$4,$4,48
  j	.Lcopy_2e60
  nop
.Lcopy_2b5c:
  andi	$7,$4,0x3
  beqz	$7,.Lcopy_2c8c
  andi	$7,$5,0x3
  beqz	$7,.Lcopy_2c8c
  nop
  srl	$7,$6,0x2
  li	$1,12
  div	$0,$7,$1
  mflo	$7
  mfhi	$3
  beqz	$3,.Lcopy_2bb4
  nop
.Lcopy_2b8c:
  lwl	$2,3($5)
  lwr	$2,0($5)
  addiu	$3,$3,-1
  swl	$2,3($4)
  swr	$2,0($4)
  addiu	$5,$5,4
  bnez	$3,.Lcopy_2b8c
  addiu	$4,$4,4
  beqz	$7,.Lcopy_2e60
  nop
.Lcopy_2bb4:
  lwl	$2,3($5)
  lwr	$2,0($5)
  lwl	$3,7($5)
  lwr	$3,4($5)
  lwl	$8,11($5)
  lwr	$8,8($5)
  lwl	$9,15($5)
  lwr	$9,12($5)
  lwl	$10,19($5)
  lwr	$10,16($5)
  lwl	$11,23($5)
  lwr	$11,20($5)
  lwl	$12,27($5)
  lwr	$12,24($5)
  lwl	$13,31($5)
  lwr	$13,28($5)
  lwl	$14,35($5)
  lwr	$14,32($5)
  lwl	$15,39($5)
  lwr	$15,36($5)
  lwl	$24,43($5)
  lwr	$24,40($5)
  lwl	$25,47($5)
  lwr	$25,44($5)
  addiu	$7,$7,-1
  swl	$2,3($4)
  swr	$2,0($4)
  swl	$3,7($4)
  swr	$3,4($4)
  swl	$8,11($4)
  swr	$8,8($4)
  swl	$9,15($4)
  swr	$9,12($4)
  swl	$10,19($4)
  swr	$10,16($4)
  swl	$11,23($4)
  swr	$11,20($4)
  swl	$12,27($4)
  swr	$12,24($4)
  swl	$13,31($4)
  swr	$13,28($4)
  swl	$14,35($4)
  swr	$14,32($4)
  swl	$15,39($4)
  swr	$15,36($4)
  swl	$24,43($4)
  swr	$24,40($4)
  swl	$25,47($4)
  swr	$25,44($4)
  addiu	$5,$5,48
  bnez	$7,.Lcopy_2bb4
  addiu	$4,$4,48
  j	.Lcopy_2e60
  nop
.Lcopy_2c8c:
  andi	$7,$4,0x3
  beqz	$7,.Lcopy_2d80
  nop
  srl	$7,$6,0x2
  li	$1,12
  div	$0,$7,$1
  mflo	$7
  mfhi	$3
  beqz	$3,.Lcopy_2cd8
  nop
.Lcopy_2cb4:
  lw	$2,0($5)
  addiu	$3,$3,-1
  swl	$2,3($4)
  swr	$2,0($4)
  addiu	$5,$5,4
  bnez	$3,.Lcopy_2cb4
  addiu	$4,$4,4
  beqz	$7,.Lcopy_2e60
  nop
.Lcopy_2cd8:
  lw	$2,0($5)
  lw	$3,4($5)
  lw	$8,8($5)
  lw	$9,12($5)
  lw	$10,16($5)
  lw	$11,20($5)
  lw	$12,24($5)
  lw	$13,28($5)
  lw	$14,32($5)
  lw	$15,36($5)
  lw	$24,40($5)
  lw	$25,44($5)
  addiu	$7,$7,-1
  swl	$2,3($4)
  swr	$2,0($4)
  swl	$3,7($4)
  swr	$3,4($4)
  swl	$8,11($4)
  swr	$8,8($4)
  swl	$9,15($4)
  swr	$9,12($4)
  swl	$10,19($4)
  swr	$10,16($4)
  swl	$11,23($4)
  swr	$11,20($4)
  swl	$12,27($4)
  swr	$12,24($4)
  swl	$13,31($4)
  swr	$13,28($4)
  swl	$14,35($4)
  swr	$14,32($4)
  swl	$15,39($4)
  swr	$15,36($4)
  swl	$24,43($4)
  swr	$24,40($4)
  swl	$25,47($4)
  swr	$25,44($4)
  addiu	$5,$5,48
  bnez	$7,.Lcopy_2cd8
  addiu	$4,$4,48
  j	.Lcopy_2e60
  nop
.Lcopy_2d80:
  srl	$7,$6,0x2
  li	$1,12
  div	$0,$7,$1
  mflo	$7
  mfhi	$3
  beqz	$3,.Lcopy_2dc0
  nop
.Lcopy_2d9c:
  lwl	$2,3($5)
  lwr	$2,0($5)
  addiu	$3,$3,-1
  sw	$2,0($4)
  addiu	$5,$5,4
  bnez	$3,.Lcopy_2d9c
  addiu	$4,$4,4
  beqz	$7,.Lcopy_2e60
  nop
.Lcopy_2dc0:
  lwl	$2,3($5)
  lwr	$2,0($5)
  lwl	$3,7($5)
  lwr	$3,4($5)
  lwl	$8,11($5)
  lwr	$8,8($5)
  lwl	$9,15($5)
  lwr	$9,12($5)
  lwl	$10,19($5)
  lwr	$10,16($5)
  lwl	$11,23($5)
  lwr	$11,20($5)
  lwl	$12,27($5)
  lwr	$12,24($5)
  lwl	$13,31($5)
  lwr	$13,28($5)
  lwl	$14,35($5)
  lwr	$14,32($5)
  lwl	$15,39($5)
  lwr	$15,36($5)
  lwl	$24,43($5)
  lwr	$24,40($5)
  lwl	$25,47($5)
  lwr	$25,44($5)
  addiu	$7,$7,-1
  sw	$2,0($4)
  sw	$3,4($4)
  sw	$8,8($4)
  sw	$9,12($4)
  sw	$10,16($4)
  sw	$11,20($4)
  sw	$12,24($4)
  sw	$13,28($4)
  sw	$14,32($4)
  sw	$15,36($4)
  sw	$24,40($4)
  sw	$25,44($4)
  addiu	$5,$5,48
  bnez	$7,.Lcopy_2dc0
  addiu	$4,$4,48
.Lcopy_2e60:
  andi	$3,$6,0x3
  beqz	$3,.Lcopy_2e84
  nop
.Lcopy_2e6c:
  lb	$2,0($5)
  addiu	$3,$3,-1
  sb	$2,0($4)
  addiu	$5,$5,1
  bnez	$3,.Lcopy_2e6c
  addiu	$4,$4,1
.Lcopy_2e84:
  move	$2,$6
  jr	$31
  nop
.end cdvdstm_copy
