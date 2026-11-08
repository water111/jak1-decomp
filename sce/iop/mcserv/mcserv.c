#include "sce/iop/mcserv/mcserv.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sifcmd/sifrpc.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/stdlib.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thsemap/thsemap.h"

#define MCSERV_BUFSIZE 8192
#define MCSERV_RPC_ID 0x80000400
#define DEFAULT_PRIORITY 104
#define INVALID_PRIORITY (-403)

#ifndef MCSERV_VERSION
#define MCSERV_VERSION 0x020c
#endif

#if MCSERV_VERSION == 0x020c
char mcserv_build_id[16] = "PsIImcserv  2210";
#elif MCSERV_VERSION == 0x020f
char mcserv_build_id[16] = "PsIImcserv  2430";
#else
char mcserv_build_id[16] = "PsIImcserv  2710";
#endif
struct ModuleInfo Module = {"mcserv", MCSERV_VERSION};

static int mcserv_result[3];
static unsigned char mcserv_buf[MCSERV_BUFSIZE];
#if MCSERV_VERSION >= 0x020f
static unsigned char mcserv_request[1048];
static unsigned char mcserv_reserved[16];
#else
static unsigned char mcserv_request[1044];
#endif
static int mcserv_thread_id;
#if MCSERV_VERSION >= 0x020f
static sceSifQueueData queue;
static sceSifServeData server;
static int mcserv_busy;
static int mcserv_stop(int mode);
#endif

extern int mcserv_stub[];
static void mcserv_thread(void);
static void* mcserv_dispatch(unsigned int command, void* request, int size);
static int _McInit(void* request);
static int _McOpen(void* request);
static int _McClose(void* request);
static int _McSeek(void* request);
static int _McRead2(void* request);
static int _McWrite(void* request);
static int _McGetDir(void* request);
static int _McChDir(void* request);
static int _McFormat(void* request);
static int _McUnformat(void* request);
static int _McGetInfo2(void* request);
static int _McGetEntSpace(void* request);
static int _McDelete(void* request);
static int _McFlush(void* request);
static int _McSetFileInfo(void* request);
static int _McCheckBlock(void* request);

#if MCSERV_VERSION >= 0x0210
void mcserv_debug_printf(const char* format, ...) {
  // does nothing.
}
#endif

int start(int argc, char** argv) {
  struct ThreadParam param;
  int priority;
  int i;
  int thread;
#if MCSERV_VERSION >= 0x020f
  if (argc < 0)
    return mcserv_stop(-argc);
#endif
  if (RegisterLibraryEntries(mcserv_stub))
    return 1;
#if MCSERV_VERSION < 0x0210
  CpuEnableIntr();
#endif
  priority = DEFAULT_PRIORITY;
  for (i = 0; i < argc; i++) {
    if (!strncmp("thpri=", argv[i], 6)) {
      priority = strtol(argv[i] + 6, 0, 10);
      if (priority < 9 || priority > 123) {
        printf("mcserv.irx: thread prority definition ignored.\n");
        priority = DEFAULT_PRIORITY;
      }
    }
  }
  param.attr = TH_C;
  param.stackSize = 4096;
  param.initPriority = priority;
  param.option = 0;
  param.entry = mcserv_thread;
  thread = CreateThread(&param);
  mcserv_thread_id = thread;
  StartThread(thread, 0);
#if MCSERV_VERSION >= 0x020f
  return 2;
#else
  return 0;
#endif
}

#if MCSERV_VERSION >= 0x020f
#define LIBRARY_NOT_FOUND (-213)

static int mcserv_stop(int mode) {
  int intr_state;
  int result;
  if (mcserv_busy)
    return 2;
  CpuSuspendIntr(&intr_state);
  result = ReleaseLibraryEntries(mcserv_stub);
  CpuResumeIntr(intr_state);
  if (result == 0 || result == LIBRARY_NOT_FOUND) {
    TerminateThread(mcserv_thread_id);
    DeleteThread(mcserv_thread_id);
    sceSifRemoveRpc(&server, &queue);
    sceSifRemoveRpcQueue(&queue);
    return 1;
  }
  return 2;
}
#endif

int mcserv_change_priority(int priority) {
  struct ThreadInfo info;
  int result;
  if (priority < 9 || priority > 123)
    return INVALID_PRIORITY;
  ReferThreadStatus(0, &info);
  ChangeThreadPriority(0, 8);
  if (mcserv_thread_id > 0) {
    result = ChangeThreadPriority(mcserv_thread_id, priority);
    if (result < 0)
      return result;
  }
  ChangeThreadPriority(0, info.currentPriority);
  return 0;
}

static void mcserv_thread(void) {
#if MCSERV_VERSION < 0x020f
  sceSifQueueData queue;
  sceSifServeData server;
  WaitSema(McGetSemaphore());
#elif MCSERV_VERSION >= 0x0210
  memset(mcserv_reserved, 0, sizeof(mcserv_reserved));
#endif
  if (!sceSifCheckInit())
    sceSifInit();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, MCSERV_RPC_ID, mcserv_dispatch, mcserv_request, 0, 0, &queue);
  sceSifRpcLoop(&queue);
}

static int mcserv_set_priority(void* request);
static int mcserv_set_sio2_mode(void* request);

static void* mcserv_dispatch(unsigned int command, void* request, int size) {
#if MCSERV_VERSION >= 0x020f
  WaitSema(McGetSemaphore());
  mcserv_busy = 1;
#endif
  switch (command) {
    case MCSERV_CMD_INIT:
      mcserv_result[0] = _McInit(mcserv_request);
      mcserv_result[1] = Module.version;
      mcserv_result[2] = McGetModuleInfo()->version;
      break;
    case MCSERV_CMD_OPEN:
      mcserv_result[0] = _McOpen(mcserv_request);
      break;
    case MCSERV_CMD_CLOSE:
      mcserv_result[0] = _McClose(mcserv_request);
      break;
    case MCSERV_CMD_READ:
      mcserv_result[0] = _McRead2(mcserv_request);
      break;
    case MCSERV_CMD_WRITE:
      mcserv_result[0] = _McWrite(mcserv_request);
      break;
    case MCSERV_CMD_SEEK:
      mcserv_result[0] = _McSeek(mcserv_request);
      break;
    case MCSERV_CMD_GET_DIR:
      mcserv_result[0] = _McGetDir(mcserv_request);
      break;
    case MCSERV_CMD_FORMAT:
      mcserv_result[0] = _McFormat(mcserv_request);
      break;
    case MCSERV_CMD_UNFORMAT:
      mcserv_result[0] = _McUnformat(mcserv_request);
      break;
    case MCSERV_CMD_GET_INFO:
      mcserv_result[0] = _McGetInfo2(mcserv_request);
      break;
    case MCSERV_CMD_DELETE:
      mcserv_result[0] = _McDelete(mcserv_request);
      break;
    case MCSERV_CMD_FLUSH:
      mcserv_result[0] = _McFlush(mcserv_request);
      break;
    case MCSERV_CMD_CHANGE_DIR:
      mcserv_result[0] = _McChDir(mcserv_request);
      break;
    case MCSERV_CMD_SET_FILE_INFO:
      mcserv_result[0] = _McSetFileInfo(mcserv_request);
      break;
    case MCSERV_CMD_GET_ENTRY_SPACE:
      mcserv_result[0] = _McGetEntSpace(mcserv_request);
      break;
    case MCSERV_CMD_CHECK_BLOCK:
      mcserv_result[0] = _McCheckBlock(mcserv_request);
      break;
    case MCSERV_CMD_SET_PRIORITY:
      mcserv_result[0] = mcserv_set_priority(mcserv_request);
      break;
    case MCSERV_CMD_SET_SIO2_MODE:
      mcserv_result[0] = mcserv_set_sio2_mode(mcserv_request);
      break;
  }
  McReplaceBadBlock();
  SignalSema(McGetSemaphore());
#if MCSERV_VERSION >= 0x020f
  mcserv_busy = 0;
#endif
  return mcserv_result;
}

static int _McInit(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McInit(dP->fd);
}

static int mcserv_set_priority(void* request) {
  struct McDescriptorRequest* p = request;
  if (p->origin >= 9 && p->origin < 124) {
    ChangeThreadPriority(0, p->origin);
    return 0;
  }
  return INVALID_PRIORITY;
}

static int mcserv_set_sio2_mode(void* request) {
  struct McDescriptorRequest* p = request;
  return McSetSio2Mode(p->port);
}

static int _McOpen(void* rpc_buf) {
  struct McNameRequest* nP = (struct McNameRequest*)rpc_buf;

  return McOpen(nP->port, nP->slot, nP->name, nP->flags);
}

static int _McClose(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McClose(dP->fd);
}

static int _McSeek(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McSeek(dP->fd, dP->offset, dP->origin);
}

static int _McRead2(void* rpc_buf) {
  struct McDescriptorRequest* request = rpc_buf;
  struct McTransferReply reply;
  sceSifDmaData dma;
  int intr_state;
  int dma_id;
  unsigned char* destination = request->buffer;
  int total = 0;
  int got = 0;
  int remaining;
  int chunk;
  reply.dest1 = 0;
  reply.dest2 = 0;
  reply.second.size2 = 0;
  reply.first.size1 = 0;
  remaining = request->size;
  if (remaining <= 64)
    reply.first.size1 = remaining;
  else {
    int aligned = ((int)destination - 1) & ~63;
    int base = (int)destination - 64;
    reply.first.size1 = aligned - base;
  }
  remaining -= reply.first.size1;
  reply.second.size2 = remaining & 63;
  remaining -= reply.second.size2;
  if (reply.second.size2)
    reply.dest2 = destination + reply.first.size1 + remaining;
  if (reply.first.size1) {
    got = McRead(request->fd, reply.src1, reply.first.size1);
    if (got < 0) {
      reply.first.size1 = 0;
      reply.second.size2 = 0;
      goto send_reply;
    }
    total = got;
    reply.dest1 = destination;
    destination += total;
    if (reply.first.size1 != total) {
      reply.first.size1 = total;
      reply.second.size2 = 0;
      got = 0;
      goto send_reply;
    }
    got = 0;
  }
  while (remaining != 0) {
    chunk = remaining;
    if (remaining > MCSERV_BUFSIZE)
      chunk = MCSERV_BUFSIZE;
    got = McRead(request->fd, mcserv_buf, chunk);
    if (got < 0) {
      reply.second.size2 = 0;
      goto send_reply;
    }
    if (got != chunk) {
      reply.second.size2 = got & 63;
      if (reply.second.size2) {
        int aligned = got & -64;
        reply.dest2 = destination + aligned;
        memcpy(reply.src2, mcserv_buf + aligned, reply.second.size2);
      }
      if (got == reply.second.size2)
        goto skip_dma;
      dma.data = (unsigned int)mcserv_buf;
      dma.addr = (unsigned int)destination;
      dma.size = got - reply.second.size2;
      dma.mode = 0;
      CpuSuspendIntr(&intr_state);
      sceSifSetDma(&dma, 1);
      CpuResumeIntr(intr_state);
    } else {
      dma.data = (unsigned int)mcserv_buf;
      dma.addr = (unsigned int)destination;
      dma.size = got;
      dma.mode = 0;
      CpuSuspendIntr(&intr_state);
      sceSifSetDma(&dma, 1);
      CpuResumeIntr(intr_state);
    }
  skip_dma:
    total += got;
    remaining -= got;
    destination += got;
    if (got != chunk) {
      got = 0;
      goto send_reply;
    }
    got = 0;
  }
  if (reply.second.size2) {
    got = McRead(request->fd, reply.src2, reply.second.size2);
    if (got < 0) {
      reply.second.size2 = 0;
      goto send_reply;
    }
    total += got;
    reply.second.size2 = got;
    got = 0;
  }
send_reply:
  dma.data = (unsigned int)&reply;
  dma.addr = (unsigned int)request->param;
  dma.size = sizeof(reply);
  dma.mode = 0;
  CpuSuspendIntr(&intr_state);
  dma_id = sceSifSetDma(&dma, 1);
  CpuResumeIntr(intr_state);
  while (sceSifDmaStat(dma_id) >= 0)
    DelayThread(100);
  if (got != 0)
    return got;
  return total;
}

static int _McWrite(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;
  sceSifReceiveData rD;
  register int size_written;

  size_written = 0;

  if (dP->origin != 0) {
    int result = McWrite(dP->fd, dP->data, dP->origin);
    if (result != dP->origin)
      return result;
    size_written = result;
  }

  while (dP->size != 0) {
    register int size_to_write, r;

    if (dP->size > MCSERV_BUFSIZE)
      size_to_write = MCSERV_BUFSIZE;
    else
      size_to_write = dP->size;

    sceSifGetOtherData(&rD, dP->buffer, &mcserv_buf, size_to_write, 0);

    r = McWrite(dP->fd, &mcserv_buf, size_to_write);
    if (r != size_to_write) {
      if (r >= 0)
        return r + size_written;
      return r;
    }

    size_written += size_to_write;
    dP->size -= size_to_write;

    dP->buffer += size_to_write;
  }

  return size_written;
}

static int _McGetDir(void* rpc_buf) {
  struct McNameRequest* nP = (struct McNameRequest*)rpc_buf;
  register int status, file_entries, flags, r;
  sceSifDmaData dmaStruct;
  int intStatus;

  file_entries = 0;
  status = 0;
  flags = nP->flags;

#if MCSERV_VERSION >= 0x020f
  if (nP->maxent < 0)
    return McGetDir(nP->port, nP->slot, nP->name, flags & 0xffff, -1, 0);
#endif
  nP->maxent--;

  while (nP->maxent != -1) {
    r = McGetDir(nP->port, nP->slot, nP->name, flags & 0xffff, 1,
                 (struct McDirectoryEntry*)mcserv_buf);
    if (r < 0)
      return r;
    if (r == 0)
      goto dma_wait;

    dmaStruct.data = (unsigned int)mcserv_buf;
    dmaStruct.addr = (unsigned int)nP->destination.mcT;
    dmaStruct.size = sizeof(struct McDirectoryEntry);
    dmaStruct.mode = 0;

    CpuSuspendIntr(&intStatus);
    status = sceSifSetDma(&dmaStruct, 1);
    CpuResumeIntr(intStatus);
    file_entries++;

    flags = 1;
    nP->destination.mcT++;
    nP->maxent--;
  }

dma_wait:
  if (status == 0)
    return file_entries;

  while (sceSifDmaStat(status) >= 0)
    DelayThread(100);

  return file_entries;
}

static int _McChDir(void* rpc_buf) {
  struct McNameRequest* nP = (struct McNameRequest*)rpc_buf;
  register int status, r;
  sceSifDmaData dmaStruct;
  int intStatus;

  r = McChDir(nP->port, nP->slot, nP->name, (char*)mcserv_buf);

  dmaStruct.data = (unsigned int)mcserv_buf;
  dmaStruct.addr = (unsigned int)nP->destination.curdir;
  dmaStruct.size = 1024;
  dmaStruct.mode = 0;

  CpuSuspendIntr(&intStatus);
  status = sceSifSetDma(&dmaStruct, 1);
  CpuResumeIntr(intStatus);

  while (sceSifDmaStat(status) >= 0)
    DelayThread(100);

  return r;
}

static int _McFormat(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McFormat(dP->port, dP->slot);
}

static int _McUnformat(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McUnformat(dP->port, dP->slot);
}

static int _McGetInfo2(void* rpc_buf) {
  struct McDescriptorRequest* request = rpc_buf;
  int free_clusters = 0;
  int result;
  struct McTransferReply reply;
  sceSifDmaData dma;
  int intr_state;
  int dma_id;
  result = McDetectCard2(request->port, request->slot);
  if (request->origin)
    reply.first.type = McGetMcType(request->port, request->slot);
  if (result < -1) {
    reply.second.free = 0;
    reply.tail.formatted = 0;
  } else {
    if (request->offset) {
      free_clusters = McGetFreeClusters(request->port, request->slot);
      if (free_clusters < 0) {
        free_clusters = -11;
        reply.second.free = 0;
        reply.first.type = 0;
        reply.tail.formatted = 0;
      } else {
        reply.second.free = free_clusters;
      }
    }
    if (request->size && free_clusters >= 0)
      reply.tail.formatted = McGetFormat(request->port, request->slot) > 0;
  }
  dma.data = (unsigned int)&reply;
  dma.addr = (unsigned int)request->param;
  dma.size = sizeof(reply);
  dma.mode = 0;
  CpuSuspendIntr(&intr_state);
  dma_id = sceSifSetDma(&dma, 1);
  CpuResumeIntr(intr_state);
  while (sceSifDmaStat(dma_id) >= 0)
    DelayThread(100);
  if (free_clusters < 0)
    return free_clusters;
  return result;
}

static int _McGetEntSpace(void* rpc_buf) {
  struct McNameRequest* nP = (struct McNameRequest*)rpc_buf;

  return McGetEntSpace(nP->port, nP->slot, nP->name);
}

static int _McDelete(void* rpc_buf) {
  struct McNameRequest* nP = (struct McNameRequest*)rpc_buf;

  return McDelete(nP->port, nP->slot, nP->name, nP->flags);
}

static int _McFlush(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McFlush(dP->fd);
}

static int _McSetFileInfo(void* rpc_buf) {
  struct McNameRequest* nP = (struct McNameRequest*)rpc_buf;
  sceSifReceiveData rD;

  sceSifGetOtherData(&rD, (void*)nP->destination.mcT, &mcserv_buf, sizeof(struct McDirectoryEntry),
                     0);

  return McSetFileInfo(nP->port, nP->slot, nP->name, (struct McDirectoryEntry*)mcserv_buf,
                       nP->flags);
}

static int _McCheckBlock(void* rpc_buf) {
  struct McDescriptorRequest* dP = (struct McDescriptorRequest*)rpc_buf;

  return McCheckBlock(dP->port, dP->slot, dP->offset);
}
