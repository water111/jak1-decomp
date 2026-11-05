#include "sce/iop/989snd/autoverb.h"

#include "sce/iop/989snd/effects.h"
#include "sce/iop/989snd/reverb.h"
#include "sce/iop/sysclib/stdlib.h"

extern BasicEffect gVerbFX[2][2];
#if SND989_VERSION == 0x020604
static inline void dispatch_reverb(int cores, int depth, int delta_time, int channel_flags) {
  if (cores & 1)
    snd_AutoReverb_L(0, depth, delta_time, channel_flags);
  if (cores & 2)
    snd_AutoReverb_L(1, depth, delta_time, channel_flags);
}
#else
#define dispatch_reverb snd_AutoReverb
#endif

void snd_AutoReverb(int core, int depth, int delta_time, int channel_flags) {
  if ((core & 1) != 0) {
    snd_AutoReverb_L(0, depth, delta_time, channel_flags);
  }

  if ((core & 2) != 0) {
    snd_AutoReverb_L(1, depth, delta_time, channel_flags);
  }
}

void snd_AutoReverb_L(int core, int depth, int delta_time, int channel_flags) {
  int depthchange;
  short original_depth;
  int effect_index;

  if (channel_flags == 3) {
    dispatch_reverb(core, depth, delta_time, 1);
    dispatch_reverb(core, depth, delta_time, 2);
    return;
  }

  if (channel_flags == 1) {
    effect_index = 0;
    original_depth = gReverbDepthL[core];
  } else {
    effect_index = 1;
    original_depth = gReverbDepthR[core];
  }

  gVerbFX[core][effect_index].delta_value = 0;
  if (delta_time == 0) {
    if (channel_flags == 1) {
      snd_SetReverbDepth_L(core, depth, gReverbDepthR[core]);
    } else {
      snd_SetReverbDepth_L(core, gReverbDepthL[core], depth);
    }
  } else {
    depthchange = depth - original_depth;
    if (depthchange == 0) {
      return;
    }

    gVerbFX[core][effect_index].destination = depth;
    if (abs(depthchange) < delta_time) {
      gVerbFX[core][effect_index].delta_time = abs(delta_time / depthchange);
      gVerbFX[core][effect_index].ec.delta_type = 1;
      gVerbFX[core][effect_index].ec.delta_counter = gVerbFX[core][effect_index].delta_time;
      gVerbFX[core][effect_index].delta_value = (depth >= original_depth) ? 1 : -1;
    } else {
      gVerbFX[core][effect_index].ec.delta_type = 0;
      gVerbFX[core][effect_index].ec.delta_counter = 0;
      gVerbFX[core][effect_index].delta_value = depthchange / delta_time;
    }
  }
}

void snd_KillAutoVerbForCore(int core) {
  gVerbFX[core][0].delta_value = 0;
  gVerbFX[core][1].delta_value = 0;
}

void snd_AutoVerbHandler() {
  int core;
  int newdepth;

  for (core = 0; core < 2; ++core) {
    if (gVerbFX[core][0].delta_value) {
      newdepth = gReverbDepthL[core] + gVerbFX[core][0].delta_value;

      if ((gVerbFX[core][0].delta_value > 0 && gVerbFX[core][0].destination < newdepth) ||
          (gVerbFX[core][0].delta_value < 0 && newdepth < gVerbFX[core][0].destination)) {
        newdepth = gVerbFX[core][0].destination;
      }

      snd_SetReverbDepth_L(core, newdepth, gReverbDepthR[core]);

      if (newdepth == gVerbFX[core][0].destination)
        gVerbFX[core][0].delta_value = 0;
    }

    if (gVerbFX[core][1].delta_value) {
      newdepth = gReverbDepthR[core] + gVerbFX[core][1].delta_value;

      if ((gVerbFX[core][1].delta_value > 0 && gVerbFX[core][1].destination < newdepth) ||
          (gVerbFX[core][1].delta_value < 0 && newdepth < gVerbFX[core][1].destination)) {
        newdepth = gVerbFX[core][1].destination;
      }

      snd_SetReverbDepth_L(core, gReverbDepthL[core], newdepth);

      if (newdepth == gVerbFX[core][1].destination)
        gVerbFX[core][1].delta_value = 0;
    }
  }
}
