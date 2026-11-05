#ifndef SCE_IOP_989SND_STREAM_CD_H
#define SCE_IOP_989SND_STREAM_CD_H

#include "sce/iop/types.h"
struct VAGStreamHeader;
extern struct VAGStreamHeader* VAGStreamList;
extern void* gMasterReadBuffer;
extern u_int gSSNeedSectors;
extern u_int gBreakDataRead;
extern int gLastDATAReadError;
extern int gDataReadBusy;
extern int gReadWaitThread;
#if SND989_VERSION == 0x020604
void snd_ShowCDError(int error);
#endif
void snd_VAGStreamDataLoadThread(void);
void snd_VAGStreamLoadDoneThread(void);
void snd_SignalDataReadDone(void);
void snd_KickDataRead(void);
void snd_KickVAGRead(void);
int snd_StreamSafeCheckCDIdle(int block);
int snd_StreamSafeCdSync(int mode);
int snd_StreamSafeCdRead(u_int sector, u_int count, void* destination);
int snd_StreamSafeCdReadEEm(u_int sector, u_int count, void* destination);
void snd_StreamSafeCdBreak(void);
int snd_StreamSafeCdGetError(void);
#endif
