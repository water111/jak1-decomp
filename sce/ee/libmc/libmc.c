#include "sce/ee/libmc/libmc.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/libkernl/sif.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

typedef struct {
  int descriptor, port, slot;
  unsigned size, offset, origin;
  void *data, *extra;
  unsigned char prefix[16];
} Request;

typedef struct {
  int port, slot;
  unsigned flags;
  int maximum;
  void* data;
  char name[1024];
} FileRequest;

typedef struct {
  int head_size, tail_size;
  unsigned char *head_destination, *tail_destination;
  unsigned char head[64], tail[64];
  int formatted;
  unsigned char padding[44];
} ReadReply;

static char __ps2_libinfo__[16] = "PsIIlibmc   2210";
static int mcRunCmdNo = 0;
static int semaidRegFunc = -1;
static SifRpcClient mcClientID;
static int *typeAddr, *freeAddr, *formAddr;
static char buffFileInfo[64] __attribute__((aligned(64)));
static Request sifParamOrd;
static FileRequest sifParamFname;
static ReadReply sifParamNext __attribute__((aligned(64)));
static char currentDir[4096] __attribute__((aligned(64)));
static int retval[16] __attribute__((aligned(64)));

#define LOCK()                     \
  if (PollSema(semaidRegFunc) < 0) \
    return -200;                   \
  if (!mcClientID.server) {        \
    SignalSema(semaidRegFunc);     \
    return -100;                   \
  }
#define CALL(command, request, callback, argument) \
  sceSifCallRpc(&mcClientID, command, 1, &request, sizeof(request), retval, 4, callback, argument)
#define FINISH(command)        \
  if (result == 0)             \
    mcRunCmdNo = command;      \
  else                         \
    SignalSema(semaidRegFunc); \
  return result

int sceMcInit(void) {
  Semaphore semaphore;
  int result, delay;
  if (semaidRegFunc < 0) {
    semaphore.option = 0;
    semaphore.maximum = 1;
    semaphore.initial = 1;
    semaidRegFunc = CreateSema(&semaphore);
  }
  sceMcSync(0, 0, 0);
  WaitSema(semaidRegFunc);
  sceSifInitRpc(0);
  for (;;) {
    if (sceSifBindRpc(&mcClientID, 0x80000400, 0) < 0) {
      printf("bind error libmc \n");
      for (;;)
        ;
    }
    if (mcClientID.server)
      break;
    for (delay = 0x100000; --delay;)
      ;
  }
  result = sceSifCallRpc(&mcClientID, 254, 0, &sifParamOrd, sizeof(sifParamOrd), retval, 12, 0, 0);
  SignalSema(semaidRegFunc);
  if (result < 0) {
    mcClientID.server = 0;
    return result - 100;
  }
  if (retval[1] < 0x20a) {
    printf("libmc: too old release of mcserv.irx\n");
    mcClientID.server = 0;
    return -120;
  }
  if (retval[2] < 0x20e) {
    printf("libmc: too old release of mcman.irx\n");
    mcClientID.server = 0;
    return -121;
  }
  return retval[0];
}

SifRpcClient* _lmcGetClientPtr(int** result, int** command) {
  *result = retval;
  *command = &mcRunCmdNo;
  retval[15] = semaidRegFunc;
  return &mcClientID;
}

int sceMcChangeThreadPriority(int priority) {
  int result;
  LOCK();
  sifParamOrd.origin = priority;
  result = CALL(20, sifParamOrd, 0, 0);
  FINISH(20);
}

int sceMcGetSlotMax(int port) {
  int result;
  LOCK();
  sifParamOrd.port = port;
  result = sceSifCallRpc(&mcClientID, 21, 0, &sifParamOrd, sizeof(sifParamOrd), retval, 4, 0, 0);
  if (result) {
    SignalSema(semaidRegFunc);
    return result;
  }
  SignalSema(semaidRegFunc);
  return retval[0];
}

int sceMcOpen(int port, int slot, const char* name, int flags) {
  int result;
  LOCK();
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.port = port;
  sifParamFname.flags = flags;
  sifParamFname.slot = slot;
  sifParamFname.name[1023] = 0;
  result = CALL(2, sifParamFname, 0, 0);
  FINISH(2);
}

int sceMcMkdir(int port, int slot, const char* name) {
  int result = sceMcOpen(port, slot, name, 64);
  if (!result)
    mcRunCmdNo = 11;
  return result;
}

int sceMcClose(int descriptor) {
  int result;
  LOCK();
  sifParamOrd.descriptor = descriptor;
  result = CALL(3, sifParamOrd, 0, 0);
  FINISH(3);
}

int sceMcSeek(int descriptor, int offset, int origin) {
  int result;
  LOCK();
  sifParamOrd.descriptor = descriptor;
  sifParamOrd.offset = offset;
  sifParamOrd.origin = origin;
  result = CALL(4, sifParamOrd, 0, 0);
  FINISH(4);
}

static void mceIntrReadFixAlign(void* argument) {
  ReadReply* reply = (ReadReply*)((unsigned)argument | 0x20000000);
  unsigned char* destination;
  int i;
  if (reply->head_size) {
    destination = reply->head_destination;
    for (i = 0; i < reply->head_size; i++)
      *destination++ = reply->head[i];
  }
  if (reply->tail_size) {
    destination = reply->tail_destination;
    for (i = 0; i < reply->tail_size; i++)
      *destination++ = reply->tail[i];
  }
}

int sceMcRead(int descriptor, void* data, int size) {
  int result;
  LOCK();
  sifParamOrd.descriptor = descriptor;
  sifParamOrd.data = data;
  sifParamOrd.size = size;
  sifParamOrd.extra = &sifParamNext;
  sceSifWriteBackDCache(data, size);
  sceSifWriteBackDCache(&sifParamNext, sizeof(sifParamNext));
  result = CALL(5, sifParamOrd, mceIntrReadFixAlign, &sifParamNext);
  FINISH(5);
}

int sceMcWrite(int descriptor, void* data, int size) {
  int result;
  unsigned i;
  LOCK();
  sifParamOrd.descriptor = descriptor;
  if (size <= 16) {
    sifParamOrd.size = 0;
    sifParamOrd.origin = size;
    sifParamOrd.data = 0;
  } else {
    unsigned base = (unsigned)data - 16;
    unsigned prefix = (((unsigned)data - 1) & ~15U) - base;
    sifParamOrd.origin = prefix;
    sifParamOrd.size = size - prefix;
    sifParamOrd.data = (unsigned char*)data + prefix;
  }
  for (i = 0; i < sifParamOrd.origin; i++)
    sifParamOrd.prefix[i] = ((unsigned char*)data)[i];
  FlushCache(0);
  result = CALL(6, sifParamOrd, 0, 0);
  FINISH(6);
}

static void mcHearAlarm(int alarm, unsigned short time, int thread) {
  iWakeupThread(thread);
}

static void mcDelayThread(unsigned short time) {
  SetAlarm(time, mcHearAlarm, GetThreadId());
  SleepThread();
}

int sceMcSync(int mode, int* command, int* result) {
  int busy;
  if (!mcRunCmdNo)
    return -1;
  busy = sceSifCheckStatRpc(&mcClientID);
  if (!mode && busy) {
    while (sceSifCheckStatRpc(&mcClientID))
      mcDelayThread(60);
    busy = 0;
  }
  busy = !busy;
  if (command)
    *command = mcRunCmdNo;
  if (busy) {
    mcRunCmdNo = 0;
    if (result)
      *result = retval[0];
    SignalSema(semaidRegFunc);
  }
  return busy;
}

static void mceGetInfoApdx(void* argument) {
  int* reply = (int*)((unsigned)argument | 0x20000000);
  if (typeAddr)
    *typeAddr = reply[0];
  if (freeAddr)
    *freeAddr = reply[1];
  if (formAddr)
    *formAddr = reply[36];
}

int sceMcGetInfo(int port, int slot, int* type, int* free_blocks, int* formatted) {
  int result;
  LOCK();
  sifParamOrd.port = port;
  sifParamOrd.slot = slot;
  sifParamOrd.extra = &sifParamNext;
  if (type)
    sifParamOrd.origin = 1;
  else
    sifParamOrd.origin = 0;
  if (free_blocks)
    sifParamOrd.offset = 1;
  else
    sifParamOrd.offset = 0;
  if (formatted)
    sifParamOrd.size = 1;
  else
    sifParamOrd.size = 0;
  typeAddr = type;
  freeAddr = free_blocks;
  formAddr = formatted;
  sceSifWriteBackDCache(&sifParamNext, sizeof(sifParamNext));
  result = CALL(1, sifParamOrd, mceGetInfoApdx, &sifParamNext);
  FINISH(1);
}

int sceMcGetDir(int port, int slot, const char* name, unsigned flags, int maximum, void* entries) {
  int result;
  LOCK();
  sifParamFname.port = port;
  sifParamFname.slot = slot;
  sifParamFname.flags = flags;
  sifParamFname.maximum = maximum;
  sifParamFname.data = entries;
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.name[1023] = 0;
  sceSifWriteBackDCache(entries, maximum * 64);
  result = CALL(13, sifParamFname, 0, 0);
  FINISH(13);
}

static void mceStorePwd(void* argument) {
  char* destination = argument;
  int length;
  if (destination) {
    length = strlen((char*)((unsigned)currentDir | 0x20000000)) < 1024
                 ? strlen((char*)((unsigned)currentDir | 0x20000000))
                 : 1023;
    memcpy(destination, (char*)((unsigned)currentDir | 0x20000000), length);
    destination[length] = 0;
  }
}

int sceMcChdir(int port, int slot, const char* name, char* previous) {
  int result;
  LOCK();
  sifParamFname.port = port;
  sifParamFname.slot = slot;
  sifParamFname.data = currentDir;
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.name[1023] = 0;
  sceSifWriteBackDCache(currentDir, 1024);
  result = CALL(12, sifParamFname, mceStorePwd, previous);
  FINISH(12);
}

int sceMcFormat(int port, int slot) {
  int result;
  LOCK();
  sifParamOrd.port = port;
  sifParamOrd.slot = slot;
  result = CALL(16, sifParamOrd, 0, 0);
  FINISH(16);
}

int sceMcDelete(int port, int slot, const char* name) {
  int result;
  LOCK();
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.port = port;
  sifParamFname.slot = slot;
  sifParamFname.name[1023] = 0;
  sifParamFname.flags = 0;
  result = CALL(15, sifParamFname, 0, 0);
  FINISH(15);
}

int sceMcFlush(int descriptor) {
  int result;
  LOCK();
  sifParamOrd.descriptor = descriptor;
  result = CALL(10, sifParamOrd, 0, 0);
  FINISH(10);
}

int sceMcSetFileInfo(int port, int slot, const char* name, const char* info, unsigned flags) {
  int result;
  LOCK();
  flags &= 7;
  sifParamFname.port = port;
  sifParamFname.slot = slot;
  sifParamFname.flags = flags;
  memcpy(buffFileInfo, info, 64);
  sifParamFname.data = buffFileInfo;
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.name[1023] = 0;
  FlushCache(0);
  result = CALL(14, sifParamFname, 0, 0);
  FINISH(14);
}

int sceMcRename(int port, int slot, const char* name, const char* new_name) {
  int result;
  LOCK();
  sifParamFname.port = port;
  sifParamFname.slot = slot;
  sifParamFname.flags = 16;
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.name[1023] = 0;
  strncpy(buffFileInfo + 32, new_name, 32);
  buffFileInfo[63] = 0;
  sifParamFname.data = buffFileInfo;
  FlushCache(0);
  result = CALL(14, sifParamFname, 0, 0);
  FINISH(19);
}

int sceMcUnformat(int port, int slot) {
  int result;
  LOCK();
  sifParamOrd.port = port;
  sifParamOrd.slot = slot;
  result = CALL(17, sifParamOrd, 0, 0);
  FINISH(17);
}

int sceMcGetEntSpace(int port, int slot, const char* name) {
  int result;
  LOCK();
  sifParamFname.port = port;
  sifParamFname.slot = slot;
  strncpy(sifParamFname.name, name, 1023);
  sifParamFname.name[1023] = 0;
  result = CALL(18, sifParamFname, 0, 0);
  FINISH(18);
}
