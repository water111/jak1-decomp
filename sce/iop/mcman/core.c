#include "sce/iop/cdvdman/libcdvd.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/mcman/device.h"
#include "sce/iop/mcman/directory.h"
#include "sce/iop/mcman/filesystem.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/transport.h"
#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/sysclib/string.h"

#if MCMAN_VERSION >= 0x0222
char mcman_signature[16] = "PsIImcman   2710";
#elif MCMAN_VERSION >= 0x0220
char mcman_signature[16] = "PsIImcman   2430";
#else
char mcman_signature[16] = "PsIImcman   2210";
#endif
int mcman_allow_ps1 = 0;
#if MCMAN_VERSION >= 0x0220
struct ModuleInfo Module = {"mcman", MCMAN_VERSION};
#else
struct ModuleInfo Module = {"mcman_tool", MCMAN_VERSION};
#endif

int mcman_chrpos(const char* text, int character) {
  const u_char* p = (const u_char*)text;
  if (*(const u_char*)text) {
    do {
      if (*p == (character & 0xff))
        goto found;
      p++;
    } while (*p);
  }
  if (*p != (character & 0xff))
    return -1;
found:
  return p - (const u_char*)text;
}

extern int mcman_stub[];

#if MCMAN_VERSION >= 0x0220
int start(int argc) {
  if (argc < 0)
    return mcman_shutdown(-argc);
#else
int start(void) {
#endif
  if (RegisterLibraryEntries(mcman_stub))
    return 1;
#if MCMAN_VERSION < 0x0222
  CpuEnableIntr();
#endif
  mcman_initPS2com();
  mcman_initPS1PDAcom();
  mcman_initcache();
  mcman_initdev();
#if MCMAN_VERSION >= 0x0220
  return 2;
#else
  return 0;
#endif
}

#if MCMAN_VERSION >= 0x0220
int mcman_shutdown(int mode) {
  int intr, result;
  CpuSuspendIntr(&intr);
  result = ReleaseLibraryEntries(mcman_stub);
  CpuResumeIntr(intr);
  if (result == 0 || result == -213) {
    mcman_shutdown_device();
    McCloseAll();
    mcman_shutdownPS2com();
    mcman_shutdownPS1com();
    return 1;
  }
  return 2;
}
#endif

int McSetSio2Mode(int mode) {
  return ((int (*)(int))sio2_call_callback3)((mode & 1) | 2);
}

int McGetFormat(int port, int slot) {
  return mcman_devices[port][slot].formatted;
}

int McGetMcType(int port, int slot) {
  return mcman_devices[port][slot].card_type;
}

struct ModuleInfo* McGetModuleInfo(void) {
  return &Module;
}

void McUnknown40(void) {
}

int McGetFreeClusters(int port, int slot) {
  int free_clusters;
  if (!mcman_devices[port][slot].formatted)
    return 0;
  if (mcman_devices[port][slot].free_clusters >= 0)
    return mcman_devices[port][slot].free_clusters;
  switch (mcman_devices[port][slot].card_type) {
    case 1:
    case 3:
      free_clusters = mcman_findfree1(port, slot, 0);
      break;
    case 2:
      free_clusters = mcman_findfree2(port, slot, 0);
      break;
    default:
      free_clusters = 0;
  }
  if (free_clusters == -3)
    free_clusters = 0;
  return free_clusters;
}

void mcman_wmemset(void* buffer, u_int size, int value) {
  int* p = buffer;
  size = size >> 2;
  if (--size != (u_int)-1) {
    do {
      *p++ = value;
    } while (size-- != 0);
  }
}

int mcman_calcEDC(const void* buffer, u_int size) {
  u_int checksum = 0;
  const u_char* p = buffer;
  u_int count;
  for (count = size - 1; count != (u_int)-1; count--)
    checksum ^= *p++;
  return checksum & 0xff;
}

int mcman_checkpath(const char* path) {
  const u_char* p = (const u_char*)path;
  while (*p) {
    if ((*p & 0xff) == '?' || (*p & 0xff) == '*' || (*p & 0xff) < 32)
      return 0;
    p++;
  }
  return 1;
}

#if MCMAN_VERSION >= 0x0220
int mcman_is_dot_path(const char* path) {
  int position = -1;
  int next;
  while ((next = mcman_chrpos(path + position + 1, '/')) >= 0)
    position += next + 1;
  path += position + 1;
  position = 0;
  if (!strcmp(path, ".") || !strcmp(path, ".."))
    position = 1;
  return position;
}
#endif

int mcman_checkdirpath(const char* name, const char* pattern) {
  int position;
  const u_char* n = (const u_char*)name;
  const u_char* p = (const u_char*)pattern;
again: {
  int question = mcman_chrpos((const char*)p, '?');
  int star = mcman_chrpos((const char*)p, '*');
  if (question < 0 && star < 0)
    goto literal;
  position = star;
  if (question >= 0) {
    position = question;
    if (star >= 0) {
      position = star;
      if (question < star)
        position = question;
    }
  }
  if (strncmp((const char*)p, (const char*)n, position))
    return 0;
  p += position;
  n += position;
  while (*p == '?') {
    p++;
    if (!*n)
      return 1;
    n++;
  }
}
  if (*p != '*')
    goto again;
  do {
    p++;
  } while (*p == '*' || *p == '?');
  if (*p) {
    int matched;
    do {
      position = mcman_chrpos((const char*)n, *p);
      n += position;
      if (position < 0)
        return 0;
      matched = mcman_checkdirpath((const char*)n, (const char*)p);
      n++;
    } while (!matched);
  }
  return 1;
literal:
  return strcmp((const char*)p, (const char*)n) == 0;
}

void mcman_invhandles(int port, int slot) {
  int i;
  for (i = 0; i < MCMAN_HANDLES; i++) {
    if (mcman_handles[i].port == port && mcman_handles[i].slot == slot)
      mcman_handles[i].status = 0;
  }
}

int McCloseAll(void) {
  int result = 0;
  int fd;
  for (fd = 0; fd < MCMAN_HANDLES; fd++) {
    if (mcman_handles[fd].status) {
      int error = McClose(fd);
      if (error < result)
        result = error;
    }
  }
  return result;
}

#if MCMAN_VERSION >= 0x0220
#define MCMAN_PS1_DISABLED -5
#else
#define MCMAN_PS1_DISABLED -19
#endif

int mcman_detectcard(int port, int slot) {
  int result;
  int type = mcman_devices[port][slot].card_type;
  if (type == 0 || type == 2) {
    result = mcman_probePS2Card2(port, slot);
    if (result >= -9) {
      mcman_devices[port][slot].card_type = 2;
      return result;
    }
    result = mcman_probePS1Card2(port, slot);
    if (result >= -9) {
      if (mcman_probePDACard(port, slot) == 0) {
        mcman_devices[port][slot].card_type = 3;
        return result;
      }
      mcman_devices[port][slot].card_type = 1;
      goto check_ps1_support;
    }
  } else {
    result = mcman_probePS1Card2(port, slot);
    if (result == 0) {
      if (mcman_allow_ps1)
        return 0;
      if (mcman_devices[port][slot].card_type == 1)
        return MCMAN_PS1_DISABLED;
      return 0;
    }
    if (result < -9)
      goto fallback;
    if (result >= 0)
      goto fallback;
    if (result < 0) {
      if (mcman_probePDACard(port, slot) == 0) {
        mcman_devices[port][slot].card_type = 3;
        return result;
      }
      mcman_devices[port][slot].card_type = 1;
    check_ps1_support:
      if (!mcman_allow_ps1)
        return MCMAN_PS1_DISABLED;
      return result;
    }
  fallback:
    result = mcman_probePS2Card2(port, slot);
    if (result >= -9) {
      mcman_devices[port][slot].card_type = 2;
      return result;
    }
  }
  if (mcman_devices[port][slot].card_type) {
    mcman_devices[port][slot].card_type = 0;
    mcman_devices[port][slot].formatted = 0;
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }
  return result;
}

int McDetectCard2(int port, int slot) {
  int result;
  int type = mcman_devices[port][slot].card_type;
  if (type == 0 || type == 2) {
    result = mcman_probePS2Card(port, slot);
    if (result >= -9) {
      mcman_devices[port][slot].card_type = 2;
      return result;
    }
    result = mcman_probePS1Card(port, slot);
    if (result >= -9) {
      if (mcman_probePDACard(port, slot) == 0) {
        mcman_devices[port][slot].card_type = 3;
        return result;
      }
      mcman_devices[port][slot].card_type = 1;
      goto check_ps1_support;
    }
  } else {
    result = mcman_probePS1Card(port, slot);
    if (result == 0) {
      if (mcman_allow_ps1)
        return 0;
      if (mcman_devices[port][slot].card_type == 1)
        return MCMAN_PS1_DISABLED;
      return 0;
    }
    if (result < -9)
      goto fallback;
    if (result >= 0)
      goto fallback;
    if (result < 0) {
      if (mcman_probePDACard(port, slot) == 0) {
        mcman_devices[port][slot].card_type = 3;
        return result;
      }
      mcman_devices[port][slot].card_type = 1;
    check_ps1_support:
      if (!mcman_allow_ps1)
        return MCMAN_PS1_DISABLED;
      return result;
    }
  fallback:
    result = mcman_probePS2Card(port, slot);
    if (result >= -9) {
      mcman_devices[port][slot].card_type = 2;
      return result;
    }
  }
  if (mcman_devices[port][slot].card_type) {
    mcman_devices[port][slot].card_type = 0;
    mcman_devices[port][slot].formatted = 0;
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }
  return result;
}

int McOpen(int port, int slot, const char* filename, int flag) {
  int r;

  r = mcman_detectcard(port, slot);
  if (r != 0)
    return r;

#if MCMAN_VERSION >= 0x0220
  if (!filename || !*(const u_char*)filename)
    return -4;
#endif
  if (!mcman_allow_ps1)
    flag &= ~0x00002000;

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    r = mcman_open1(port, slot, filename, flag);
  else
    r = mcman_open2(port, slot, filename, flag);

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int McClose(int fd) {
  McFileHandle* fh = &mcman_handles[fd];
  int r;

  if (!((u_int)fd < MCMAN_HANDLES))
    return MCMAN_DENIED;

  if (!fh->status)
    return MCMAN_DENIED;

  fh->status = 0;
  r = mcman_detectcard(fh->port, fh->slot);
  if (r != 0)
    return r;

  r = McFlushCache(fh->port, fh->slot);
  if (r < -9) {
    mcman_invhandles(fh->port, fh->slot);
    mcman_clearcache(fh->port, fh->slot);
  }

  if (r != 0)
    return r;

  if (fh->unknown2 != 0) {
    fh->unknown2 = 0;
    if (mcman_devices[fh->port][fh->slot].card_type != MCMAN_CARD_PS2)
      r = mcman_close1(fd);
    else
      r = mcman_close2(fd);

    if (r < -9) {
      mcman_invhandles(fh->port, fh->slot);
      mcman_clearcache(fh->port, fh->slot);
    }

    if (r != 0)
      return r;
  }

  r = McFlushCache(fh->port, fh->slot);
  if (r < -9) {
    mcman_invhandles(fh->port, fh->slot);
    mcman_clearcache(fh->port, fh->slot);
  }

  return r;
}

int McFlush(int fd) {
  int r;
  McFileHandle* fh = &mcman_handles[fd];

  if (!((u_int)fd < MCMAN_HANDLES))
    return MCMAN_DENIED;

  if (!fh->status)
    return MCMAN_DENIED;

  r = mcman_detectcard(fh->port, fh->slot);
  if (r != 0)
    return r;

  r = McFlushCache(fh->port, fh->slot);
  if (r < -9) {
    mcman_invhandles(fh->port, fh->slot);
    mcman_clearcache(fh->port, fh->slot);
  }

  if (r != 0) {
    fh->status = 0;
    return r;
  }

  if (fh->unknown2 != 0) {
    fh->unknown2 = 0;
    if (mcman_devices[fh->port][fh->slot].card_type != MCMAN_CARD_PS2)
      r = mcman_close1(fd);
    else
      r = mcman_close2(fd);

    if (r < -9) {
      mcman_invhandles(fh->port, fh->slot);
      mcman_clearcache(fh->port, fh->slot);
    }

    if (r != 0) {
      fh->status = 0;
      return r;
    }
  }

  r = McFlushCache(fh->port, fh->slot);
  if (r < 0)
    fh->status = 0;

  if (r < -9) {
    mcman_invhandles(fh->port, fh->slot);
    mcman_clearcache(fh->port, fh->slot);
  }

  return r;
}

int McSeek(int fd, int offset, int origin) {
  int r;
  McFileHandle* fh;

  if (!((u_int)fd < MCMAN_HANDLES))
    return MCMAN_DENIED;

  fh = (McFileHandle*)&mcman_handles[fd];
  if (!fh->status)
    return MCMAN_DENIED;

  r = mcman_detectcard(fh->port, fh->slot);
  if (r != 0)
    return r;

  if (origin == MCMAN_SEEK_CUR)
    goto relative;
  switch (origin) {
    case MCMAN_SEEK_SET:
      r = offset;
      break;
    case MCMAN_SEEK_END:
      r = fh->size + offset;
      break;
    default:
    relative:
      r = fh->position + offset;
      break;
  }
  if (r < 0)
    r = 0;
  fh->position = r;
  return r;
}

int McRead(int fd, void* buf, int length) {
  int r;

  if (!((u_int)fd < MCMAN_HANDLES))
    return MCMAN_DENIED;

  if (!mcman_handles[fd].status)
    return MCMAN_DENIED;

  if (!mcman_handles[fd].read_flag)
    return MCMAN_DENIED;

  r = mcman_detectcard(mcman_handles[fd].port, mcman_handles[fd].slot);
  if (r != 0)
    return r;

  if (mcman_devices[mcman_handles[fd].port][mcman_handles[fd].slot].card_type != MCMAN_CARD_PS2)
    r = mcman_read1(fd, buf, length);
  else
    r = mcman_read2(fd, buf, length);

  if (r < 0)
    mcman_handles[fd].status = 0;

  if (r < -9) {
    mcman_invhandles(mcman_handles[fd].port, mcman_handles[fd].slot);
    mcman_clearcache(mcman_handles[fd].port, mcman_handles[fd].slot);
  }

  return r;
}

int McWrite(int fd, void* buf, int length) {
  int r;

  if ((u_int)fd >= MCMAN_HANDLES || !mcman_handles[fd].status || !mcman_handles[fd].write_flag)
    return MCMAN_DENIED;

  r = mcman_detectcard(mcman_handles[fd].port, mcman_handles[fd].slot);
  if (r != 0)
    return r;

  if (mcman_devices[mcman_handles[fd].port][mcman_handles[fd].slot].card_type != MCMAN_CARD_PS2)
    r = mcman_write1(fd, buf, length);
  else
    r = mcman_write2(fd, buf, length);

  if (r < 0)
    mcman_handles[fd].status = 0;

  if (r < -9) {
    mcman_invhandles(mcman_handles[fd].port, mcman_handles[fd].slot);
    mcman_clearcache(mcman_handles[fd].port, mcman_handles[fd].slot);
  }

  return r;
}

int McGetDir(int port,
             int slot,
             const char* dirname,
             int flags,
             int maxent,
             struct McDirectoryEntry* info) {
  u_short search_flags;
  int r;

  search_flags = flags & 0xffff;

  r = mcman_detectcard(port, slot);
  if (r != 0)
    return r;
#if MCMAN_VERSION >= 0x0220
  if (!dirname || !*(const u_char*)dirname)
    return -4;
#endif

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    r = mcman_getdir1(port, slot, dirname, search_flags, maxent, info);
  else
    r = mcman_getdir2(port, slot, dirname, search_flags, maxent, info);

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int McGetEntSpace(int port, int slot, const char* dirname) {
  int r;

  r = mcman_detectcard(port, slot);
  if (r != 0)
    return r;
#if MCMAN_VERSION >= 0x0220
  if (!dirname || !*(const u_char*)dirname)
    return -4;
#endif

  if (mcman_devices[port][slot].card_type == MCMAN_CARD_PS2) {
    r = mcman_getentspace(port, slot, dirname);
  }

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int mcman_dread(int fd, struct McIoDirEntry* output) {
  int result;
  if ((u_int)fd >= MCMAN_HANDLES || !mcman_handles[fd].status || !mcman_handles[fd].directory_flag)
    return -5;
  result = mcman_detectcard(mcman_handles[fd].port, mcman_handles[fd].slot);
  if (result != 0)
    return result;
  if (mcman_devices[mcman_handles[fd].port][mcman_handles[fd].slot].card_type != MCMAN_CARD_PS2)
    result = mcman_dread1(fd, output);
  else
    result = mcman_dread2(fd, output);
  if (result < 0)
    mcman_handles[fd].status = 0;
  if (result < -9) {
    mcman_invhandles(mcman_handles[fd].port, mcman_handles[fd].slot);
    mcman_clearcache(mcman_handles[fd].port, mcman_handles[fd].slot);
  }
  return result;
}

int mcman_getstat(int port, int slot, const char* name, struct McIoStat* output) {
  int result = mcman_detectcard(port, slot);
  if (result != 0)
    return result;
#if MCMAN_VERSION >= 0x0220
  if (!name || !*(const u_char*)name)
    return -4;
#endif

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    result = mcman_getstat1(port, slot, name, output);
  else
    result = mcman_getstat2(port, slot, name, output);
  if (result < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }
  return result;
}

int McSetFileInfo(int port,
                  int slot,
                  const char* filename,
                  struct McDirectoryEntry* info,
                  int flags) {
  int r;

  r = mcman_detectcard(port, slot);
  if (r != 0)
    return r;
#if MCMAN_VERSION >= 0x0220
  if (!filename || !*(const u_char*)filename)
    return -4;
  if (mcman_is_dot_path(filename))
    return -5;
#endif

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    r = mcman_setinfo1(port, slot, filename, info, flags);
  else
    r = mcman_setinfo2(port, slot, filename, info, flags);

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  if (r == 0) {
    r = McFlushCache(port, slot);
    if (r < -9) {
      mcman_invhandles(port, slot);
      mcman_clearcache(port, slot);
    }
  }

  return r;
}

int McChDir(int port, int slot, const char* newdir, char* currentdir) {
  int r;

  r = mcman_detectcard(port, slot);
  if (r != 0)
    return r;
#if MCMAN_VERSION >= 0x0220
  if (!newdir || !*(const u_char*)newdir)
    return -4;
#endif

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2) {
    currentdir[0] = 0;
    r = 0;
  } else
    r = mcman_chdir(port, slot, newdir, currentdir);

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int McDelete(int port, int slot, const char* filename, int flags) {
  int r;

  r = mcman_detectcard(port, slot);
  if (r != 0)
    return r;
#if MCMAN_VERSION >= 0x0220
  if (!filename || !*(const u_char*)filename)
    return -4;
  if (mcman_is_dot_path(filename))
    return -5;
#endif

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    r = mcman_delete1(port, slot, filename, flags);
  else
    r = mcman_delete2(port, slot, filename, flags);

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int McFormat(int port, int slot) {
  int r;

  mcman_invhandles(port, slot);

  r = mcman_detectcard(port, slot);
  if (r < -2)
    return r;

  mcman_clearcache(port, slot);

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    r = mcman_format1(port, slot);
  else
    r = mcman_format2(port, slot);

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int McUnformat(int port, int slot) {
  int r;

  mcman_invhandles(port, slot);

  r = mcman_detectcard(port, slot);
  if (r < -2)
    return r;

  mcman_clearcache(port, slot);

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2)
    r = mcman_unformat1(port, slot);
  else
    r = mcman_unformat2(port, slot);

  mcman_devices[port][slot].formatted = 0;

  if (r < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }

  return r;
}

int mcman_rename(int port, int slot, const char* old_name, const char* new_name) {
  int result = mcman_detectcard(port, slot);
  struct McDirectoryEntry info;
  if (result != 0)
    return result;
#if MCMAN_VERSION >= 0x0220
  if (!old_name || !*(const u_char*)old_name)
    return -4;
  if (!new_name || !*(const u_char*)new_name)
    return -4;
  if (mcman_is_dot_path(old_name))
    return -5;
#endif

  if (mcman_devices[port][slot].card_type != MCMAN_CARD_PS2) {
    int position = -1;
    while ((result = mcman_chrpos(new_name + position + 1, '/')) >= 0)
      position += result + 1;
    strcpy(info.name, new_name + position + 1);
    result = mcman_setinfo1(port, slot, old_name, &info, 0x10);
  } else
    result = mcman_rename2(port, slot, old_name, new_name);
  if (result < -9) {
    mcman_invhandles(port, slot);
    mcman_clearcache(port, slot);
  }
  return result;
}

#define MCMAN_BCD_TO_BINARY(value) (((value) >> 4) * 10 + ((value) & 15))

int mcman_getmcrtime(struct McDateTime* time) {
  sceCdCLOCK clock;
#if MCMAN_VERSION >= 0x0222
  sceCdReadClock(&clock);
#else
  int retries = 64;
  do {
    if (_sceCdRC(&clock))
      break;
  } while (--retries != 0);
  if (clock.status & 0x80) {
    time->year = 2000;
    time->month = 3;
    time->day = 4;
    time->hour = 0;
    time->minute = 0;
    time->second = 0;
    time->reserved = 0;
    return 0;
  }
#endif
  time->reserved = 0;
  time->second = MCMAN_BCD_TO_BINARY(clock.second);
  time->minute = MCMAN_BCD_TO_BINARY(clock.minute);
  time->hour = MCMAN_BCD_TO_BINARY(clock.hour);
  time->day = MCMAN_BCD_TO_BINARY(clock.day);
  {
    int month = clock.month & 15;
    if (clock.month & 0x10)
      month += 10;
    time->month = month;
  }
  time->year = (clock.year >> 4) * 10 + ((clock.year & 15) | 2000);
  return 0;
}
