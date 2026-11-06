#ifndef NEXT_KERNEL_KSOCKET_H
#define NEXT_KERNEL_KSOCKET_H

int ReceiveToBuffer(char* buffer);
int SendFromBuffer(char* buffer, int size);
void InitListenerConnect();
void InitCheckListener();
char* WaitForMessageAndAck();
void CloseListener();

#endif
