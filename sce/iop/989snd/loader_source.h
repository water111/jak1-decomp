#ifndef SCE_IOP_989SND_LOADER_SOURCE_H
#define SCE_IOP_989SND_LOADER_SOURCE_H
#define SND_CD_SECTOR_SIZE 2048
struct SoundBank;
struct SoundBank* snd_ReadBank(void);
extern int gFileHandle;
extern int gFileStartSector, gFileStartOffset;
extern int gFileNextReadSector, gFileNextReadOffset;
extern int gFileMarkSector, gFileMarkOffset;
extern int gLastLoadError;
int snd_OpenDataSourceByName(const char* name, int offset);
int snd_OpenDataSourceByLoc(int sector, int offset);
void snd_CloseDataSource(void);
int snd_SeekDataSource(int where, int from_where);
void snd_SetDataSourceMark(void);
int snd_GetLastLoadError(void);
int snd_ReadBytes(void* buffer, int count);
int snd_FileRead(int sector, int offset, int size, void* buffer);
#endif
