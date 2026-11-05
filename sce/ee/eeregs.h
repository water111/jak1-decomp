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

typedef unsigned int EeQuad __attribute__((mode(TI)));

#define D1_MADR ((volatile unsigned*)0x10009010)
#define D1_QWC ((volatile unsigned*)0x10009020)
#define D1_TADR ((volatile unsigned*)0x10009030)
#define D2_MADR ((volatile unsigned*)0x1000a010)
#define D2_QWC ((volatile unsigned*)0x1000a020)
#define D2_TADR ((volatile unsigned*)0x1000a030)
#define GIF_CTRL ((volatile unsigned*)0x10003000)
#define GIF_STAT ((volatile unsigned*)0x10003020)
#define VIF1_STAT ((volatile unsigned*)0x10003c00)
#define VIF1_FBRST ((volatile unsigned*)0x10003c10)
#define VIF1_ERR ((volatile unsigned*)0x10003c20)
#define VIF1_FIFO ((volatile EeQuad*)0x10005000)
#define GS_SMODE2 ((volatile unsigned long*)0x12000020)
#define GS_DISPFB1 ((volatile unsigned long*)0x12000070)
#define GS_DISPLAY1 ((volatile unsigned long*)0x12000080)
#define GS_DISPFB2 ((volatile unsigned long*)0x12000090)
#define GS_DISPLAY2 ((volatile unsigned long*)0x120000a0)
#define GS_EXTDATA ((volatile unsigned long*)0x120000c0)
#define GS_CSR ((volatile unsigned long*)0x12001000)
#define GS_BUSDIR ((volatile unsigned long*)0x12001040)

#endif
