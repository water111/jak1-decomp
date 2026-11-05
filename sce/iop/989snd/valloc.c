#include "sce/iop/989snd/valloc.h"

#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thsemap/thsemap.h"

#if SND989_VERSION == 0x020608
int gLockBlockerSema = -1;
#endif
int gTickAttentionNeeded = 0;
int gVoiceAllocationBlocked = 0;
int gVoiceAllocatorInUse = 0;
int gChannelMode = 0;
int gReverbMode = 0;
struct VoiceRange gVoiceRanges[SND_VOICE_GROUPS] = {{0, 47}, {0, 47}, {0, 47}, {0, 47},
                                                    {0, 47}, {0, 47}, {0, 47}, {0, 47}};

extern struct VoiceAttributes gChannelStatus[SND_NUM_VOICES];
struct VoiceAttributes* gPlayingListHead = 0;
int gThreadWaitingForVoiceAllocator = -1;

extern signed char gAutoFreeState[SND_NUM_VOICES];
extern u_int gAwaitingKeyOn[2];
extern u_int gAwaitingKeyOff[2];
extern u_int gKeyedOnVoices[2];
extern u_int gKeyedOffVoices[2];
extern u_int gReverbVoices[2];

#define SND_SEMA_EMPTY (-419)
#define SND_INTR_CONTEXT (-102)

void snd_InitVoiceAllocator(void) {
  int count;
#if SND989_VERSION == 0x020608
  struct SemaParam sema;
#endif
  gVoiceAllocationBlocked = 0;
  gTickAttentionNeeded = 0;
  for (count = 0; count < SND_NUM_VOICES; count++) {
    snd_MarkVoiceFree(&gChannelStatus[count]);
    gChannelStatus[count].voice = count;
    gAutoFreeState[count] = 0;
  }
  gAwaitingKeyOn[0] = gAwaitingKeyOn[1] = 0;
  gAwaitingKeyOff[0] = gAwaitingKeyOff[1] = 0;
  gKeyedOnVoices[0] = gKeyedOnVoices[1] = 0;
  gKeyedOffVoices[0] = gKeyedOffVoices[1] = 0;
  gReverbVoices[0] = gReverbVoices[1] = 0;
  gChannelMode = 0;
  gReverbMode = 0;
  gPlayingListHead = 0;
  for (count = 0; count < SND_VOICE_GROUPS; count++) {
    gVoiceRanges[count].min = 0;
    gVoiceRanges[count].max = SND_NUM_VOICES - 1;
  }
#if SND989_VERSION == 0x020608
  sema.attr = SA_THPRI;
  sema.initCount = 0;
  sema.maxCount = 1;
  gLockBlockerSema = CreateSema(&sema);
#endif
}

#if SND989_VERSION == 0x020608

void snd_CleanupVoiceAllocator(void) {
  DeleteSema(gLockBlockerSema);
  gLockBlockerSema = 0;
}

#endif

void snd_SetMixerMode(int channel_mode, int reverb_mode) {
  int count;
  gChannelMode = channel_mode;
  gReverbMode = reverb_mode;
  if (channel_mode == 1) {
    for (count = 0; count < SND_VOICE_GROUPS; count++) {
      gVoiceRanges[count].min = SND_VOICES_PER_CORE;
      gVoiceRanges[count].max = SND_NUM_VOICES - 1;
    }
    gVoiceRanges[1].min = 0;
    gVoiceRanges[1].max = SND_VOICES_PER_CORE - 1;
#if SND989_VERSION == 0x020608
  } else {
    for (count = 0; count < SND_VOICE_GROUPS; count++) {
      gVoiceRanges[count].min = 0;
      gVoiceRanges[count].max = SND_NUM_VOICES - 1;
    }
#endif
  }
}

void snd_SetGroupVoiceRange(int group, int min, int max) {
  if (min < 0) {
    min = 0;
  }
  if (max >= SND_NUM_VOICES) {
    max = SND_NUM_VOICES - 1;
  }
  gVoiceRanges[group].min = min;
  gVoiceRanges[group].max = max;
}

int snd_LockVoiceAllocator(int blocking) {
  int state;
  int disabled = CpuSuspendIntr(&state);
  if (gVoiceAllocatorInUse) {
    if (!disabled)
      CpuResumeIntr(state);
    if (!blocking || gThreadWaitingForVoiceAllocator != -1)
      return 0;
#if SND989_VERSION == 0x020604
    gThreadWaitingForVoiceAllocator = GetThreadId();
    SleepThread();
    disabled = CpuSuspendIntr(&state);
#else
    if (disabled)
      snd_ShowError(79, 0, 0, 0, 0);
    while (PollSema(gLockBlockerSema) != SND_SEMA_EMPTY) {
    }
    {
      int result = WaitSema(gLockBlockerSema);
      if (result)
        snd_ShowError(80, result, 0, 0, 0);
    }
#endif
  }
  gVoiceAllocatorInUse = 1;
  if (!disabled)
    CpuResumeIntr(state);
  return 1;
}

int snd_IsVoiceAllocatorLocked(void) {
  return gVoiceAllocatorInUse;
}

int snd_CheckAllocatorCueTick(void) {
  if (!gVoiceAllocatorInUse)
    return 0;
  gTickAttentionNeeded++;
  return 1;
}

void snd_UnlockVoiceAllocator(void) {
  int state;
  int disabled = CpuSuspendIntr(&state);
  gVoiceAllocatorInUse = 0;
#if SND989_VERSION == 0x020604
  while (gTickAttentionNeeded && !gLockMasterTick) {
    gTickAttentionNeeded--;
    snd_SoundTickHandler();
  }
  if (!disabled)
    CpuResumeIntr(state);
  if (gThreadWaitingForVoiceAllocator != -1) {
    int thread = gThreadWaitingForVoiceAllocator;
    gThreadWaitingForVoiceAllocator = -1;
    WakeupThread(thread);
  }
#else
  if (!disabled) {
    CpuResumeIntr(state);
    SignalSema(gLockBlockerSema);
  } else if (disabled == SND_INTR_CONTEXT) {
    iSignalSema(gLockBlockerSema);
  } else {
    snd_ShowError(81, 0, 0, 0, 0);
  }
#endif
}

int snd_ExternVoiceAlloc(int group, int priority) {
  int voice;
  if (group >= SND_VOICE_GROUPS)
    group = SND_VOICE_GROUPS - 1;
  voice = snd_AllocateVoice(group, priority);
  if (voice)
    gChannelStatus[voice].status = 3;
  return voice;
}

void snd_ExternVoiceFree(int voice) {
  snd_MarkVoiceFree(&gChannelStatus[voice]);
}

int snd_AllocateVoice(int group, int priority) {
  int count;
  int start;
  int end;
  int lowest_priority;
  if (group >= SND_VOICE_GROUPS)
    group = SND_VOICE_GROUPS - 1;
  start = gVoiceRanges[group].min;
  end = gVoiceRanges[group].max + 1;
  for (count = start; count < end; count++) {
    if (!gChannelStatus[count].status)
      goto free_voice;
  }
  lowest_priority = start;
  for (count = start + 1; count < end; count++) {
    if (gChannelStatus[count].priority < (u_int)gChannelStatus[lowest_priority].priority ||
        (gChannelStatus[count].priority == gChannelStatus[lowest_priority].priority &&
         gChannelStatus[count].start_tick < gChannelStatus[lowest_priority].start_tick))
      lowest_priority = count;
  }
  if (priority < gChannelStatus[lowest_priority].priority ||
      gChannelStatus[lowest_priority].priority >= 126u)
    return -1;
  if (gChannelStatus[lowest_priority].owner_proc)
    gChannelStatus[lowest_priority].owner_proc(lowest_priority,
                                               gChannelStatus[lowest_priority].owner, 1);
  snd_MarkVoiceFree(&gChannelStatus[lowest_priority]);
  gChannelStatus[lowest_priority].status = 2;
  gChannelStatus[lowest_priority].start_tick = snd_GetTick();
  gChannelStatus[lowest_priority].priority = priority;
  gChannelStatus[lowest_priority].group = group;
  gChannelStatus[lowest_priority].owner_proc = 0;
#if SND989_VERSION == 0x020608
  {
    // TODO hack
    register int allocated __asm__("$2") = lowest_priority;
    gAutoFreeState[allocated] = 4;
    __asm__("" : "+r"(allocated));
    return allocated;
  }
#endif
  return lowest_priority;

free_voice:
  gChannelStatus[count].status = 2;
  gChannelStatus[count].start_tick = snd_GetTick();
  gChannelStatus[count].priority = priority;
  gChannelStatus[count].group = group;
#if SND989_VERSION == 0x020608
  gAutoFreeState[count] = 4;
#endif
#if SND989_VERSION == 0x020604
  // TODO hack
  __asm__("");
#endif
  return count;
}

void snd_StartVAGVoice(int voice) {
  sceSdBatch batch[8];
  u_int results[8];
  int note;
  int fine;
  u_int core = voice / SND_VOICES_PER_CORE;
  u_int channel = voice % SND_VOICES_PER_CORE;
  gChannelStatus[voice].start_tick = snd_GetTick();
  batch[0].func = SD_BSET_PARAM;
  batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
  batch[0].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[voice].volume_left,
                                                 gChannelStatus[voice].group) >>
                   1;
  batch[1].func = SD_BSET_PARAM;
  batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
  batch[1].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[voice].volume_right,
                                                 gChannelStatus[voice].group) >>
                   1;
  batch[2].func = SD_BSET_SWITCH;
  batch[2].entry = core | SD_S_VMIXL;
  batch[2].value = sceSdGetSwitch(core | SD_S_VMIXL) | (1 << channel);
  batch[3].func = SD_BSET_SWITCH;
  batch[3].entry = core | SD_S_VMIXR;
  batch[3].value = sceSdGetSwitch(core | SD_S_VMIXR) | (1 << channel);
  batch[4].func = SD_BSET_PARAM;
  batch[4].entry = core | (channel << 1) | SD_VP_PITCH;
  snd_PitchBendTone(gChannelStatus[voice].tone, gChannelStatus[voice].pitch_bend,
                    gChannelStatus[voice].pitch_modifier, gChannelStatus[voice].start_note,
                    gChannelStatus[voice].start_fine, &note, &fine);
  batch[4].value = PS1Note2Pitch(gChannelStatus[voice].tone->center_note,
                                 gChannelStatus[voice].tone->center_fine, note, fine);
  batch[5].func = SD_BSET_ADDR;
  batch[5].entry = core | (channel << 1) | SD_VA_SSA;
  batch[5].value = gChannelStatus[voice].tone->sample_address;
  batch[6].func = SD_BSET_PARAM;
  batch[6].entry = core | (channel << 1) | SD_VP_ADSR1;
  batch[6].value = gChannelStatus[voice].tone->adsr1;
  batch[7].func = SD_BSET_PARAM;
  batch[7].entry = core | (channel << 1) | SD_VP_ADSR2;
  batch[7].value = gChannelStatus[voice].tone->adsr2;
  sceSdProcBatch(batch, results, 8);
  if (gChannelStatus[voice].tone->flags & SND_TONE_REVERB)
    gReverbVoices[core] |= 1 << channel;
  else
    gReverbVoices[core] &= ~(1 << channel);
  gAwaitingKeyOn[core] |= 1 << channel;
  snd_MarkVoicePlaying(&gChannelStatus[voice]);
}

void snd_StartVoices(struct VoiceFlags* voices) {
  int count;
  for (count = 0; count < SND_NUM_VOICES; count++) {
    if ((count < SND_VOICES_PER_CORE && (voices->core[0] & (1 << count))) ||
        (count >= SND_VOICES_PER_CORE &&
         (voices->core[1] & (1 << (count - SND_VOICES_PER_CORE))))) {
      snd_MarkVoicePlaying(&gChannelStatus[count]);
      gChannelStatus[count].start_tick = snd_GetTick();
    }
  }
  gAwaitingKeyOn[0] |= voices->core[0];
  gAwaitingKeyOn[1] |= voices->core[1];
}

void snd_StartPendingVoices(void) {
  sceSdBatch batch[6];
  if (gAwaitingKeyOn[0] || gAwaitingKeyOn[1]) {
    batch[0].func = SD_BSET_SWITCH;
    batch[0].entry = SD_S_VMIXEL;
    batch[0].value = gReverbVoices[0];
    batch[1].func = SD_BSET_SWITCH;
    batch[1].entry = SD_S_VMIXER;
    batch[1].value = gReverbVoices[0];
    batch[2].func = SD_BSET_SWITCH;
    batch[2].entry = SD_S_VMIXEL | 1;
    batch[2].value = gReverbVoices[1];
    batch[3].func = SD_BSET_SWITCH;
    batch[3].entry = SD_S_VMIXER | 1;
    batch[3].value = gReverbVoices[1];
    batch[4].func = SD_BSET_SWITCH;
    batch[4].entry = SD_S_KON;
    batch[4].value = gAwaitingKeyOn[0];
    batch[5].func = SD_BSET_SWITCH;
    batch[5].entry = SD_S_KON | 1;
    batch[5].value = gAwaitingKeyOn[1];
    sceSdProcBatch(batch, 0, 6);
    gKeyedOffVoices[0] &= ~gAwaitingKeyOn[0];
    gKeyedOffVoices[1] &= ~gAwaitingKeyOn[1];
    gKeyedOnVoices[0] |= gAwaitingKeyOn[0];
    gKeyedOnVoices[1] |= gAwaitingKeyOn[1];
    gAwaitingKeyOn[0] = gAwaitingKeyOn[1] = 0;
  }
}

void snd_StopPendingVoices(void) {
  if (gAwaitingKeyOff[0] || gAwaitingKeyOff[1]) {
    sceSdSetSwitch(SD_S_KOFF, gAwaitingKeyOff[0]);
    sceSdSetSwitch(SD_S_KOFF | 1, gAwaitingKeyOff[1]);
    gKeyedOnVoices[0] &= ~gAwaitingKeyOff[0];
    gKeyedOnVoices[1] &= ~gAwaitingKeyOff[1];
    gKeyedOffVoices[0] |= gAwaitingKeyOff[0];
    gKeyedOffVoices[1] |= gAwaitingKeyOff[1];
    gAwaitingKeyOff[0] = gAwaitingKeyOff[1] = 0;
  }
}

void snd_GetKeyedOnVoices(struct VoiceFlags* voices) {
  voices->core[0] = gKeyedOnVoices[0];
  voices->core[1] = gKeyedOnVoices[1];
}

void snd_GetKeyedOffVoices(struct VoiceFlags* voices) {
  voices->core[0] = gKeyedOffVoices[0];
  voices->core[1] = gKeyedOffVoices[1];
}

void snd_VoiceIsDone(int voice) {
  if (gChannelStatus[voice].priority != SND_VOICE_PRIORITY_PERMANENT)
    snd_MarkVoiceFree(&gChannelStatus[voice]);
  if (gChannelStatus[voice].owner_proc)
    gChannelStatus[voice].owner_proc(voice, gChannelStatus[voice].owner, 2);
  gChannelStatus[voice].owner_proc = 0;
  gKeyedOffVoices[voice / SND_VOICES_PER_CORE] &= ~(1 << (voice % SND_VOICES_PER_CORE));
}

u_int snd_GetVoiceAge(int voice) {
  if (gChannelStatus[voice].status == 1)
    return snd_GetTick() - gChannelStatus[voice].start_tick;
  return 0;
}

int snd_GetVoiceStatus(int voice) {
  return gChannelStatus[voice].status;
}

int snd_GetVoicePriority(int voice) {
  return gChannelStatus[voice].priority;
}

void snd_KeyOffVoice(int voice) {
  int core;
  int channel;
  if (gChannelStatus[voice].status == 1 && gChannelStatus[voice].owner_proc)
    gChannelStatus[voice].owner_proc(voice, gChannelStatus[voice].owner, 3);
  gChannelStatus[voice].owner_proc = 0;
  core = voice / SND_VOICES_PER_CORE;
  channel = voice % SND_VOICES_PER_CORE;
  if (gAwaitingKeyOn[core] & (1 << channel))
    gAwaitingKeyOn[core] &= ~(1 << channel);
  gAwaitingKeyOff[core] |= 1 << channel;
}

void snd_KeyOffVoicesEx(struct VoiceFlags* voices, int notify_owner) {
  int count;
  int core;
  int channel;
  for (count = 0; count < SND_NUM_VOICES; count++) {
    core = count / SND_VOICES_PER_CORE;
    channel = count % SND_VOICES_PER_CORE;
    if ((voices->core[core] & (1 << channel)) && gChannelStatus[count].status == 1) {
      gAwaitingKeyOff[core] |= 1 << channel;
      if (notify_owner && gChannelStatus[count].owner_proc)
        gChannelStatus[count].owner_proc(count, gChannelStatus[count].owner, 3);
      gChannelStatus[count].owner_proc = 0;
      if (gAwaitingKeyOn[core] & (1 << channel))
        gAwaitingKeyOn[core] &= ~(1 << channel);
    }
  }
}

void snd_SilenceVoicesEx(struct VoiceFlags* voices, int notify_owner) {
  int count;
  int core;
  int channel;
  sceSdBatch batch[2];
  for (count = 0; count < SND_NUM_VOICES; count++) {
    core = count / SND_VOICES_PER_CORE;
    channel = count % SND_VOICES_PER_CORE;
    if (voices->core[core] & (1 << channel)) {
      if (notify_owner && gChannelStatus[count].status == 1 && gChannelStatus[count].owner_proc)
        gChannelStatus[count].owner_proc(count, gChannelStatus[count].owner, 3);
      gChannelStatus[count].owner_proc = 0;
      if (gChannelStatus[count].priority != SND_VOICE_PRIORITY_PERMANENT)
        snd_MarkVoiceFree(&gChannelStatus[count]);
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
      batch[0].value = 0;
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
      batch[1].value = 0;
      sceSdProcBatch(batch, 0, 2);
#if SND989_VERSION == 0x020604
      if (gAwaitingKeyOn[core] & (1 << channel))
        gAwaitingKeyOn[core] &= ~(1 << channel);
#else
      snd_KeyOffVoiceRaw(core, channel);
#endif
    }
  }
}

void snd_PauseVoices(struct VoiceFlags* voices) {
  int count;
  int core;
  int channel;
  sceSdBatch batch[3];
  for (count = 0; count < SND_NUM_VOICES; count++) {
    core = count / SND_VOICES_PER_CORE;
    channel = count % SND_VOICES_PER_CORE;
    if (voices->core[core] & (1 << channel)) {
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
      batch[0].value = 0;
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
      batch[1].value = 0;
      batch[2].func = SD_BSET_PARAM;
      batch[2].entry = core | (channel << 1) | SD_VP_PITCH;
      batch[2].value = 0;
      sceSdProcBatch(batch, 0, 3);
    }
  }
}

#if SND989_VERSION == 0x020608

void snd_PauseVoicesOwnedWithOwner(struct GenericSoundHandler* owner) {
  int count;
  int core;
  int channel;
  sceSdBatch batch[3];
  snd_LockVoiceAllocator(1);
  for (count = 0; count < SND_NUM_VOICES; count++) {
    if (gChannelStatus[count].status && gChannelStatus[count].owner == owner) {
      core = count / SND_VOICES_PER_CORE;
      channel = count % SND_VOICES_PER_CORE;
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
      batch[0].value = 0;
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
      batch[1].value = 0;
      batch[2].func = SD_BSET_PARAM;
      batch[2].entry = core | (channel << 1) | SD_VP_PITCH;
      batch[2].value = 0;
      sceSdProcBatch(batch, 0, 3);
    }
  }
  snd_UnlockVoiceAllocator();
}

#endif

void snd_UnPauseVoices(struct VoiceFlags* voices) {
  int count;
  int core;
  int channel;
  sceSdBatch batch[3];
  int note;
  int fine;
  for (count = 0; count < SND_NUM_VOICES; count++) {
    core = count / SND_VOICES_PER_CORE;
    channel = count % SND_VOICES_PER_CORE;
    if (voices->core[core] & (1 << channel)) {
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
      batch[0].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[count].volume_left,
                                                     gChannelStatus[count].group) >>
                       1;
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
      batch[1].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[count].volume_right,
                                                     gChannelStatus[count].group) >>
                       1;
      batch[2].func = SD_BSET_PARAM;
      batch[2].entry = core | (channel << 1) | SD_VP_PITCH;
      snd_PitchBendTone(gChannelStatus[count].tone, gChannelStatus[count].pitch_bend,
                        gChannelStatus[count].pitch_modifier, gChannelStatus[count].start_note,
                        gChannelStatus[count].start_fine, &note, &fine);
      batch[2].value = PS1Note2Pitch(gChannelStatus[count].tone->center_note,
                                     gChannelStatus[count].tone->center_fine, note, fine);
      sceSdProcBatch(batch, 0, 3);
    }
  }
}

#if SND989_VERSION == 0x020608

void snd_UnPauseVoicesOwnedWithOwner(struct GenericSoundHandler* owner) {
  int count;
  int core;
  int channel;
  sceSdBatch batch[3];
  int note;
  int fine;
  snd_LockVoiceAllocator(1);
  for (count = 0; count < SND_NUM_VOICES; count++) {
    if (gChannelStatus[count].status && gChannelStatus[count].owner == owner) {
      core = count / SND_VOICES_PER_CORE;
      channel = count % SND_VOICES_PER_CORE;
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
      batch[0].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[count].volume_left,
                                                     gChannelStatus[count].group) >>
                       1;
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
      batch[1].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[count].volume_right,
                                                     gChannelStatus[count].group) >>
                       1;
      batch[2].func = SD_BSET_PARAM;
      batch[2].entry = core | (channel << 1) | SD_VP_PITCH;
      snd_PitchBendTone(gChannelStatus[count].tone, gChannelStatus[count].pitch_bend,
                        gChannelStatus[count].pitch_modifier, gChannelStatus[count].start_note,
                        gChannelStatus[count].start_fine, &note, &fine);
      batch[2].value = PS1Note2Pitch(gChannelStatus[count].tone->center_note,
                                     gChannelStatus[count].tone->center_fine, note, fine);
      sceSdProcBatch(batch, 0, 3);
    }
  }
  snd_UnlockVoiceAllocator();
}

#endif

struct VoiceAttributes* snd_GetVoiceStatusPtr(int voice) {
  return &gChannelStatus[voice];
}

u_short PS1Note2Pitch(signed char center_note, signed char center_fine, u_short note, short fine) {
  u_int pitch;
  int ps1;
  int center = center_note;
  if (center < 0) {
    ps1 = 0;
    center = -center;
    center_note = center;
  } else {
    ps1 = 1;
  }
  pitch = sceSdNote2Pitch((u_short)center_note, (u_short)center_fine, note, fine);
  if (ps1)
    pitch = 44100 * pitch / 48000;
  return pitch;
}

void snd_PitchBendTone(struct Tone* tone,
                       int bend,
                       int modifier,
                       int note,
                       int fine,
                       int* new_note,
                       int* new_fine) {
  volatile int unused;
  int pitch = (note << 7) + fine + modifier;
  if (bend < 0)
    pitch += tone->bend_low * (bend << 7) / 0x8000;
  else
    pitch += tone->bend_high * (bend << 7) / 0x7fff;
  *new_note = pitch / 128;
  *new_fine = pitch % 128;
}

void snd_MarkVoicePlaying(struct VoiceAttributes* voice) {
  struct VoiceAttributes* walk;
  voice->status = 1;
  voice->next = 0;
  if (!gPlayingListHead) {
    gPlayingListHead = voice;
  } else {
    walk = gPlayingListHead;
#if SND989_VERSION == 0x020608
    if (voice == walk) {
      snd_ShowError(82, 0, 0, 0, 0);
      return;
    }
    while (walk->next && walk->next != voice)
      walk = walk->next;
    if (walk->next == voice) {
      snd_ShowError(82, 0, 0, 0, 0);
    } else {
      walk->next = voice;
    }
#else
    while (walk->next)
      walk = walk->next;
    walk->next = voice;
#endif
  }
}

void snd_MarkVoiceFree(struct VoiceAttributes* voice) {
  struct VoiceAttributes* walk;
  voice->status = 0;
  if (voice == gPlayingListHead) {
    gPlayingListHead = voice->next;
  } else {
    walk = gPlayingListHead;
    while (walk && walk->next != voice)
      walk = walk->next;
    if (walk)
      walk->next = voice->next;
  }
}

void snd_KeyOnVoiceRaw(int core, int voice) {
  gAwaitingKeyOn[core] |= 1 << voice;
}

void snd_KeyOffVoiceRaw(int core, int voice) {
  gAwaitingKeyOn[core] &= ~(1 << voice);
  gAwaitingKeyOff[core] |= 1 << voice;
}
