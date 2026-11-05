#ifndef _INTRMAN_H
#define _INTRMAN_H

// Interrupt manager

extern int CpuDisableIntr();
extern int CpuEnableIntr();

int RegisterIntrHandler(int interrupt, int mode, int (*handler)(void*), void* arg);
int ReleaseIntrHandler(int interrupt);
int EnableIntr(int interrupt);
int DisableIntr(int interrupt, int* old_mode);
int QueryIntrContext(void);
int CpuSuspendIntr(int* state);
int CpuResumeIntr(int state);

#endif
