#ifndef _THREAD_H
#define _THREAD_H

#include "sce/iop/types.h"

extern int GetThreadId();

extern int DelayThread(unsigned int usec);

struct MsgPacket {
  struct MsgPacket* next;
  u_char msgPriority;
  u_char dummy[3];
};

extern int SendMbx(int mbxid, struct MsgPacket* sendmsg);
extern int PollMbx(struct MsgPacket** recvmsg, int mbxid);

struct MbxParam {
  u_int attr;
  u_int option;
};

extern int CreateMbx(struct MbxParam* param);

#define TH_C 0x02000000

struct ThreadParam {
  u_int attr;
  u_int option;
  void* entry;
  int stackSize;
  int initPriority;
};

extern int CreateThread(struct ThreadParam* param);
extern int StartThread(int thid, u_long arg);
extern int SleepThread();

struct SysClock {
  u_int low;
  u_int hi;
};

extern int GetSystemTime(struct SysClock* clock);

#endif
