#ifndef SCE_IOP_989SND_LOADER_MIDI_H
#define SCE_IOP_989SND_LOADER_MIDI_H

#include "sce/iop/types.h"
#define SND_MIDI_ID 0x2044494dU
#define SND_MULTI_MIDI_ID 0x44494d4dU
struct SoundBank;
struct MultiMIDIBlockHeader;

typedef struct MIDIBlockHeader {
  u_int data_id;
  short version;
  signed char flags;
  signed char reserved;
  u_int id;
  struct MIDIBlockHeader* next;
  u_int bank_id;
  struct SoundBank* bank;
  signed char* data_start;
  struct MultiMIDIBlockHeader* parent;
  u_int tempo;
  int ppq;
} MIDIBlockHeader;

typedef struct MultiMIDIBlockHeader {
  u_int data_id;
  short version;
  signed char flags;
  signed char block_count;
  u_int id;
  MIDIBlockHeader* next;
  MIDIBlockHeader* blocks[1];
} MultiMIDIBlockHeader;

MultiMIDIBlockHeader* snd_MMDLoad(char* name, int offset);
MultiMIDIBlockHeader* snd_MMDLoadByLoc(int sector, int offset);
MultiMIDIBlockHeader* snd_MMDLoadFromDataSource(void);
MIDIBlockHeader* snd_FindMIDIBlock(u_int id);
void snd_InsertMIDIBlockInList(MIDIBlockHeader* midi);
int snd_RegisterMIDI(MIDIBlockHeader* midi);
void snd_UnregisterMIDI(MIDIBlockHeader* midi);
void snd_MIDIStreamGone(MIDIBlockHeader* midi);
void snd_UnloadMMD(MIDIBlockHeader* midi);
void snd_UnloadAllMMD(void);
#endif
