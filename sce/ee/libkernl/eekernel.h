#ifndef SCE_EE_LIBKERNL_EEKERNEL_H
#define SCE_EE_LIBKERNL_EEKERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

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

void SyncDCache(void* start, void* end);

typedef struct {
  int status;
  void (*entry)(void);
  void* stack;
  int stack_size;
  void* gp;
  int priority, current_priority;
  unsigned attributes, option;
  int wait_type, wait_id, wakeups;
} Thread;

int DeleteSema(int id);
int iSignalSema(int id);
int DIntr(void);
int EIntr(void);
int ExitDeleteThread(void);
int ReferThreadStatus(int id, Thread* thread);
int CreateThread(Thread* thread);
int StartThread(int id, void* argument);
int ChangeThreadPriority(int id, int priority);

#define EE_UNCACHED_MASK 0x20000000

enum CacheOperation {
  WRITEBACK_DCACHE = 0,
  INVALIDATE_ICACHE = 2
};

int EnableIntc(int);
int AddIntcHandler(int, int (*)(int), int);
unsigned int SetDebugHandler(int, unsigned int);

#ifdef __cplusplus
}
#endif

#endif
