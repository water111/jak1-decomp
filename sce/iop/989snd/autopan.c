#include "sce/iop/989snd/autopan.h"

#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/playback.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/sysclib/stdlib.h"

void snd_AutoPan(u_int handle, int pan, int direction, int ticks, int delta_from) {
  int would_change;
  int change;
  int shortest = 0;
  struct GenericSoundHandler* handler;
  BasicEffect* effect;
  int new_effect = 0;
  snd_LockMasterTick();
  handler = snd_CheckHandlerStillActive(handle);
  if (!handler) {
    snd_UnlockMasterTick();
    return;
  }
  change = pan - handler->Current_Pan;
  if (!direction) {
    shortest = 1;
    direction = 1;
  }
  if (direction > 0 && change < 0)
    change += 360;
  if (direction < 0 && change > 0)
    change -= 360;
  if (shortest && abs(change) > 180) {
    direction = -direction;
    change -= 360;
  }
  if (delta_from == 4)
    ticks = ticks * abs(change) / 180;
  if (!ticks) {
    snd_RemoveEffect(handler, 4);
    snd_SetSoundVolPan(handle, 0x7fffffff, pan);
    snd_UnlockMasterTick();
    return;
  }
  if (!change) {
    snd_RemoveEffect(handler, 4);
    snd_UnlockMasterTick();
    return;
  }
  effect = (BasicEffect*)snd_FindEffect(handler, 4);
  if (effect) {
    effect->ec.flags |= 0x80000000;
    if (delta_from == 3) {
      would_change = pan - effect->destination;
      if (direction > 0 && would_change < 0)
        would_change += 360;
      if (direction < 0 && would_change > 0)
        would_change -= 360;
      if (!would_change) {
        snd_RemoveEffect(handler, 4);
        snd_SetSoundVolPan(handle, 0x7fffffff, pan);
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
      snd_SetSoundVolPan(handle, 0x7fffffff, pan);
      snd_UnlockMasterTick();
      return;
    }
    effect->ec.flags |= 4;
    effect->ec.proc = snd_AutoPanHandler;
    new_effect = 1;
  }
  if (abs(change) < ticks) {
    int interval = abs(ticks / change);
    effect->delta_time = interval;
    effect->ec.delta_type = 1;
    effect->ec.delta_counter = interval;
    if (change < 0)
      effect->delta_value = -1;
    else
      effect->delta_value = 1;
  } else {
    effect->delta_value = 32 * change / ticks;
    effect->ec.delta_type = 0;
    effect->ec.delta_counter = 0;
  }
  effect->destination = pan;
  if (new_effect)
    snd_AddEffectToHandler(handler, &effect->ec);
  else
    effect->ec.flags &= ~0x80000000;
  snd_UnlockMasterTick();
}

int snd_AutoPanHandler(BasicEffect* effect, struct GenericSoundHandler* handler) {
  short pan;
  short destination;
  if (abs(effect->delta_value) >= 2)
    pan = (32 * handler->Current_Pan + effect->delta_value) >> 5;
  else
    pan = handler->Current_Pan + effect->delta_value;

  if (pan >= 360 && effect->destination < handler->Current_Pan)
    destination = effect->destination + 360;
  else if (pan < 0 && effect->destination > handler->Current_Pan)
    destination = effect->destination - 360;
  else
    destination = effect->destination;

  if ((effect->delta_value > 0 && pan > destination && destination > handler->Current_Pan) ||
      (effect->delta_value < 0 && pan < destination && destination < handler->Current_Pan)) {
    pan = effect->destination;
  } else if (pan < 0) {
    pan += 360;
  } else if (pan >= 360) {
    pan -= 360;
  }
  snd_SetSoundVolPan(handler->OwnerID, 0x7fffffff, pan);
  if (pan == effect->destination) {
    snd_RemoveEffectFromHandler(handler, &effect->ec);
    return 0;
  }
  if (effect->ec.delta_type == 1)
    effect->ec.delta_counter = effect->delta_time;
  return 0;
}
