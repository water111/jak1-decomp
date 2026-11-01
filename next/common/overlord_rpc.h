#ifndef OVERLORD_RPC_H
#define OVERLORD_RPC_H

#define SPLASH_SCREEN_ADDR 0x1000000

// RAMDISK (aka Server)
// See overlord/ramdisk.c
#define RAMDISK_RPC_ID 0xdeb3

// fnos
// Clear the entire ramdisk and load this file to IOP with given ID
#define FNO_RAMDISK_RESET_AND_LOAD 1
// Get data from the file by ID. Contents returned via RPC buffer
#define FNO_RAMDISK_GET_DATA 0
// Load an entire file to EE via ramdisk. Loaded to the given EE address.
#define FNO_RAMDISK_BYPASS_LOAD 4

// Load a file from DVD into the IOP Ramdisk
typedef struct RamdiskCommand {
  int unused;
  unsigned int file_id_or_ee_addr;
  int offset;
  int size;
  char name[16];
} RamdiskCommand;

typedef struct DgoRpcCommand {
  unsigned short reserved;
  unsigned short result;
  unsigned int buffer1;
  unsigned int buffer2;
  unsigned int buffer_heap_top;
  char name[16];
} DgoRpcCommand;

#endif
