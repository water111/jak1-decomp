#ifndef ISO_H
#define ISO_H

#include "overlord/isocommon.h"

FileRecord *FindISOFile(const char *name);
int GetISOFileLength(FileRecord *file);
#if REVISION > 20011002
void SetVAGVol();
#endif

#endif
