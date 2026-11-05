#include "sce/iop/989snd/reverb.h"

#include "sce/iop/989snd/autoverb.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/stdio/stdio.h"

int gReverbType[2] = {-1, -1};
u_int gRevRamStart[2] = {0, 0};
#if SND989_VERSION == 0x020608
int gRevRamPreAllocType[2] = {-1, -1};
#endif
short gReverbDepthL[2] = {0, 0};
short gReverbDepthR[2] = {0, 0};
int gAwaitingReverbChange[2] = {0, 0};
int gAwaitingReverbChangeTick[2] = {0, 0};
u_int gRevRamUse[10] = {128, 9920, 8000, 18496, 28640, 44512, 63168, 98368, 98368, 15360};

#if SND989_VERSION == 0x020608

void snd_PreAllocReverbWorkArea(int cores, int type) {
  if (cores & 1)
    snd_PreAllocReverbWorkArea_L(0, type);
  if (cores & 2)
    snd_PreAllocReverbWorkArea_L(1, type);
}

void snd_PreAllocReverbWorkArea_L(int core, int type) {
  int disabled;
  if (gReverbType[core] > 0)
    snd_SetReverbType_L(core, 0, 0, 0);
  if (gRevRamPreAllocType[core] > 0) {
    disabled = CpuDisableIntr();
    snd_SRAMFree(gRevRamStart[core], gRevRamUse[gRevRamPreAllocType[core]]);
    if (!disabled)
      CpuEnableIntr();
  }
  gRevRamPreAllocType[core] = type;
  if (type > 0) {
    disabled = CpuDisableIntr();
    gRevRamStart[core] = snd_SRAMMallocRev(gRevRamUse[type]);
    if (!gRevRamStart[core]) {
      if (!disabled)
        CpuEnableIntr();
      gReverbType[core] = -1;
      gRevRamPreAllocType[core] = -1;
      snd_ShowError(85, 0, 0, 0, 0);
    } else if (!disabled) {
      CpuEnableIntr();
    }
  }
}

void snd_SetReverbEx(int cores, int type, int depth, int delay, int feedback) {
  if (cores & 1) {
    snd_SetReverbType_L(0, type, delay, feedback);
    snd_SetReverbDepth_L(0, depth, depth);
  }
  if (cores & 2) {
    snd_SetReverbType_L(1, type, delay, feedback);
    snd_SetReverbDepth_L(1, depth, depth);
  }
}

#endif

void snd_SetReverbType(int cores, int type) {
  if (cores & 1) {
#if SND989_VERSION == 0x020604
    snd_SetReverbType_L(0, type);
#else
    snd_SetReverbType_L(0, type, 0, 0);
#endif
  }
  if (cores & 2) {
#if SND989_VERSION == 0x020604
    snd_SetReverbType_L(1, type);
#else
    snd_SetReverbType_L(1, type, 0, 0);
#endif
  }
}

#if SND989_VERSION == 0x020604
int snd_SetReverbType_L(int core, int type) {
#else
int snd_SetReverbType_L(int core, int type, int delay, int feedback) {
#endif
  SdEffectAttr effect;
  int channel;
  int disabled;
  int warned;
  u_int end;
  if (gReverbType[core] == type) {
#if SND989_VERSION == 0x020608
    effect.core = core;
    effect.mode = type;
    effect.depth_left = 0;
    effect.depth_right = 0;
    effect.delay = delay;
    effect.feedback = feedback;
    sceSdSetEffectAttr(core, &effect);
#endif
    return 0;
  }
#if SND989_VERSION == 0x020608
  if (gRevRamPreAllocType[core] > 0 && gRevRamUse[gRevRamPreAllocType[core]] < gRevRamUse[type]) {
    snd_ShowError(87, 0, 0, 0, 0);
    return -13;
  }
  if (gReverbType[core] > 0)
#endif
  {
    sceSdSetCoreAttr(SD_C_EFFECT_ENABLE | core, 0);
    sceSdSetParam(SD_P_EVOLL | core, 0);
    sceSdSetParam(SD_P_EVOLR | core, 0);
  }
  if (gReverbType[core] > 0
#if SND989_VERSION == 0x020608
      && gRevRamPreAllocType[core] <= 0
#endif
  ) {
    disabled = CpuDisableIntr();
    snd_SRAMFree(gRevRamStart[core], gRevRamUse[gReverbType[core]]);
    if (!disabled)
      CpuEnableIntr();
  }
#if SND989_VERSION == 0x020608
  if (gRevRamPreAllocType[core] <= 0)
#endif
  {
    disabled = CpuDisableIntr();
    if (type != 0) {
      gRevRamStart[core] = snd_SRAMMallocRev(gRevRamUse[type]);
      if (!gRevRamStart[core]) {
        if (!disabled)
          CpuEnableIntr();
        gReverbType[core] = -1;
#if SND989_VERSION == 0x020608
        snd_ShowError(86, 0, 0, 0, 0);
#endif
        return -11;
      }
    }
    if (!disabled)
      CpuEnableIntr();
  }
  snd_KillAutoVerbForCore(core);
  if (type != 0) {
    end = gRevRamStart[core] - 1;
    end += gRevRamUse[type];
    sceSdSetAddr(SD_A_EEA | core, end);
  }
  effect.core = core;
  effect.mode = type;
  effect.depth_left = 0;
  effect.depth_right = 0;
#if SND989_VERSION == 0x020604
  effect.delay = 0;
  effect.feedback = 0;
#else
  effect.delay = delay;
  effect.feedback = feedback;
#endif
  sceSdSetEffectAttr(core, &effect);
  if (type != 0) {
    warned = 0;
    while ((channel = snd_GetFreeSPUDMA()) == -1) {
      if (!warned) {
#if SND989_VERSION == 0x020604
        printf("snd_BankTransfer: Waiting for a free DMA channel!\n");
#endif
        warned = 1;
      }
      for (; channel < 1000; channel++) {
      }
    }
    gAwaitingReverbChange[core] = channel + 1;
    gAwaitingReverbChangeTick[core] = 10;
    sceSdClearEffectWorkArea(core, channel, type);
  }
  gReverbType[core] = type;
  return 0;
}

void snd_SetReverbDepth(int cores, int left, int right) {
  if (cores & 1) {
    snd_KillAutoVerbForCore(0);
    snd_SetReverbDepth_L(0, left, right);
  }
  if (cores & 2) {
    snd_KillAutoVerbForCore(1);
    snd_SetReverbDepth_L(1, left, right);
  }
}

void snd_SetReverbDepth_L(int core, int left, int right) {
  gReverbDepthL[core] = left;
  gReverbDepthR[core] = right;
  if (!gAwaitingReverbChange[core]) {
    sceSdSetParam(SD_P_EVOLL | core, gReverbDepthL[core]);
    sceSdSetParam(SD_P_EVOLR | core, gReverbDepthR[core]);
  }
}

void snd_CheckVerb(void) {
  if (gAwaitingReverbChangeTick[0]) {
    gAwaitingReverbChangeTick[0]--;
    if (!gAwaitingReverbChangeTick[0]) {
      sceSdSetParam(SD_P_EVOLL | 0, gReverbDepthL[0]);
      sceSdSetParam(SD_P_EVOLR | 0, gReverbDepthR[0]);
      sceSdSetCoreAttr(SD_C_EFFECT_ENABLE | 0, 1);
      snd_FreeSPUDMA(gAwaitingReverbChange[0] - 1);
      gAwaitingReverbChange[0] = 0;
    }
  }
  if (gAwaitingReverbChangeTick[1]) {
    gAwaitingReverbChangeTick[1]--;
    if (!gAwaitingReverbChangeTick[1]) {
      sceSdSetParam(SD_P_EVOLL | 1, gReverbDepthL[1]);
      sceSdSetParam(SD_P_EVOLR | 1, gReverbDepthR[1]);
      sceSdSetCoreAttr(SD_C_EFFECT_ENABLE | 1, 1);
      snd_FreeSPUDMA(gAwaitingReverbChange[1] - 1);
      gAwaitingReverbChange[1] = 0;
    }
  }
  snd_AutoVerbHandler();
}
