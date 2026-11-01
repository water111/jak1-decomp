#ifndef SND989_H
#define SND989_H

int snd_GetFreeSPUDMA();
void snd_FreeSPUDMA(int ch);
void snd_KeyOnVoiceRaw(int core, int voice);
void snd_KeyOffVoiceRaw(int core, int voice);
void* snd_BankLoadByLoc(int sector, int offset);
int snd_GetLastLoadError(void);
void snd_ResolveBankXREFS(void);

#endif
