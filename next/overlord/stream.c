#include "overlord/iso.h"
#include "overlord/iso_api.h"
#include "overlord/isocommon.h"
#include "overlord/overlord.h"
#include "sce/iop/intrman.h"
#include "sce/iop/sif.h"
#include "sce/iop/sifrpc.h"
#include "sce/iop/thread.h"

typedef struct StrFileHeader {
  unsigned int sectors[64];
  unsigned int sizes[64];
} StrFileHeader;

typedef struct CacheEntry {
  FileRecord* file;
#if REVISION <= 20010903
  FileRecord* vag_file;
#endif
  int countdown;
  StrFileHeader header;
} CacheEntry;

enum {
  STREAM_CACHE_ENTRIES = 4
};

static CacheEntry sCache[STREAM_CACHE_ENTRIES];
static StrCommand sRPCBuf;
static PlayCommand sRPCBuf2[
#if REVISION <= 20011002
    1
#else
    2
#endif
];

static void* RPC_STR(unsigned int fno, void* data, int size);
static void* RPC_PLAY(unsigned int fno, void* data, int size);

unsigned int STRThread(void) {
  sceSifQueueData queue;
  sceSifServeData server;

  CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, STR_RPC_ID, RPC_STR, &sRPCBuf, 0, 0, &queue);
  CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

unsigned int PLAYThread(void) {
  sceSifQueueData queue;
  sceSifServeData server;

  CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, PLAY_RPC_ID, RPC_PLAY, sRPCBuf2, 0, 0, &queue);
  CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

static void* RPC_STR(unsigned int fno, void* data, int size) {
  StrCommand* cmd = data;
  char name[16];
#if REVISION <= 20010903
  char vag_name[16];
#elif REVISION <= 20011002
  char vag_wad_name[16];
#endif
  FileRecord* file;
#if REVISION <= 20010903
  FileRecord* vag_file;
#elif REVISION <= 20011002
  FileRecord* vag_wad;
  VagDirEntry* vag;
#endif
  int entry;
  int oldest;
  int oldest_entry;

  if (cmd->chunk_id >= 0) {
    // Get animation file name and look up in filesystem.
    ISONameFromAnimationName((IsoName*)name, cmd->name);
    file = isofs->find_in(name);
    if (!file) {
      cmd->result = 1;
      return cmd;
    }

#if REVISION <= 20010903
    memcpy(vag_name, name, 12);
    strcpy(&vag_name[8], "INT");
    vag_file = isofs->find_in(vag_name);
    if (!vag_file) {
      strcpy(&vag_name[8], gLanguage);
      vag_file = isofs->find_in(vag_name);
    }
    if (!vag_file) {
    } else {
    }
#elif REVISION <= 20011002
    memcpy(vag_wad_name, "VAGWAD  ", 8);
    strcpy(&vag_wad_name[8], gLanguage);
    vag_wad = isofs->find_in(vag_wad_name);
    vag = FindVAGFile(name);
#endif

    /* BUG: a hit stops aging the remaining entries and does not refresh the
     * hit countdown. This is not a true least-recently-used cache. */
    oldest = 0x7fffffff;
    for (entry = 0; entry < STREAM_CACHE_ENTRIES; entry++) {
      if (sCache[entry].file == file)
        break;
      sCache[entry].countdown--;
      if (sCache[entry].countdown < oldest) {
        oldest_entry = entry;
        oldest = sCache[entry].countdown;
      }
    }

    // on miss, replace the oldest entry and load its header
    if (entry == STREAM_CACHE_ENTRIES) {
      entry = oldest_entry;
      sCache[entry].file = file;
#if REVISION <= 20010903
      sCache[entry].vag_file = vag_file;
#endif
      sCache[entry].countdown = 0x7ffffffe;
      if (!LoadISOFileToIOP(file, &sCache[entry].header, sizeof(StrFileHeader))) {
        cmd->result = 1;
        return cmd;
      }
    }

    // Load the chunk (no bounds check!!)
    if (!LoadISOFileChunkToEE(file, cmd->ee_addr, sCache[entry].header.sizes[cmd->chunk_id],
                              sCache[entry].header.sectors[cmd->chunk_id])) {
      cmd->result = 1;
    } else {
      cmd->length = sCache[entry].header.sizes[cmd->chunk_id];
      cmd->result = 0;
    }
#if REVISION <= 20010903
    if (vag_file) {
      QueueVAGStream(vag_file, 0, 1);
    }
#elif REVISION <= 20011002
    if (vag) {
      QueueVAGStream(vag_wad, vag, 0, 1);
    }
#endif
  } else {
    // A negative chunk ID means we do a plain file load.
    FileRecord* file;
    file = isofs->find(cmd->name);
    if (!file) {
      cmd->result = 1;
    } else {
      cmd->length = LoadISOFileToEE(file, cmd->ee_addr, cmd->length);
      if (!cmd->length)
        cmd->result = 1;
      else
        cmd->result = 0;
    }
  }
  return cmd;
}

static void* RPC_PLAY(unsigned int fno, void* data, int size) {
#if REVISION <= 20010903
  PlayCommand* cmd = data;
#elif REVISION <= 20011002
  PlayCommand* cmd;
#endif
  char name[16];
  FileRecord* file;
#if REVISION <= 20010903
  char* literal;
  int length;
  int i;
#else
  VagDirEntry* vag;
#endif
#if REVISION > 20011002
  int count = size / 64;
  int message;
#endif
#if REVISION >= 20011031
  static int global_vag_count;
#endif

#if REVISION > 20010903 && REVISION <= 20011002
  {
    cmd = data;
#elif REVISION > 20011002
  for (message = 0; message < count; message++) {
    PlayCommand* cmd = (PlayCommand*)data + message;
    fno = cmd->result;
#endif

    // Get the name
    if (cmd->name[0] != '$') {
      ISONameFromAnimationName((IsoName*)name, cmd->name);
    } else {
#if REVISION <= 20010903
      literal = &cmd->name[1];
      length = strlen(literal);
#else
      char* literal = &cmd->name[1];
      int length = strlen(literal);
      int i;
#endif
      if (length > 8) {
        memcpy(name, literal, 8);
      } else {
        memset(name, ' ', 8);
        memcpy(name, literal, length);
      }
      // upper case.
      for (i = 0; i < 8; i++) {
        if (name[i] >= 'a' && name[i] <= 'z')
          name[i] -= 'a' - 'A';
      }
    }
#if REVISION <= 20010903
    strcpy(&name[8], "INT");
    file = isofs->find_in(name);
    if (!file) {
      strcpy(&name[8], gLanguage);
      file = isofs->find_in(name);
    }
    if (!file) {
    }
#else
    vag = FindVAGFile(name);
    memcpy(name, "VAGWAD  ", 8);
    strcpy(&name[8], gLanguage);

#if defined(MEDIA_DVD) && REVISION >= 20011031
    // adjust the name to SAGWAD on every 64th load.
    // this fails to find the file (no big deal since the proper name is
    // checked below), but triggers anti-piracy code that locks up the IOP
    // if there's less than 1 GB of data. (eg: hackers made a CD version of the
    // game.)
    if (fno == 0 && (++global_vag_count & 0x3f) == 0) {
      name[0] -= 3;
      file = isofs->find_in(name);
      name[0] += 3;
    }
#endif
    file = isofs->find_in(name);
#endif
    if (fno == 0) {
#if REVISION <= 20010903
      PlayVAGStream(file, 0, 0x400, 1);
#elif REVISION <= 20011002
      PlayVAGStream(file, vag, 0, 0x400, 1);
#else
      PlayVAGStream(file, vag, cmd->address, 0x400, 1, 0);
#endif
    }
#if REVISION <= 20011002
    else {
#else
    else if (fno == 1) {
#endif
#if REVISION <= 20010903
      StopVAGStream(file, 1);
#else
      StopVAGStream(vag, 1);
#endif
#if REVISION > 20011002
    }
    else {
      QueueVAGStream(file, vag, 0, 1);
#endif
    }
#if REVISION > 20010903
  }
#endif
#if REVISION <= 20011002
  return cmd;
#else
  return data;
#endif
}
