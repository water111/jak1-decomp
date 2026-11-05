#ifndef SCE_IOP_989SND_BLOCKSOUND_H
#define SCE_IOP_989SND_BLOCKSOUND_H

#include "sce/iop/989snd/lfo.h"
#include "sce/iop/types.h"

struct BlockSoundHandler;
struct GenericSoundHandler;
struct SFX2;
struct SFXBlock2;
struct SFXGrain2;

u_int snd_PlaySFX(struct SFXBlock2* block,
                  int index,
                  int volume,
                  int pan,
                  short pitch_modifier,
                  short bend);
int snd_SFX_GRAIN_TYPE_TONE(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain);
int snd_DoGrain(struct BlockSoundHandler* handler);
void snd_SetSFXVolPan(u_int handle, int volume, int pan);
void snd_UpdateSFXPitch(struct BlockSoundHandler* handler);
void snd_SetSFXPitch(u_int handle, int pitch);
void snd_SetSFXPitchbend(u_int handle, short bend);
void snd_SetSFXPitchModifier(u_int handle, short modifier);
void snd_SFXOwnerProc(int voice, struct GenericSoundHandler* owner, int event);
int snd_DoesSFXLoop(struct SFXBlock2* block, int sound);
int snd_ProcessBlockSoundTick(struct BlockSoundHandler* handler);
int snd_DoBlockSoundStop(struct BlockSoundHandler* handler);
int snd_CalcSoundNameHash(const char* name);
int snd_GetOnStopGrainIndex(struct BlockSoundHandler* handler);
u_int snd_PlaySFXGrain(struct SFXBlock2* block, int sound_index, int grain_index);
int snd_SFX_GRAIN_TYPE_NULL(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_XREF_ID(struct BlockSoundHandler* handler,
                               struct SFX2* sound,
                               struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_XREF_NUM(struct BlockSoundHandler* handler,
                                struct SFX2* sound,
                                struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_LFO_SETTINGS(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_CONTROL_NULL(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_LOOP_START(struct BlockSoundHandler* handler,
                                  struct SFX2* sound,
                                  struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_LOOP_END(struct BlockSoundHandler* handler,
                                struct SFX2* sound,
                                struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_LOOP_CONTINUE(struct BlockSoundHandler* handler,
                                     struct SFX2* sound,
                                     struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_STOP(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_RAND_PLAY(struct BlockSoundHandler* handler,
                                 struct SFX2* sound,
                                 struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_RAND_DELAY(struct BlockSoundHandler* handler,
                                  struct SFX2* sound,
                                  struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_RAND_PB(struct BlockSoundHandler* handler,
                               struct SFX2* sound,
                               struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_PB(struct BlockSoundHandler* handler,
                          struct SFX2* sound,
                          struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_ADD_PB(struct BlockSoundHandler* handler,
                              struct SFX2* sound,
                              struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_SET_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_SET_REGISTER_RAND(struct BlockSoundHandler* handler,
                                         struct SFX2* sound,
                                         struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_INC_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_DEC_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_TEST_REGISTER(struct BlockSoundHandler* handler,
                                     struct SFX2* sound,
                                     struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_MARKER(struct BlockSoundHandler* handler,
                              struct SFX2* sound,
                              struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_GOTO_MARKER(struct BlockSoundHandler* handler,
                                   struct SFX2* sound,
                                   struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_GOTO_RANDOM_MARKER(struct BlockSoundHandler* handler,
                                          struct SFX2* sound,
                                          struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_WAIT_FOR_ALL_VOICES(struct BlockSoundHandler* handler,
                                           struct SFX2* sound,
                                           struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_PLAY_CYCLE(struct BlockSoundHandler* handler,
                                  struct SFX2* sound,
                                  struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_ADD_REGISTER(struct BlockSoundHandler* handler,
                                    struct SFX2* sound,
                                    struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_KEY_OFF_VOICES(struct BlockSoundHandler* handler,
                                      struct SFX2* sound,
                                      struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_KILL_VOICES(struct BlockSoundHandler* handler,
                                   struct SFX2* sound,
                                   struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_ON_STOP_MARKER(struct BlockSoundHandler* handler,
                                      struct SFX2* sound,
                                      struct SFXGrain2* grain);
#if SND989_VERSION == 0x020608
int snd_GetSoundIndexInBlock(struct SFXBlock2* block, struct SFX2* sound);
#endif

#define SND_SFX_LOOP 1

struct EffectChain;

struct SFX2 {
  signed char Vol;
  signed char VolGroup;
  short Pan;
  signed char NumGrains;
  signed char InstanceLimit;
  u_short Flags;
  struct SFXGrain2* FirstGrain;
};

struct GenericSoundHandler {
  u_int OwnerID;
  void* Sound;
  struct EffectChain* Effects;
  short Original_Vol;
  short Original_Pan;
  short Current_Vol;
  short Current_Pan;
  short Current_PM;
  u_char flags;
  signed char VolGroup;
  u_int Voices[2];
  struct GenericSoundHandler* next;
  struct GenericSoundHandler* parent;
  struct GenericSoundHandler* child;
};

struct BlockSoundHandler {
  struct GenericSoundHandler SH;
  signed char Current_Note;
  signed char Current_Fine;
  short Current_PB;
  short LFO_Vol;
  short App_Vol;
  short LFO_Pan;
  short App_Pan;
  short LFO_PB;
  short App_PB;
  short LFO_PM;
  short App_PM;
  short CountDown;
  short NextGrain;
  signed char Registers[4];
  signed char sk_doing_skipping_play;
  signed char pad;
  short sk_grains_to_play;
  short sk_grains_to_skip;
  u_short pad2;
#if SND989_VERSION == 0x020608
  struct SFXBlock2* block;
#endif
  struct LFOTracker lfo[4];
};

#endif
