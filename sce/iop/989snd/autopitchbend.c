#include "sce/iop/989snd/autopitchbend.h"

#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/playback.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/sysclib/stdlib.h"

void snd_AutoPitchBend(u_int handle, int pitch, int ticks, int delta_from) {
  int would_change;
  int change;
  int current_pitch;
  struct GenericSoundHandler* handler;
  BasicEffect* effect;
  int new_effect = 0;
  snd_LockMasterTick();
  handler = snd_CheckHandlerStillActive(handle);
  if (!handler) {
    snd_UnlockMasterTick();
    return;
  }
  current_pitch = snd_GetSoundPitchBend(handle);
  change = pitch - current_pitch;
  if (!ticks || !change) {
    snd_RemoveEffect(handler, 16);
    snd_SetSoundPitchBend(handle, pitch);
    snd_UnlockMasterTick();
    return;
  }
  effect = (BasicEffect*)snd_FindEffect(handler, 16);
  if (effect) {
    effect->ec.flags |= 0x80000000;
    if (delta_from == 3) {
      would_change = pitch - effect->destination;
      if (!would_change) {
        snd_RemoveEffect(handler, 16);
        snd_SetSoundPitchBend(handle, pitch);
        snd_UnlockMasterTick();
        return;
      }
      ticks = abs(change * ticks / would_change);
      if (ticks <= 0)
        ticks = 1;
    }
  } else {
    effect = snd_GetFreeBasicEffect();
    if (!effect) {
      snd_SetSoundPitchBend(handle, pitch);
      snd_UnlockMasterTick();
      return;
    }
    effect->ec.flags |= 16;
    effect->ec.proc = snd_AutoPitchBendHandler;
    new_effect = 1;
  }
  if (abs(change) < ticks) {
    int interval = abs(ticks / change);
    effect->delta_time = interval;
    effect->ec.delta_type = 1;
    effect->ec.delta_counter = interval;
    if (pitch >= current_pitch)
      effect->delta_value = 1;
    else
      effect->delta_value = -1;
  } else {
    effect->delta_value = change / ticks;
    effect->ec.delta_type = 0;
    effect->ec.delta_counter = 0;
  }
  effect->destination = pitch;
  if (new_effect)
    snd_AddEffectToHandler(handler, &effect->ec);
  else
    effect->ec.flags &= ~0x80000000;
  snd_UnlockMasterTick();
}

int snd_AutoPitchBendHandler(BasicEffect* effect, struct GenericSoundHandler* handler) {
  int pitch = snd_GetSoundPitchBend(handler->OwnerID) + effect->delta_value;
  if ((effect->delta_value > 0 && pitch > effect->destination) ||
      (effect->delta_value < 0 && pitch < effect->destination))
    pitch = effect->destination;
  snd_SetSoundPitchBend(handler->OwnerID, pitch);
  if (pitch == effect->destination) {
    snd_RemoveEffectFromHandler(handler, &effect->ec);
    return 0;
  }
  if (effect->ec.delta_type == 1)
    effect->ec.delta_counter = effect->delta_time;
  return 0;
}
