#ifndef _SDMACRO_H_
#define _SDMACRO_H_

#define SD_BSET_PARAM 0x01
#define SD_BSET_SWITCH 0x02
#define SD_BSET_ADDR 0x03

#define SD_VA_SSA ((0x20 << 8) + (0x01 << 6))
#define SD_VA_LSAX ((0x21 << 8) + (0x01 << 6))
#define SD_VA_NAX ((0x22 << 8) + (0x01 << 6))

#define SD_VP_VOLL (0x00 << 8)
#define SD_VP_VOLR (0x01 << 8)
#define SD_VP_PITCH (0x02 << 8)
#define SD_VP_ADSR1 (0x03 << 8)
#define SD_VP_ADSR2 (0x04 << 8)

#define SD_S_VMIXL (0x18 << 8)
#define SD_S_VMIXR (0x1a << 8)

typedef int (*sceSdTransIntrHandler)(int, void*);

typedef struct {
  unsigned short func;
  unsigned short entry;
  unsigned int value;
} sceSdBatch;

#endif
