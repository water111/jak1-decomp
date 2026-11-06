#include "next/kernel/fileio.h"
#include "next/kernel/kmalloc.h"
#include "next/kernel/kprint.h"
#include "sce/ee/libkernl/sifdev.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/libstdcxx/stl_alloc.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

// This file likely included an STL header which
// had this code somehwere, causing its symbol to show up in metadata.
template class __default_alloc_template<false, 0>;

#define ART_FILE_VERSION 6
#if REVISION < 20010920
#define LEVEL_FILE_VERSION 29
#else
#define LEVEL_FILE_VERSION 30
#endif
#define DGO_FILE_VERSION 1
#define RES_FILE_VERSION 1
#define TX_PAGE_VERSION 7

// Return the string's terminating zero.
char* strend(char* str) {
  while (*str)
    str++;
  return str;
}

// Decode a 1, 2, 3, or 4 byte word. Length is stored in the lowest 2 bits.
unsigned int ReadHufWord(unsigned char** loc_ptr) {
  unsigned char* loc = *loc_ptr;
  unsigned int value = *loc++;
  switch (value & 3) {
    case 1:
      value = (value & 0xfc) | (*loc++ << 8);
      break;
    case 2:
      value = (value & 0xfc) | (*loc++ << 8);
      value |= *loc++ << 16;
      break;
    case 3:
      value = (value & 0xfc) | (*loc++ << 8);
      value |= *loc++ << 16;
      value |= *loc++ << 24;
      break;
  }
  *loc_ptr = loc;
  return value;
}

// Copy a string, including the terminating zero.
void kstrcpy(char* dest, char* src) {
  while (*src)
    *dest++ = *src++;
  *dest = 0;
}

// Copy a string while converting ASCII letters to uppercase.
void kstrcpyup(char* dest, char* src) {
  char c;
  while ((c = *src)) {
    if (c >= 'a' && c <= 'z')
      c -= 0x20;
    *dest++ = c;
    src++;
  }
  *dest = 0;
}

// Append a string, including the terminating zero.
void kstrcat(char* dest, char* src) {
  while (*dest)
    dest++;
  while (*src)
    *dest++ = *src++;
  *dest = 0;
}

// Append up to the given total length, then terminate the destination.
void kstrncat(char* dest, char* src, int count) {
  int i = 0;
  while (*dest) {
    dest++;
    i++;
  }
  while (*src && i < count) {
    *dest++ = *src++;
    i++;
  }
  *dest = 0;
}

// Shift a string right and fill the opening characters with padding.
char* kstrinsert(char* str, char pad, int count) {
  int i;
  int len = strlen(str) + 1;
  for (i = len - 1; i >= 0; i--)
    str[i + count] = str[i];
  for (i = 0; i < count; i++)
    str[i] = pad;
  return str;
}

// Return the final path component, accepting both slash conventions.
char* basename(char* str) {
  char* ptr = str;
  while (*ptr)
    ptr++;
  while (ptr > str) {
    ptr--;
    if (*ptr == '\\' || *ptr == '/')
      return ptr + 1;
  }
  return str;
}

// Convert a "virtual" file name to a real file. Files without a prefix are
// treated as code.
char* DecodeFileName(char* name) {
  if (name[0] == '$') {
    if (!strncmp(name, "$TEXTURE/", 9)) {
      return MakeFileName(TX_PAGE_FILE_TYPE, name + 9, 0, 0, 0);
    } else if (!strncmp(name, "$ART_GROUP/", 0xb)) {
      return MakeFileName(ART_GROUP_FILE_TYPE, name + 0xb, 0, 0, 0);
    } else if (!strncmp(name, "$LEVEL/", 7)) {
      if (name[strlen(name) - 4] == '.') {
        return MakeFileName(LEVEL_WITH_EXTENSION_FILE_TYPE, name + 7, 0, 0, 0);
      } else {
        return MakeFileName(LEVEL_FILE_TYPE, name + 7, 0, 0, 0);
      }
    } else if (!strncmp(name, "$DATA/", 6)) {
      return MakeFileName(DATA_FILE_TYPE, name + 6, 0, 0, 0);
    } else if (!strncmp(name, "$CODE/", 6)) {
      return MakeFileName(CODE_FILE_TYPE, name + 6, 0, 0, 0);
    } else if (!strncmp(name, "$RES/", 5)) {
      return MakeFileName(RES_FILE_TYPE, name + 5, 0, 0, 0);
    } else {
      return 0;
    }
  } else {
    return MakeFileName(CODE_FILE_TYPE, name, 0, 0, 0);
  }
  return 0;
}

// Construct a development-host path, optionally copying it onto the global
// heap.
char* MakeFileName(int type,
                   const char* name,
                   int new_string,
                   const char* unused_prefix,
                   MakeFileNameInfo* info) {
  static char buffer[512] __asm__("buffer.633");
  static char nextDir[] __asm__("nextDir.634") = "/";
  char* result;
  char prefix[64];
  char* buf;
  kstrcpy(buffer, "host:");
  buf = strend(buffer);
  kstrcpy(prefix, "");
  switch (type) {
    case LISTENER_TO_KERNEL_FILE_TYPE:
      kstrcpy(buf, "kernel/LISTENERTOKERNEL");
      break;
    case KERNEL_TO_LISTENER_FILE_TYPE:
      kstrcpy(buf, "kernel/KERNELTOLISTENER");
      break;
    case LISTENER_TO_KERNEL_LOCK_FILE_TYPE:
      kstrcpy(buf, "kernel/LISTENERTOKERNEL_LOCK");
      break;
    case KERNEL_TO_LISTENER_LOCK_FILE_TYPE:
      kstrcpy(buf, "kernel/KERNELTOLISTENER_LOCK");
      break;
    case CODE_FILE_TYPE:
      sprintf(buf, "game/obj/%s.o", name);
      break;
    case TX_PAGE_FILE_TYPE:
    case 0x28:
    case 0x29:
      sprintf(buf, "%sdata/texture-page%d/%s.go", prefix, TX_PAGE_VERSION, name);
      break;
    case DATA_FILE_TYPE:
      sprintf(buf, "%sdata/%s.go", prefix, name);
      break;
    case ART_GROUP_FILE_TYPE:
      sprintf(buf, "%sdata/art-group%d/%s-ag.go", prefix, ART_FILE_VERSION, name);
      break;
    case JA_FILE_TYPE:
      sprintf(buf, "%sdd_next/artdata%d/%s-ja.go", prefix, ART_FILE_VERSION, name);
      break;
    case JG_FILE_TYPE:
      sprintf(buf, "%sdd_next/artdata%d/%s-jg.go", prefix, ART_FILE_VERSION, name);
      break;
    case MA_FILE_TYPE:
      sprintf(buf, "%sdd_next/artdata%d/%s-ma.go", prefix, ART_FILE_VERSION, name);
      break;
    case MG_FILE_TYPE:
      sprintf(buf, "%sdd_next/artdata%d/%s-mg.go", prefix, ART_FILE_VERSION, name);
      break;
    case TG_FILE_TYPE:
      sprintf(buf, "%sdata/%s-tg.go", prefix, name);
      break;
    case LEVEL_FILE_TYPE:
      sprintf(buf, "%sdata/level%d/%s-bt.go", prefix, LEVEL_FILE_VERSION, name);
      break;
    case VS_FILE_TYPE:
      sprintf(buf, "%sdata/level%d/%s-vs.go", prefix, LEVEL_FILE_VERSION, name);
      break;
    case VS_BIN_FILE_TYPE:
      sprintf(buf, "%sdata/level%d/%s-vs.bin", prefix, LEVEL_FILE_VERSION, name);
      break;
    case LEVEL_WITH_EXTENSION_FILE_TYPE:
      sprintf(buf, "%sdata/level%d/%s", prefix, LEVEL_FILE_VERSION, name);
      break;
    case DGO_TXT_FILE_TYPE:
      sprintf(buf, "%sdata/dgo%d/%s.txt", prefix, DGO_FILE_VERSION, name);
      break;
    case DATA_DGO_FILE_TYPE:
      sprintf(buf, "%sdata/dgo%d/%s.dgo", prefix, DGO_FILE_VERSION, name);
      break;
    case GAME_DGO_FILE_TYPE:
      sprintf(buf, "game/dgo%d/%s.dgo", DGO_FILE_VERSION, name);
      break;
    case DATA_CGO_FILE_TYPE:
      sprintf(buf, "%sdata/dgo%d/%s.cgo", prefix, DGO_FILE_VERSION, name);
      break;
    case GAME_CGO_FILE_TYPE:
      sprintf(buf, "game/dgo%d/%s.cgo", DGO_FILE_VERSION, name);
      break;
    case TX_FILE_TYPE:
      sprintf(buf, "%sdata/res%d/%s-tx.go", prefix, RES_FILE_VERSION, name);
      break;
    case CNT_FILE_TYPE:
      sprintf(buf, "%sdata/res%d/game-cnt.go", prefix, RES_FILE_VERSION);
      break;
    case RES_FILE_TYPE:
      sprintf(buf, "%sdata/res%d/%s.go", prefix, RES_FILE_VERSION, name);
      break;
    case REFPLANT_FILE_TYPE:
      sprintf(buf, "%sconfig_data/refplant/%s", nextDir, name);
      break;
    case GAMEPAD_FILE_TYPE:
      sprintf(buffer, "pad:0");
      break;
    case IOP_MODULE_FILE_TYPE:
      sprintf(buffer, "host0:/usr/local/sce/iop/modules/%s.irx", name);
      break;
  }
  if (new_string) {
    result = kmalloc(kglobalheap, strlen(buffer) + 1, 0, "filename");
    kstrcpy(result, buffer);
  } else {
    result = buffer;
  }
  return result;
}

// Unimplemented, no files exist I guess.
unsigned int FileExists(const char* name) {
  return 0;
}

// Unimplemented filesystem deletion function.
void FileDelete(const char* name) {
}

// Unimplemented filesystem copy function.
int FileCopy(const char* source, const char* destination) {
  return 0;
}

// Return a file's length, or the kernel's read-error code.
int FileLength(const char* name) {
  int fd = sceOpen(name, SCE_RDONLY);
  if (fd < 0) {
    MsgErr("dkernel: file length !open: '%s' (%d)\n", name, fd);
    sceClose(fd);
    return -5;
  }
  int size = sceLseek(fd, 0, SCE_SEEK_END);
  sceClose(fd);
  return size;
}

// Read a whole file into the supplied buffer, or allocate one from the given
// heap.
unsigned char* FileLoad(const char* name,
                        kheapinfo* heap,
                        unsigned char* memory,
                        unsigned int flags,
                        int* size_out) {
  int read_amount;
  long long size, position;
  int fd = sceOpen(name, SCE_RDONLY);
  if (fd < 0) {
    MsgErr("dkernel: file read !open: '%s' (%d)\n", name, fd);
    sceClose(fd);
    return (unsigned char*)0xfffffffb;
  }
  position = sceLseek(fd, 0, SCE_SEEK_CUR);
  size = sceLseek(fd, 0, SCE_SEEK_END);
  position = sceLseek(fd, position, SCE_SEEK_SET);
  if (size <= 0)
    return (unsigned char*)size;
  if (!memory) {
    memory = (unsigned char*)kmalloc(heap, size + 64, flags, name);
    if (!memory) {
      MsgErr("dkernel: mem full for file read: '%s' (%d bytes)\n", name, size);
      return (unsigned char*)0xfffffffd;
    }
  }
  read_amount = sceRead(fd, memory, size);
  if (read_amount != size) {
    MsgErr("dkernel: can't read full file (%d of %d): '%s'\n", read_amount, size, name);
    sceClose(fd);
    return (unsigned char*)0xfffffffb;
  }
  sceClose(fd);
  if (size_out)
    *size_out = size;
  return memory;
}

// Write a whole file, returning the kernel's write-error code on failure.
int FileSave(const char* name, char* data, int size) {
  int fd = sceOpen(name, SCE_WRONLY | SCE_TRUNC | SCE_CREAT);
  if (fd < 0) {
    MsgErr("dkernel: file write !open: '%s'\n", name);
    sceClose(fd);
    return -6;
  }
  if (size) {
    int written = sceWrite(fd, data, size);
    if (written != size) {
      MsgErr("dkernel: can't write full file: '%s'\n", name);
      sceClose(fd);
      return -6;
    }
  }
  sceClose(fd);
  return 0;
}
