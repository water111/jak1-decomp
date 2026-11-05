#include "sce/iop/989snd/init.h"

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/loader_midi.h"
#include "sce/iop/989snd/loader_state.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/989snd/timer.h"
#include "sce/iop/989snd/timer_remove.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thsemap/thsemap.h"
#include "sce/iop/xtimrman/xtimrman.h"

#if SND989_VERSION == 0x020608
int gSTickSema = -1;
#endif
int gTimerThreadID = 0;
volatile int gTimerID = 0;
int gTickTrigger = 0;
int gTickMode = -1;
unsigned int gMicsPerTick = 0;
int gTickRate = 0;

void snd_CloseVAGStreaming(void);
extern unsigned int gAwaitingKeyOff[2];

extern int gThreadPriority_TICK;
extern struct SoundTimerInfo gTimerInfo;
#if SND989_VERSION == 0x020604
extern struct ThreadParam timer_thread_param;
#endif

#if SND989_VERSION == 0x020604
static int timer_reserved = 0;
int gKillTimer = 0;
#endif

int snd_SystemRunning(void) {
  return gTickMode != -1;
}

int snd_StartSoundSystem(void) {
  int result;
  if (gTickMode != -1)
    return -1;
  snd_SRAMInitMalloc();
  snd_InitLoader();
  snd_InitVoiceAllocator();
  snd_InitSoundHandlers();
  sceSdInit(0);
  EnableIntr(36);
  EnableIntr(40);
  EnableIntr(9);
  gTickMode = 1;
  gTickRate = snd_GetTickRate();
  gMicsPerTick = 100000000 / (999 * gTickRate / 10);
  result = snd_InstallSoundTick();
  if (result) {
    gTickMode = -1;
    return result;
  }
  snd_SetPlaybackMode(0);
  sceSdSetParam(SD_P_MMIX, 0xf00);
  sceSdSetParam(SD_P_MMIX | 1, 0xf0c);
  sceSdSetParam(SD_P_MVOLL, 0x3fff);
  sceSdSetParam(SD_P_MVOLR, 0x3fff);
  sceSdSetParam(SD_P_EVOLL, 0);
  sceSdSetParam(SD_P_EVOLR, 0);
  sceSdSetParam(SD_P_EVOLL | 1, 0);
  sceSdSetParam(SD_P_EVOLR | 1, 0);
  sceSdSetParam(SD_P_AVOLL | 1, 0x7fff);
  sceSdSetParam(SD_P_AVOLR | 1, 0x7fff);
  sceSdSetCoreAttr(SD_C_SPDIF_MODE, 0);
  sceSdSetCoreAttr(SD_C_EFFECT_ENABLE, 0);
  snd_SetMasterVolume(0, 1024);
  snd_SetMasterVolume(1, 1024);
  snd_SetMasterVolume(2, 1024);
  snd_SetMasterVolume(3, 1024);
  snd_SetMasterVolume(4, 1024);
  snd_SetMasterVolume(5, 1024);
  snd_SetMasterVolume(6, 1024);
  snd_SetMasterVolume(8, 1024);
  gLockMasterTick = 0;
  return 0;
}

int snd_InstallSoundTick(void) {
  struct SysClock clock;
#if SND989_VERSION == 0x020604
#define param timer_thread_param
#else
  struct ThreadParam param;
  struct SemaParam sema;
  sema.attr = SA_THPRI;
  sema.initCount = 0;
  sema.maxCount = 1;
  gSTickSema = CreateSema(&sema);
#endif
  param.attr = TH_C;
  param.entry = snd_TimerLLIRQLoop;
  param.initPriority = gThreadPriority_TICK;
  param.stackSize = 0x800;
  param.option = 0;
  gTimerThreadID = CreateThread(&param);
  gKillTimer = 0;
  USec2SysClock(1000000, &clock);
  gTimerInfo.thread_id = &gTimerThreadID;
  gTimerInfo.compare = clock.low / 240;
  gTimerID = AllocHardTimer(1, 32, 1);
  SetTimerHandler(gTimerID, gTimerInfo.compare, (int (*)(void*))snd_TimerLLTick, &gTimerInfo);
  SetupHardTimer(gTimerID, 1, 0, 1);
  StartHardTimer(gTimerID);
  if (gTimerThreadID > 0)
    StartThread(gTimerThreadID, 0);
  return 0;
}

void snd_RemoveSoundTick(void) {
  remove_sound_tick();
}

int snd_StopSoundSystem(void) {
  gLockMasterTick = 1;
  snd_UnloadAllBanks();
  snd_UnloadAllMMD();
#if SND989_VERSION == 0x020608
  snd_CleanupVoiceAllocator();
#endif
  snd_CloseVAGStreaming();
  gAwaitingKeyOff[0] = 0xffffff;
  gAwaitingKeyOff[1] = 0xffffff;
  snd_StopPendingVoices();
#if SND989_VERSION == 0x020604
  remove_sound_tick();
#else
  snd_RemoveSoundTick();
#endif
  sceSdSetSwitch(SD_S_VMIXL, 0);
  sceSdSetSwitch(SD_S_VMIXR, 0);
  sceSdSetSwitch(SD_S_VMIXL | 1, 0);
  sceSdSetSwitch(SD_S_VMIXR | 1, 0);
  snd_SetMasterVolume(8, 0);
  snd_WaitDMADone();
#if SND989_VERSION == 0x020604
  gLockMasterTick = 0;
  gTickMode = -1;
#else
  gTickMode = -1;
  gLockMasterTick = 0;
#endif
  return 0;
}

int snd_GetTickRate(void) {
  return 240;
}

#if SND989_VERSION == 0x020604

int snd_TimerLLTick(struct SoundTimerInfo* timer) {
  iWakeupThread(*timer->thread_id);
  if (gKillTimer) {
    gKillTimer = 0;
    return 0;
  }
  return timer->compare;
}

#endif

int snd_TimerLLIRQLoop(void) {
  while (1) {
    SleepThread();
    snd_SoundTickHandler();
#if SND989_VERSION == 0x020608
    SignalSema(gSTickSema);
#endif
  }
}
