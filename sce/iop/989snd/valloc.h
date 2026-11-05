#ifndef SCE_IOP_989SND_VALLOC_H
#define SCE_IOP_989SND_VALLOC_H

#include "sce/iop/types.h"

struct GenericSoundHandler;
struct Tone;
struct VoiceAttributes;
struct VoiceFlags;

void snd_InitVoiceAllocator(void);
int snd_AllocateVoice(int group, int priority);
void snd_StartVAGVoice(int voice);
void snd_StartPendingVoices(void);
void snd_VoiceIsDone(int voice);
void snd_KeyOffVoice(int voice);
void snd_KeyOffVoicesEx(struct VoiceFlags* voices, int notify_owner);
void snd_SilenceVoicesEx(struct VoiceFlags* voices, int notify_owner);
void snd_UnPauseVoices(struct VoiceFlags* voices);
void snd_SetMixerMode(int channel_mode, int reverb_mode);
void snd_SetGroupVoiceRange(int group, int min, int max);
int snd_GetVoiceStatus(int voice);
int snd_LockVoiceAllocator(int blocking);
int snd_ExternVoiceAlloc(int group, int priority);
void snd_ExternVoiceFree(int voice);
void snd_UnlockVoiceAllocator(void);
void snd_KeyOnVoiceRaw(int core, int voice);
void snd_KeyOffVoiceRaw(int core, int voice);
int snd_IsVoiceAllocatorLocked(void);
void snd_StartVoices(struct VoiceFlags* voices);
void snd_StopPendingVoices(void);
void snd_GetKeyedOnVoices(struct VoiceFlags* voices);
void snd_GetKeyedOffVoices(struct VoiceFlags* voices);
u_int snd_GetVoiceAge(int voice);
struct VoiceAttributes* snd_GetVoiceStatusPtr(int voice);
int snd_CheckAllocatorCueTick(void);
void snd_PauseVoices(struct VoiceFlags* voices);
void snd_PitchBendTone(struct Tone* tone,
                       int bend,
                       int modifier,
                       int note,
                       int fine,
                       int* new_note,
                       int* new_fine);
u_short PS1Note2Pitch(signed char center_note, signed char center_fine, u_short note, short fine);
void snd_MarkVoicePlaying(struct VoiceAttributes* voice);
void snd_MarkVoiceFree(struct VoiceAttributes* voice);
int snd_GetVoicePriority(int voice);
#if SND989_VERSION == 0x020608
void snd_CleanupVoiceAllocator(void);
void snd_PauseVoicesOwnedWithOwner(struct GenericSoundHandler* owner);
void snd_UnPauseVoicesOwnedWithOwner(struct GenericSoundHandler* owner);
#endif

#define SND_VOICES_PER_CORE 24
#define SND_NUM_VOICES 48
#define SND_VOICE_GROUPS 8
#define SND_VOICE_PRIORITY_PERMANENT 127
#define SND_TONE_REVERB 1

struct Tone {
  signed char priority;
  signed char volume;
  signed char center_note;
  signed char center_fine;
  short pan;
  signed char map_low;
  signed char map_high;
  signed char bend_low;
  signed char bend_high;
  u_short adsr1;
  u_short adsr2;
  u_short flags;
  u_int sample_address;
  u_int reserved;
};
struct Prog;

struct VoiceAttributes {
  struct VoiceAttributes* next;
  int voice;
  u_int status;
  struct GenericSoundHandler* owner;
  void (*owner_proc)(int voice, struct GenericSoundHandler* owner, int event);
  struct Tone* tone;
  signed char start_note;
  signed char start_fine;
  u_char priority;
  signed char group;
  u_int start_tick;
  short volume_left;
  short volume_right;
  short pitch_bend;
  short pitch_modifier;
  u_int reserved1;
  signed char midi_channel;
  u_char velocity;
  u_short reserved2;
  u_int midi_should_be_off;
  struct Prog* program;
};

extern struct VoiceAttributes gChannelStatus[SND_NUM_VOICES];
extern struct VoiceAttributes* gPlayingListHead;

struct VoiceFlags {
  u_int core[2];
};

struct VoiceRange {
  short min;
  short max;
};

extern int gChannelMode;
extern int gReverbMode;
extern struct VoiceRange gVoiceRanges[SND_VOICE_GROUPS];

#endif
