#ifndef SCE_IOP_989SND_BANK_H
#define SCE_IOP_989SND_BANK_H

#include "sce/iop/types.h"
#define SOUND_BANK_V2_ID 0x32764253u
struct SoundBank;
struct MIDIBlockHeader;

typedef struct Sound {
  int type;
  struct SoundBank* bank;
  void* original_bank;
  signed char original_type, program, note, fine;
  short volume;
  signed char reserved1, volume_group;
  short pan;
  signed char xref_sound, reserved2;
  u_short flags, reserved3;
} Sound;

typedef struct MIDISound {
  int type;
  struct SoundBank* bank;
  void* original_bank;
  u_int midi_id;
  short volume;
  signed char repeats, volume_group;
  short pan;
  signed char index;
  u_char flags;
  struct MIDIBlockHeader* midi;
} MIDISound;
struct Tone;

struct Prog {
  signed char tone_count;
  signed char volume;
  short pan;
  struct Tone* tones;
};

typedef struct SoundBank {
  u_int data_id;
  u_int version;
  u_int flags;
  u_int id;
  signed char number;
  signed char reserved1;
  short reserved2;
  short sound_count, program_count, tone_count, vag_count;
  struct Sound* sounds;
  struct Prog* programs;
  struct Tone* tones;
  void* samples;
  u_int sample_size;
  struct SoundBank* next;
} SoundBank;

SoundBank* snd_FindBankByID(u_int id);
struct SFX2;
struct SFXGrain2;

typedef struct SFXBlock2 {
  u_int data_id, version, flags, id;
  signed char number, reserved1;
  short reserved2, reserved3;
  short sound_count, grain_count, vag_count;
  struct SFX2* sounds;
  struct SFXGrain2* grains;
  void* samples;
  u_int sample_size, allocation_size;
  struct SFXBlock2* next;
} SFXBlock2;

SFXBlock2* snd_FindBlockByID(u_int id);
SoundBank* snd_FindBankByNum(signed char number);
SFXBlock2* snd_FindBlockByNum(signed char number);
void snd_ResolveBankXREFS(void);
void snd_UnloadAllBanks(void);
void snd_UnloadBank(SoundBank* bank);
void snd_UnloadBlock(SFXBlock2* block);
void snd_RemoveBank(SoundBank* bank);
void snd_RemoveBlock(SFXBlock2* block);
void snd_StopAllSoundsInBank(SoundBank* bank, int silence);
int snd_BankLoaded(SoundBank* bank);
int snd_BlockLoaded(SFXBlock2* block);
#endif
