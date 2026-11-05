#ifndef SCE_IOP_989SND_FRONTEND_H
#define SCE_IOP_989SND_FRONTEND_H

#include "sce/iop/989snd/extern_proc.h"
#include "sce/iop/989snd/loader_memory.h"
#include "sce/iop/989snd/options.h"

#include "sce/iop/989snd/commands.h"

int snd_StartEEMessaging(void);
int snd_StartEELoaderMessaging(void);
int _start(int argc, char** argv);
void* snd_EEMessageParser(u_int command, void* data, int size);
void* snd_EELoaderMessageParser(u_int command, void* data, int size);

#endif
