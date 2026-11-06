#include "next/kernel/kdgo.h"
#include "next/kernel/fileio.h"
#include "next/kernel/klink.h"
#include "next/kernel/kprint.h"
#include "next/kernel/kscheme.h"
#include "sce/ee/libdma/libdma.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/libkernl/sif.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

// Only 6 of 8 of these slots are used.
static SifRpcClient cd[8] __attribute__((aligned(64)));
static unsigned int sShowStallMsg = 1;

// Submit an RPC. Doesn't wait for completion - must check with RpcBusy.
int RpcCall(int channel,
            unsigned int function,
            unsigned int mode,
            void* send,
            int send_size,
            void* receive,
            int receive_size) {
  return sceSifCallRpc(&cd[channel], function, mode, send, send_size, receive, receive_size, 0, 0);
}

// Report whether the channel still has a pending RPC.
unsigned int RpcBusy(int channel) {
  return sceSifCheckStatRpc(&cd[channel].request);
}

// Wait for an RPC to finish, optionally reporting that we're waiting.
void RpcSync(int channel) {
  if (RpcBusy(channel)) {
    if (sShowStallMsg)
      Msg(6, "STALL: [kernel] waiting for IOP on RPC port #%d\n", channel);
    while (RpcBusy(channel)) {
      for (int delay = 0; delay < 1000; delay++) {
      }
    }
  }
}

// Bind a RPC channel to an ID.
unsigned int RpcBind(int channel, int id) {
  int delay;
  for (;;) {
    int result = sceSifBindRpc(&cd[channel], id, SIF_RPCM_NOWAIT);
    if (result < 0) {
      MsgErr("Error: RpcBind failed on port #%d [%4.4X]\n", channel, id);
      return 1;
    }
    Msg(6, "kernel: RPC port #%d started [%4.4X]\n", channel, id);
    FlushCache(WRITEBACK_DCACHE);
    for (delay = 0; delay < 10000; delay++) {
    }
    if (cd[channel].server)
      return 0;
    Msg(6, "kernel: RPC port #%d not responding.\n", channel);
  }
}

// Bind all six OVERLORD services, loop forever if it fails.
void InitRPC() {
  if (RpcBind(PLAYER_RPC_CHANNEL, PLAYER_RPC_ID) || RpcBind(LOADER_RPC_CHANNEL, LOADER_RPC_ID) ||
      RpcBind(RAMDISK_RPC_CHANNEL, RAMDISK_RPC_ID) || RpcBind(DGO_RPC_CHANNEL, DGO_RPC_ID) ||
      RpcBind(STR_RPC_CHANNEL, STR_RPC_ID) || RpcBind(PLAY_RPC_CHANNEL, PLAY_RPC_ID)) {
    printf("Entering endless loop ... please wait\n");
    for (;;) {
    }
  }
}

// Send the player shutdown command, then wait for DMA to finish.
#if REVISION >= 20010920
void StopIOP() {
  static unsigned short x[2] __asm__("x.539") __attribute__((aligned(64)));
  x[1] = PLAYER_RPC_SHUTDOWN;
  RpcSync(PLAYER_RPC_CHANNEL);
  // Send size is 80 bytes but buffer is only 4, likely sends junk.
  RpcCall(PLAYER_RPC_CHANNEL, 0, 0, x, 0x50, 0, 0);
  printf("IOP shut down\n");
  // Unclear why we wait for VIF1 DMA here. Maybe this was a typo for IOP DMA?
  sceDmaSync(EE_DMA_VIF1, 0, 0);
  printf("DMA shut down\n");
}
#endif

static unsigned int sMsgNum = 0;
static RPC_Dgo_Cmd* sLastMsg = 0;
static RPC_Dgo_Cmd sMsg[2] __attribute__((aligned(64)));

// Start a DGO load with the fancy double-buffered loader.
void BeginLoadingDGO(char* name,
                     unsigned int buffer1,
                     unsigned int buffer2,
                     unsigned int current_heap) {
  RPC_Dgo_Cmd* message = &sMsg[sMsgNum];
  sMsgNum ^= 1;
  RpcSync(DGO_RPC_CHANNEL);
  message->result = DGO_RPC_RESULT_INIT;
  message->buffer1 = buffer1;
  message->buffer2 = buffer2;
  message->buffer_heap_top = current_heap;
  strcpy(message->name, name);
  RpcCall(DGO_RPC_CHANNEL, DGO_RPC_LOAD_FNO, SIF_RPCM_NOWAIT, message, sizeof(RPC_Dgo_Cmd), message,
          sizeof(RPC_Dgo_Cmd));
  sLastMsg = message;
}

// Wait for the next object, return the address it was loaded to and set
// last_object.
void* GetNextDGO(int* last_object) {
  void* buffer = 0;
  *last_object = 1;
  RpcSync(DGO_RPC_CHANNEL);
  if (sLastMsg) {
    if (sLastMsg->result == DGO_RPC_RESULT_MORE || sLastMsg->result == DGO_RPC_RESULT_DONE)
      buffer = (void*)sLastMsg->buffer1;
    if (sLastMsg->result == DGO_RPC_RESULT_MORE)
      *last_object = 0;
    sLastMsg = 0;
  }
  return buffer;
}

// Notify the IOP that we're done with the most recent object and request the
// next, updating the current heap pointer so the IOP can load the final object
// directly to the heap if it wants to.
void ContinueLoadingDGO(unsigned int current_heap) {
  RPC_Dgo_Cmd* message = &sMsg[sMsgNum];
  sMsgNum ^= 1;
  message->result = DGO_RPC_RESULT_INIT;
  message->buffer1 = 0;
  message->buffer2 = 0;
  message->buffer_heap_top = current_heap;
  RpcCall(DGO_RPC_CHANNEL, DGO_RPC_LOAD_NEXT_FNO, SIF_RPCM_NOWAIT, message, sizeof(RPC_Dgo_Cmd),
          message, sizeof(RPC_Dgo_Cmd));
  sLastMsg = message;
}

// Unused test function to load a DGO.
void LoadDGOTest() {
  int last_object = 0;
  unsigned int old_stall = sShowStallMsg;
  sShowStallMsg = 0;
  BeginLoadingDGO("TEST.DGO", DGO_TEST_BUFFER1, DGO_TEST_BUFFER2, DGO_TEST_HEAP);
  for (;;) {
    DgoObjectHeader* object = (DgoObjectHeader*)GetNextDGO(&last_object);
    if (!object)
      continue;
    Msg(6, "Loaded %s at %8.8X length %d\n", object->name, object, object->size);
    if (last_object)
      break;
    ContinueLoadingDGO(DGO_TEST_HEAP);
  }
  sShowStallMsg = old_stall;
}

// Load and link a DGO now, blocking.
void load_and_link_dgo(gstring* name, kheapinfo* heap, unsigned int flags, int buffer_size) {
  load_and_link_dgo_from_c(name->data, heap, flags, buffer_size);
}

// Load and link a DGO now, blocking.
void load_and_link_dgo_from_c(char* name, kheapinfo* heap, unsigned int flags, int buffer_size) {
  char* old_heap_top = heap->top;
  unsigned int old_stall = sShowStallMsg;

  // allocate temporary buffers for the double-buffered loader
  char* buffer2 = kmalloc(heap, buffer_size, KMALLOC_TOP | KMALLOC_ALIGN_64, "dgo-buffer-2");
  char* buffer1 = kmalloc(heap, buffer_size, KMALLOC_TOP | KMALLOC_ALIGN_64, "dgo-buffer-2");
  int last_object = 0;
  char filename[32];
  char object_name[64];
  kstrcpyup(filename, name);
  if (filename[strlen(filename) - 4] != '.') {
    filename[8] = 0;
    strcat(filename, ".CGO");
  }
  sShowStallMsg = 0;
  BeginLoadingDGO(filename, (unsigned int)buffer1, (unsigned int)buffer2,
                  ((int)heap->current + 63) & ~63);
  for (; !last_object;) {
    DgoObjectHeader* object = (DgoObjectHeader*)GetNextDGO(&last_object);
    if (object) {
      if (last_object)
        heap->top = old_heap_top;
      void* data = object + 1;
      strcpy(object_name, object->name);
      void* entry = link_and_exec(data, object_name, object->size, heap, flags);
      if (!last_object)
        ContinueLoadingDGO(((int)heap->current + 63) & ~63);
    }
  }
  sShowStallMsg = old_stall;
}
