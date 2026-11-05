#include "sce/iop/989snd/midi.h"

#include "sce/iop/989snd/ame.h"
#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/midi_control.h"
#include "sce/iop/989snd/stream_sound.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/libsd/libsd.h"

extern u_int gMicsPerTick;

u_int snd_PlayMIDISound(struct MIDISound* sound, int volume, int pan, short modifier, short bend) {
  MIDIBlockHeader* stream = snd_GetRealMIDIDataStream(sound);
  struct MIDIHandler* handler;
  if (!stream || !stream->bank)
    return 0;
  handler = snd_GetFreeMIDIHandler();
  if (!handler)
    return -1;
  if (volume == 0x7fffffff)
    volume = 1024;
  handler->SH.Current_Vol = (volume * sound->volume) >> 10;
  if (handler->SH.Current_Vol > 127)
    handler->SH.Current_Vol = 127;
  if (pan == -1 || pan == -2)
    handler->SH.Current_Pan = sound->pan;
  else
    handler->SH.Current_Pan = pan;
  handler->SH.Current_PM = modifier;
  handler->repeats = sound->repeats;
  handler->SH.Sound = sound;
  handler->SH.Original_Vol = sound->volume;
  handler->SH.Original_Pan = sound->pan;
  handler->SH.Effects = 0;
  handler->SH.flags = 0;
  handler->SH.VolGroup = sound->volume_group;
  snd_SetupMIDIStreamHandler(handler, stream);
  snd_ActivateHandler(&handler->SH);
  return handler->SH.OwnerID;
}

void snd_SetupMIDIStreamHandler(struct MIDIHandler* handler, MIDIBlockHeader* stream) {
  handler->bank = stream->bank;
  handler->play_position = handler->data_start = stream->data_start;
  handler->tempo = stream->tempo;
  handler->tick_error = 0;
  handler->ppq = stream->ppq;
  snd_ResetControllers(handler);
  handler->midi_flags |= 1;
}

MIDIBlockHeader* snd_GetRealMIDIDataStream(struct MIDISound* sound) {
  MIDIBlockHeader* stream = sound->midi;
  if (!stream) {
#if SND989_VERSION == 0x020608
    snd_ShowError(28, 0, 0, 0, 0);
#endif
    return 0;
  }
  if (stream->data_id == SND_MIDI_ID)
    return stream;
  if (((MultiMIDIBlockHeader*)stream)->block_count - 1 < sound->index) {
#if SND989_VERSION == 0x020608
    snd_ShowError(29, 0, 0, 0, 0);
#endif
    return 0;
  }
  return ((MultiMIDIBlockHeader*)stream)->blocks[sound->index];
}

void snd_MuteMIDIChannel(struct MIDIHandler* handler, int channel) {
  handler->mute_state |= 1 << channel;
}

void snd_UnmuteMIDIChannel(struct MIDIHandler* handler, int channel) {
  handler->mute_state &= ~(1 << channel);
}

int snd_ProcessMIDITick(struct MIDIHandler* stream) {
  static int in_function = 0;
  int used_bytes;
  int stream_done;
  SoundBank* bank;

  if (in_function != 0) {
    return 0;
  }
  in_function = 1;

  if ((stream->midi_flags & 1) != 0) {
    stream->pulses_per_tick = 1000 * gMicsPerTick / (stream->tempo / (stream->ppq / 10u));
    stream->running_status = 0;
    stream->tick_delta = 100 * ReadVarLen((u_char*)stream->play_position, &used_bytes);
    stream->tick_countdown =
        (stream->tempo * (stream->tick_delta / 100) / stream->ppq + (gMicsPerTick - 1)) /
        gMicsPerTick;
    stream->play_position += used_bytes;
    stream->midi_flags &= ~1;
    stream->tick_error = 0;
  } else {
    stream->tick_countdown--;
  }
  stream_done = 0;

  while (stream_done == 0 && stream->tick_countdown == 0) {
    if (*stream->play_position < 0) {
      stream->running_status = *stream->play_position;
      stream->play_position++;
    }

    if (stream->running_status == 0xFF) {
      switch (*stream->play_position) {
        case 0x2f:
          stream->repeats--;
          if (stream->repeats != 0) {
            stream->play_position = stream->data_start;
            if (stream->repeats < 0) {
              stream->repeats = 0;
            }
          } else {
            stream_done = 1;
          }
          break;
        case 0x51:
          stream->play_position += 2;
          stream->tempo = ((u_char)stream->play_position[0] << 16) |
                          ((u_char)stream->play_position[1] << 8) |
                          (u_char)stream->play_position[2];
          stream->pulses_per_tick = 1000 * gMicsPerTick / (stream->tempo / (stream->ppq / 10u));
          stream->play_position += 3;
          break;
      }
    } else if (stream->running_status == 0xf0) {
      if (*stream->play_position == 0x75) {
        stream->play_position++;
        stream->play_position = snd_AMEFunction(stream, (u_char*)stream->play_position);
        if (!stream->play_position) {
          stream_done = 1;
        } else {
          stream->play_position++;
        }
      } else {
        while (*(u_char*)stream->play_position != 0xf7) {
          stream->play_position++;
        }
        stream->play_position++;
      }
    } else {
      switch (stream->running_status & 0xF0) {
        case 0x80:
          snd_MIDINoteOff(stream);
          stream->play_position += 2;
          break;
        case 0x90:
          if (stream->play_position[1] == 0) {
            snd_MIDINoteOff(stream);
          } else if (((stream->mute_state >> (stream->running_status & 0xF)) & 1) == 0) {
            snd_MIDINoteOn(stream);
          }
          stream->play_position += 2;
          break;
        case 0xD0:
          snd_MIDINoteOff(stream);
          stream->play_position += 1;
          break;
        case 0xE0:
          snd_PitchBend(stream);
          stream->play_position += 2;
          break;
        case 0xB0:
          switch (*stream->play_position) {
            case 0:
              bank = snd_FindBankByNum(stream->play_position[1]);
              if (!bank) {
                stream_done = 1;
              } else {
                stream->bank = bank;
              }
              break;
            case 0x7:
              stream->channel_volume[stream->running_status & 0xf] = stream->play_position[1];
              break;
            case 0xA:
              stream->channel_pan[stream->running_status & 0xf] =
                  snd_MIDITo360Pan(stream->play_position[1]);
              break;
            case 0x40:
              if (stream->play_position[1] >= 64) {
                stream->damper_state |= 1 << (stream->running_status & 0xf);
              } else {
                snd_ReleaseDamper(stream);
              }
              break;
          }
          stream->play_position += 2;
          break;
        case 0xC0:
          stream->channel_program[stream->running_status & 0xf] = stream->play_position[0];
          stream->play_position += 1;
          break;
        default:
          break;
      }
    }

    if (!stream_done) {
      stream->tick_delta =
          100 * ReadVarLen((u_char*)stream->play_position, &used_bytes) + stream->tick_error;
      if (stream->tick_delta < 0 || stream->tick_delta < stream->pulses_per_tick / 2) {
        stream->tick_error = stream->tick_delta;
        stream->tick_delta = 0;
      }

      if (stream->tick_delta) {
        stream->tick_countdown =
            (stream->tempo * (stream->tick_delta / 100) / stream->ppq + (gMicsPerTick - 1)) /
            gMicsPerTick;
        stream->tick_error = stream->tick_delta - stream->pulses_per_tick * stream->tick_countdown;
      }

      stream->play_position += used_bytes;
    }
  }

  in_function = 0;
  return stream_done;
}

u_int ReadVarLen(u_char* buffer, int* used_bytes) {
  u_int value = *buffer;
  int used = 1;
  if (value & 128) {
    value &= 127;
    do {
      ++buffer;
      ++used;
      value = (value << 7) + (*buffer & 127);
    } while (*buffer & 128);
  }
  *used_bytes = used;
  return value;
}

void snd_MIDINoteOn(struct MIDIHandler* handler) {
  struct Tone* tones[32];
  int count, i, voice;
  int channel = handler->running_status & 15;
  int program = handler->channel_program[channel];
  signed char note = handler->play_position[0];
  signed char volume = handler->play_position[1];
  int pan, core, core_voice;
  count = snd_CollectTones(handler->bank, program, note, tones);
  if (!snd_LockVoiceAllocator(1))
    return;
  for (i = 0; i < count; ++i) {
    voice = snd_AllocateVoice(((struct Sound*)handler->SH.Sound)->volume_group, tones[i]->priority);
    if (voice != -1) {
      core = voice / SND_VOICES_PER_CORE;
      core_voice = voice % SND_VOICES_PER_CORE;
      handler->SH.Voices[core] |= 1 << core_voice;
      gChannelStatus[voice].owner = &handler->SH;
      gChannelStatus[voice].owner_proc = snd_MIDISoundOwnerProc;
      gChannelStatus[voice].tone = tones[i];
      gChannelStatus[voice].start_note = note;
      gChannelStatus[voice].start_fine = 0;
      gChannelStatus[voice].priority = tones[i]->priority;
      gChannelStatus[voice].group = ((struct Sound*)handler->SH.Sound)->volume_group;
      {
        struct VoiceAttributes* attributes = &gChannelStatus[voice];
        attributes->volume_right = 0;
      }
      gChannelStatus[voice].volume_left = 0;
      pan = handler->channel_pan[channel] + handler->SH.Current_Pan;
      if (pan >= 360)
        pan -= 360;
      snd_MakeVolumesB(handler->SH.Current_Vol, volume * handler->channel_volume[channel] / 127,
                       pan, handler->bank->programs[program].volume,
                       handler->bank->programs[program].pan, tones[i]->volume, tones[i]->pan,
                       (VolPair*)&gChannelStatus[voice].volume_left);
      gChannelStatus[voice].midi_channel = channel;
      gChannelStatus[voice].pitch_bend = handler->channel_pitch_bend[channel];
      gChannelStatus[voice].pitch_modifier = handler->SH.Current_PM;
      gChannelStatus[voice].velocity = volume;
      gChannelStatus[voice].program = &handler->bank->programs[program];
      gChannelStatus[voice].midi_should_be_off = 0;
      snd_StartVAGVoice(voice);
    }
  }
  snd_UnlockVoiceAllocator();
}

void snd_MIDINoteOff(struct MIDIHandler* handler) {
  struct VoiceAttributes* voice;
  int channel = handler->running_status & 15;
  signed char note = handler->play_position[0];
  if (!snd_LockVoiceAllocator(1))
    return;
  for (voice = gPlayingListHead; voice; voice = voice->next) {
    if (voice->owner == &handler->SH && voice->midi_channel == channel &&
        voice->start_note == note) {
      if (!((handler->damper_state >> channel) & 1))
        snd_KeyOffVoice(voice->voice);
      else
        voice->midi_should_be_off = 1;
    }
  }
  snd_UnlockVoiceAllocator();
}

void snd_ReleaseDamper(struct MIDIHandler* handler) {
  struct VoiceAttributes* voice;
  int channel = handler->running_status & 15;
  handler->damper_state &= ~(1 << channel);
  snd_LockVoiceAllocator(1);
  for (voice = gPlayingListHead; voice; voice = voice->next) {
    if (voice->owner == &handler->SH && voice->midi_channel == channel &&
        voice->midi_should_be_off == 1)
      snd_KeyOffVoice(voice->voice);
  }
  snd_UnlockVoiceAllocator();
}

void snd_ResetControllers(struct MIDIHandler* handler) {
  int i;
  handler->mute_state = 0;
  for (i = 0; i < 16; ++i) {
    handler->channel_program[i] = 0;
    handler->channel_volume[i] = 127;
    handler->channel_pan[i] = 0;
    handler->channel_pitch_bend[i] = 0;
  }
  handler->damper_state = 0;
}

void snd_PitchBend(struct MIDIHandler* handler) {
  struct VoiceAttributes* voice;
  int note, fine;
  int core, core_voice, channel;
  u_int value = (handler->play_position[0] & 127) | ((handler->play_position[1] & 127) << 7);
  value = 65535 * value / 16383;
  channel = handler->running_status & 15;
  handler->channel_pitch_bend[channel] = (int)value - 32768;
  snd_LockVoiceAllocator(1);
  for (voice = gPlayingListHead; voice; voice = voice->next) {
    if (voice->owner == &handler->SH && voice->midi_channel == channel) {
      voice->pitch_bend = handler->channel_pitch_bend[channel];
      core = voice->voice / SND_VOICES_PER_CORE;
      core_voice = voice->voice % SND_VOICES_PER_CORE;
      snd_PitchBendTone(voice->tone, voice->pitch_bend, voice->pitch_modifier, voice->start_note,
                        voice->start_fine, &note, &fine);
      sceSdSetParam(core | (core_voice << 1) | SD_VP_PITCH,
                    PS1Note2Pitch(voice->tone->center_note, voice->tone->center_fine, note, fine));
    }
  }
  snd_UnlockVoiceAllocator();
}

void snd_SetMIDISoundVolumePan(u_int handle, int volume, int pan) {
  struct MIDIHandler* handler = (struct MIDIHandler*)snd_CheckHandlerStillActive(handle);
  if (handler)
    snd_SetMIDIHandlerVolumePan(handler, volume, pan);
}

void snd_SetMIDIHandlerVolumePan(struct MIDIHandler* handler, int volume, int pan) {
  struct VoiceAttributes* voice;
  VolPair output;
  int channel_pan;
  snd_LockVoiceAllocator(1);
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
  for (voice = gPlayingListHead; voice; voice = voice->next) {
    if (voice->owner == &handler->SH) {
      int core, core_voice;
      channel_pan = handler->channel_pan[voice->midi_channel] + handler->SH.Current_Pan;
      if (channel_pan >= 360)
        channel_pan -= 360;
      snd_MakeVolumesB(handler->SH.Current_Vol,
                       voice->velocity * handler->channel_volume[voice->midi_channel] / 127,
                       channel_pan, voice->program->volume, voice->program->pan,
                       voice->tone->volume, voice->tone->pan, (VolPair*)&voice->volume_left);
      output.left = (u_short)snd_AdjustVolToGroup(voice->volume_left, voice->group) >> 1;
      output.right = (u_short)snd_AdjustVolToGroup(voice->volume_right, voice->group) >> 1;
      core = voice->voice / SND_VOICES_PER_CORE;
      core_voice = voice->voice % SND_VOICES_PER_CORE;
      sceSdSetParam(core | (core_voice << 1) | SD_VP_VOLL, output.left);
      sceSdSetParam(core | (core_voice << 1) | SD_VP_VOLR, output.right);
    }
  }
  snd_UnlockVoiceAllocator();
}

void snd_SetMIDISoundPitchModifier(u_int handle, short modifier) {
  struct MIDIHandler* handler = (struct MIDIHandler*)snd_CheckHandlerStillActive(handle);
  if (handler)
    snd_SetMIDIHandlerPitchModifier(handler, modifier);
}

void snd_SetMIDIHandlerPitchModifier(struct MIDIHandler* handler, short modifier) {
  struct VoiceAttributes* voice;
  int note, fine;
  int core, core_voice;
  snd_LockVoiceAllocator(1);
  handler->SH.Current_PM = modifier;
  for (voice = gPlayingListHead; voice; voice = voice->next) {
    if (voice->owner == &handler->SH) {
      voice->pitch_modifier = modifier;
      snd_PitchBendTone(voice->tone, voice->pitch_bend, modifier, voice->start_note,
                        voice->start_fine, &note, &fine);
      core = voice->voice / SND_VOICES_PER_CORE;
      core_voice = voice->voice % SND_VOICES_PER_CORE;
      sceSdSetParam(core | (core_voice << 1) | SD_VP_PITCH,
                    PS1Note2Pitch(voice->tone->center_note, voice->tone->center_fine, note, fine));
    }
  }
  snd_UnlockVoiceAllocator();
}

void snd_MIDISoundOwnerProc(int voice, struct GenericSoundHandler* owner, int event) {
  int core = voice / SND_VOICES_PER_CORE;
  int core_voice = voice % SND_VOICES_PER_CORE;
  owner->Voices[core] &= ~(1 << core_voice);
}
