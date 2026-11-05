#ifndef SCE_IOP_989SND_STREAM_SOUND_H
#define SCE_IOP_989SND_STREAM_SOUND_H

#include "sce/iop/989snd/handlers.h"
#include "sce/iop/types.h"

struct GenericSoundHandler;
struct Sound;
struct SoundBank;
struct StreamSoundHandler;
struct Tone;

u_int snd_PlayStreamSound(struct Sound* sound, int volume, int pan, short modifier, short bend);
void snd_SetStreamSoundVolumePan(u_int handle, int volume, int pan);
void snd_UpdateStreamSoundPitch(struct StreamSoundHandler* handler);
void snd_SetStreamSoundPitch(u_int handle, int pitch);
void snd_StreamSoundVoiceOwnerProc(int voice, struct GenericSoundHandler* handler, int event);
int snd_CollectTones(struct SoundBank* bank, int program, int note, struct Tone** output);
void snd_SetStreamSoundPitchBend(u_int handle, short bend);
void snd_SetStreamSoundPitchModifier(u_int handle, short modifier);
#endif
