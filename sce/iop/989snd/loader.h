#ifndef SCE_IOP_989SND_LOADER_H
#define SCE_IOP_989SND_LOADER_H

#include "sce/iop/types.h"

struct MIDIBlockHeader;
struct MultiMIDIBlockHeader;
struct SFXBlock2;
struct SoundBank;

int snd_FileRead(int sector, int offset, int size, void* destination);
struct SoundBank* snd_BankLoadByLoc(int sector, int offset);
int snd_BankTransfer(struct SoundBank* bank,
                     void* samples,
                     int size,
                     int offset,
                     int continuing,
                     void (*done)(void));
#if SND989_VERSION == 0x020604
int snd_BlockTransfer(struct SFXBlock2* bank,
                      void* samples,
                      int size,
                      int offset,
                      int continuing,
                      void (*done)(void));
int snd_EndBlockTransfer(struct SFXBlock2* block);
#endif
void snd_ResolveBankXREFS(void);
void snd_UnloadBank(struct SoundBank* bank);
struct MultiMIDIBlockHeader* snd_MMDLoadByLoc(int sector, int offset);
void snd_UnloadMMD(struct MIDIBlockHeader* midi);
void snd_RegisterIOPMemAllocator(void* (*allocate)(int, int, int*), void (*release)(void*));
void snd_InitLoader(void);
struct MIDIBlockHeader* snd_FindMIDIBlock(u_int id);
void snd_InsertMIDIBlockInList(struct MIDIBlockHeader* midi);
int snd_IsCurrentTransferComplete(void);
int snd_EndBankTransfer(struct SoundBank* bank);
void snd_RemoveBank(struct SoundBank* bank);
void snd_UnloadAllBanks(void);
void snd_UnloadAllMMD(void);
int snd_RegisterMIDI(struct MIDIBlockHeader* midi);
void snd_UnregisterMIDI(struct MIDIBlockHeader* midi);
struct SoundBank* snd_FindBankByID(u_int id);
struct SoundBank* snd_FindBankByNum(signed char number);
void snd_UnloadBlock(struct SFXBlock2* block);
void snd_RemoveBlock(struct SFXBlock2* block);
struct SFXBlock2* snd_FindBlockByID(u_int id);
struct SFXBlock2* snd_FindBlockByNum(signed char number);
#if SND989_VERSION == 0x020604
int snd_TransCallback(int channel, void* data);
#endif
void* snd_IOPMemAlloc(int size, int use, int* actual_size);
void snd_IOPMemFree(void* memory);
int snd_BankLoaded(struct SoundBank* bank);
int snd_BlockLoaded(struct SFXBlock2* block);
void snd_MIDIStreamGone(struct MIDIBlockHeader* midi);
#if SND989_VERSION == 0x020608
int snd_OpenDataSourceByName(const char* name, int offset);
int snd_OpenDataSourceByLoc(int sector, int offset);
void snd_CloseDataSource(void);
int snd_SeekDataSource(int where, int from_where);
void snd_SetDataSourceMark(void);
int snd_GetLastLoadError(void);
int snd_ReadBytes(void* buffer, int count);
struct SoundBank* snd_BankLoad(char* name, int offset);
struct SoundBank* snd_ReadBank(void);
struct MultiMIDIBlockHeader* snd_MMDLoad(char* name, int offset);
struct MultiMIDIBlockHeader* snd_MMDLoadFromDataSource(void);
void snd_SetCDSifReg(int reg8, int reg9);
#endif

#endif
