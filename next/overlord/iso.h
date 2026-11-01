#ifndef ISO_H
#define ISO_H

#include "overlord/isocommon.h"

FileRecord* FindISOFile(const char* name);
int GetISOFileLength(FileRecord* file);
#if REVISION > 20011002
void SetVAGVol();
#endif

extern int iso_mbx;
extern MusicTweaks gMusicTweakInfo;
extern IsoFs* isofs;

unsigned int InitISOFS(const char* fs_mode, const char* loading_screen);

#endif
