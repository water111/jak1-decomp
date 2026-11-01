#ifndef ISO_QUEUE_H
#define ISO_QUEUE_H

#include "overlord/isocommon.h"

void InitBuffers(void);
IsoBufferHeader* AllocateBuffer(unsigned int size);
void FreeBuffer(IsoBufferHeader* buffer);
unsigned int QueueMessage(IsoMessage* cmd, int priority, const char* name);
void UnqueueMessage(IsoMessage* cmd);
void ReturnMessage(IsoMessage* cmd);
void ReleaseMessage(IsoMessage* cmd);
IsoBufferHeader* TryAllocateBuffer(unsigned int size);
IsoMessage* GetMessage(void);
void ProcessMessageData(void);
VagCommand* GetVAGCommand(void);

#endif
