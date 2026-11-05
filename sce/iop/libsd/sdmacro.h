#ifndef _SDMACRO_H_
#define _SDMACRO_H_

#define SD_C_EFFECT_ENABLE 2

#define SD_BSET_PARAM 0x01
#define SD_BSET_SWITCH 0x02
#define SD_BSET_ADDR 0x03

#define SD_A_EEA 0x1d00

#define SD_VA_SSA ((0x20 << 8) + (0x01 << 6))
#define SD_VA_LSAX ((0x21 << 8) + (0x01 << 6))
#define SD_VA_NAX ((0x22 << 8) + (0x01 << 6))

#define SD_VP_VOLL (0x00 << 8)
#define SD_VP_VOLR (0x01 << 8)
#define SD_VP_PITCH (0x02 << 8)
#define SD_VP_ADSR1 (0x03 << 8)
#define SD_VP_ADSR2 (0x04 << 8)
#define SD_VP_ENVX (0x05 << 8)

#define SD_P_MMIX 0x0800
#define SD_P_AVOLL 0x0d80
#define SD_P_AVOLR 0x0e80
#define SD_C_SPDIF_MODE 10

#define SD_P_MVOLL 0x0980
#define SD_P_MVOLR 0x0a80
#define SD_P_EVOLL 0x0b80
#define SD_P_EVOLR 0x0c80

#define SD_S_KON (0x15 << 8)
#define SD_S_KOFF (0x16 << 8)
#define SD_S_VMIXEL (0x19 << 8)
#define SD_S_VMIXER (0x1b << 8)
#define SD_S_VMIXL (0x18 << 8)
#define SD_S_VMIXR (0x1a << 8)

typedef int (*sceSdTransIntrHandler)(int, void*);

typedef struct {
  unsigned short func;
  unsigned short entry;
  unsigned int value;
} sceSdBatch;

#endif
