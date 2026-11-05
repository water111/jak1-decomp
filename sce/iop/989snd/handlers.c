#include "sce/iop/989snd/handlers.h"

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/blocksound.h"
#include "sce/iop/989snd/effects.h"
#include "sce/iop/989snd/loader_midi.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/thbase/thread.h"

struct GenericSoundHandler* gActiveSoundListHead = 0;
struct GenericSoundHandler* gActiveSoundListTail = 0;
extern struct StreamSoundHandler gStreamSoundHandler[48];
extern struct BlockSoundHandler gBlockSoundHandler[64];
extern struct MIDIHandler gMIDIHandler[32];
extern struct AMEHandler gAMEHandler[4];
extern struct BasicEffect gBasicEffect[64];

#if SND989_VERSION == 0x020604
#define INIT_HANDLERS(array, count, type)              \
  for (i = 0; i < count; i++) {                        \
    array[i].SH.OwnerID = 1;                           \
    array[i].SH.OwnerID |= (i << 16) | (type << 24);   \
    array[i].SH.Sound = 0;                             \
    array[i].SH.Effects = 0;                           \
    array[i].SH.flags = 0;                             \
    array[i].SH.VolGroup = 0;                          \
    array[i].SH.Voices[0] = array[i].SH.Voices[1] = 0; \
  }
#endif

void snd_PauseVAGStream(u_int handle);
void snd_ContinueVAGStream(u_int handle);
void snd_StopSound(u_int handle);

void snd_StopVAGStream(u_int handle);

int snd_ProcessMIDITick(struct MIDIHandler* handler);
void snd_ProcessVAGStreamTick(struct VAGStreamHandler* handler);
#if SND989_VERSION == 0x020608

void snd_InitHandlers(struct GenericSoundHandler* handlers, int count, u_int type, int size) {
  int i;
  for (i = 0; i < count; i++) {
    handlers->OwnerID = 1;
    handlers->OwnerID |= (i << 16) | (type << 24);
    handlers->Sound = 0;
    handlers->Effects = 0;
    handlers->flags = 0;
    handlers->VolGroup = 0;
    handlers->Voices[1] = 0;
    handlers->Voices[0] = 0;
    handlers = (struct GenericSoundHandler*)((char*)handlers + size);
  }
}

#endif

void snd_InitSoundHandlers(void) {
  int i;
#if SND989_VERSION == 0x020604
  INIT_HANDLERS(gStreamSoundHandler, 48, 0);
  INIT_HANDLERS(gBlockSoundHandler, 64, 5);
  INIT_HANDLERS(gMIDIHandler, 32, 1);
  INIT_HANDLERS(gAMEHandler, 4, 2);
#else
  snd_InitHandlers(&gStreamSoundHandler[0].SH, 48, 0, sizeof(struct StreamSoundHandler));
  snd_InitHandlers(&gBlockSoundHandler[0].SH, 64, 5, sizeof(struct BlockSoundHandler));
  snd_InitHandlers(&gMIDIHandler[0].SH, 32, 1, sizeof(struct MIDIHandler));
  snd_InitHandlers(&gAMEHandler[0].SH, 4, 2, sizeof(struct AMEHandler));
#endif
  for (i = 0; i < 64; i++) {
    gBasicEffect[i].ec.flags = 0;
    gBasicEffect[i].ec.next = 0;
  }
  gActiveSoundListTail = 0;
  gActiveSoundListHead = 0;
}

#if SND989_VERSION == 0x020608

struct GenericSoundHandler* snd_FindFreeHandler(struct GenericSoundHandler* handlers,
                                                int count,
                                                u_int type,
                                                int size) {
  int i;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (i = 0; i < count; i++) {
    if (!handlers->Sound) {
      handlers->OwnerID = (u_short)(handlers->OwnerID + 1) | (i << 16) | (type << 24);
      handlers->next = 0;
      handlers->parent = 0;
      handlers->child = 0;
      handlers->flags = 0;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return handlers;
    }
    handlers = (struct GenericSoundHandler*)((char*)handlers + size);
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
}

#endif

struct BlockSoundHandler* snd_GetFreeBlockHandler(void) {
#if SND989_VERSION == 0x020604
  int i;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (i = 0; i < 64; i++) {
    if (!gBlockSoundHandler[i].SH.Sound) {
      gBlockSoundHandler[i].SH.OwnerID =
          (u_short)(gBlockSoundHandler[i].SH.OwnerID + 1) | (i << 16) | (5u << 24);
      gBlockSoundHandler[i].SH.next = 0;
      gBlockSoundHandler[i].SH.parent = 0;
      gBlockSoundHandler[i].SH.child = 0;
      gBlockSoundHandler[i].SH.flags = 0;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return &gBlockSoundHandler[i];
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
#else
  return (struct BlockSoundHandler*)snd_FindFreeHandler(&gBlockSoundHandler[0].SH, 64, 5,
                                                        sizeof(struct BlockSoundHandler));
#endif
}

struct StreamSoundHandler* snd_GetFreeStreamHandler(void) {
#if SND989_VERSION == 0x020604
  int i;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (i = 0; i < 48; i++) {
    if (!gStreamSoundHandler[i].SH.Sound) {
      gStreamSoundHandler[i].SH.OwnerID =
          (u_short)(gStreamSoundHandler[i].SH.OwnerID + 1) | (i << 16) | (0u << 24);
      gStreamSoundHandler[i].SH.next = 0;
      gStreamSoundHandler[i].SH.parent = 0;
      gStreamSoundHandler[i].SH.child = 0;
      gStreamSoundHandler[i].SH.flags = 0;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return &gStreamSoundHandler[i];
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
#else
  return (struct StreamSoundHandler*)snd_FindFreeHandler(&gStreamSoundHandler[0].SH, 48, 0,
                                                         sizeof(struct StreamSoundHandler));
#endif
}

struct MIDIHandler* snd_GetFreeMIDIHandler(void) {
#if SND989_VERSION == 0x020604
  int i;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (i = 0; i < 32; i++) {
    if (!gMIDIHandler[i].SH.Sound) {
      gMIDIHandler[i].SH.OwnerID =
          (u_short)(gMIDIHandler[i].SH.OwnerID + 1) | (i << 16) | (1u << 24);
      gMIDIHandler[i].SH.next = 0;
      gMIDIHandler[i].SH.parent = 0;
      gMIDIHandler[i].SH.child = 0;
      gMIDIHandler[i].SH.flags = 0;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return &gMIDIHandler[i];
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
#else
  return (struct MIDIHandler*)snd_FindFreeHandler(&gMIDIHandler[0].SH, 32, 1,
                                                  sizeof(struct MIDIHandler));
#endif
}

struct AMEHandler* snd_GetFreeAMEHandler(void) {
#if SND989_VERSION == 0x020604
  int i;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (i = 0; i < 4; i++) {
    if (!gAMEHandler[i].SH.Sound) {
      gAMEHandler[i].SH.OwnerID = (u_short)(gAMEHandler[i].SH.OwnerID + 1) | (i << 16) | (2u << 24);
      gAMEHandler[i].SH.next = 0;
      gAMEHandler[i].SH.parent = 0;
      gAMEHandler[i].SH.child = 0;
      gAMEHandler[i].SH.flags = 0;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return &gAMEHandler[i];
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
#else
  return (struct AMEHandler*)snd_FindFreeHandler(&gAMEHandler[0].SH, 4, 2,
                                                 sizeof(struct AMEHandler));
#endif
}

void snd_ActivateHandler(struct GenericSoundHandler* handler) {
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  handler->OwnerID |= 0x80000000;
  handler->next = 0;
  if (!gActiveSoundListTail)
    gActiveSoundListHead = handler;
  else
    gActiveSoundListTail->next = handler;
  gActiveSoundListTail = handler;
  if (!disabled)
    CpuResumeIntr(intr_state);
}

void snd_DeactivateHandler(struct GenericSoundHandler* handler, int and_child)

{
  struct GenericSoundHandler* previous = 0;
  struct GenericSoundHandler* walk = gActiveSoundListHead;
  int intr_state;
  int disabled;
#if SND989_VERSION == 0x020604
  if (!(handler->OwnerID & 0x80000000))
    return;
  disabled = CpuSuspendIntr(&intr_state);
#else
  disabled = CpuSuspendIntr(&intr_state);
  if (!(handler->OwnerID & 0x80000000)) {
    handler->Sound = 0;
    if (!disabled)
      CpuResumeIntr(intr_state);
    return;
  }
#endif
  handler->Sound = 0;
  handler->OwnerID &= 0x7fffffff;
  while (walk && walk != handler) {
    previous = walk;
    walk = previous->next;
  }
  if (!walk) {
    if (!disabled)
      CpuResumeIntr(intr_state);
    return;
  }
  if (!previous)
    gActiveSoundListHead = walk->next;
  else
    previous->next = walk->next;
  if (!walk->next)
    gActiveSoundListTail = previous;
  if (handler->Effects) {
    snd_FreeEffectChain(handler->Effects);
    handler->Effects = 0;
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  if (and_child && handler->child)
    snd_DeactivateHandler(handler->child, and_child);
}

struct GenericSoundHandler* snd_CheckHandlerStillActive(u_int handle) {
  int index = (handle >> 16) & 0xff;
  int type = (handle >> 24) & 0x1f;
  if (type == 5 && gBlockSoundHandler[index].SH.OwnerID == handle)
    return &gBlockSoundHandler[index].SH;
  if (type == 0 && gStreamSoundHandler[index].SH.OwnerID == handle)
    return &gStreamSoundHandler[index].SH;
  if (type == 1 && gMIDIHandler[index].SH.OwnerID == handle)
    return &gMIDIHandler[index].SH;
  if (type == 2 && gAMEHandler[index].SH.OwnerID == handle)
    return &gAMEHandler[index].SH;
  if (type == 4 && gVAGStreamHandler[index].SH.OwnerID == handle)
    return &gVAGStreamHandler[index].SH;
  return 0;
}

void snd_StopHandler(u_int handle, int and_child, int silence)

{
  int index = (handle >> 16) & 0xff;
  int type = (handle >> 24) & 0x1f;
  struct GenericSoundHandler* handler;
  if (type == 0)
    handler = &gStreamSoundHandler[index].SH;
  else if (type == 5)
    handler = &gBlockSoundHandler[index].SH;
  else if (type == 1)
    handler = &gMIDIHandler[index].SH;
  else if (type == 2)
    handler = &gAMEHandler[index].SH;
  else if (type == 4)
    handler = &gVAGStreamHandler[index].SH;
  else
    return;
  snd_StopHandlerPtr(handler, and_child, silence);
}

void snd_PauseHandlerPtr(struct GenericSoundHandler* handler, int and_child)

{
  handler->flags |= 2;
#if SND989_VERSION == 0x020604
  snd_PauseVoices((struct VoiceFlags*)handler->Voices);
#else
  snd_PauseVoicesOwnedWithOwner(handler);
#endif
  if (and_child && handler->child)
    snd_PauseHandlerPtr(handler->child, 1);
}

void snd_ContinueHandlerPtr(struct GenericSoundHandler* handler, int and_child)

{
#if SND989_VERSION == 0x020604
  snd_UnPauseVoices((struct VoiceFlags*)handler->Voices);
#else
  snd_UnPauseVoicesOwnedWithOwner(handler);
#endif
  if (and_child && handler->child)
    snd_ContinueHandlerPtr(handler->child, 1);
  handler->flags &= ~2;
}

void snd_PauseAllSoundsInGroup(u_int groups) {
  struct GenericSoundHandler* walk = gActiveSoundListHead;
  int type;
  snd_LockMasterTick();
  for (; walk; walk = walk->next) {
    if ((groups & (1 << walk->VolGroup)) && !(walk->flags & 2)) {
      type = (walk->OwnerID >> 24) & 0x1f;
      if (type == 4)
        snd_PauseVAGStream(walk->OwnerID);
      else
        snd_PauseHandlerPtr(walk, 1);
    }
  }
  snd_UnlockMasterTick();
}

void snd_StopAllSoundsInGroup(u_int groups) {
  struct GenericSoundHandler* walk = gActiveSoundListHead;
  snd_LockMasterTick();
  for (; walk; walk = walk->next) {
    if (groups & (1 << walk->VolGroup))
      snd_StopSound(walk->OwnerID);
  }
  snd_UnlockMasterTick();
}

void snd_ContinueAllSoundsInGroup(u_int groups) {
  struct GenericSoundHandler* walk = gActiveSoundListHead;
  int type;
  snd_LockMasterTick();
  for (; walk; walk = walk->next) {
    if ((groups & (1 << walk->VolGroup)) && (walk->flags & 2)) {
      type = (walk->OwnerID >> 24) & 0x1f;
      if (type == 4)
        snd_ContinueVAGStream(walk->OwnerID);
      else
        snd_ContinueHandlerPtr(walk, 1);
    }
  }
  snd_UnlockMasterTick();
}

void snd_StopAllSounds(void) {
  struct GenericSoundHandler* walk = gActiveSoundListHead;
  struct VoiceFlags voices;
  snd_LockMasterTick();
  for (; walk; walk = walk->next) {
    if (!walk->parent)
      snd_StopHandlerPtr(walk, 1, 1);
  }
  voices.core[0] = -1;
  voices.core[1] = -1;
  snd_SilenceVoicesEx(&voices, 1);
  snd_UnlockMasterTick();
#if SND989_VERSION == 0x020608
  while (gActiveSoundListHead)
    DelayThread(8000);
#endif
}

void snd_StopHandlerPtr(struct GenericSoundHandler* handler, int and_child, int silence) {
  int do_voice_and_deactivate = 1;
  int type = (handler->OwnerID >> 24) & 0x1f;
  if (type == 5)
    do_voice_and_deactivate = !snd_DoBlockSoundStop((struct BlockSoundHandler*)handler);
  else if (type == 2)
    ((MIDISound*)handler->Sound)->midi->flags &= ~2;
  else if (type == 4) {
    snd_StopVAGStream(handler->OwnerID);
    do_voice_and_deactivate = 0;
  }
  if (do_voice_and_deactivate) {
    snd_LockVoiceAllocator(1);
    if (silence || (handler->flags & 2))
      snd_SilenceVoicesEx((struct VoiceFlags*)handler->Voices, 1);
    else
      snd_KeyOffVoicesEx((struct VoiceFlags*)handler->Voices, 1);
    snd_UnlockVoiceAllocator();
    snd_DeactivateHandler(handler, 0);
  }
  if (handler->parent)
    handler->parent->child = handler->child;
  if (handler->child) {
    handler->child->parent = handler->parent;
    if (and_child)
      snd_StopHandlerPtr(handler->child, and_child, silence);
  }
  handler->child = 0;
}

void snd_StopAllHandlersForSound(Sound* sound, int silence) {
  struct GenericSoundHandler* walk;
  for (walk = gActiveSoundListHead; walk; walk = walk->next) {
    if (walk->Sound == sound) {
#if SND989_VERSION == 0x020604
      snd_StopHandler(walk->OwnerID, 1, 0);
#else
      snd_StopHandler(walk->OwnerID, 1, silence);
#endif
    }
  }
}

int snd_GetNextHandlerVoice(struct GenericSoundHandler* handler, int start_voice) {
  int voice;
  int core;
  int core_voice;
  for (voice = start_voice; voice < 48; voice++) {
    core = voice / 24;
    core_voice = voice % 24;
    if (handler->Voices[core] & (1 << core_voice))
      return voice;
  }
  return -1;
}

void snd_UpdateHandlers(void) {
  struct GenericSoundHandler* walk = gActiveSoundListHead;
  int type;
  int stop = 0;
  for (; walk; walk = walk->next) {
    type = (walk->OwnerID >> 24) & 0x1f;
    switch (type) {
      case 0:
        break;
      case 5:
        if (!(walk->flags & 2))
          stop = snd_ProcessBlockSoundTick((struct BlockSoundHandler*)walk);
        break;
      case 1:
      case 3:
        if (!(walk->flags & 2))
          stop = snd_ProcessMIDITick((struct MIDIHandler*)walk);
        break;
      case 2:
        if (!walk->child)
          stop = 1;
        break;
      case 4:
        snd_ProcessVAGStreamTick((struct VAGStreamHandler*)walk);
        break;
    }
    if (!stop && walk->Effects && !(walk->flags & 2))
      stop = snd_UpdateEffect(walk->Effects, walk);
    if (stop) {
      snd_StopHandlerPtr(walk, 0, 0);
      stop = 0;
    }
  }
}

int snd_UpdateEffect(EffectChain* effect, struct GenericSoundHandler* owner) {
  if (effect->next && snd_UpdateEffect(effect->next, owner) == 1)
    return 1;
  if (!(effect->flags & 0x80000000)) {
    if (effect->delta_type == 1)
      effect->delta_counter--;
    if (effect->delta_counter == 0)
      goto update;
  }
  return 0;
update:
  return effect->proc((BasicEffect*)effect, owner);
}

void snd_FreeEffectChain(EffectChain* effect) {
  // Bug -- doesn't free anything, just iterates?
  if (effect->next)
    snd_FreeEffectChain(effect->next);
}

void snd_RemoveEffect(struct GenericSoundHandler* handler, int type_flag) {
  EffectChain* found = snd_FindEffect(handler, type_flag);
  if (found)
    snd_RemoveEffectFromHandler(handler, found);
}

EffectChain* snd_FindEffect(struct GenericSoundHandler* handler, int type_flag) {
  EffectChain* walk = handler->Effects;
  while (walk && !(walk->flags & type_flag))
    walk = walk->next;
  return walk;
}

BasicEffect* snd_GetFreeBasicEffect(void) {
  int i;
  for (i = 0; i < 64; i++) {
    if (!(gBasicEffect[i].ec.flags & 1)) {
      gBasicEffect[i].ec.flags = 1;
      gBasicEffect[i].ec.next = 0;
      return &gBasicEffect[i];
    }
  }
  return 0;
}

void snd_AddEffectToHandler(struct GenericSoundHandler* handler, EffectChain* effect) {
  EffectChain* walk;
  if (!handler->Effects)
    handler->Effects = effect;
  else {
    walk = handler->Effects;
    while (walk->next)
      walk = walk->next;
    walk->next = effect;
  }
}

void snd_RemoveEffectFromHandler(struct GenericSoundHandler* handler, EffectChain* effect) {
  int intr_state;
  int disabled;
  EffectChain* walk;
  if (effect) {
    disabled = CpuSuspendIntr(&intr_state);
    effect->flags &= ~1;
    if (effect == handler->Effects)
      handler->Effects = effect->next;
    else {
      walk = handler->Effects;
      while (walk) {
        if (walk->next == effect) {
          walk->next = effect->next;
          walk = 0;
        } else
          walk = walk->next;
      }
    }
    if (!disabled)
      CpuResumeIntr(intr_state);
  }
}
