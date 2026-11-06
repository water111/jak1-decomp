#ifndef SCE_IOP_SDRDRV_H
#define SCE_IOP_SDRDRV_H

typedef int (*sceSdrUserCommandFunction)(unsigned int command, void* data, int size);
int sceSdrChangeThreadPriority(int sdr_main_thread_priority, int sdr_callback_thread_priority);
sceSdrUserCommandFunction sceSdrSetUserCommandFunction(int command,
                                                       sceSdrUserCommandFunction function);

#endif
