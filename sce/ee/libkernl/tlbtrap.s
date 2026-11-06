.file 1 "tlbtrap.s"
.set noreorder
.set noat
.text
.align 6
.globl _kTLBException
.ent _kTLBException
_kTLBException:
.loc 1 24
lui $26,%hi(EEREG)
.loc 1 25
addiu $26,$26,%lo(EEREG)
.loc 1 27
sq $1, 16($26)
.loc 1 28
sq $2, 32($26)
.loc 1 29
sq $3, 48($26)
.loc 1 30
sq $4, 64($26)
.loc 1 31
sq $5, 80($26)
.loc 1 32
sq $6, 96($26)
.loc 1 33
sq $7, 112($26)
.loc 1 34
sq $8, 128($26)
.loc 1 35
sq $9, 144($26)
.loc 1 36
sq $10, 160($26)
.loc 1 37
sq $11, 176($26)
.loc 1 38
sq $12, 192($26)
.loc 1 39
sq $13, 208($26)
.loc 1 40
sq $14, 224($26)
.loc 1 41
sq $15, 240($26)
.loc 1 42
sq $16, 256($26)
.loc 1 43
sq $17, 272($26)
.loc 1 44
sq $18, 288($26)
.loc 1 45
sq $19, 304($26)
.loc 1 46
sq $20, 320($26)
.loc 1 47
sq $21, 336($26)
.loc 1 48
sq $22, 352($26)
.loc 1 49
sq $23, 368($26)
.loc 1 50
sq $24, 384($26)
.loc 1 51
sq $25, 400($26)
.loc 1 52
sq $28, 448($26)
.loc 1 53
sq $29, 464($26)
.loc 1 54
sq $30, 480($26)
.loc 1 55
sq $31, 496($26)
.loc 1 58
mfhi $2
.loc 1 59
lui $1,%hi(EERH0)
sd $2,%lo(EERH0)($1)
.loc 1 60
mfhi1 $2
.loc 1 61
lui $1,%hi(EERH1)
sd $2,%lo(EERH1)($1)
.loc 1 62
mflo $2
.loc 1 63
lui $1,%hi(EERL0)
sd $2,%lo(EERL0)($1)
.loc 1 64
mflo1 $2
.loc 1 65
lui $1,%hi(EERL1)
sd $2,%lo(EERL1)($1)
.loc 1 66
mfsa $2
.loc 1 67
lui $1,%hi(EERSA)
sd $2,%lo(EERSA)($1)
.loc 1 70
mfc0 $4,$12
.loc 1 71
mfc0 $5,$13
.loc 1 72
mfc0 $6,$14
.loc 1 73
mfc0 $7,$8
.loc 1 74
la $8,EEREG
.loc 1 76
lui $1,%hi(EEEPC)
sw $6,%lo(EEEPC)($1)
.loc 1 79
lui $1,%hi(_xlaunch)
.loc 1 80
addiu $1,$1,%lo(_xlaunch)
.loc 1 81
mtc0 $1,$14
.loc 1 82
sync.p
.loc 1 84
mfc0 $1,$12
.loc 1 85
li $2,-2
.loc 1 86
and $1,$1,$2
.loc 1 87
mtc0 $1,$12
.loc 1 88
sync.p
.loc 1 90
eret
.globl _xlaunch
.type _xlaunch,@object
_xlaunch:
.loc 1 92
lui $1,%hi(_kTLBRefillHandler)
.loc 1 93
lw $1,%lo(_kTLBRefillHandler)($1)
.loc 1 94
lui $29,%hi(hstack)
.loc 1 95
jalr $1
.loc 1 96
addiu $29,$29,%lo(hstack)
.loc 1 98
li $3,-84
.loc 1 99
syscall
.end _kTLBException
nop
nop
nop
nop
nop
nop
nop
nop
nop
.align 6
.globl _kExitTLBHandler
.ent _kExitTLBHandler
_kExitTLBHandler:
.loc 1 103
mfc0 $1,$12
.loc 1 103
li $26,-28
.loc 1 103
and $1,$1,$26
.loc 1 103
mtc0 $1,$12
.loc 1 103
sync.p
.loc 1 105
lw $2,EEEPC
.loc 1 106
mtc0 $2,$14
.loc 1 107
sync.p
.loc 1 109
ld $2,EERH0
.loc 1 110
mthi $2
.loc 1 111
ld $2,EERH1
.loc 1 112
mthi1 $2
.loc 1 113
ld $2,EERL0
.loc 1 114
mtlo $2
.loc 1 115
ld $2,EERL1
.loc 1 116
mtlo1 $2
.loc 1 117
ld $2,EERSA
.loc 1 118
mtsa $2
.loc 1 119
sync.p
.loc 1 121
lui $26,%hi(EEREG)
.loc 1 122
addiu $26,$26,%lo(EEREG)
.loc 1 124
lq $1, 16($26)
.loc 1 125
lq $2, 32($26)
.loc 1 126
lq $3, 48($26)
.loc 1 127
lq $4, 64($26)
.loc 1 128
lq $5, 80($26)
.loc 1 129
lq $6, 96($26)
.loc 1 130
lq $7, 112($26)
.loc 1 131
lq $8, 128($26)
.loc 1 132
lq $9, 144($26)
.loc 1 133
lq $10, 160($26)
.loc 1 134
lq $11, 176($26)
.loc 1 135
lq $12, 192($26)
.loc 1 136
lq $13, 208($26)
.loc 1 137
lq $14, 224($26)
.loc 1 138
lq $15, 240($26)
.loc 1 139
lq $16, 256($26)
.loc 1 140
lq $17, 272($26)
.loc 1 141
lq $18, 288($26)
.loc 1 142
lq $19, 304($26)
.loc 1 143
lq $20, 320($26)
.loc 1 144
lq $21, 336($26)
.loc 1 145
lq $22, 352($26)
.loc 1 146
lq $23, 368($26)
.loc 1 147
lq $24, 384($26)
.loc 1 148
lq $25, 400($26)
.loc 1 149
lq $28, 448($26)
.loc 1 150
lq $29, 464($26)
.loc 1 151
lq $30, 480($26)
.loc 1 152
lq $31, 496($26)
.loc 1 154
mfc0 $26,$12
.loc 1 154
ori $26,$26,0x13
.loc 1 154
mtc0 $26,$12
.loc 1 154
sync.p
.loc 1 154
eret
.end _kExitTLBHandler
nop
nop
nop
.align 6
.globl _kDebugException
.ent _kDebugException
_kDebugException:
.loc 1 162
lui $26,%hi(EEREG)
.loc 1 163
addiu $26,$26,%lo(EEREG)
.loc 1 165
sq $1, 16($26)
.loc 1 166
sq $2, 32($26)
.loc 1 167
sq $3, 48($26)
.loc 1 168
sq $4, 64($26)
.loc 1 169
sq $5, 80($26)
.loc 1 170
sq $6, 96($26)
.loc 1 171
sq $7, 112($26)
.loc 1 172
sq $8, 128($26)
.loc 1 173
sq $9, 144($26)
.loc 1 174
sq $10, 160($26)
.loc 1 175
sq $11, 176($26)
.loc 1 176
sq $12, 192($26)
.loc 1 177
sq $13, 208($26)
.loc 1 178
sq $14, 224($26)
.loc 1 179
sq $15, 240($26)
.loc 1 180
sq $16, 256($26)
.loc 1 181
sq $17, 272($26)
.loc 1 182
sq $18, 288($26)
.loc 1 183
sq $19, 304($26)
.loc 1 184
sq $20, 320($26)
.loc 1 185
sq $21, 336($26)
.loc 1 186
sq $22, 352($26)
.loc 1 187
sq $23, 368($26)
.loc 1 188
sq $24, 384($26)
.loc 1 189
sq $25, 400($26)
.loc 1 190
sq $28, 448($26)
.loc 1 191
sq $29, 464($26)
.loc 1 192
sq $30, 480($26)
.loc 1 193
sq $31, 496($26)
.loc 1 196
mfhi $2
.loc 1 197
lui $1,%hi(EERH0)
sd $2,%lo(EERH0)($1)
.loc 1 198
mfhi1 $2
.loc 1 199
lui $1,%hi(EERH1)
sd $2,%lo(EERH1)($1)
.loc 1 200
mflo $2
.loc 1 201
lui $1,%hi(EERL0)
sd $2,%lo(EERL0)($1)
.loc 1 202
mflo1 $2
.loc 1 203
lui $1,%hi(EERL1)
sd $2,%lo(EERL1)($1)
.loc 1 204
mfsa $2
.loc 1 205
lui $1,%hi(EERSA)
sd $2,%lo(EERSA)($1)
.loc 1 208
mfc0 $4,$12
.loc 1 209
mfc0 $5,$13
.loc 1 210
mfc0 $6,$14
.loc 1 211
mfc0 $7,$8
.loc 1 212
mfc0 $8,$23
.loc 1 213
la $9,EEREG
.loc 1 215
lui $1,%hi(2f)
.loc 1 216
addiu $1,$1,%lo(2f)
.loc 1 217
mtc0 $1,$14
.loc 1 218
sync.p
.loc 1 220
mfc0 $1,$12
.loc 1 221
li $2,-2
.loc 1 222
and $1,$1,$2
.loc 1 223
mtc0 $1,$12
.loc 1 224
sync.p
.loc 1 226
eret
2:
.loc 1 228
andi $2,$5,0x7c
.loc 1 229
lui $1,%hi(_kDebugHandler)
.loc 1 230
addu $1,$1,$2
.loc 1 231
lw $1,%lo(_kDebugHandler)($1)
.loc 1 232
lui $29,%hi(hstack)
.loc 1 233
jalr $1
.loc 1 234
addiu $29,$29,%lo(hstack)
.loc 1 236
break 0xfffff
.end _kDebugException
nop
.bss
.align 6
.space 4096
hstack:
EEREG:
EER00: .space 16
EERAT: .space 16
EERV0: .space 16
EERV1: .space 16
EER04: .space 16
EER05: .space 16
EER06: .space 16
EER07: .space 16
EER08: .space 16
EER09: .space 16
EER10: .space 16
EER11: .space 16
EER12: .space 16
EER13: .space 16
EER14: .space 16
EER15: .space 16
EER16: .space 16
EER17: .space 16
EER18: .space 16
EER19: .space 16
EER20: .space 16
EER21: .space 16
EER22: .space 16
EER23: .space 16
EER24: .space 16
EER25: .space 16
EER26: .space 16
EER27: .space 16
EERGP: .space 16
EERSP: .space 16
EERFP: .space 16
EERRA: .space 16
EERH0: .space 8
EERH1: .space 8
EERL0: .space 8
EERL1: .space 8
EERSA: .space 8
EEEPC: .space 8
.appfile "tlbtrap.s"
.file 2 "../kernel/kparam.h"
