#ifndef SCE_IOP_989SND_LOADER_DMA_H
#define SCE_IOP_989SND_LOADER_DMA_H
extern int gDMAInUse[2];
extern int gWaitingDMAComplete;
int snd_GetFreeSPUDMA(void);
void snd_FreeSPUDMA(int channel);
void snd_WaitDMADone(void);
#endif
