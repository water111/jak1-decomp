#ifndef SCE_IOP_989SND_GRAIN_H
#define SCE_IOP_989SND_GRAIN_H

#include "sce/iop/989snd/valloc.h"
#include "sce/iop/types.h"
#define SND_GRAIN_ON_STOP_MARKER 43

struct SndGrainLFOSettings {
  u_char index, target, target_extra, type;
  u_short duty_cycle, depth;
  u_char flags, reserved;
  u_short start_offset;
  u_int step_size;
};

struct SFXGrain2 {
  int type;
  short delay;
  short reserved;

  union {
    u_int words[8];
    struct Tone tone;
    struct SndGrainLFOSettings lfo;
    short parameters[16];
  } data;
};
struct BlockSoundHandler;
struct SFX2;
typedef int (*SndGrainProc)(struct BlockSoundHandler*, struct SFX2*, struct SFXGrain2*);
extern SndGrainProc gGrainHandler[];
int snd_DoGrain(struct BlockSoundHandler* handler);
int snd_GetOnStopGrainIndex(struct BlockSoundHandler* handler);
int snd_SFX_GRAIN_TYPE_NULL(struct BlockSoundHandler* handler,
                            struct SFX2* sound,
                            struct SFXGrain2* grain);
int snd_SFX_GRAIN_TYPE_TONE(struct BlockSoundHandler* handler,
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
#endif
