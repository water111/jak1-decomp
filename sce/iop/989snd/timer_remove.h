#ifndef SCE_IOP_989SND_TIMER_REMOVE_H
#define SCE_IOP_989SND_TIMER_REMOVE_H

#include "sce/iop/989snd/timer.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/xtimrman/xtimrman.h"

static inline void remove_sound_tick(void) {
  TerminateThread(gTimerThreadID);
  DeleteThread(gTimerThreadID);
  gKillTimer = 1;
  do {
    FlushDcache();
  } while (gKillTimer);
#if SND989_VERSION == 0x020604
  printf("StopHardTimer returned %d\n", StopHardTimer(gTimerID));
#else
  StopHardTimer(gTimerID);
#endif
  FreeHardTimer(gTimerID);
}

#endif
