#include "sce/iop/989snd/autovol.h"

#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/playback.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/sysclib/stdlib.h"

int snd_AutoVolHandler(BasicEffect* effect, struct GenericSoundHandler* handler);

void snd_AutoVol(u_int handle, int vol, int delta_time, int delta_from) {
  int wouldhavechanged;
  int volchange;
  int stopit = 0;
  struct GenericSoundHandler* handler;
  BasicEffect* effect;
  int new_effect = 0;
  snd_LockMasterTick();
  handler = snd_CheckHandlerStillActive(handle);
  if (handler == 0) {
    snd_UnlockMasterTick();
    return;
  }

  if (vol == -4) {
    vol = 0;
    stopit = 1;
  } else {
    vol = (vol * handler->Original_Vol) >> 10;
    if (vol > 127) {
      vol = 127;
    }
  }

  if (delta_time == 0) {
    snd_RemoveEffect(handler, 2);
    if (stopit == 1) {
      snd_StopSound(handle);
    } else {
      snd_SetSoundVolPan(handle, -vol, -2);
    }

    snd_UnlockMasterTick();
    return;
  }

  volchange = vol - handler->Current_Vol;
  if (volchange == 0) {
    snd_RemoveEffect(handler, 2);
    snd_UnlockMasterTick();
    return;
  }

  effect = (BasicEffect*)snd_FindEffect(handler, 2);
  if (effect) {
    effect->ec.flags |= 0x80000000;
    if (delta_from == 3) {
      wouldhavechanged = vol - effect->destination;
      if (wouldhavechanged == 0) {
        snd_RemoveEffect(handler, 2);
        snd_SetSoundVolPan(handle, -vol, -2);
        snd_UnlockMasterTick();
        return;
      }

      delta_time = abs(volchange * delta_time / wouldhavechanged);
      if (delta_time <= 0) {
        delta_time = 1;
      }
    }
  } else {
    effect = snd_GetFreeBasicEffect();
    if (effect == 0) {
      snd_SetSoundVolPan(handle, -vol, -2);
      snd_UnlockMasterTick();
      return;
    }
    effect->ec.flags |= 2;
    effect->ec.proc = snd_AutoVolHandler;
    new_effect = 1;
  }

  if (abs(volchange) < delta_time) {
    effect->delta_time = abs(delta_time / volchange);
    effect->ec.delta_type = 1;
    effect->ec.delta_counter = effect->delta_time;
    effect->delta_value = vol < handler->Current_Vol ? -1 : 1;
  } else {
    effect->delta_value = 32 * volchange / delta_time;
    effect->ec.delta_type = 0;
    effect->ec.delta_counter = 0;
  }

  if (stopit != 1) {
    effect->destination = vol;
  } else {
    effect->destination = -4;
  }

  if (new_effect) {
    snd_AddEffectToHandler(handler, &effect->ec);
  } else {
    effect->ec.flags &= ~0x80000000;
  }

  snd_UnlockMasterTick();
}

int snd_AutoVolHandler(BasicEffect* effect, struct GenericSoundHandler* handler) {
  short newMvol;
  short stopit = 0;

  if (effect->destination == -4) {
    effect->destination = 0;
    stopit = 1;
  }

  if (abs(effect->delta_value) >= 2) {
    newMvol = (32 * handler->Current_Vol + effect->delta_value) >> 5;
  } else {
    newMvol = handler->Current_Vol + effect->delta_value;
  }

  if ((effect->delta_value > 0 && effect->destination < newMvol) ||
      (effect->delta_value < 0 && effect->destination > newMvol)) {
    newMvol = effect->destination;
  }

  snd_SetSoundVolPan(handler->OwnerID, -newMvol, -2);
  if (newMvol == effect->destination) {
    snd_RemoveEffectFromHandler(handler, &effect->ec);
    return stopit;
  }

  if (effect->ec.delta_type == 1) {
    effect->ec.delta_counter = effect->delta_time;
  }

  if (stopit) {
    effect->destination = -4;
  }

  return 0;
}
