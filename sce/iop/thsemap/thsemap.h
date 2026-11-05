#ifndef SCE_IOP_THSEMAP_H
#define SCE_IOP_THSEMAP_H

#include "sce/iop/types.h"

struct SemaParam {
  u_int attr;
  u_int option;
  int initCount;
  int maxCount;
};

extern int CreateSema(struct SemaParam* param);
extern int DeleteSema(int semid);
extern int SignalSema(int semid);
extern int iSignalSema(int semid);
extern int WaitSema(int semid);
extern int PollSema(int semid);

#define AT_THPRI 1
#define SA_THPRI AT_THPRI

#endif
