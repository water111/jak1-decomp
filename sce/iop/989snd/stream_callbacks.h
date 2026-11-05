#ifndef SCE_IOP_989SND_STREAM_CALLBACKS_H
#define SCE_IOP_989SND_STREAM_CALLBACKS_H
extern int gLastDMA0Complete, gLastDMA1Complete;
void snd_DoneVAGReadCB(int code);
void snd_KickVAGDMA(int channel);
void snd_VAGDMADone(int channel);
void snd_VAGStreamDMADoneThread(void);
void snd_VAGStreamDMAThread(void);
int snd_VAGDMADone0(int channel, void* data);
#if SND989_VERSION == 0x020604
int snd_VAGDMADone1(int channel, void* data);
#endif
#endif
