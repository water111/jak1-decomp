#ifndef NEXT_KERNEL_KDGO_H
#define NEXT_KERNEL_KDGO_H

#include "next/kernel/kmalloc.h"
#include "next/kernel/kscheme.h"

void InitRPC();
void StopIOP();
void load_and_link_dgo_from_c(char*, kheapinfo*, unsigned int, int);

void load_and_link_dgo(gstring*, kheapinfo*, unsigned int, int);
int RpcCall(int channel,
            unsigned int function,
            unsigned int mode,
            void* send,
            int send_size,
            void* receive,
            int receive_size);
unsigned int RpcBusy(int channel);
void LoadDGOTest();

enum RpcChannel {
  PLAYER_RPC_CHANNEL,
  LOADER_RPC_CHANNEL,
  RAMDISK_RPC_CHANNEL,
  DGO_RPC_CHANNEL,
  STR_RPC_CHANNEL,
  PLAY_RPC_CHANNEL
};

enum RpcService {
  PLAYER_RPC_ID = 0xdeb1,
  LOADER_RPC_ID,
  RAMDISK_RPC_ID,
  DGO_RPC_ID,
  STR_RPC_ID,
  PLAY_RPC_ID
};

#define PLAYER_RPC_SHUTDOWN 0x14
#define DGO_RPC_LOAD_FNO 0
#define DGO_RPC_LOAD_NEXT_FNO 1
#define DGO_RPC_RESULT_INIT 666
#define DGO_RPC_RESULT_MORE 2
#define DGO_RPC_RESULT_DONE 0

struct RPC_Dgo_Cmd {
  unsigned short reserved;
  unsigned short result;
  unsigned int buffer1;
  unsigned int buffer2;
  unsigned int buffer_heap_top;
  char name[16];
};

struct DgoObjectHeader {
  unsigned int size;
  char name[60];
};

#define DGO_TEST_BUFFER1 0x04800000
#define DGO_TEST_BUFFER2 0x04c00000
#define DGO_TEST_HEAP 0x04000000

void RpcSync(int channel);
unsigned int RpcBind(int channel, int id);
void BeginLoadingDGO(char*, unsigned int buffer1, unsigned int buffer2, unsigned int current_heap);
void* GetNextDGO(int* last_object);
void ContinueLoadingDGO(unsigned int current_heap);
#endif
