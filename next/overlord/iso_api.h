#ifndef ISO_API_H
#define ISO_API_H

#include "overlord/isocommon.h"

int LoadISOFileToIOP(FileRecord *file, void *dest, int length);
int LoadISOFileToEE(FileRecord *file, unsigned int dest, int length);

#endif
