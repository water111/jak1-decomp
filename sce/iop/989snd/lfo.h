#ifndef SCE_IOP_989SND_LFO_H
#define SCE_IOP_989SND_LFO_H

#include "sce/iop/types.h"

struct BlockSoundHandler;
struct LFOTracker;

u_int snd_DoOneLFO(struct LFOTracker* tracker);
void snd_InitLFO(struct LFOTracker* tracker);
void snd_RemoveLFO(struct LFOTracker* lfo);
void snd_RemoveLFOsForHandler(struct BlockSoundHandler* handler);
void snd_HandleLFOs(void);
int snd_InsertLFO(struct LFOTracker* lfo);
void snd_CalcLFODepth(struct LFOTracker* lfo);
int snd_GetLFO(struct LFOTracker* tracker, int step_mult);
int snd_LFO_TYPE_OFF(struct LFOTracker* tracker, int step);
int snd_LFO_TYPE_SINE(struct LFOTracker* tracker, int step);
int snd_LFO_TYPE_SQUARE(struct LFOTracker* tracker, int step);
int snd_LFO_TYPE_TRIANGLE(struct LFOTracker* tracker, int step);
int snd_LFO_TYPE_SAW(struct LFOTracker* tracker, int step);
int snd_LFO_TYPE_RAND(struct LFOTracker* tracker, int step);

struct LFOTracker {
  u_short type;
  u_char target;
  u_char target_extra;
  u_char setup_flags;
  u_char running_flags;
  u_short depth;
  u_int orig_depth;
  int next_step;
  u_int step_size;
  u_int orig_step_size;
  int state_hold1;
  int state_hold2;
  int range;
  int last_lfo;
  struct BlockSoundHandler* handler;
  struct LFOTracker* next;
};

enum LFOType {
  LFO_TYPE_OFF,
  LFO_TYPE_SINE,
  LFO_TYPE_SQUARE,
  LFO_TYPE_TRIANGLE,
  LFO_TYPE_SAW,
  LFO_TYPE_RAND
};

enum LFOTarget {
  LFO_TARGET_VOL = 1,
  LFO_TARGET_PAN,
  LFO_TARGET_PM,
  LFO_TARGET_PB,
  LFO_TARGET_STEP,
  LFO_TARGET_DEPTH
};

extern short gLFO_sine[2048];
extern struct LFOTracker* gActiveLFOs;
extern int gNumLFOsInUse;
#endif
