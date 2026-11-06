#ifndef SCE_EE_LIBKERNL_SIF_H
#define SCE_EE_LIBKERNL_SIF_H

typedef struct {
  unsigned packet_size : 8;
  unsigned data_size : 24;
  unsigned destination, command, option;
} SifCommandHeader;

typedef struct {
  unsigned source, destination, size, attributes;
} SifDmaTransfer;

extern void sceSifStopDma(void);
extern unsigned sceSifGetReg(unsigned);
extern unsigned sceSifSetReg(unsigned, unsigned);
extern unsigned sceSifSetDma(SifDmaTransfer*, int);
extern void sceSifWriteBackDCache(void*, int);
extern void sceSifInitRpc(unsigned);
extern void sceSifExitRpc(void);

typedef struct {
  void* packet;
  unsigned packet_id;
  int thread_id;
  unsigned mode;
} SifRpcRequest;

typedef struct {
  SifRpcRequest request;
  unsigned command;
  void *buffer, *callback_buffer;
  void (*callback)(void*);
  void* callback_argument;
  struct RpcServer* server;
} SifRpcClient;

extern int sceSifBindRpc(SifRpcClient*, unsigned, unsigned);
extern int
sceSifCallRpc(SifRpcClient*, unsigned, unsigned, void*, int, void*, int, void (*)(void*), void*);

int sceSifCheckStatRpc(SifRpcRequest* request);

int sceSifDmaStat(unsigned id);

void sceSifRemoveCmdHandler(unsigned command);
void sceSifAddCmdHandler(unsigned command, void (*handler)(void*, void*), void* argument);

#endif
