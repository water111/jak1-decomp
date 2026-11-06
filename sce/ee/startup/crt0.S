    .file 1 "/usr/local/sce/ee/lib/crt0.s"
    .set noreorder
    .set noat
    .text
    .align 3
    .loc 1 32
    nop
    .loc 1 33
    nop
    .globl _start
    .ent _start
_start:
    .globl ENTRYPOINT
    .type ENTRYPOINT,@object
ENTRYPOINT:
zerobss:
    .loc 1 40
    lui $2,%hi(_fbss)
    .loc 1 41
    lui $3,%hi(_end)
    .loc 1 42
    addiu $2,$2,%lo(_fbss)
    .loc 1 43
    addiu $3,$3,%lo(_end)
1:
    .loc 1 46
    sq $0,0($2)
    .loc 1 47
    sltu $1,$2,$3
    nop
    nop
    nop
    .loc 1 48
    bnez $1,1b
    .loc 1 49
    addiu $2,$2,16
    .loc 1 54
    lui $4,%hi(_gp)
    .loc 1 55
    lui $5,%hi(_stack)
    .loc 1 56
    lui $6,%hi(_stack_size)
    .loc 1 57
    lui $7,%hi(_args)
    .loc 1 58
    lui $8,%hi(_root)
    .loc 1 59
    addiu $4,$4,%lo(_gp)
    .loc 1 60
    addiu $5,$5,%lo(_stack)
    .loc 1 61
    addiu $6,$6,%lo(_stack_size)
    .loc 1 62
    addiu $7,$7,%lo(_args)
    .loc 1 63
    addiu $8,$8,%lo(_root)
    .loc 1 64
    move $28,$4
    .loc 1 65
    li $3,60
    .loc 1 66
    syscall
    .loc 1 67
    move $29,$2
    .loc 1 72
    lui $4,%hi(_end)
    .loc 1 73
    lui $5,%hi(_heap_size)
    .loc 1 74
    addiu $4,$4,%lo(_end)
    .loc 1 75
    addiu $5,$5,%lo(_heap_size)
    .loc 1 76
    li $3,61
    .loc 1 77
    syscall
    .loc 1 82
    jal _InitSys
    .loc 1 83
    nop
    .loc 1 88
    jal FlushCache
    .loc 1 89
    move $4,$0
    .loc 1 94
    ei
    .loc 1 95
    lui $2,%hi(_args)
    .loc 1 96
    addiu $2,$2,%lo(_args)
    .loc 1 97
    lw $4,0($2)
    .loc 1 98
    jal main
    .loc 1 99
    addiu $5,$2,4
    .loc 1 101
    j Exit
    .loc 1 102
    move $4,$2
    .end _start
    nop
    .globl _exit
    .ent _exit
_exit:
    .loc 1 110
    j Exit
    .loc 1 111
    move $4,$0
    .end _exit
    .ent _root
_root:
    .loc 1 117
    li $3,35
    .loc 1 118
    syscall
    .end _root
    .bss
    .align 6
_args:
    .space 324
    .appfile "/usr/local/sce/ee/lib/crt0.s"
