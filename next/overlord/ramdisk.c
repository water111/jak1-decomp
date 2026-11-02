#include "overlord/ramdisk.h"
#include "common/overlord_rpc.h"
#include "overlord/iso.h"
#include "overlord/iso_api.h"
#include "overlord/isocommon.h"
#include "overlord/srpc.h"
#include "sce/iop/intrman.h"
#include "sce/iop/sifrpc.h"
#include "sce/iop/sysmem.h"
#include "sce/iop/thread.h"

// Handler for the RAMDISK RPC.
// The ramdisk loads files from the DVD to IOP RAM and allows
// the EE to request chunks of the file. Reading from the ramdisk
// is faster than reading from the DVD, but not instant.
// The game stores compressed visibility data in this ramdisk.
// There is also a bypass load mode which loads a file to the EE directly.
// The ramdisk supports multiple files, but this feature is effectively disabled.

#define RAMDISK_MAX_FILES 16
#define RAMDISK_RETURN_BUFFER_SIZE 0x2000

// the ramdisk size slowly increased over time...
#if REVISION <= 20010903
#define RAMDISK_SIZE 0xc8c00;
#elif REVISION <= 20011002
#define RAMDISK_SIZE 0xc9800;
#else
#define RAMDISK_SIZE 0xcac00;
#endif

typedef struct RamdiskFileRecord {
  int size;
  int offset;
  unsigned int file_id;
} RamdiskFileRecord;

static int gNumFiles = 0;
static int gMemUsed = 0;
static int gMemSize;
static unsigned char* gMem;
static RamdiskFileRecord gFiles[RAMDISK_MAX_FILES];
static unsigned char gReturnBuffer[RAMDISK_RETURN_BUFFER_SIZE];
static unsigned char gRPCBuf[sizeof(RamdiskCommand)];
unsigned char* gRamdiskRAM;
unsigned long gMemFreeAtStart;

static void* RPC_Ramdisk(unsigned int fno, void* data, int size);

// Initialize ramdisk globals
void InitRamdisk(void) {
  gNumFiles = 0;
  gMemUsed = 0;
  gMemSize = RAMDISK_SIZE;

  // Detect if there's more than 2 MB of free memory.
  // If so, we're running on a devkit and we allocate
  // an unused allocation. It's not clear what this was for,
  // possibly to simulate have less free memory like retail,
  // but the sizes here don't quite seem to be enough.
  if (QueryTotalFreeMemSize() > 0x200000) {
    void* unused = AllocSysMemory(SMEM_Low,
#if REVISION <= 20010903
                                  0x291c00,
#else
                                  0x1f5d00,
#endif
                                  0);
  }

  // Allocate the ramdisk memory
  gMem = AllocSysMemory(SMEM_Low, gMemSize, 0);
  if (!gMem) {
    return;
  }
  gMemFreeAtStart = QueryTotalFreeMemSize();
  gRamdiskRAM = gMem;
}

// Entrypoint for the Ramdisk SERVER thread. Standard RPC handler thread setup.
unsigned int Thread_Server(void) {
  sceSifQueueData queue;
  sceSifServeData server;
  CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, RAMDISK_RPC_ID, RPC_Ramdisk, gRPCBuf, 0, 0, &queue);
  CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

// RPC Handler for RAMDISK requests.
static void* RPC_Ramdisk(unsigned int fno, void* data, int size) {
  if (fno == FNO_RAMDISK_RESET_AND_LOAD) {
    // Load the file to the ramdisk
    RamdiskCommand* cmd = data;
    int length;
    FileRecord* file;
    gNumFiles = 0;
    gMemUsed = 0;

    // Look up file on disk
    file = FindISOFile(cmd->name);
    if (!file) {
      return 0;
    }
    length = GetISOFileLength(file);

    // bug: size check happens before alignment rounding.
    if (length + gMemUsed > gMemSize) {
      goto load_done;
    }
    if (gNumFiles == RAMDISK_MAX_FILES) {
      goto load_done;
    }
    {
      RamdiskFileRecord* record = &gFiles[gNumFiles++];
      unsigned char* dest = gMem + gMemUsed;
      unsigned int frame = gFrameNum;  // unused variable
      record->file_id = cmd->file_id_or_ee_addr;
      record->size = (length + 15) & ~15;
      record->offset = 0;
      LoadISOFileToIOP(file, dest, length);
      gMemUsed += record->size;
    }
  load_done:;
  } else if (fno == FNO_RAMDISK_GET_DATA) {
    // Look up file by ID
    RamdiskCommand* cmd = data;
    int index;
    int offset;
    offset = cmd->offset;
    for (index = 0; index < gNumFiles; index++) {
      if (gFiles[index].file_id == cmd->file_id_or_ee_addr) {
        break;
      }
      offset += gFiles[index].size;
    }
    if (index == gNumFiles) {
      return 0;
    }

    // combine requested offset with offset in cache
    offset += gFiles[index].offset;
    if (cmd->size > RAMDISK_RETURN_BUFFER_SIZE) {
      return 0;
    }

    // copy to buffer returned over RPC
    memcpy(gReturnBuffer, gMem + offset, cmd->size);
    return gReturnBuffer;
  } else if (fno == FNO_RAMDISK_BYPASS_LOAD) {
    // Locate file and load to EE directly (streaming copy)
    RamdiskCommand* cmd = data;
    FileRecord* file;
    file = FindISOFile(cmd->name);
    if (!file) {
      return 0;
    }
    LoadISOFileToEE(file, cmd->file_id_or_ee_addr, cmd->size);
    return 0;
  } else {
    // unsupported command...
  }
  return 0;
}
