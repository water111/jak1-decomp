#ifndef SCE_IOP_989SND_HANDLERS_H
#define SCE_IOP_989SND_HANDLERS_H

#include "sce/iop/989snd/blocksound.h"
#include "sce/iop/types.h"

struct AMEHandler;
struct BasicEffect;
struct BlockSoundHandler;
struct EffectChain;
struct GenericSoundHandler;
struct MIDIHandler;
struct Sound;
struct StreamSoundHandler;

void snd_InitSoundHandlers(void);
struct BlockSoundHandler* snd_GetFreeBlockHandler(void);
struct StreamSoundHandler* snd_GetFreeStreamHandler(void);
struct MIDIHandler* snd_GetFreeMIDIHandler(void);
struct AMEHandler* snd_GetFreeAMEHandler(void);
struct GenericSoundHandler* snd_CheckHandlerStillActive(u_int handle);
void snd_StopHandlerPtr(struct GenericSoundHandler* handler, int and_child, int silence);
void snd_StopAllSounds(void);
void snd_PauseAllSoundsInGroup(u_int groups);
void snd_ContinueAllSoundsInGroup(u_int groups);
void snd_StopAllSoundsInGroup(u_int groups);
void snd_ActivateHandler(struct GenericSoundHandler* handler);
void snd_DeactivateHandler(struct GenericSoundHandler* handler, int and_child);
void snd_PauseHandlerPtr(struct GenericSoundHandler* handler, int and_child);
void snd_ContinueHandlerPtr(struct GenericSoundHandler* handler, int and_child);
void snd_StopHandler(u_int handle, int and_child, int silence);
void snd_StopAllHandlersForSound(struct Sound* sound, int silence);
int snd_GetNextHandlerVoice(struct GenericSoundHandler* handler, int start_voice);
void snd_UpdateHandlers(void);
int snd_UpdateEffect(struct EffectChain* effect, struct GenericSoundHandler* owner);
void snd_FreeEffectChain(struct EffectChain* effect);
void snd_RemoveEffect(struct GenericSoundHandler* handler, int type_flag);
struct EffectChain* snd_FindEffect(struct GenericSoundHandler* handler, int type_flag);
struct BasicEffect* snd_GetFreeBasicEffect(void);
void snd_AddEffectToHandler(struct GenericSoundHandler* handler, struct EffectChain* effect);
void snd_RemoveEffectFromHandler(struct GenericSoundHandler* handler, struct EffectChain* effect);
#if SND989_VERSION == 0x020608
void snd_InitHandlers(struct GenericSoundHandler* handlers, int count, u_int type, int size);
struct GenericSoundHandler* snd_FindFreeHandler(struct GenericSoundHandler* handlers,
                                                int count,
                                                u_int type,
                                                int size);
#endif
struct VAGStreamHeader;
struct VAGStreamQueEntry;

struct VAGStreamHandler {
  struct GenericSoundHandler SH;
  int num_streams;
  signed char group_volumes[16];
  struct VAGStreamHeader* queued_stream;
  struct VAGStreamQueEntry* queue;
};
extern struct VAGStreamHandler* gVAGStreamHandler;
struct VAGStreamHandler* snd_GetFreeVAGStreamHandler(void);

struct StreamSoundHandler {
  struct GenericSoundHandler SH;
  signed char Current_Note;
  signed char Current_Fine;
  short Current_PB;
  u_int payload[2];
};
struct SoundBank;

struct MIDIHandler {
  struct GenericSoundHandler SH;
  struct SoundBank* bank;
  signed char* data_start;
  signed char* play_position;
  u_int tick_countdown;
  int tick_delta;
  u_int tempo;
  int pulses_per_tick;
  int tick_error;
  short ppq;
  short mute_state;
  signed char channel_program[16];
  signed char channel_volume[16];
  short channel_pan[16];
  short channel_pitch_bend[16];
  short damper_state;
  u_char midi_flags;
  u_char running_status;
  signed char repeats;
  u_char reserved1;
  u_short reserved2;
};

struct AMEGroup {
  signed char channel_count;
  signed char basis;
  short reserved;
  signed char channel[16];
  signed char excite_min[16];
  signed char excite_max[16];
};

struct AMEHandler {
  struct GenericSoundHandler SH;
  struct AMEGroup groups[16];
  signed char registers[16];
  u_char* macros[16];
};
#endif
