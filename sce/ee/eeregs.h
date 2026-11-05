#ifndef SCE_EE_EEREGS_H
#define SCE_EE_EEREGS_H

#define D0_CHCR ((volatile unsigned*)0x10008000)
#define D1_CHCR ((volatile unsigned*)0x10009000)
#define D2_CHCR ((volatile unsigned*)0x1000a000)
#define D3_CHCR ((volatile unsigned*)0x1000b000)
#define D4_CHCR ((volatile unsigned*)0x1000b400)
#define D5_CHCR ((volatile unsigned*)0x1000c000)
#define D6_CHCR ((volatile unsigned*)0x1000c400)
#define D7_CHCR ((volatile unsigned*)0x1000c800)
#define D8_CHCR ((volatile unsigned*)0x1000d000)
#define D9_CHCR ((volatile unsigned*)0x1000d400)

#define D_CTRL ((volatile unsigned*)0x1000e000)
#define D_STAT ((volatile unsigned*)0x1000e010)
#define D_PCR ((volatile unsigned*)0x1000e020)
#define D_SQWC ((volatile unsigned*)0x1000e030)
#define D_RBSR ((volatile unsigned*)0x1000e040)
#define D_RBOR ((volatile unsigned*)0x1000e050)
#define D_STADR ((volatile unsigned*)0x1000e060)

#define GS_PMODE ((volatile unsigned long*)0x12000000)
#define GS_BGCOLOR ((volatile unsigned long*)0x120000e0)

#endif
