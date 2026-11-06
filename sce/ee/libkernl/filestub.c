#include "sce/ee/libkernl/sif.h"
#include "sce/ee/newlib/stdarg.h"
#include "sce/ee/newlib/string.h"

typedef struct {
  int count, maximum, initial, waiters, attributes, option;
} Semaphore;

typedef struct {
  int descriptor;
  unsigned flags;
  int operation, result;
} FileHandle;

volatile int _sceFs_q[32] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static int _fs_init = 0;
static int _fs_semid = -1, _fs_iob_semid = -1, _fs_fsq_semid = -1;
static int* ip0 __asm__("ip0.48") __attribute__((aligned(64)));
static unsigned char _send_data[3136] __attribute__((aligned(64)));
static unsigned char _rcv_data_rpc[64] __attribute__((aligned(64)));
static unsigned char _rcv_data_cmd[1088] __attribute__((aligned(64)));
static FileHandle _iob[32];
static SifRpcClient _cd;
static char _fsversion[4];
static unsigned _sif_FsRcv_Data[16] __attribute__((aligned(8)));

extern int CreateSema(Semaphore*), WaitSema(int), SignalSema(int);

static void _sceFsIobSemaMK(void) {
  if (_fs_iob_semid == -1) {
    Semaphore semaphore;
    semaphore.option = 0;
    semaphore.initial = 1;
    semaphore.maximum = 1;
    _fs_iob_semid = CreateSema(&semaphore);
    _fs_fsq_semid = CreateSema(&semaphore);
  }
}

static FileHandle* new_iob(void) {
  FileHandle* handle;
  _sceFsIobSemaMK();
  WaitSema(_fs_iob_semid);
  for (handle = _iob; handle < _iob + 32; handle++) {
    if (!handle->flags) {
      handle->flags = 0x10000000;
      SignalSema(_fs_iob_semid);
      return handle;
    }
  }
  SignalSema(_fs_iob_semid);
  return 0;
}

static FileHandle* get_iob(unsigned descriptor) {
  FileHandle* handle;
  _sceFsIobSemaMK();
  WaitSema(_fs_iob_semid);
  if (descriptor >= 32) {
    SignalSema(_fs_iob_semid);
    return 0;
  }
  handle = &_iob[descriptor];
  SignalSema(_fs_iob_semid);
  return handle;
}

typedef struct {
  int head_size, tail_size;
  unsigned char *head_destination, *tail_destination;
  unsigned char head[64], tail[64];
} ReadReply;

extern int iSignalSema(int);

static void _sceFs_Rcv_Intr(void* packet, void* argument) {
  int semaphore, command;
  void* destination;
  unsigned size;
  void* output;
  unsigned output_size;
  ReadReply* reply;
  unsigned char* bytes;
  int i;
  memcpy(&semaphore, (void*)((unsigned)_rcv_data_cmd | 0x20000000), 4);
  memcpy(&command, (void*)((unsigned)(_rcv_data_cmd + 4) | 0x20000000), 4);
  memcpy(&destination, (void*)((unsigned)(_rcv_data_cmd + 8) | 0x20000000), 4);
  memcpy(&size, (void*)((unsigned)(_rcv_data_cmd + 12) | 0x20000000), 4);
  memcpy(destination, (void*)((unsigned)(_rcv_data_cmd + 16) | 0x20000000), size);
  switch (command) {
    case 2:
      reply = (ReadReply*)((unsigned)(_rcv_data_cmd + 20) | 0x20000000);
      if (reply->head_size > 0) {
        bytes = reply->head_destination;
        for (i = 0; i < reply->head_size; i++)
          bytes[i] = reply->head[i];
      }
      if (reply->tail_size > 0) {
        bytes = reply->tail_destination;
        for (i = 0; i < reply->tail_size; i++)
          bytes[i] = reply->tail[i];
      }
      break;
    case 11:
      memcpy(&output, (void*)((unsigned)(_rcv_data_cmd + 20) | 0x20000000), 4);
      memcpy(output, (void*)((unsigned)(_rcv_data_cmd + 24) | 0x20000000), 324);
      break;
    case 12:
      memcpy(&output, (void*)((unsigned)(_rcv_data_cmd + 20) | 0x20000000), 4);
      memcpy(output, (void*)((unsigned)(_rcv_data_cmd + 24) | 0x20000000), 64);
      break;
    case 23:
    case 25:
    case 26:
      memcpy(&output, (void*)((unsigned)(_rcv_data_cmd + 20) | 0x20000000), 4);
      memcpy(&output_size, (void*)((unsigned)(_rcv_data_cmd + 24) | 0x20000000), 4);
      if (output_size > 1024)
        output_size = 1024;
      memcpy(output, (void*)((unsigned)(_rcv_data_cmd + 28) | 0x20000000), output_size);
      break;
  }
  if (semaphore < 0) {
    semaphore = -semaphore;
    for (i = 0; i < 32; i++) {
      if (_sceFs_q[i] == semaphore) {
        _sceFs_q[i] = -1;
        break;
      }
    }
  } else {
    iSignalSema(semaphore);
  }
}

static void _sceFsSemInit(void) {
  if (_fs_semid == -1) {
    Semaphore semaphore;
    semaphore.option = 0;
    semaphore.initial = 1;
    semaphore.maximum = 1;
    _fs_semid = CreateSema(&semaphore);
  }
}

static int _sceFsWaitS(int command) {
  _sceFsSemInit();
  WaitSema(_fs_semid);
  return 0;
}

static void _sceFsSigSema(void) {
  SignalSema(_fs_semid);
}

extern int DIntr(void), EIntr(void);
extern char __ps2_klibinfo__[];

int sceFsInit(void) {
  int delay;
  FileHandle* handle;
  void* receive;
  sceSifInitRpc(0);
  DIntr();
  sceSifAddCmdHandler(0x80000011, _sceFs_Rcv_Intr, _sif_FsRcv_Data);
  EIntr();
  for (;;) {
    if (sceSifBindRpc(&_cd, 0x80000001, 0) < 0)
      return -1;
    if (_cd.server)
      break;
    for (delay = 0x100000; delay--;)
      ;
  }
  _sceFsIobSemaMK();
  WaitSema(_fs_iob_semid);
  for (handle = _iob; handle < _iob + 32; handle++)
    handle->flags = 0;
  SignalSema(_fs_iob_semid);
  receive = _rcv_data_cmd;
  if (sceSifCallRpc(&_cd, 255, 0, &receive, 4, _rcv_data_rpc, 4, 0, 0) < 0)
    return -65537;
  memcpy(_fsversion, (void*)((unsigned)_rcv_data_rpc | 0x20000000), 4);
  _fs_init = 1;
  return 0;
}

static char* _fswildcard = "....";

static int _fs_version(void) {
  char* version = __ps2_klibinfo__ + 12;
  return memcmp(_fsversion, version, 4) && memcmp(_fsversion, _fswildcard, 4) &&
         memcmp(version, _fswildcard, 4);
}

int sceFsReset(void) {
  _fs_init = 0;
  memset(_fsversion, 0, 4);
  return 0;
}

extern int DeleteSema(int);

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  unsigned flags;
  int mode;
  char name[1024];
} OpenRequest;

int sceOpen(const char* name, int flags, ...) {
  OpenRequest* request = (OpenRequest*)_send_data;
  FileHandle* handle;
  Semaphore semaphore;
  va_list args;
  int mode, length, id, accepted, result;
  _sceFsWaitS(0);
  if (!_fs_init)
    sceFsInit();
  if (_fs_version()) {
    _sceFsSigSema();
    return -65540;
  }
  handle = new_iob();
  if (!handle) {
    _sceFsSigSema();
    return -19;
  }
  va_start(args, flags);
  mode = va_arg(args, int);
  va_end(args);
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
    length = 1023;
  }
  request->mode = mode;
  request->flags = flags & 0x0fffffff;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 0, 0, _send_data, length + 21, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  if (result < 0) {
    WaitSema(_fs_iob_semid);
    handle->flags = 0;
    SignalSema(_fs_iob_semid);
    return result;
  }
  WaitSema(_fs_iob_semid);
  handle->descriptor = result;
  handle->flags |= flags;
  accepted = handle - _iob;
  SignalSema(_fs_iob_semid);
  return accepted;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  int descriptor;
} CloseRequest;

int sceClose(int descriptor) {
  CloseRequest* request = (CloseRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result;
  _sceFsWaitS(1);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !handle->flags) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 1, 0, _send_data, 16, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  handle->flags = 0;
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  if (result < 0)
    return result;
  return 0;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  int descriptor, offset, origin;
} SeekRequest;

int sceLseek(int descriptor, int offset, int origin) {
  SeekRequest* request = (SeekRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result, i;
  unsigned flags;
  _sceFsWaitS(4);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !(flags = handle->flags)) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  request->offset = offset;
  request->origin = origin;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (flags & 0x8000) {
    WaitSema(_fs_fsq_semid);
    for (i = 0; i < 32; i++) {
      if (_sceFs_q[i] == -1) {
        _sceFs_q[i] = request->semaphore;
        request->semaphore = -request->semaphore;
        break;
      }
    }
    SignalSema(_fs_fsq_semid);
  }
  if (sceSifCallRpc(&_cd, 4, 0, _send_data, 24, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  if ((short)flags & 0x8000) {
    DeleteSema(id);
    return 0;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  int descriptor;
  void* data;
  int size, reserved;
} ReadRequest;

int sceRead(int descriptor, void* data, int size) {
  ReadRequest* request = (ReadRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result, i;
  unsigned flags;
  _sceFsWaitS(2);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !(flags = handle->flags)) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  request->data = data;
  request->size = size;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (flags & 0x8000) {
    WaitSema(_fs_fsq_semid);
    for (i = 0; i < 32; i++) {
      if (_sceFs_q[i] == -1) {
        _sceFs_q[i] = request->semaphore;
        request->semaphore = -request->semaphore;
        break;
      }
    }
    SignalSema(_fs_fsq_semid);
  }
  if (!(flags & 0x20000000))
    sceSifWriteBackDCache(data, size);
  sceSifWriteBackDCache(_rcv_data_cmd, 164);
  sceSifWriteBackDCache(request, 28);
  if (sceSifCallRpc(&_cd, 2, 0, _send_data, 28, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  if ((short)flags & 0x8000) {
    DeleteSema(id);
    return 0;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  int descriptor;
  void* data;
  int size, prefix_size;
  char prefix[16];
} WriteRequest;

int sceWrite(int descriptor, void* data, int size) {
  WriteRequest* request = (WriteRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result, i, j, prefix;
  unsigned flags;
  _sceFsWaitS(3);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !(flags = handle->flags)) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  request->size = size;
  request->data = data;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (flags & 0x8000) {
    WaitSema(_fs_fsq_semid);
    for (i = 0; i < 32; i++) {
      if (_sceFs_q[i] == -1) {
        _sceFs_q[i] = request->semaphore;
        request->semaphore = -request->semaphore;
        break;
      }
    }
    SignalSema(_fs_fsq_semid);
  }
  if (!((unsigned)data & 15))
    prefix = 0;
  else {
    unsigned base = (unsigned)data - 16;
    prefix = (((unsigned)data >> 4) << 4) - base;
  }
  if (size < prefix)
    prefix = size;
  if (!(flags & 0x20000000))
    sceSifWriteBackDCache(data, size);
  data = (void*)((unsigned)data | 0x20000000);
  request->prefix_size = prefix;
  for (j = 0; j < prefix; j++)
    request->prefix[j] = ((char*)data)[j];
  if (sceSifCallRpc(&_cd, 3, 0, _send_data, 44, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  if ((short)flags & 0x8000) {
    DeleteSema(id);
    return 0;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  int descriptor, command;
  char argument[1024];
  void* output;
  unsigned output_size, input_size;
} IoctlRequest;

int sceIoctl(int descriptor, int command, void* argument) {
  IoctlRequest* request = (IoctlRequest*)_send_data;
  int minimum_flags = 1;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result, i;
  _sceFsWaitS(5);
  ip0 = argument;
  if (!_fs_init)
    sceFsInit();
  if (!handle || handle->flags < minimum_flags) {
    _sceFsSigSema();
    return -9;
  }
  request->output = 0;
  request->output_size = 0;
  switch (command) {
    case 1:
      WaitSema(_fs_fsq_semid);
      for (i = 0; i < 32; i++)
        if (_sceFs_q[i] != -1)
          break;
      if (i == 32)
        *ip0 = 0;
      else
        *ip0 = 1;
      SignalSema(_fs_fsq_semid);
      _sceFsSigSema();
      return 0;
    case 2:
      *(int*)argument = *(int*)((unsigned)(_rcv_data_cmd + 16) | 0x20000000);
      _sceFsSigSema();
      return 0;
    case 3:
      *(long*)argument = *(long*)((unsigned)(_rcv_data_cmd + 16) | 0x20000000);
      _sceFsSigSema();
      return 0;
  }
  request->descriptor = handle->descriptor;
  request->command = command;
  if (!argument)
    request->input_size = 0;
  else {
    request->input_size = 1024;
    memcpy(request->argument, argument, 1024);
  }
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  sceSifWriteBackDCache(_send_data, 1056);
  if (sceSifCallRpc(&_cd, 5, 0, _send_data, 1056, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceIoctl2(int descriptor,
              int command,
              void* argument,
              unsigned input_size,
              void* output,
              unsigned output_size) {
  IoctlRequest* request = (IoctlRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result;
  _sceFsWaitS(26);
  if (!_fs_init)
    sceFsInit();
  if (!handle || !handle->flags) {
    _sceFsSigSema();
    return -9;
  }
  if (input_size > 1024 || output_size > 1024)
    return -22;
  if (!argument)
    request->input_size = 0;
  else
    memcpy(request->argument, argument, input_size);
  request->descriptor = handle->descriptor;
  request->command = command;
  request->input_size = input_size;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  request->output = output;
  request->output_size = output_size;
  sceSifWriteBackDCache(_send_data, 1056);
  if (sceSifCallRpc(&_cd, 26, 0, _send_data, 1056, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  char name[1024];
} PathRequest;

static int _sceCallCode(const char* name, int command) {
  PathRequest* request = (PathRequest*)_send_data;
  Semaphore semaphore;
  int length, id, accepted, result;
  _sceFsWaitS(command);
  if (!_fs_init)
    sceFsInit();
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
    length = 1023;
  }
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, command, 0, _send_data, length + 13, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceRemove(const char* name) {
  return _sceCallCode(name, 6);
}

typedef struct {
  int semaphore;
  void* result;
  int result_size, mode;
  char name[1024];
} MkdirRequest;

int sceMkdir(const char* name, int mode) {
  MkdirRequest* request = (MkdirRequest*)_send_data;
  Semaphore semaphore;
  int length, id, accepted, result;
  _sceFsWaitS(7);
  if (!_fs_init)
    sceFsInit();
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
    length = 1023;
  }
  request->mode = mode;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 7, 0, _send_data, length + 17, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceRmdir(const char* name) {
  return _sceCallCode(name, 8);
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  char device[1024], block_device[1024], argument[1024];
  int argument_size;
} FormatRequest;

int sceFormat(const char* device, const char* block_device, void* argument, int size) {
  FormatRequest* request = (FormatRequest*)_send_data;
  Semaphore semaphore;
  int i, id, accepted, result;
  _sceFsWaitS(14);
  if (!_fs_init)
    sceFsInit();
  for (i = 0; i < 1024; i++)
    if (!(request->device[i] = device[i]))
      break;
  if (i == 1024)
    request->device[1023] = 0;
  if (!block_device)
    request->block_device[0] = 0;
  else {
    for (i = 0; i < 1024; i++)
      if (!(request->block_device[i] = block_device[i]))
        break;
    if (i == 1024)
      request->block_device[1023] = 0;
  }
  if (size > 1024) {
    _sceFsSigSema();
    return -7;
  }
  for (i = 0; i < size; i++)
    request->argument[i] = ((char*)argument)[i];
  request->argument_size = size;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  sceSifWriteBackDCache(_send_data, 3088);
  if (sceSifCallRpc(&_cd, 14, 0, _send_data, 3088, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  void* driver;
} DriverRequest;

int sceAddDrv(void* driver) {
  DriverRequest* request = (DriverRequest*)_send_data;
  Semaphore semaphore;
  int id, accepted, result;
  _sceFsWaitS(15);
  if (!_fs_init)
    sceFsInit();
  request->driver = driver;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 15, 0, _send_data, 16, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -1;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -1;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceDelDrv(const char* name) {
  return _sceCallCode(name, 16);
}

int sceDopen(const char* name) {
  FileHandle* handle = new_iob();
  int result;
  if (!handle)
    return -19;
  result = _sceCallCode(name, 9);
  if (result < 0) {
    WaitSema(_fs_iob_semid);
    handle->flags = 0;
    SignalSema(_fs_iob_semid);
    return result;
  }
  WaitSema(_fs_iob_semid);
  handle->descriptor = result;
  result = handle - _iob;
  SignalSema(_fs_iob_semid);
  return result;
}

int sceDclose(int descriptor) {
  CloseRequest* request = (CloseRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result;
  _sceFsWaitS(10);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !handle->flags) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 10, 0, _send_data, 16, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  handle->flags = 0;
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  if (result < 0)
    return result;
  return 0;
}

int sceDread(int descriptor, void* entry) {
  ReadRequest* request = (ReadRequest*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, result;
  _sceFsWaitS(11);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !handle->flags) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  request->data = entry;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 11, 0, _send_data, 28, _rcv_data_rpc, 4, 0, 0) < 0) {
    WaitSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  void* status;
  unsigned char name[1024];
} GetstatRequest;

int sceGetstat(const char* name, void* status) {
  GetstatRequest* request = (GetstatRequest*)_send_data;
  Semaphore semaphore;
  int length, id, accepted, result;
  _sceFsWaitS(12);
  if (!_fs_init)
    sceFsInit();
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
    length = 1023;
  }
  request->status = status;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 12, 0, _send_data, length + 17, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  unsigned mask;
  unsigned char status[64], name[1024];
} ChstatRequest;

int sceChstat(const char* name, const void* status, unsigned mask) {
  ChstatRequest* request = (ChstatRequest*)_send_data;
  Semaphore semaphore;
  int length, id, accepted, result;
  _sceFsWaitS(13);
  if (!_fs_init)
    sceFsInit();
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
    length = 1023;
  }
  memcpy(request->status, status, 64);
  request->mask = mask;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  sceSifWriteBackDCache(_send_data, 1104);
  if (sceSifCallRpc(&_cd, 13, 0, _send_data, length + 81, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  char old_name[1024], new_name[1024];
} RenameRequest;

int sceRename(const char* old_name, const char* new_name) {
  RenameRequest* request = (RenameRequest*)_send_data;
  Semaphore semaphore;
  int i, id, accepted, result;
  _sceFsWaitS(17);
  if (!_fs_init)
    sceFsInit();
  for (i = 0; i < 1024; i++)
    if (!(request->old_name[i] = old_name[i]))
      break;
  if (i == 1024)
    request->old_name[1023] = 0;
  for (i = 0; i < 1024; i++)
    if (!(request->new_name[i] = new_name[i]))
      break;
  if (i == 1024)
    request->new_name[1023] = 0;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  sceSifWriteBackDCache(_send_data, 2060);
  if (sceSifCallRpc(&_cd, 17, 0, _send_data, 2060, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceChdir(const char* name) {
  return _sceCallCode(name, 18);
}

int sceSync(const char* name, int mode) {
  OpenRequest* request = (OpenRequest*)_send_data;
  Semaphore semaphore;
  int length, id, accepted, result;
  _sceFsWaitS(19);
  if (!_fs_init)
    sceFsInit();
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
  }
  request->mode = mode;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 19, 0, _send_data, 1044, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  char device[1024], block_device[1024], argument[1024];
  int flags, argument_size;
} MountRequest;

int sceMount(const char* device, const char* block_device, int flags, void* argument, int size) {
  MountRequest* request = (MountRequest*)_send_data;
  Semaphore semaphore;
  int i, id, accepted, result;
  _sceFsWaitS(20);
  if (!_fs_init)
    sceFsInit();
  for (i = 0; i < 1024; i++)
    if (!(request->device[i] = device[i]))
      break;
  if (i == 1024)
    request->device[1023] = 0;
  for (i = 0; i < 1024; i++)
    if (!(request->block_device[i] = block_device[i]))
      break;
  if (i == 1024)
    request->block_device[1023] = 0;
  if (size > 1024) {
    _sceFsSigSema();
    return -7;
  }
  for (i = 0; i < size; i++)
    request->argument[i] = ((char*)argument)[i];
  request->argument_size = size;
  request->flags = flags;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  sceSifWriteBackDCache(_send_data, 3092);
  if (sceSifCallRpc(&_cd, 20, 0, _send_data, 3092, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceUmount(const char* name) {
  return _sceCallCode(name, 21);
}

typedef struct {
  int semaphore;
  void* result;
  int result_size, descriptor;
  long offset;
  int origin;
} Seek64Request;

long sceLseek64(int descriptor, long offset, int origin) {
  Seek64Request* request = (Seek64Request*)_send_data;
  FileHandle* handle = get_iob(descriptor);
  Semaphore semaphore;
  int id, accepted, i;
  long result;
  unsigned flags;
  _sceFsWaitS(22);
  if (!_fs_init) {
    _sceFsSigSema();
    return -1;
  }
  if (!handle || !(flags = handle->flags)) {
    _sceFsSigSema();
    return -9;
  }
  request->descriptor = handle->descriptor;
  request->offset = offset;
  request->origin = origin;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 8;
  if (flags & 0x8000) {
    WaitSema(_fs_fsq_semid);
    for (i = 0; i < 32; i++) {
      if (_sceFs_q[i] == -1) {
        _sceFs_q[i] = request->semaphore;
        request->semaphore = -request->semaphore;
        break;
      }
    }
    SignalSema(_fs_fsq_semid);
  }
  if (sceSifCallRpc(&_cd, 22, 0, _send_data, 32, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  if ((short)flags & 0x8000) {
    DeleteSema(id);
    return 0;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  char name[1024], argument[1024];
  int command;
  unsigned input_size;
  void* output;
  unsigned output_size;
} DevctlRequest;

int sceDevctl(const char* name,
              int command,
              void* argument,
              unsigned input_size,
              void* output,
              unsigned output_size) {
  DevctlRequest* request;
  Semaphore semaphore;
  int i, id, accepted, result;
  _sceFsWaitS(23);
  request = (DevctlRequest*)_send_data;
  if (!_fs_init)
    sceFsInit();
  for (i = 0; i < 1024; i++)
    if (!(request->name[i] = name[i]))
      break;
  if (i == 1024)
    request->name[1023] = 0;
  if (input_size > 1024 || output_size > 1024)
    return -22;
  for (i = 0; i < input_size; i++)
    request->argument[i] = ((char*)argument)[i];
  request->input_size = input_size;
  request->command = command;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  request->output = output;
  request->output_size = output_size;
  sceSifWriteBackDCache(_send_data, 2076);
  if (sceSifCallRpc(&_cd, 23, 0, _send_data, 2076, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

int sceSymlink(const char* old_name, const char* new_name) {
  RenameRequest* request = (RenameRequest*)_send_data;
  Semaphore semaphore;
  int i, id, accepted, result;
  _sceFsWaitS(17);
  if (!_fs_init)
    sceFsInit();
  for (i = 0; i < 1024; i++)
    if (!(request->old_name[i] = old_name[i]))
      break;
  if (i == 1024)
    request->old_name[1023] = 0;
  for (i = 0; i < 1024; i++)
    if (!(request->new_name[i] = new_name[i]))
      break;
  if (i == 1024)
    request->new_name[1023] = 0;
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 24, 0, _send_data, 2060, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}

typedef struct {
  int semaphore;
  void* result;
  int result_size;
  unsigned size;
  void* output;
  char name[1024];
} ReadlinkRequest;

int sceReadlink(const char* name, void* output, unsigned size) {
  ReadlinkRequest* request = (ReadlinkRequest*)_send_data;
  Semaphore semaphore;
  int length, id, accepted, result;
  _sceFsWaitS(17);
  if (!_fs_init)
    sceFsInit();
  for (length = 0; length < 1024; length++)
    if (!(request->name[length] = name[length]))
      break;
  if (length == 1024) {
    request->name[1023] = 0;
  }
  if (size >= 1024)
    size = 1023;
  request->output = output;
  request->size = size;
  sceSifWriteBackDCache(output, size);
  semaphore.maximum = 1;
  semaphore.initial = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  request->semaphore = id;
  request->result = &result;
  request->result_size = 4;
  if (sceSifCallRpc(&_cd, 25, 0, _send_data, 2060, _rcv_data_rpc, 4, 0, 0) < 0) {
    DeleteSema(id);
    _sceFsSigSema();
    return -11;
  }
  accepted = *(int*)((unsigned)_rcv_data_rpc | 0x20000000);
  _sceFsSigSema();
  if (!accepted) {
    DeleteSema(id);
    return -11;
  }
  WaitSema(id);
  DeleteSema(id);
  return result;
}
