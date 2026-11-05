#ifndef SCE_IOP_989SND_MIDI_H
#define SCE_IOP_989SND_MIDI_H

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/loader_midi.h"
#include "sce/iop/types.h"

struct GenericSoundHandler;
struct MIDIBlockHeader;
struct MIDIHandler;
struct MIDISound;

int snd_ProcessMIDITick(struct MIDIHandler* stream);
void snd_MIDINoteOn(struct MIDIHandler* handler);
void snd_PitchBend(struct MIDIHandler* handler);
void snd_SetMIDIHandlerVolumePan(struct MIDIHandler* handler, int volume, int pan);
void snd_SetMIDIHandlerPitchModifier(struct MIDIHandler* handler, short modifier);
void snd_MIDINoteOff(struct MIDIHandler* handler);
void snd_ReleaseDamper(struct MIDIHandler* handler);
void snd_ResetControllers(struct MIDIHandler* handler);
u_int snd_PlayMIDISound(struct MIDISound* sound, int volume, int pan, short modifier, short bend);
void snd_SetupMIDIStreamHandler(struct MIDIHandler* handler, struct MIDIBlockHeader* stream);
void snd_MuteMIDIChannel(struct MIDIHandler* handler, int channel);
void snd_UnmuteMIDIChannel(struct MIDIHandler* handler, int channel);
struct MIDIBlockHeader* snd_GetRealMIDIDataStream(struct MIDISound* sound);
void snd_SetMIDISoundVolumePan(u_int handle, int volume, int pan);
void snd_SetMIDISoundPitchModifier(u_int handle, short modifier);
void snd_MIDISoundOwnerProc(int voice, struct GenericSoundHandler* owner, int event);
u_int ReadVarLen(u_char* buffer, int* used_bytes);
#endif
