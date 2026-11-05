#include "sce/iop/989snd/tick.h"

#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/lfo.h"
#include "sce/iop/989snd/reverb.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/libsd/libsd.h"

extern u_int gAwaitingKeyOn[2];
extern u_int gKeyedOffVoices[2];
extern u_int gKeyedOnVoices[2];
void snd_UpdateHandlers(void);

#define VOICES_PER_CORE 24
#define SD_VOICE_ENVX 0x0500
#if SND989_VERSION == 0x020604
#define AUTO_FREE_RESET 1
#else
#define AUTO_FREE_RESET 4
#endif

static u_int sTick = 0;
int gLockMasterTick = 0;
static int sInHandler = 0;
extern signed char gAutoFreeState[48];

void snd_LockMasterTick(void) {
  gLockMasterTick++;
}

void snd_UnlockMasterTick(void) {
  if (gLockMasterTick) {
    gLockMasterTick--;
#if SND989_VERSION == 0x020604
    if (!gLockMasterTick && !gVoiceAllocatorInUse) {
      while (gTickAttentionNeeded) {
        gTickAttentionNeeded--;
        snd_SoundTickHandler();
      }
    }
#else
    if (gLockMasterTick < 0)
      snd_ShowError(36, 0, 0, 0, 0);
#endif
  }
}

int snd_SoundTickHandler(void) {
  short env;
  struct VoiceAttributes* walk;
  int core;
  int voice;

  if (sInHandler)
    return 0;
  sInHandler = 1;
  if (gVoiceAllocatorInUse || gLockMasterTick) {
    gTickAttentionNeeded++;
    sInHandler = 0;
    return 0;
  }
#if SND989_VERSION == 0x020604
  sTick++;
#else
  gTickAttentionNeeded++;
#endif
  for (walk = gPlayingListHead; walk; walk = walk->next) {
    core = walk->voice / VOICES_PER_CORE;
    voice = walk->voice % VOICES_PER_CORE;
    if (!(gAwaitingKeyOn[core] & (1 << voice))) {
      env = sceSdGetParam(core | (voice << 1) | SD_VOICE_ENVX);
      if (!env) {
        if (gKeyedOffVoices[core] & (1 << voice)) {
          snd_VoiceIsDone(walk->voice);
        } else if (gKeyedOnVoices[core] & (1 << voice)) {
          gAutoFreeState[walk->voice]--;
          if (!gAutoFreeState[walk->voice]) {
            snd_KeyOffVoice(walk->voice);
          } else if (gAutoFreeState[walk->voice] < 0) {
            gAutoFreeState[walk->voice] = AUTO_FREE_RESET;
          }
        }
      }
#if SND989_VERSION == 0x020608
      else {
        gAutoFreeState[walk->voice] = AUTO_FREE_RESET;
      }
#endif
    }
  }
#if SND989_VERSION == 0x020608
  while (gTickAttentionNeeded) {
    sTick++;
#endif
    snd_UpdateHandlers();
    snd_StopPendingVoices();
    snd_StartPendingVoices();
    snd_CheckVerb();
    snd_HandleLFOs();
#if SND989_VERSION == 0x020608
    gTickAttentionNeeded--;
  }
#endif
  sInHandler = 0;
  return 0;
}

unsigned int snd_GetTick(void) {
  return sTick;
}

void snd_SetTick(unsigned int value) {
  sTick = value;
}
