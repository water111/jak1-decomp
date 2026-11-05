#include "sce/iop/989snd/stream_sound.h"

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/libsd/sdmacro.h"

u_int snd_PlayStreamSound(struct Sound* sound, int volume, int pan, short modifier, short bend) {
  struct Tone* tones[32];
  struct StreamSoundHandler* handler = 0;
  int count, i, voice, core, core_voice, play_volume;
  int allocated = 0;
  if (sound->type == 2)
    snd_StopAllHandlersForSound(sound, 0);
  if (volume == 0x7fffffff)
    volume = 1024;
  play_volume = (volume * sound->volume) >> 10;
  if (play_volume > 127)
    play_volume = 127;
  if (pan == -1 || pan == -2)
    pan = sound->pan;
  count = snd_CollectTones(sound->bank, sound->program, sound->note, tones);
  snd_LockVoiceAllocator(1);
  for (i = 0; i < count; ++i) {
    voice = snd_AllocateVoice(sound->volume_group, tones[i]->priority);
    if (voice != -1) {
      core = voice / SND_VOICES_PER_CORE;
      core_voice = voice % SND_VOICES_PER_CORE;
      if (!allocated) {
        handler = snd_GetFreeStreamHandler();
        handler->SH.Sound = sound;
        handler->SH.Original_Vol = sound->volume;
        handler->SH.Original_Pan = sound->pan;
        handler->SH.Current_Vol = play_volume;
        handler->SH.Current_Pan = pan;
        handler->SH.Current_PM = modifier;
        handler->Current_Note = sound->note;
        handler->Current_Fine = sound->fine;
        handler->Current_PB = bend;
        handler->payload[1] = 0;
        handler->SH.Effects = 0;
        handler->SH.flags = 0;
        handler->SH.VolGroup = sound->volume_group;
        allocated = 1;
      }
      handler->SH.Voices[core] |= 1 << core_voice;
      gChannelStatus[voice].owner = &handler->SH;
      gChannelStatus[voice].owner_proc = snd_StreamSoundVoiceOwnerProc;
      gChannelStatus[voice].tone = tones[i];
      gChannelStatus[voice].start_note = sound->note;
      gChannelStatus[voice].start_fine = sound->fine;
      gChannelStatus[voice].pitch_bend = bend;
      gChannelStatus[voice].pitch_modifier = modifier;
      gChannelStatus[voice].priority = tones[i]->priority;
      gChannelStatus[voice].group = sound->volume_group;
      {
        struct VoiceAttributes* attributes = &gChannelStatus[voice];
        attributes->volume_right = 0;
      }
      gChannelStatus[voice].volume_left = 0;
      snd_MakeVolumes(play_volume, pan, sound->bank->programs[sound->program].volume,
                      sound->bank->programs[sound->program].pan, tones[i]->volume, tones[i]->pan,
                      (VolPair*)&gChannelStatus[voice].volume_left);
      snd_StartVAGVoice(voice);
    }
  }
  snd_UnlockVoiceAllocator();
  {
    u_int result = 0;
    if (handler) {
      snd_ActivateHandler(&handler->SH);
      result = handler->SH.OwnerID;
    }
    return result;
  }
}

void snd_SetStreamSoundVolumePan(u_int handle, int volume, int pan) {
  int voice = -1;
  struct StreamSoundHandler* handler =
      (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
  int locked;
  VolPair output;
  if (!handler)
    return;
  if (volume < 0)
    handler->SH.Current_Vol = -volume;
  else if (volume != 0x7fffffff)
    handler->SH.Current_Vol = (volume * ((Sound*)handler->SH.Sound)->volume) >> 10;
  if (handler->SH.Current_Vol > 127)
    handler->SH.Current_Vol = 127;
  if (pan == -1)
    handler->SH.Current_Pan = ((Sound*)handler->SH.Sound)->pan;
  else if (pan != -2)
    handler->SH.Current_Pan = pan;
  locked = snd_LockVoiceAllocator(1);
  while ((voice = snd_GetNextHandlerVoice(&handler->SH, voice + 1)) != -1) {
    snd_MakeVolumes(
        handler->SH.Current_Vol, handler->SH.Current_Pan,
        ((Sound*)handler->SH.Sound)->bank->programs[((Sound*)handler->SH.Sound)->program].volume,
        ((Sound*)handler->SH.Sound)->bank->programs[((Sound*)handler->SH.Sound)->program].pan,
        gChannelStatus[voice].tone->volume, gChannelStatus[voice].tone->pan,
        (VolPair*)&gChannelStatus[voice].volume_left);
    output.left = (u_short)snd_AdjustVolToGroup(gChannelStatus[voice].volume_left,
                                                ((Sound*)handler->SH.Sound)->volume_group) >>
                  1;
    output.right = (u_short)snd_AdjustVolToGroup(gChannelStatus[voice].volume_right,
                                                 ((Sound*)handler->SH.Sound)->volume_group) >>
                   1;
    sceSdSetParam((voice / SND_VOICES_PER_CORE) | ((voice % SND_VOICES_PER_CORE) << 1) | SD_VP_VOLL,
                  output.left);
    sceSdSetParam((voice / SND_VOICES_PER_CORE) | ((voice % SND_VOICES_PER_CORE) << 1) | SD_VP_VOLR,
                  output.right);
  }
  if (locked)
    snd_UnlockVoiceAllocator();
}

void snd_SetStreamSoundPitch(u_int handle, int pitch) {
  struct StreamSoundHandler* handler =
      (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    handler->Current_Note = pitch / 128;
    handler->Current_Fine = pitch % 128;
    snd_UpdateStreamSoundPitch(handler);
  }
}

void snd_SetStreamSoundPitchBend(u_int handle, short bend) {
  struct StreamSoundHandler* handler =
      (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    handler->Current_PB = bend;
    snd_UpdateStreamSoundPitch(handler);
  }
}

void snd_SetStreamSoundPitchModifier(u_int handle, short modifier) {
  struct StreamSoundHandler* handler =
      (struct StreamSoundHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    handler->SH.Current_PM = modifier;
    snd_UpdateStreamSoundPitch(handler);
  }
}

void snd_UpdateStreamSoundPitch(struct StreamSoundHandler* handler) {
  int voice = -1;
  int note, fine;
  int locked = snd_LockVoiceAllocator(1);
  while ((voice = snd_GetNextHandlerVoice(&handler->SH, voice + 1)) != -1) {
    gChannelStatus[voice].pitch_bend = handler->Current_PB;
    gChannelStatus[voice].pitch_modifier = handler->SH.Current_PM;
    snd_PitchBendTone(gChannelStatus[voice].tone, handler->Current_PB, handler->SH.Current_PM,
                      handler->Current_Note, handler->Current_Fine, &note, &fine);
    sceSdSetParam(
        (voice / SND_VOICES_PER_CORE) | ((voice % SND_VOICES_PER_CORE) << 1) | SD_VP_PITCH,
        PS1Note2Pitch(gChannelStatus[voice].tone->center_note,
                      gChannelStatus[voice].tone->center_fine, note, fine));
  }
  if (locked)
    snd_UnlockVoiceAllocator();
}

void snd_StreamSoundVoiceOwnerProc(int voice, struct GenericSoundHandler* handler, int event) {
  int core = voice / SND_VOICES_PER_CORE;
  int core_voice = voice % SND_VOICES_PER_CORE;
  handler->Voices[core] &= ~(1 << core_voice);
  if (!handler->Voices[0] && !handler->Voices[1])
    snd_DeactivateHandler(handler, 0);
}

int snd_CollectTones(SoundBank* bank, int program, int note, struct Tone** output) {
  int count = 0;
  int i;
  int tone_count;
  struct Tone* tone;
  if (program >= bank->program_count) {
#if SND989_VERSION == 0x020608
    snd_ShowError(78, 0, 0, 0, 0);
#endif
    return 0;
  }
  tone_count = bank->programs[program].tone_count;
  tone = bank->programs[program].tones;
  for (i = 0; i < tone_count && i < 32; i++) {
    if (note >= tone[i].map_low && note <= tone[i].map_high) {
      output[count] = &tone[i];
      count++;
    }
  }
  return count;
}
