#ifndef SCE_IOP_989SND_EFFECTS_H
#define SCE_IOP_989SND_EFFECTS_H

#include "sce/iop/types.h"
struct BasicEffect;
struct GenericSoundHandler;

typedef struct EffectChain {
  u_int flags;
  short delta_counter;
  short delta_type;
  struct EffectChain* next;
  int (*proc)(struct BasicEffect*, struct GenericSoundHandler*);
} EffectChain;

typedef struct BasicEffect {
  EffectChain ec;
  int value;
  short delta_value;
  short delta_time;
  short destination;
  short pad;
} BasicEffect;

int snd_UpdateEffect(EffectChain* effect, struct GenericSoundHandler* owner);
void snd_FreeEffectChain(EffectChain* effect);
void snd_RemoveEffect(struct GenericSoundHandler* handler, int type_flag);
EffectChain* snd_FindEffect(struct GenericSoundHandler* handler, int type_flag);
BasicEffect* snd_GetFreeBasicEffect(void);
void snd_AddEffectToHandler(struct GenericSoundHandler* handler, EffectChain* effect);
void snd_RemoveEffectFromHandler(struct GenericSoundHandler* handler, EffectChain* effect);
#endif
