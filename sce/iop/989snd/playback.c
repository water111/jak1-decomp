#include "sce/iop/989snd/playback.h"

#include "sce/iop/989snd/ame.h"
#include "sce/iop/989snd/autopan.h"
#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/blocksound.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/loader_state.h"
#include "sce/iop/989snd/midi.h"
#include "sce/iop/989snd/midi_control.h"
#include "sce/iop/989snd/stream_sound.h"
#include "sce/iop/989snd/tick.h"

void snd_PauseVAGStream(u_int handle);
void snd_ContinueVAGStream(u_int handle);

u_int snd_PlaySoundVolPanPMPB(struct SoundBank* bank,
                              int index,
                              int volume,
                              int pan,
                              short pitch_modifier,
                              short bend) {
  struct Sound* sound;
  u_int result;
  if (bank == (struct SoundBank*)-1)
    bank = (struct SoundBank*)gBlockListHead;
  if (!bank)
    return 0;
  if (bank->data_id == 0x6b6c4253)
    return snd_PlaySFX((struct SFXBlock2*)bank, index, volume, pan, pitch_modifier, bend);
  if (index >= bank->sound_count || index < 0)
    return 0;
  sound = &bank->sounds[index];
  if (!sound->bank)
    return 0;
  switch (sound->type) {
    case 1:
    case 2:
    case 3:
      result = snd_PlayStreamSound(sound, volume, pan, pitch_modifier, bend);
      break;
    case 4:
      result = snd_PlayMIDISound((MIDISound*)sound, volume, pan, pitch_modifier, bend);
      break;
    case 5:
      result = snd_PlayAMESound((struct MIDISound*)sound, volume, pan, pitch_modifier, bend);
      break;
    default:
      result = 0;
  }
  return result;
}

void snd_StopSound(u_int handle) {
  struct GenericSoundHandler* handler;
  snd_LockMasterTick();
  handler = snd_CheckHandlerStillActive(handle);
  if (handler)
    snd_StopHandlerPtr(handler, 1, 0);
  snd_UnlockMasterTick();
}

u_int snd_SoundIsStillPlaying(u_int handle) {
  if (!handle)
    return 0;
  if (snd_CheckHandlerStillActive(handle))
    return handle;
  return 0;
}

int snd_IsSoundALooper(SoundBank* bank, int index) {
  Sound* sound;
  if (!bank)
    return 0;
  if (bank->data_id == 0x6b6c4253)
    return snd_DoesSFXLoop((SFXBlock2*)bank, index);
  if (index >= bank->sound_count || index < 0)
    return 0;
  sound = &bank->sounds[index];
  if (!sound->bank)
    return 0;
  switch (sound->type) {
    case 4:
      return ((MIDISound*)sound)->repeats == 0;
    case 5:
      return 1;
    case 3:
      return 1;
  }
  return 0;
}

void snd_SetVAGStreamVolPan(u_int handle, int volume, int pan);

#if SND989_VERSION == 0x020604
static __inline__ void set_sound_vol_pan(u_int handle, int volume, int pan)
#else
#define set_sound_vol_pan snd_SetSoundVolPan

void snd_SetSoundVolPan(u_int handle, int volume, int pan)
#endif
{
  int type = (handle >> 24) & 0x1f;
  snd_LockMasterTick();
  switch (type) {
    case 0:
      snd_SetStreamSoundVolumePan(handle, volume, pan);
      break;
    case 5:
      snd_SetSFXVolPan(handle, volume, pan);
      break;
    case 1:
      snd_SetMIDISoundVolumePan(handle, volume, pan);
      break;
    case 2:
      snd_SetAMESoundVolumePan(handle, volume, pan);
      break;
    case 4:
      snd_SetVAGStreamVolPan(handle, volume, pan);
      break;
  }
  snd_UnlockMasterTick();
}
#if SND989_VERSION == 0x020604
void snd_SetSoundVolPan(u_int handle, int volume, int pan) {
  set_sound_vol_pan(handle, volume, pan);
}
#endif

u_int snd_SetSoundParams(u_int handle,
                         u_int mask,
                         int volume,
                         int pan,
#if SND989_VERSION == 0x020604
                         short pitch_modifier,
                         short bend
#else
                         int pitch_modifier,
                         int bend
#endif
) {
  int p;
#if SND989_VERSION == 0x020608
  // Retain the full-width stack arguments in the original saved registers.
  register int saved_modifier __asm__("$19") = pitch_modifier;
  register int saved_bend __asm__("$21") = bend;
#endif
  if (!snd_CheckHandlerStillActive(handle))
    return 0;
  p = pan;
  if (!(mask & 1))
    volume = 0x7fffffff;
  if (!(mask & 2))
    p = -2;
  if (mask & 3)
    set_sound_vol_pan(handle, volume, p);
  if (mask & 4)
    snd_AutoPan(handle, pan, 0, 30, 4);
#if SND989_VERSION == 0x020608
  __asm__("" : : "r"(saved_modifier), "r"(saved_bend));
#endif
  if (mask & 8)
#if SND989_VERSION == 0x020604
    snd_SetSoundPitchModifier(handle, pitch_modifier);
#else
    snd_SetSoundPitchModifier(handle, saved_modifier);
#endif
  if (mask & 16)
#if SND989_VERSION == 0x020604
    snd_SetSoundPitchBend(handle, bend);
#else
    snd_SetSoundPitchBend(handle, saved_bend);
#endif
  return handle;
}

int snd_GetSoundOriginalPitch(SoundBank* bank, int index) {
  Sound* sound;
  if (!bank)
    return 0;
  if (bank->data_id == 0x6b6c4253)
    return 0x1e00;
  if (index >= bank->sound_count || index < 0)
    return 0;
  sound = &bank->sounds[index];
  if (!sound->bank)
    return 0;
  switch (sound->type) {
    case 1:
    case 2:
    case 3:
      return (sound->note << 7) + sound->fine;
    default:
      return 0;
  }
}

int snd_GetSoundCurrentPitch(u_int handle) {
  int type = (handle >> 24) & 0x1f;
  int result = 0;
  struct StreamSoundHandler* handler;
  snd_LockMasterTick();
  if (type == 0 || type == 5) {
    handler = (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
    if (handler)
      result = (handler->Current_Note << 7) + handler->Current_Fine;
  }
  snd_UnlockMasterTick();
  return result;
}

int snd_GetSoundPitchBend(u_int handle) {
  int type = (handle >> 24) & 0x1f;
  int result = 0;
  struct StreamSoundHandler* handler;
  snd_LockMasterTick();
  switch (type) {
    case 5:
      handler = (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
      if (handler)
        result = ((struct BlockSoundHandler*)handler)->App_PB;
      break;
    case 0:
      handler = (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
      if (handler)
        result = handler->Current_PB;
      break;
  }
  snd_UnlockMasterTick();
  return result;
}

void snd_SetSoundPitch(u_int handle, int pitch) {
  int type = (handle >> 24) & 0x1f;
  snd_LockMasterTick();
  switch (type) {
    case 0:
      snd_SetStreamSoundPitch(handle, pitch);
      break;
    case 5:
      snd_SetSFXPitch(handle, pitch);
      break;
    case 1:
      break;
  }
  snd_UnlockMasterTick();
}

void snd_SetSoundPitchBend(u_int handle, short bend) {
  int type = (handle >> 24) & 0x1f;
  snd_LockMasterTick();
  switch (type) {
    case 0:
      snd_SetStreamSoundPitchBend(handle, bend);
      break;
    case 5:
      snd_SetSFXPitchbend(handle, bend);
      break;
    case 1:
      break;
  }
  snd_UnlockMasterTick();
}

void snd_SetSoundPitchModifier(u_int handle, short modifier) {
  int type = (handle >> 24) & 0x1f;
  snd_LockMasterTick();
  switch (type) {
    case 0:
      snd_SetStreamSoundPitchModifier(handle, modifier);
      break;
    case 5:
      snd_SetSFXPitchModifier(handle, modifier);
      break;
    case 1:
      snd_SetMIDISoundPitchModifier(handle, modifier);
      break;
    case 2:
      snd_SetAMESoundPitchModifier(handle, modifier);
      break;
  }
  snd_UnlockMasterTick();
}

int snd_GetSoundPitchModifier(u_int handle) {
  int type = (handle >> 24) & 0x1f;
  snd_LockMasterTick();
  switch (type) {
    case 0:
    case 1:
    case 2:
      snd_CheckHandlerStillActive(handle);
      break;
    case 5:
      snd_CheckHandlerStillActive(handle);
      break;
    case 4:
      snd_CheckHandlerStillActive(handle);
      break;
  }
  snd_UnlockMasterTick();
  // Both versions perform the lookup but always return zero.
  return 0;
}

void snd_PauseSound(u_int handle) {
  int type = (handle >> 24) & 0x1f;
  struct GenericSoundHandler* handler;
  snd_LockMasterTick();
  switch (type) {
    case 0:
    case 1:
    case 2:
    case 5:
      handler = snd_CheckHandlerStillActive(handle);
      if (handler)
        snd_PauseHandlerPtr(handler, 1);
      break;
    case 4:
      snd_PauseVAGStream(handle);
      break;
  }
  snd_UnlockMasterTick();
}

void snd_ContinueSound(u_int handle) {
  int type = (handle >> 24) & 0x1f;
  struct GenericSoundHandler* handler;
  snd_LockMasterTick();
  switch (type) {
    case 0:
    case 1:
    case 2:
    case 5:
      handler = snd_CheckHandlerStillActive(handle);
      if (handler)
        snd_ContinueHandlerPtr(handler, 1);
      break;
    case 4:
      snd_ContinueVAGStream(handle);
      break;
  }
  snd_UnlockMasterTick();
}

void snd_StopAllSoundsInBank(SoundBank* bank, int silence) {
  int i;
  if (!bank)
    return;
  snd_LockMasterTick();
  if (bank->data_id == 0x6b6c4253) {
    SFXBlock2* block = (SFXBlock2*)bank;
    for (i = 0; i < block->sound_count; i++)
      snd_StopAllHandlersForSound((Sound*)&block->sounds[i], silence);
  } else {
    for (i = 0; i < bank->sound_count; i++)
      snd_StopAllHandlersForSound(&bank->sounds[i], silence);
  }
  snd_UnlockMasterTick();
}
