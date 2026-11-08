#ifndef _LOADCORE_H
#define _LOADCORE_H

void FlushDcache();

// Read by iopfixup.
struct ModuleInfo {
  const char* name;
  unsigned short version;
};

int RegisterLibraryEntries(void* exports);
int ReleaseLibraryEntries(void* exports);

#endif
