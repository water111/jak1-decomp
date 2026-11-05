#ifndef _THREAD_H
#define _THREAD_H

#include "sce/iop/types.h"

extern int GetThreadId();

extern int DelayThread(unsigned int usec);

#define TH_C 0x02000000

struct ThreadParam {
  u_int attr;
  u_int option;
  void* entry;
  int stackSize;
  int initPriority;
};

extern int CreateThread(struct ThreadParam* param);
extern int TerminateThread(int thid);
extern int DeleteThread(int thid);
extern int StartThread(int thid, u_long arg);
extern int SleepThread();
extern int WakeupThread(int thid);
extern int iWakeupThread(int thid);

struct SysClock {
  u_int low;
  u_int hi;
};

extern int GetSystemTime(struct SysClock* clock);
void USec2SysClock(unsigned int usec, struct SysClock* clock);

// Status fields used by the priority-changing RPC service.
struct ThreadInfo {
  unsigned int attr;
  unsigned int option;
  int status;
  void* entry;
  void* stack;
  int stackSize;
  void* gp;
  int initPriority;
  int currentPriority;
  int waitType;
  int waitId;
  int wakeupCount;
  void* context;
  int reserved[4];
};
int ReferThreadStatus(int id, struct ThreadInfo* info);
int ChangeThreadPriority(int id, int priority);

#endif
