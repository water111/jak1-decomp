#include "sce/ee/libkernl/sif.h"

extern void* memcpy(void*, const void*, unsigned);
extern void* memset(void*, int, unsigned);
extern int memcmp(const void*, const void*, unsigned);
extern char* strncpy(char*, const char*, unsigned);
extern char __ps2_klibinfo__[];

static int _bind_check = -1;
__asm__(".local _lf_bind,cd\n.local _senddata\n.local _lfversion");
static char* _lfwildcard = "....";
static unsigned char _senddata[512] __attribute__((aligned(64)));
static SifRpcClient cd;
static char _lfversion[4];

typedef struct {
  int value, length;
  char name[252], arguments[252];
} LoadRequest;

typedef struct {
  unsigned entry, gp;
} LoadedElf;

#define REQUEST ((LoadRequest*)_senddata)

static int _lf_bind(void) {
  int delay;
  if (_bind_check < 0) {
    while (1) {
      if (sceSifBindRpc(&cd, 0x80000006, 0) < 0)
        return -1;
      if (cd.server) {
        _bind_check = 0;
        if (sceSifCallRpc(&cd, 255, 0, 0, 0, _senddata, 4, 0, 0) < 0)
          return -65537;
        memcpy(_lfversion, _senddata, 4);
        return 0;
      }
      for (delay = 0x100000; delay--;) {
      }
    }
  }
  return 0;
}

static int _lf_version(void) {
  char* version = __ps2_klibinfo__ + 12;
  return memcmp(_lfversion, version, 4) && memcmp(_lfversion, _lfwildcard, 4) &&
         memcmp(version, _lfwildcard, 4);
}

int sceSifLoadFileReset(void) {
  _bind_check = -1;
  memset(_lfversion, 0, 4);
  return 0;
}

static int _sceSifLoadModuleBuffer(void* buffer, int length, const char* arguments, int* result) {
  if (_lf_bind() < 0)
    return -65536;
  if (_lf_version())
    return -65540;
  REQUEST->value = (unsigned)buffer;
  if (arguments) {
    if (length > 252) {
      memcpy(REQUEST->arguments, arguments, 252);
      REQUEST->length = 252;
    } else {
      memcpy(REQUEST->arguments, arguments, length);
      REQUEST->length = length;
    }
  } else
    REQUEST->length = 0;
  if (sceSifCallRpc(&cd, 6, 0, _senddata, 512, _senddata, 8, 0, 0) < 0)
    return -65537;
  {
    int value = REQUEST->value;
    *result = REQUEST->length;
    return value;
  }
}

int sceSifLoadModuleBuffer(void* buffer, int length, const char* arguments) {
  int result;
  return _sceSifLoadModuleBuffer(buffer, length, arguments, &result);
}

int sceSifLoadStartModuleBuffer(void* buffer, int length, const char* arguments, int* result) {
  return _sceSifLoadModuleBuffer(buffer, length, arguments, result);
}

static int _sceSifLoadModule(const char* path,
                             int length,
                             const char* arguments,
                             int* result,
                             int command) {
  if (_lf_bind() < 0)
    return -65536;
  if (_lf_version())
    return -65540;
  strncpy(REQUEST->name, path, 252);
  REQUEST->name[251] = 0;
  if (arguments) {
    if (length > 252) {
      memcpy(REQUEST->arguments, arguments, 252);
      REQUEST->value = 252;
    } else {
      memcpy(REQUEST->arguments, arguments, length);
      REQUEST->value = length;
    }
  } else {
    REQUEST->arguments[0] = 0;
    REQUEST->value = 0;
  }
  if (sceSifCallRpc(&cd, command, 0, _senddata, 512, _senddata, 8, 0, 0) < 0)
    return -65537;
  {
    int value = REQUEST->value;
    *result = REQUEST->length;
    return value;
  }
}

int sceSifLoadModule(const char* path, int length, const char* arguments) {
  int result;
  return _sceSifLoadModule(path, length, arguments, &result, 0);
}

int sceSifLoadStartModule(const char* path, int length, const char* arguments, int* result) {
  return _sceSifLoadModule(path, length, arguments, result, 0);
}

static int _sceSifLoadElfPart(const char* path, const char* part, LoadedElf* result, int command) {
  if (_lf_bind() < 0)
    return -65536;
  if (_lf_version())
    return -65540;
  strncpy(REQUEST->name, path, 252);
  REQUEST->name[251] = 0;
  strncpy(REQUEST->arguments, part, 252);
  REQUEST->arguments[251] = 0;
  if (sceSifCallRpc(&cd, command, 0, _senddata, 512, _senddata, 16, 0, 0) < 0)
    return -65537;
  if (!REQUEST->value)
    return -65539;
  result->entry = REQUEST->value;
  result->gp = REQUEST->length;
  return 0;
}

int sceSifLoadElfPart(const char* path, const char* part, LoadedElf* result) {
  return _sceSifLoadElfPart(path, part, result, 1);
}

int sceSifLoadElf(const char* path, LoadedElf* result) {
  return _sceSifLoadElfPart(path, "all", result, 1);
}

int sceSifGetIopAddr(unsigned address, void* result, int type) {
  unsigned* words;
  if (_lf_bind() < 0)
    return -65536;
  if ((unsigned)type > 2)
    return -65538;
  words = (unsigned*)_senddata;
  words[0] = address;
  words[1] = type;
  if (sceSifCallRpc(&cd, 3, 0, _senddata, 32, _senddata, 32, 0, 0) < 0)
    return -65537;
  if (type == 0)
    *(unsigned char*)result = _senddata[0];
  else if (type == 1)
    *(unsigned short*)result = *(unsigned short*)_senddata;
  else if (type == 2)
    *(unsigned*)result = words[0];
  else
    return -65538;
  return 0;
}

int sceSifSetIopAddr(unsigned address, const void* value, int type) {
  unsigned* words;
  if (_lf_bind() < 0)
    return -65536;
  words = (unsigned*)_senddata;
  words[0] = address;
  words[1] = type;
  if (type == 0)
    _senddata[8] = *(unsigned char*)value;
  else if (type == 1)
    *(unsigned short*)(_senddata + 8) = *(unsigned short*)value;
  else if (type == 2)
    words[2] = *(unsigned*)value;
  else
    return -65538;
  if (sceSifCallRpc(&cd, 2, 0, _senddata, 32, _senddata, 16, 0, 0) < 0)
    return -65537;
  return 0;
}
