#ifndef NEXT_KERNEL_KLISTEN_H
#define NEXT_KERNEL_KLISTEN_H

#include "next/kernel/kscheme.h"

extern unsigned int* ListenerLinkBlock;
extern unsigned int* ListenerFunction;
extern void (**kernel_dispatcher)();
extern symbol* kernel_packages;
extern unsigned int* print_column;
extern unsigned int ListenerStatus;

void ProcessListenerMessage(char* message);
void ClearPending();
void SendAck();
void InitListener();

#endif
