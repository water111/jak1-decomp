#ifndef SCE_IOP_MCSERV_H
#define SCE_IOP_MCSERV_H

#include "sce/iop/mcman/mcman.h"

// RPC command numbers for the MCSERV RPC
#define MCSERV_CMD_GET_INFO 1
#define MCSERV_CMD_OPEN 2
#define MCSERV_CMD_CLOSE 3
#define MCSERV_CMD_SEEK 4
#define MCSERV_CMD_READ 5
#define MCSERV_CMD_WRITE 6
#define MCSERV_CMD_FLUSH 10
#define MCSERV_CMD_CHANGE_DIR 12
#define MCSERV_CMD_GET_DIR 13
#define MCSERV_CMD_SET_FILE_INFO 14
#define MCSERV_CMD_DELETE 15
#define MCSERV_CMD_FORMAT 16
#define MCSERV_CMD_UNFORMAT 17
#define MCSERV_CMD_GET_ENTRY_SPACE 18
#define MCSERV_CMD_SET_PRIORITY 20
#define MCSERV_CMD_SET_SIO2_MODE 21
#define MCSERV_CMD_CHECK_BLOCK 51
#define MCSERV_CMD_INIT 254

struct McDescriptorRequest {
  int fd;
  int port;
  int slot;
  int size;
  int offset;
  int origin;
  unsigned char* buffer;
  void* param;
  unsigned char data[16];
};

struct McNameRequest {
  int port;
  int slot;
  int flags;
  int maxent;

  union {
    struct McDirectoryEntry* mcT;
    char* curdir;
  } destination;

  char name[1024];
};

struct McTransferReply {
  union {
    int size1;
    int type;
  } first;

  union {
    int size2;
    int free;
  } second;

  void* dest1;
  void* dest2;
  unsigned char src1[64];
  unsigned char src2[64];

  union {
    int formatted;
    unsigned char reserved[48];
  } tail;
};

int mcserv_change_priority(int priority);

#endif
