#ifndef SCE_IOP_989SND_LOADER_CD_H
#define SCE_IOP_989SND_LOADER_CD_H

#include "sce/iop/sifcmd/sifcmd.h"

struct SndCDSifRegPacket {
  sceSifCmdHeader header;
  int reg;
  int value;
};
extern struct SndCDSifRegPacket gCDSifRegPacket;
#if SND989_VERSION == 0x020608
void snd_SetCDSifReg(int reg8, int reg9);
#endif
#endif
