#include "sce/iop/989snd/timer.h"

#include "sce/iop/989snd/loader_callbacks.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/loader_state.h"
#include "sce/iop/989snd/stream_callbacks.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thsemap/thsemap.h"

#if SND989_VERSION == 0x020608
int gKillTimer = 0;
int gTransfering = 0;
void (*gTransferDoneCallback)(void) = 0;
int gDoneLoadSema = 0;
int gDMAInUse[2] = {0, 0};
int gLastDMA0Complete = 0;
int gLastDMA1Complete = 0;
int gDoneDMASema = 0;
int gWaitingDMAComplete = 0;

int snd_TimerLLTick(struct SoundTimerInfo* timer) {
  iWakeupThread(*timer->thread_id);
  if (gKillTimer) {
    gKillTimer = 0;
    return 0;
  }
  return timer->compare;
}

int snd_TransCallback(int channel, void* data) {
  if (!gTransfering)
    return 1;
  snd_FreeSPUDMA(gTransfering - 1);
  gTransfering = 0;
  if (gTransferDoneCallback)
    gTransferDoneCallback();
  return 0;
}

extern int gDoneLoadSema;
extern int gDoneDMASema;

void snd_DoneVAGReadCB(int code) {
  if (code == 1)
    iSignalSema(gDoneLoadSema);
}

int snd_VAGDMADone0(int channel, void* data) {
#if SND989_VERSION == 0x020604
  gDMAInUse[0] = 0;
  gLastDMA0Complete = 1;
#else
  gDMAInUse[channel] = 0;
  if (channel == 0)
    gLastDMA0Complete = 1;
  else
    gLastDMA1Complete = 1;
#endif
  iSignalSema(gDoneDMASema);
  if (gWaitingDMAComplete > 0) {
    iWakeupThread(gWaitingDMAComplete);
    gWaitingDMAComplete = 0;
  }
  return 0;
}
#if SND989_VERSION == 0x020604
int snd_VAGDMADone1(int channel, void* data) {
  gDMAInUse[1] = 0;
  gLastDMA1Complete = 1;
  iSignalSema(gDoneDMASema);
  if (gWaitingDMAComplete > 0) {
    iWakeupThread(gWaitingDMAComplete);
    gWaitingDMAComplete = 0;
  }
  return 0;
}
#endif
#endif
