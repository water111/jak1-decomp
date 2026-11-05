#include "sce/iop/989snd/blocksound.h"

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/grain.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/lfo.h"
#include "sce/iop/989snd/playback.h"
#include "sce/iop/989snd/random.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/libsd/sdmacro.h"
#include "sce/iop/stdio/stdio.h"

SndGrainProc gGrainHandler[] = {
    snd_SFX_GRAIN_TYPE_NULL,
    snd_SFX_GRAIN_TYPE_TONE,
    snd_SFX_GRAIN_TYPE_XREF_ID,
    snd_SFX_GRAIN_TYPE_XREF_NUM,
    snd_SFX_GRAIN_TYPE_LFO_SETTINGS,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    snd_SFX_GRAIN_TYPE_CONTROL_NULL,
    snd_SFX_GRAIN_TYPE_LOOP_START,
    snd_SFX_GRAIN_TYPE_LOOP_END,
    snd_SFX_GRAIN_TYPE_LOOP_CONTINUE,
    snd_SFX_GRAIN_TYPE_STOP,
    snd_SFX_GRAIN_TYPE_RAND_PLAY,
    snd_SFX_GRAIN_TYPE_RAND_DELAY,
    snd_SFX_GRAIN_TYPE_RAND_PB,
    snd_SFX_GRAIN_TYPE_PB,
    snd_SFX_GRAIN_TYPE_ADD_PB,
    snd_SFX_GRAIN_TYPE_SET_REGISTER,
    snd_SFX_GRAIN_TYPE_SET_REGISTER_RAND,
    snd_SFX_GRAIN_TYPE_INC_REGISTER,
    snd_SFX_GRAIN_TYPE_DEC_REGISTER,
    snd_SFX_GRAIN_TYPE_TEST_REGISTER,
    snd_SFX_GRAIN_TYPE_MARKER,
    snd_SFX_GRAIN_TYPE_GOTO_MARKER,
    snd_SFX_GRAIN_TYPE_GOTO_RANDOM_MARKER,
    snd_SFX_GRAIN_TYPE_WAIT_FOR_ALL_VOICES,
    snd_SFX_GRAIN_TYPE_PLAY_CYCLE,
    snd_SFX_GRAIN_TYPE_ADD_REGISTER,
    snd_SFX_GRAIN_TYPE_KEY_OFF_VOICES,
    snd_SFX_GRAIN_TYPE_KILL_VOICES,
    snd_SFX_GRAIN_TYPE_ON_STOP_MARKER,
};

u_int snd_PlaySFX(struct SFXBlock2* block,
                  int index,
                  int volume,
                  int pan,
                  short pitch_modifier,
                  short bend) {
  struct SFX2* sound;
  struct BlockSoundHandler* handler;
  int play_volume, i, stop;
  if (index < 0 || index >= block->sound_count)
    goto invalid;
  sound = &block->sounds[index];
  if (!sound->NumGrains) {
  invalid:
    return 0;
  } else {
    if (volume == 0x7fffffff)
      volume = 1024;
    play_volume = (volume * sound->Vol) >> 10;
    if (play_volume > 127)
      play_volume = 127;
    if (pan == -1 || pan == -2)
      pan = sound->Pan;
    if (sound->Flags & 2)
      snd_StopAllHandlersForSound((struct Sound*)sound, 0);
    handler = snd_GetFreeBlockHandler();
    if (!handler)
      goto invalid;
    handler->SH.Sound = sound;
#if SND989_VERSION == 0x020608
    handler->SH.Effects = 0;
#endif
    handler->SH.Original_Vol = sound->Vol;
    handler->SH.Original_Pan = sound->Pan;
    handler->SH.Current_Vol = play_volume;
#if SND989_VERSION == 0x020604
    handler->App_Vol = play_volume;
    handler->LFO_Vol = 0;
#endif
    handler->SH.Current_Pan = pan;
#if SND989_VERSION == 0x020604
    handler->App_Pan = pan;
    handler->LFO_Pan = 0;
#endif
    handler->SH.Current_PM = pitch_modifier;
#if SND989_VERSION == 0x020604
    handler->Current_PB = bend;
    handler->App_PB = bend;
    handler->LFO_PB = 0;
    handler->SH.Effects = 0;
#endif
    handler->SH.flags = 0;
    handler->SH.VolGroup = sound->VolGroup;
#if SND989_VERSION == 0x020608
    handler->SH.Voices[0] = 0;
    handler->SH.Voices[1] = 0;
    handler->Current_Note = 60;
    handler->Current_Fine = 0;
    handler->Current_PB = bend;
    handler->LFO_Vol = 0;
    handler->App_Vol = play_volume;
    handler->LFO_Pan = 0;
    handler->App_Pan = pan;
    handler->LFO_PB = 0;
    handler->App_PB = bend;
    handler->LFO_PM = 0;
    handler->App_PM = 0;
#endif
    handler->CountDown = sound->FirstGrain[0].delay;
    handler->NextGrain = 0;
#if SND989_VERSION == 0x020604
    handler->Current_Note = 60;
    handler->Current_Fine = 0;
#else
    handler->Registers[0] = 0;
    handler->Registers[1] = 0;
    handler->Registers[2] = 0;
    handler->Registers[3] = 0;
    handler->sk_doing_skipping_play = 0;
    handler->sk_grains_to_play = 0;
    handler->sk_grains_to_skip = 0;
    handler->block = block;
#endif
    for (i = 0; i < 4; ++i) {
      handler->lfo[i].type = 0;
      handler->lfo[i].next_step = 0;
      handler->lfo[i].target = 0;
      handler->lfo[i].setup_flags = 0;
      handler->lfo[i].running_flags = 0;
      handler->lfo[i].handler = handler;
    }
    stop = 0;
    while (!stop && handler->NextGrain != -1 && !handler->CountDown)
      stop = snd_DoGrain(handler);
    if (stop || (!handler->CountDown && !handler->SH.Voices[0] && !handler->SH.Voices[1])) {
      snd_StopHandlerPtr(&handler->SH, 0, 0);
      handler->SH.Sound = 0;
      handler = 0;
    }
    {
      u_int result = 0;
      if (handler) {
        snd_ActivateHandler(&handler->SH);
        result = handler->SH.OwnerID;
      }
      return result;
    }
  }
}

u_int snd_PlaySFXGrain(struct SFXBlock2* block, int sound_index, int grain_index) {
  struct SFX2* sound = &block->sounds[sound_index];
  struct BlockSoundHandler* handler;
  if (!sound->NumGrains)
    return 0;
  handler = snd_GetFreeBlockHandler();
  if (!handler)
    return 0;
  handler->SH.Sound = sound;
  handler->SH.Original_Vol = sound->Vol;
  handler->SH.Original_Pan = sound->Pan;
  handler->SH.Current_Vol = sound->Vol;
  handler->App_Vol = 127;
  handler->LFO_Vol = 0;
  handler->SH.Current_Pan = sound->Pan;
  handler->App_Pan = 0;
  handler->LFO_Pan = 0;
  handler->SH.Current_PM = 0;
  handler->Current_PB = 0;
  handler->App_PB = 0;
  handler->LFO_PB = 0;
  handler->SH.Effects = 0;
  handler->SH.flags = 0;
  handler->SH.VolGroup = sound->VolGroup;
  handler->CountDown = 0;
  handler->NextGrain = grain_index;
  handler->Current_Note = 60;
  handler->Current_Fine = 0;
  snd_DoGrain(handler);
  handler->NextGrain = -1;
  if (!handler->CountDown && !handler->SH.Voices[0] && !handler->SH.Voices[1]) {
    snd_DeactivateHandler(&handler->SH, 0);
    handler = 0;
  }
  if (handler) {
    snd_ActivateHandler(&handler->SH);
    return handler->SH.OwnerID;
  }
  return 0;
}

int snd_ProcessBlockSoundTick(struct BlockSoundHandler* handler) {
  int stop = 0;
  if (handler->NextGrain == -1)
    return stop;
  else {
    handler->CountDown--;
    while (!stop && handler->NextGrain != -1 && handler->CountDown <= 0)
      stop = snd_DoGrain(handler);
  }
  return stop;
}

int snd_DoBlockSoundStop(struct BlockSoundHandler* handler) {
  int stop = 0;
  int index = snd_GetOnStopGrainIndex(handler);
  if (index != -1) {
    snd_KeyOffVoicesEx((struct VoiceFlags*)handler->SH.Voices, 0);
    handler->SH.Voices[1] = 0;
    handler->SH.Voices[0] = 0;
    handler->NextGrain = index + 1;
    while (!stop && handler->NextGrain != -1)
      stop = snd_DoGrain(handler);
    if (handler->SH.Voices[0] || handler->SH.Voices[1])
      return 1;
  }
  snd_RemoveLFOsForHandler(handler);
  return 0;
}

int snd_GetOnStopGrainIndex(struct BlockSoundHandler* handler) {
  int i;
  struct SFX2* sound = handler->SH.Sound;
  for (i = 0; i < sound->NumGrains; i++) {
    if (sound->FirstGrain[i].type == SND_GRAIN_ON_STOP_MARKER) {
      if (i == sound->NumGrains - 1)
        return -1;
      return i;
    }
  }
  return -1;
}

int snd_SFX_GRAIN_TYPE_NULL(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain) {
  return 0;
}

int snd_SFX_GRAIN_TYPE_TONE(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain) {
  int voice;
  handler->SH.Current_Vol = handler->App_Vol + handler->LFO_Vol;
  if (handler->SH.Current_Vol > 127)
    handler->SH.Current_Vol = 127;
  if (handler->SH.Current_Vol < 0)
    handler->SH.Current_Vol = 0;
  handler->SH.Current_Pan = handler->App_Pan + handler->LFO_Pan;
  while (handler->SH.Current_Pan >= 360)
    handler->SH.Current_Pan -= 360;
  while (handler->SH.Current_Pan < 0)
    handler->SH.Current_Pan += 360;
  snd_LockVoiceAllocator(1);
  voice = snd_AllocateVoice(sound->VolGroup, grain->data.tone.priority);
  if (voice != -1) {
    int core = voice / SND_VOICES_PER_CORE;
    int core_voice = voice - core * SND_VOICES_PER_CORE;
    handler->SH.Voices[core] |= 1 << core_voice;
    gChannelStatus[voice].owner = &handler->SH;
    gChannelStatus[voice].owner_proc = snd_SFXOwnerProc;
    gChannelStatus[voice].tone = &grain->data.tone;
    gChannelStatus[voice].start_note = handler->Current_Note;
    gChannelStatus[voice].start_fine = handler->Current_Fine;
    gChannelStatus[voice].pitch_bend = handler->Current_PB;
    gChannelStatus[voice].pitch_modifier = handler->SH.Current_PM;
    gChannelStatus[voice].priority = grain->data.tone.priority;
    gChannelStatus[voice].group = sound->VolGroup;
    gChannelStatus[voice].volume_left = gChannelStatus[voice].volume_right = 0;
    snd_MakeVolumes(127, 0, handler->SH.Current_Vol, handler->SH.Current_Pan,
                    grain->data.tone.volume, grain->data.tone.pan,
                    (VolPair*)&gChannelStatus[voice].volume_left);
    snd_StartVAGVoice(voice);
  }
  snd_UnlockVoiceAllocator();
  return 0;
}

int snd_SFX_GRAIN_TYPE_XREF_ID(struct BlockSoundHandler* handler,
                               struct SFX2* sound,
                               struct SFXGrain2* grain) {
  return 0;
}

int snd_SFX_GRAIN_TYPE_XREF_NUM(struct BlockSoundHandler* handler,
                                struct SFX2* sound,
                                struct SFXGrain2* grain) {
  return 0;
}

int snd_SFX_GRAIN_TYPE_LFO_SETTINGS(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain) {
  struct LFOTracker* lfo = &handler->lfo[grain->data.lfo.index];
  lfo->target = grain->data.lfo.target;
  if (!lfo->target) {
    snd_RemoveLFO(lfo);
    lfo->type = LFO_TYPE_OFF;
    return 0;
  }
  lfo->type = grain->data.lfo.type;
  lfo->target = grain->data.lfo.target;
  lfo->target_extra = grain->data.lfo.target_extra;
  lfo->setup_flags = grain->data.lfo.flags;
  lfo->depth = grain->data.lfo.depth;
  lfo->orig_depth = grain->data.lfo.depth;
  lfo->step_size = grain->data.lfo.step_size;
  lfo->orig_step_size = grain->data.lfo.step_size;
  lfo->state_hold1 = 0;
  lfo->last_lfo = 0;
  if (lfo->type == LFO_TYPE_SQUARE)
    lfo->state_hold1 = grain->data.lfo.duty_cycle;
  lfo->state_hold2 = 0;
  if (lfo->setup_flags & 2)
    lfo->next_step = (snd_RandomUInt16() & 0x7ff) << 16;
  else
    lfo->next_step = grain->data.lfo.start_offset << 16;
  snd_InitLFO(lfo);
  return 0;
}

int snd_SFX_GRAIN_TYPE_CONTROL_NULL(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain) {
  return 0;
}

int snd_SFX_GRAIN_TYPE_LOOP_START(struct BlockSoundHandler* handler,
                                  struct SFX2* sound,
                                  struct SFXGrain2* grain) {
  return 0;
}

int snd_SFX_GRAIN_TYPE_LOOP_END(struct BlockSoundHandler* handler,
                                struct SFX2* sound,
                                struct SFXGrain2* grain) {
  int found = 0;
  int i;
  for (i = handler->NextGrain - 1; i >= 0 && !found; i--) {
    if (sound->FirstGrain[i].type == 21) {
      handler->NextGrain = i - 1;
      found = 1;
    }
  }
  if (!found) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_DoGrain: encountered a \"Loop End\" grain but couldn't find a previous \"Loop "
        "Start\"!\n");
#else
    snd_ShowError(8, 0, 0, 0, 0);
#endif
    return -1;
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_LOOP_CONTINUE(struct BlockSoundHandler* handler,
                                     struct SFX2* sound,
                                     struct SFXGrain2* grain) {
  int found = 0;
  int i;
  for (i = handler->NextGrain + 1; i < sound->NumGrains && !found; i++) {
    if (sound->FirstGrain[i].type == 22) {
      handler->NextGrain = i;
      found = 1;
    }
  }
  if (!found) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_DoGrain: encountered a \"Loop Continue\" grain but couldn't find the next \"Loop "
        "End\"!\n");
#else
    snd_ShowError(9, 0, 0, 0, 0);
#endif
    return -1;
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_STOP(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain) {
  return -1;
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
int snd_GetSoundIndexInBlock(struct SFXBlock2* block, struct SFX2* sound) {
  int i;
  for (i = 0; i < block->sound_count; ++i) {
    if (&block->sounds[i] == sound)
      return i;
  }
  return -1;
}
#endif

#endif

int snd_SFX_GRAIN_TYPE_RAND_PLAY(struct BlockSoundHandler* handler,
                                 struct SFX2* sound,
                                 struct SFXGrain2* grain) {
  int choice;
#if SND989_VERSION == 0x020604
  // TODO HACK
  register int result __asm__("$2");
  if (!handler->sk_doing_skipping_play) {
#else
  if (handler->sk_doing_skipping_play) {
    snd_ShowError(10, handler->block->id, snd_GetSoundIndexInBlock(handler->block, sound),
                  handler->NextGrain + 1, 0);
    return -1;
  }
#endif
    choice = snd_RandomUInt16() % grain->data.parameters[0];
#if SND989_VERSION == 0x020608
    if (choice == grain->data.parameters[2]) {
      ++choice;
      if (choice == grain->data.parameters[0])
        choice = 0;
    }
    grain->data.parameters[2] = choice;
#endif
    handler->NextGrain += choice * grain->data.parameters[1];
    handler->sk_grains_to_play = grain->data.parameters[1] + 1;
    handler->sk_grains_to_skip =
        (grain->data.parameters[0] - (choice + 1)) * grain->data.parameters[1];
    handler->sk_doing_skipping_play = 1;

#if SND989_VERSION == 0x020604
    result = 0;
#else
  return 0;
#endif
  }
#if SND989_VERSION == 0x020604
  else {
    printf(
        "snd_DoGrain: Can't have Random Play control grains inside other Random/Cycle Play "
        "blocks!\n");
    result = -1;
  }
  return result;
}
#endif

int snd_SFX_GRAIN_TYPE_RAND_DELAY(struct BlockSoundHandler* handler,
                                  struct SFX2* sound,
                                  struct SFXGrain2* grain) {
  return snd_RandomUInt16() % (grain->data.parameters[0] + 1);
}

int snd_SFX_GRAIN_TYPE_RAND_PB(struct BlockSoundHandler* handler,
                               struct SFX2* sound,
                               struct SFXGrain2* grain) {
  // TODO lots of hacks
  u_short random = snd_RandomUInt16() % 32767u;
  register int bend asm("$5");
  int scaled_product;
  int random_product = 65535 * random;
  int biased_quotient;
  register int sign asm("$3");
  biased_quotient = random_product / 32767 + (random_product >> 31);
  sign = random_product >> 31;
  asm("subu %0,%1,%2" : "=r"(bend) : "r"(biased_quotient), "r"(sign) : "memory");
  {
    register int percent asm("$2");
    percent = grain->data.parameters[0];
    bend -= 32768;
    scaled_product = bend * percent;
  }
  {
    int high_product;
    int scaled_sign;
    int scaled;
    u_int owner;
    asm volatile("" ::"r"(scaled_product));
    high_product = 0x51eb851f;
    asm volatile("mult %0,%1" ::"r"(scaled_product), "r"(high_product));
    owner = handler->SH.OwnerID;
    asm("" : "+r"(scaled_product) : "r"(owner));
    scaled_sign = scaled_product >> 31;
    asm volatile("mfhi %0" : "=r"(high_product) : "0"(high_product), "r"(owner), "r"(scaled_sign));
    high_product >>= 5;
    asm("subu %0,%1,%2" : "=r"(scaled) : "r"(high_product), "r"(scaled_sign));
    snd_SetSFXPitchbend(owner, scaled);
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_PB(struct BlockSoundHandler* handler,
                          struct SFX2* sound,
                          struct SFXGrain2* grain) {
  int bend;
  if (grain->data.parameters[0] < 0)
    bend = -32768 * grain->data.parameters[0] / -127;
  else
    bend = 32767 * grain->data.parameters[0] / 127;
  snd_SetSFXPitchbend(handler->SH.OwnerID, bend);
  return 0;
}

int snd_SFX_GRAIN_TYPE_ADD_PB(struct BlockSoundHandler* handler,
                              struct SFX2* sound,
                              struct SFXGrain2* grain) {
  int bend = handler->Current_PB + 32767 * grain->data.parameters[0] / 127;
  if (bend > 32767)
    bend = 32767;
  if (bend < -32768)
    bend = -32768;
  snd_SetSFXPitchbend(handler->SH.OwnerID, bend);
  return 0;
}

int snd_SFX_GRAIN_TYPE_SET_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain) {
  handler->Registers[grain->data.parameters[0]] = grain->data.parameters[1];
  return 0;
}

int snd_SFX_GRAIN_TYPE_SET_REGISTER_RAND(struct BlockSoundHandler* handler,
                                         struct SFX2* sound,
                                         struct SFXGrain2* grain) {
  int value = grain->data.parameters[2] - grain->data.parameters[1] + 1;
  value = snd_RandomUInt16() % value + grain->data.parameters[1];
  handler->Registers[grain->data.parameters[0]] = value;
  return 0;
}

int snd_SFX_GRAIN_TYPE_INC_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain) {
  int value = handler->Registers[grain->data.parameters[0]] + 1;
  if (value > 127)
    value = 127;
  handler->Registers[grain->data.parameters[0]] = value;
  return 0;
}

int snd_SFX_GRAIN_TYPE_DEC_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain) {
  int value = handler->Registers[grain->data.parameters[0]] - 1;
  if (value < -128)
    value = -128;
  handler->Registers[grain->data.parameters[0]] = value;
  return 0;
}

int snd_SFX_GRAIN_TYPE_TEST_REGISTER(struct BlockSoundHandler* handler,
                                     struct SFX2* sound,
                                     struct SFXGrain2* grain) {
  switch (grain->data.parameters[1]) {
    case 0:
      if (handler->Registers[grain->data.parameters[0]] >= grain->data.parameters[2])
        handler->NextGrain++;
      break;
    case 1:
      if (handler->Registers[grain->data.parameters[0]] != grain->data.parameters[2])
        handler->NextGrain++;
      break;
    case 2:
      if (handler->Registers[grain->data.parameters[0]] <= grain->data.parameters[2])
        handler->NextGrain++;
      break;
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_MARKER(struct BlockSoundHandler* handler,
                              struct SFX2* sound,
                              struct SFXGrain2* grain) {
  return 0;
}

int snd_SFX_GRAIN_TYPE_GOTO_MARKER(struct BlockSoundHandler* handler,
                                   struct SFX2* sound,
                                   struct SFXGrain2* grain) {
  int i;
  int found = 0;
  int marker = grain->data.parameters[0];
  for (i = 0; i < sound->NumGrains && !found; i++) {
    if (sound->FirstGrain[i].type == 35 && sound->FirstGrain[i].data.parameters[0] == marker) {
      handler->NextGrain = i - 1;
      found = 1;
    }
  }
  if (!found) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_DoGrain: encountered a \"Goto Marker\" grain but couldn't find the matching "
        "marker!\n");
#else
    snd_ShowError(11, 0, 0, 0, 0);
#endif
    return -1;
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_GOTO_RANDOM_MARKER(struct BlockSoundHandler* handler,
                                          struct SFX2* sound,
                                          struct SFXGrain2* grain) {
  int marker = grain->data.parameters[1] - grain->data.parameters[0] + 1;
  int i;
  int found;
  marker = snd_RandomUInt16() % marker;
  marker += grain->data.parameters[0];
  found = 0;
  for (i = 0; i < sound->NumGrains && !found; i++) {
    if (sound->FirstGrain[i].type == 35 && sound->FirstGrain[i].data.parameters[0] == marker) {
      handler->NextGrain = i - 1;
      found = 1;
    }
  }
  if (!found) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_DoGrain: encountered a \"Goto Random Marker\" grain but couldn't find the matching "
        "marker for %d!\n",
        marker);
#else
    snd_ShowError(12, marker, 0, 0, 0);
#endif
    return -1;
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_WAIT_FOR_ALL_VOICES(struct BlockSoundHandler* handler,
                                           struct SFX2* sound,
                                           struct SFXGrain2* grain) {
  if (handler->SH.Voices[0] || handler->SH.Voices[1]) {
    handler->NextGrain--;
    return 1;
  }
  return 0;
}

int snd_SFX_GRAIN_TYPE_PLAY_CYCLE(struct BlockSoundHandler* handler,
                                  struct SFX2* sound,
                                  struct SFXGrain2* grain) {
  if (handler->sk_doing_skipping_play) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_DoGrain: Can't have Cycle Play control grains inside other Random/Cycle Play "
        "blocks!\n");
#else
    snd_ShowError(13, 0, 0, 0, 0);
#endif
    return -1;
  } else {
    int cycle = grain->data.parameters[2];
    grain->data.parameters[2]++;
    if (grain->data.parameters[2] == grain->data.parameters[0])
      grain->data.parameters[2] = 0;
    handler->NextGrain += cycle * grain->data.parameters[1];
    handler->sk_grains_to_play = grain->data.parameters[1] + 1;
    handler->sk_grains_to_skip =
        (grain->data.parameters[0] - (cycle + 1)) * grain->data.parameters[1];
    handler->sk_doing_skipping_play = 1;
    return 0;
  }
}

int snd_SFX_GRAIN_TYPE_ADD_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain) {
  int value = handler->Registers[grain->data.parameters[1]] + grain->data.parameters[0];
  if (value > 127)
    value = 127;
  if (value < -128)
    value = -128;
  handler->Registers[grain->data.parameters[1]] = value;
  return 0;
}

int snd_SFX_GRAIN_TYPE_KEY_OFF_VOICES(struct BlockSoundHandler* handler,
                                      struct SFX2* sound,
                                      struct SFXGrain2* grain) {
  snd_KeyOffVoicesEx((struct VoiceFlags*)handler->SH.Voices, 0);
  handler->SH.Voices[1] = 0;
  handler->SH.Voices[0] = 0;
  return 0;
}

int snd_SFX_GRAIN_TYPE_KILL_VOICES(struct BlockSoundHandler* handler,
                                   struct SFX2* sound,
                                   struct SFXGrain2* grain) {
  snd_SilenceVoicesEx((struct VoiceFlags*)handler->SH.Voices, 0);
  handler->SH.Voices[1] = 0;
  handler->SH.Voices[0] = 0;
  return 0;
}

int snd_SFX_GRAIN_TYPE_ON_STOP_MARKER(struct BlockSoundHandler* handler,
                                      struct SFX2* sound,
                                      struct SFXGrain2* grain) {
  handler->NextGrain = sound->NumGrains - 1;
  return 0;
}

int snd_DoGrain(struct BlockSoundHandler* handler) {
  struct SFX2* sound = handler->SH.Sound;
  struct SFXGrain2* grain = &sound->FirstGrain[handler->NextGrain];
  int stop = 0;
  int delay = gGrainHandler[grain->type](handler, sound, grain);
  if (delay < 0)
    return 1;
  if (handler->sk_doing_skipping_play) {
    handler->sk_grains_to_play--;
    if (!handler->sk_grains_to_play) {
      handler->NextGrain += handler->sk_grains_to_skip;
      handler->sk_doing_skipping_play = 0;
    }
  }
  handler->NextGrain++;
  if (handler->NextGrain > sound->NumGrains - 1) {
    if (!handler->SH.Voices[0] && !handler->SH.Voices[1])
      stop = 1;
    else
      handler->NextGrain = -1;
  } else {
    handler->CountDown = sound->FirstGrain[handler->NextGrain].delay + delay;
  }
  return stop;
}

void snd_SetSFXVolPan(u_int handle, int volume, int pan) {
  int voice = -1;
  struct BlockSoundHandler* handler =
      (struct BlockSoundHandler*)snd_CheckHandlerStillActive(handle);
  struct SFX2* sound;
  int final_volume, final_pan, locked;
  VolPair output;
  if (!handler)
    return;
  sound = handler->SH.Sound;
  if (volume < 0)
    handler->App_Vol = -volume;
  else if (volume != 0x7fffffff)
    handler->App_Vol = (volume * sound->Vol) >> 10;
  if (handler->App_Vol > 127)
    handler->App_Vol = 127;
  if (pan == -1)
    handler->App_Pan = sound->Pan;
  else if (pan != -2)
    handler->App_Pan = pan;
  final_volume = handler->App_Vol + handler->LFO_Vol;
  if (final_volume > 127)
    final_volume = 127;
  if (final_volume < 0)
    final_volume = 0;
  final_pan = handler->App_Pan + handler->LFO_Pan;
  while (final_pan >= 360)
    final_pan -= 360;
  while (final_pan < 0)
    final_pan += 360;
  if (final_volume == handler->SH.Current_Vol && final_pan == handler->SH.Current_Pan)
    return;
  handler->SH.Current_Vol = final_volume;
  handler->SH.Current_Pan = final_pan;
  locked = snd_LockVoiceAllocator(1);
  while ((voice = snd_GetNextHandlerVoice(&handler->SH, voice + 1)) != -1) {
    snd_MakeVolumes(127, 0, handler->SH.Current_Vol, handler->SH.Current_Pan,
                    gChannelStatus[voice].tone->volume, gChannelStatus[voice].tone->pan,
                    (VolPair*)&gChannelStatus[voice].volume_left);
    output.left =
        (u_short)snd_AdjustVolToGroup(gChannelStatus[voice].volume_left, handler->SH.VolGroup) >> 1;
    output.right =
        (u_short)snd_AdjustVolToGroup(gChannelStatus[voice].volume_right, handler->SH.VolGroup) >>
        1;
    sceSdSetParam((voice / SND_VOICES_PER_CORE) | ((voice % SND_VOICES_PER_CORE) << 1) | SD_VP_VOLL,
                  output.left);
    sceSdSetParam((voice / SND_VOICES_PER_CORE) | ((voice % SND_VOICES_PER_CORE) << 1) | SD_VP_VOLR,
                  output.right);
  }
  if (locked)
    snd_UnlockVoiceAllocator();
}

void snd_SetSFXPitch(u_int handle, int pitch) {
  struct BlockSoundHandler* handler =
      (struct BlockSoundHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    handler->Current_Note = pitch / 128;
    handler->Current_Fine = pitch % 128;
    snd_UpdateSFXPitch(handler);
  }
}

void snd_SetSFXPitchbend(u_int handle, short bend) {
  struct BlockSoundHandler* handler =
      (struct BlockSoundHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    handler->App_PB = bend;
    snd_UpdateSFXPitch(handler);
  }
}

void snd_SetSFXPitchModifier(u_int handle, short modifier) {
  struct BlockSoundHandler* handler =
      (struct BlockSoundHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    handler->App_PM = modifier;
    snd_UpdateSFXPitch(handler);
  }
}

void snd_UpdateSFXPitch(struct BlockSoundHandler* handler) {
  int voice = -1;
  int note, fine;
  int bend;
  int locked;
  handler->SH.Current_PM = handler->App_PM + handler->LFO_PM;
  bend = handler->App_PB + handler->LFO_PB;
  if (bend > 32767)
    handler->Current_PB = 32767;
  else if (bend < -32768)
    handler->Current_PB = -32768;
  else
    handler->Current_PB = bend;
  locked = snd_LockVoiceAllocator(1);
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

int snd_DoesSFXLoop(SFXBlock2* block, int sound) {
  return block->sounds[sound].Flags & SND_SFX_LOOP;
}

void snd_SFXOwnerProc(int voice, struct GenericSoundHandler* owner, int event) {
  struct BlockSoundHandler* handler = (struct BlockSoundHandler*)owner;
  int core = voice / SND_VOICES_PER_CORE;
  int core_voice = voice % SND_VOICES_PER_CORE;
  handler->SH.Voices[core] &= ~(1 << core_voice);
  if (!handler->SH.Voices[0] && !handler->SH.Voices[1] && handler->NextGrain == -1) {
    snd_RemoveLFOsForHandler(handler);
    snd_DeactivateHandler(&handler->SH, 0);
  }
}

int snd_CalcSoundNameHash(const char* name) {
  return (name[0] + name[1] + name[2] + name[3] + name[4] + name[5] + name[6] + name[7]) % 32;
}
