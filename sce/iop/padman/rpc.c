#include "sce/iop/padman/rpc.h"
#include "sce/iop/padman/padman.h"
#include "sce/iop/sifcmd/sifrpc.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/thbase/thread.h"
#define PAD_RPC_ID 0x80000100
#define PAD_RPC_EXTENDED_ID 0x80000101
#define PAD_RPC_DEFAULT_PRIORITY 46

typedef struct PadRpcServer {
  int thread_id;
  sceSifQueueData queue;
  sceSifServeData server;
  u_int buffer[32];
} PadRpcServer;

static PadRpcServer rpc_servers[2];

static void* pad_rpc_initialize(u_int* data) {
  data[3] = padInit((void*)data[4]);
  return data;
}

static void* pad_rpc_end(u_int* data) {
  data[3] = padEnd();
  return data;
}

static void* pad_rpc_debug(u_int* data) {
  data[2] = padSetDebug(data[1]);
  return data;
}

static void* pad_rpc_version(u_int* data) {
  data[3] = padGetModVersion();
  return data;
}

static void* pad_rpc_open(u_int* data) {
  data[3] = padPortOpen(data[1], data[2], (void*)data[4], (void**)&data[5]);
  return data;
}

static void* pad_rpc_close(u_int* data) {
  data[3] = padPortClose(data[1], data[2], data[4]);
  return data;
}

static void* pad_rpc_info_act(u_int* data) {
  data[5] = padInfoAct(data[1], data[2], data[3], data[4]);
  return data;
}

static void* pad_rpc_info_comb(u_int* data) {
  data[5] = padInfoComb(data[1], data[2], data[3], data[4]);
  return data;
}

static void* pad_rpc_info_mode(u_int* data) {
  data[5] = padInfoMode(data[1], data[2], data[3], data[4]);
  return data;
}

static void* pad_rpc_main_mode(u_int* data) {
  data[5] = padSetMainMode(data[1], data[2], data[3], data[4]);
  return data;
}

static void* pad_rpc_act_direct(u_int* data) {
  data[5] = padSetActDirect(data[1], data[2], (u_char*)&data[3]);
  return data;
}

static void* pad_rpc_act_align(u_int* data) {
  data[5] = padSetActAlign(data[1], data[2], (u_char*)&data[3]);
  return data;
}

static void* pad_rpc_button_mask(u_int* data) {
  data[3] = padGetButtonMask(data[1], data[2]);
  return data;
}

static void* pad_rpc_button_info(u_int* data) {
  data[4] = padSetButtonInfo(data[1], data[2], data[3]);
  return data;
}

static void* pad_rpc_vref(u_int* data) {
  data[7] = padSetVrefParam(data[1], data[2], (u_char*)&data[3]);
  return data;
}

static void* pad_rpc_port_max(u_int* data) {
  data[3] = padGetPortMax();
  return data;
}

static void* pad_rpc_slot_max(u_int* data) {
  data[3] = padGetSlotMax(data[1]);
  return data;
}

static void* pad_rpc_dispatch(u_int function, void* data, int size) {
  function = (u_int)data;
  switch (*(u_int*)function) {
    case PAD_RPC_INIT:
      pad_rpc_initialize((u_int*)function);
      break;
    case PAD_RPC_END:
      pad_rpc_end((u_int*)function);
      break;
    case PAD_RPC_GET_MOD_VERSION:
      pad_rpc_version((u_int*)function);
      break;
    case PAD_RPC_PORT_OPEN:
      pad_rpc_open((u_int*)function);
      break;
    case PAD_RPC_PORT_CLOSE:
      pad_rpc_close((u_int*)function);
      break;
    case PAD_RPC_INFO_ACT:
      pad_rpc_info_act((u_int*)function);
      break;
    case PAD_RPC_INFO_COMB:
      pad_rpc_info_comb((u_int*)function);
      break;
    case PAD_RPC_INFO_MODE:
      pad_rpc_info_mode((u_int*)function);
      break;
    case PAD_RPC_SET_MAIN_MODE:
      pad_rpc_main_mode((u_int*)function);
      break;
    case PAD_RPC_SET_ACT_DIRECT:
      pad_rpc_act_direct((u_int*)function);
      break;
    case PAD_RPC_SET_ACT_ALIGN:
      pad_rpc_act_align((u_int*)function);
      break;
    case PAD_RPC_GET_BUTTON_MASK:
      pad_rpc_button_mask((u_int*)function);
      break;
    case PAD_RPC_SET_BUTTON_INFO:
      pad_rpc_button_info((u_int*)function);
      break;
    case PAD_RPC_SET_VREF_PARAM:
      pad_rpc_vref((u_int*)function);
      break;
    case PAD_RPC_GET_PORT_MAX:
      pad_rpc_port_max((u_int*)function);
      break;
    case PAD_RPC_GET_SLOT_MAX:
      pad_rpc_slot_max((u_int*)function);
      break;
    case PAD_RPC_SET_DEBUG:
      pad_rpc_debug((u_int*)function);
      break;
    case PAD_RPC_NOOP:
      break;
    default:
      printf("invalid function code (%03x)\n", *(u_int*)data);
      break;
  }
  return data;
}

static void pad_rpc_thread(void) {
  if (!sceSifCheckInit()) {
    printf("yet sif hasn't been init\n");
    sceSifInit();
  }
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&rpc_servers[0].queue, GetThreadId());
  sceSifRegisterRpc(&rpc_servers[0].server, PAD_RPC_ID, pad_rpc_dispatch, rpc_servers[0].buffer, 0,
                    0, &rpc_servers[0].queue);
  sceSifRpcLoop(&rpc_servers[0].queue);
}

static void* pad_rpc_extended_dispatch(u_int function, void* buffer, int size) {
  printf("Extend Service : this service is not support\n");
  return buffer;
}

static void pad_rpc_extended_thread(void) {
  if (!sceSifCheckInit()) {
    printf("yet sif hasn't been init\n");
    sceSifInit();
  }
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&rpc_servers[1].queue, GetThreadId());
  sceSifRegisterRpc(&rpc_servers[1].server, PAD_RPC_EXTENDED_ID, pad_rpc_extended_dispatch,
                    rpc_servers[1].buffer, 0, 0, &rpc_servers[1].queue);
  sceSifRpcLoop(&rpc_servers[1].queue);
}

int pad_rpc_init(int priority) {
  struct ThreadParam thread;
  if (!priority)
    priority = PAD_RPC_DEFAULT_PRIORITY;
  thread.attr = TH_C;
  thread.entry = pad_rpc_thread;
  thread.initPriority = priority;
  thread.stackSize = 2048;
  rpc_servers[0].thread_id = CreateThread(&thread);
  if (!rpc_servers[0].thread_id)
    return 0;
  StartThread(rpc_servers[0].thread_id, 0);
  thread.attr = TH_C;
  thread.entry = pad_rpc_extended_thread;
  thread.initPriority = priority;
  thread.stackSize = 2048;
  rpc_servers[1].thread_id = CreateThread(&thread);
  if (!rpc_servers[1].thread_id)
    return 0;
  StartThread(rpc_servers[1].thread_id, 0);
  return 1;
}
