#ifndef SCE_IOP_989SND_OPTIONS_H
#define SCE_IOP_989SND_OPTIONS_H

extern int gThreadPriority_TICK;
extern int gThreadPriority_RPC;
extern int gThreadPriority_STREAM;
#if SND989_VERSION == 0x020608
extern int gDoRPC;
#endif

void snd_ParseCommandLineArg(char* argument);
int snd_GetVal(char* text);
void snd_DumpVersionAndInfo(void);

#endif
