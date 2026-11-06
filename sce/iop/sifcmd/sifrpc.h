#ifndef _SIFRPC_H_DEFS
#define _SIFRPC_H_DEFS

// EE/IOP Remote Procedure Calls with SIF.

typedef struct _sif_rpc_data {
  void* paddr;        // packet address
  unsigned int pid;   // packet id
  int tid;            // thread id
  unsigned int mode;  // call mode
} sceSifRpcData;

typedef void (*sceSifEndFunc)(void*);

typedef struct _sif_client_data {
  struct _sif_rpc_data rpcd;
  unsigned int command;
  void* buff;
  void* cbuff;
  sceSifEndFunc func;
  void* para;
  struct _sif_serve_data* serve;
} sceSifClientData;

typedef void* (*sceSifRpcFunc)(unsigned int, void*, int);

typedef struct _sif_serve_data {
  unsigned int command;
  sceSifRpcFunc func;
  void* buff;
  int size;
  sceSifRpcFunc cfunc;
  void* cbuff;
  int csize;
  sceSifClientData* client;
  void* paddr;
  unsigned int fno;
  void* receive;
  int rsize;
  int rmode;
  unsigned int rid;
  struct _sif_serve_data* link;
  struct _sif_serve_data* next;
  struct _sif_queue_data* base;
} sceSifServeData;

typedef struct _sif_queue_data {
  int key;
  int active;
  struct _sif_serve_data* link;
  struct _sif_serve_data* start;
  struct _sif_serve_data* end;
  struct _sif_queue_data* next;
} sceSifQueueData;

void sceSifInitRpc(unsigned int mode);
void sceSifSetRpcQueue(sceSifQueueData*, int);
void sceSifRegisterRpc(sceSifServeData*,
                       unsigned int,
                       sceSifRpcFunc,
                       void*,
                       sceSifRpcFunc,
                       void*,
                       sceSifQueueData*);
void sceSifRpcLoop(sceSifQueueData*);

typedef struct {
  sceSifRpcData rpcd;
  void* src;
  void* dest;
  int size;
} sceSifReceiveData;

int sceSifGetOtherData(sceSifReceiveData* request,
                       void* src,
                       void* dest,
                       int size,
                       unsigned int mode);

int sceSifBindRpc(sceSifClientData* client, unsigned int command, unsigned int mode);
int sceSifCallRpc(sceSifClientData* client,
                  unsigned int function,
                  unsigned int mode,
                  void* send,
                  int send_size,
                  void* receive,
                  int receive_size,
                  sceSifEndFunc callback,
                  void* argument);

#endif
