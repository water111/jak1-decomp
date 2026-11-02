#ifndef ISO_API_H
#define ISO_API_H

#include "overlord/isocommon.h"

int LoadISOFileToIOP(FileRecord* file, void* dest, int length);
int LoadISOFileToEE(FileRecord* file, unsigned int dest, int length);
int LoadISOFileChunkToEE(FileRecord* file, unsigned int dest, int length, unsigned int offset);

#if REVISION <= 20010903
void QueueVAGStream(FileRecord* file, unsigned int id, unsigned int priority);
void PlayVAGStream(FileRecord* file, unsigned int id, int volume, unsigned int priority);
void StopVAGStream(FileRecord* file, unsigned int priority);
#elif REVISION <= 20011002
void QueueVAGStream(FileRecord* file, VagDirEntry* vag, unsigned int id, unsigned int priority);
void PlayVAGStream(FileRecord* file,
                   VagDirEntry* vag,
                   unsigned int id,
                   int volume,
                   unsigned int priority);
void StopVAGStream(VagDirEntry* vag, unsigned int priority);
#else
void QueueVAGStream(FileRecord* file, VagDirEntry* vag, unsigned int id, unsigned int priority);
void PlayVAGStream(FileRecord* file,
                   VagDirEntry* vag,
                   unsigned int id,
                   int volume,
                   unsigned int priority,
                   Vec3w* position);
void StopVAGStream(VagDirEntry* vag, unsigned int priority);
#endif

void PauseVAGStream(void);
void UnpauseVAGStream(void);
void SetVAGStreamVolume(int volume);
void LoadSoundBank(const char* name, SoundBank* bank);
void LoadMusic(const char* name, void** bank);

#if REVISION > 20010903
void ShutdownFilingSystem(void);
void SetDialogVolume(int volume);
#endif

#endif
