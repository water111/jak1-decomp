#ifndef SCE_EE_LIBKERNL_EEKERNEL_H
#define SCE_EE_LIBKERNL_EEKERNEL_H

void GetOsdConfigParam(unsigned* config);
int GetOsdConfigParam2(void* config, int size, int offset);

void LoadExecPS2(const char* filename, int argc, char** argv);

typedef struct {
  int count, initial, maximum, waiters, attributes, option;
} Semaphore;

int CreateSema(Semaphore* semaphore);
int WaitSema(int id);
int PollSema(int id);
int SignalSema(int id);
int GetThreadId(void);
int SleepThread(void);
int iWakeupThread(int id);
int SetAlarm(unsigned short time, void (*callback)(int, unsigned short, int), int argument);
void FlushCache(int operation);

unsigned long GsPutIMR(unsigned long mask);
unsigned long GsGetIMR(void);
int DisableIntc(int cause);
int RemoveIntcHandler(int cause, int handler);
void SetGsCrt(short inter, short output, short frame);
void VSync(void);
long VSync2(void);

#endif
