#include "sce/ee/libpad/libpad.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/libkernl/sif.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

#define PAD_RPC_ID 0x80000100
#define PAD_RPC_EXTENDED_ID 0x80000101

enum {
  PAD_RPC_PORT_OPEN = 1,
  PAD_RPC_SET_MAIN_MODE = 6,
  PAD_RPC_SET_ACT_ALIGN = 8,
  PAD_RPC_SET_BUTTON_INFO = 10,
  PAD_RPC_SET_VREF_PARAM = 11,
  PAD_RPC_GET_PORT_MAX = 12,
  PAD_RPC_GET_SLOT_MAX = 13,
  PAD_RPC_PORT_CLOSE = 14,
  PAD_RPC_END = 15,
  PAD_RPC_INIT = 16,
  PAD_RPC_GET_MOD_VERSION = 18,
  PAD_RPC_SET_DEBUG = 20,
};

typedef struct {
  unsigned char data[32], reserved[16];
  unsigned char actuators[4][4], combinations[4][4];
  unsigned short modes[4];
  int frame, unused, length;
  unsigned char stage, mode, button_stage, open;
  unsigned char mode_count, mode_offset, actuator_count, combination_count;
  unsigned char unknown[4], state, request, ready, tail[6], buttons[4], padding[3];
} PadDma;

typedef struct {
  unsigned sequence, command, length;
  unsigned char data[20], padding[32];
} PadOutput;

typedef struct {
  PadDma* dma;
  PadOutput* output;
  unsigned iop_address;
  int transfer, open, unknown[2];
} PadPort;

static char __ps2_libinfo__[16] = "PsIIlibpad  2200";
static int isInit = 0;
static int isWarning = 1;
static SifRpcClient padsif[2];
static PadPort PadInfo[2][4];
static PadOutput dma_iop_data[2][4] __attribute__((aligned(64)));
static int buffer[32] __attribute__((aligned(64)));

static int scePadInit2(int);
static int scePadGetModVersion(void);

static void _send_to_iop(int port, int slot) {
  SifDmaTransfer transfer[16];
  PadOutput* output = PadInfo[port][slot].output;
  unsigned destination;
  int id;
  if (sceSifDmaStat(PadInfo[port][slot].transfer) >= 0) {
    if (isWarning)
      printf("libpad: sceSifSetDma faild\n");
    return;
  }
  destination = PadInfo[port][slot].iop_address;
  output->sequence++;
  destination += (output->sequence & 1) * 32;
  SyncDCache(output, (char*)output + 32);
  transfer[0].source = (unsigned)output;
  transfer[0].destination = destination;
  transfer[0].size = 32;
  transfer[0].attributes = 0;
  id = sceSifSetDma(transfer, 1);
  if (!id && isWarning)
    printf("libpad: sceSifSetDma faild\n");
  PadInfo[port][slot].transfer = id;
}

int scePadInit(int mode) {
  int delay, version;
  if (sizeof(PadDma) != 128) {
    if (isWarning)
      printf("libpad: tPadDma Structure Invalid\n");
    return 0;
  }
  isInit = 1;
  for (;;) {
    sceSifBindRpc(&padsif[0], PAD_RPC_ID, 0);
    if (padsif[0].server)
      break;
    for (delay = 0x10000; delay--;)
      ;
  }
  for (;;) {
    sceSifBindRpc(&padsif[1], PAD_RPC_EXTENDED_ID, 0);
    if (padsif[1].server)
      break;
    for (delay = 0x10000; delay--;)
      ;
  }
  version = scePadGetModVersion();
  if ((version >> 8) != 4) {
    if (isWarning) {
      printf("libpad: Module version mismatch ");
      printf("[libpad.a = %d.%d, padman.irx = %d.%d]\n", 4, 0, version >> 8, version & 255);
    }
    return 0;
  }
  return scePadInit2(mode);
}

#define CALL() sceSifCallRpc(&padsif[0], 1, 0, buffer, 128, buffer, 128, 0, 0)

static int scePadInit2(int mode) {
  int slot;
  for (slot = 0; slot < 4; slot++) {
    PadInfo[0][slot].open = 0;
    PadInfo[0][slot].unknown[1] = 0;
    PadInfo[0][slot].unknown[0] = 0;
    PadInfo[1][slot].open = 0;
    PadInfo[1][slot].unknown[1] = 0;
    PadInfo[1][slot].unknown[0] = 0;
  }
  buffer[0] = PAD_RPC_INIT;
  buffer[4] = 0;
  if (CALL() < 0)
    return 0;
  return buffer[3];
}

int scePadEnd(void) {
  int result;
  buffer[0] = PAD_RPC_END;
  if (CALL() < 0)
    return 0;
  result = buffer[3];
  if (result == 1)
    isInit = 0;
  return result;
}

#ifdef PAD_OPEN_ASM
#include "sce/ee/libpad/port-open.inc"
#else
int scePadPortOpen(int port, int slot, void* address) {
  PadDma* dma;
  PadOutput* output;
  int i;
  if ((unsigned)address & 63) {
    if (isWarning)
      printf("libpad: buffer addr is not 64 byte align. %08x\n", address);
    return 0;
  }
  if (PadInfo[port][slot].open == 1) {
    if (isWarning)
      printf("libpad: pad port is already open [%d][%d]\n", port, slot);
    return 0;
  }
  for (i = 0, dma = address; i < 2; i++, dma++) {
    dma->frame = 0;
    dma->state = scePadStateExecCmd;
    dma->request = scePadReqStateBusy;
    dma->open = 0;
    memset(dma->data, 255, 32);
    dma->length = 0;
  }
  buffer[0] = PAD_RPC_PORT_OPEN;
  buffer[1] = port;
  buffer[2] = slot;
  buffer[4] = (int)address;
  if (CALL() < 0)
    return 0;
  output = &dma_iop_data[port][slot];
  PadInfo[port][slot].open = 1;
  PadInfo[port][slot].iop_address = buffer[5];
  PadInfo[port][slot].dma = address;
  PadInfo[port][slot].transfer = 0;
  PadInfo[port][slot].output = output;
  output->sequence = 0;
  return buffer[3];
}

#endif

int scePadPortClose(int port, int slot) {
  if (!PadInfo[port][slot].open)
    return 0;
  buffer[0] = PAD_RPC_PORT_CLOSE;
  buffer[1] = port;
  buffer[2] = slot;
  buffer[4] = 1;
  if (CALL() < 0)
    return 0;
  PadInfo[port][slot].open = 0;
  return buffer[3];
}

static PadDma* scePadGetDmaStr(int port, int slot) {
  PadDma* dma = PadInfo[port][slot].dma;
  int index;
  SyncDCache(dma, dma + 2);
  index = dma->frame < dma[1].frame;
  return dma + index;
}

int scePadGetFrameCount(int port, int slot) {
  if (!PadInfo[port][slot].open)
    return 0;
  return scePadGetDmaStr(port, slot)->frame;
}

int scePadRead(int port, int slot, unsigned char* data) {
  PadDma* dma;
  if (!PadInfo[port][slot].open)
    return 0;
  dma = scePadGetDmaStr(port, slot);
  memcpy(data, dma->data, dma->length);
  return dma->length;
}

int scePadGetState(int port, int slot) {
  PadDma* dma;
  if (!PadInfo[port][slot].open)
    return scePadStateClosed;
  dma = scePadGetDmaStr(port, slot);
  if (dma->state == scePadStateStable) {
    if (dma->request == scePadReqStateBusy)
      return scePadStateExecCmd;
  }
  return dma->state;
}

static char* PadStateStr[] = {"DISCONNECT", "", "FINDCTP1", "", "", "EXECCMD", "STABLE", "ERROR"};

void scePadStateIntToStr(int state, char* text) {
  if (state >= 0 && state < 8)
    strcpy(text, PadStateStr[state]);
  else
    strcpy(text, "");
}

static int scePadSetReqState(int port, int slot, int state) {
  if (!PadInfo[port][slot].open)
    return 0;
  scePadGetDmaStr(port, slot)->request = state;
  return 1;
}

int scePadGetReqState(int port, int slot) {
  if (!PadInfo[port][slot].open)
    return 0;
  return scePadGetDmaStr(port, slot)->request;
}

static char* ReqStateStr[] = {"COMPLETE", "FAILED", "BUSY"};

void scePadReqIntToStr(int state, char* text) {
  if (state >= 0 && state < 4)
    strcpy(text, ReqStateStr[state]);
  else
    strcpy(text, "");
}

int scePadInfoAct(int port, int slot, int actuator, int term) {
  PadDma* dma;
  if (!PadInfo[port][slot].open)
    return 0;
  dma = scePadGetDmaStr(port, slot);
  if (dma->ready != 1)
    return 0;
  if (dma->stage < 2)
    return 0;
  if (actuator >= dma->actuator_count)
    return 0;
  if (actuator == -1)
    return dma->actuator_count;
  switch (term) {
    case InfoActFunc:
      return dma->actuators[actuator][0];
    case InfoActSub:
      return dma->actuators[actuator][1];
    case InfoActSize:
      return dma->actuators[actuator][2];
    case InfoActCurr:
      return dma->actuators[actuator][3];
  }
  return 0;
}

int scePadInfoComb(int port, int slot, int combination, int offset) {
  PadDma* dma;
  if (!PadInfo[port][slot].open)
    return 0;
  dma = scePadGetDmaStr(port, slot);
  if (dma->ready != 1)
    return 0;
  if (dma->stage < 2)
    return 0;
  if (combination == -1)
    return dma->combination_count;
  if (combination >= dma->combination_count)
    return 0;
  switch (offset) {
    case -1:
      return dma->combinations[combination][0];
    case 0:
      return dma->combinations[combination][1];
    case 1:
      return dma->combinations[combination][2];
    case 2:
      return dma->combinations[combination][3];
  }
  return 0;
}

int scePadInfoMode(int port, int slot, int term, int offset) {
  PadDma* dma;
  if (!PadInfo[port][slot].open)
    return 0;
  dma = scePadGetDmaStr(port, slot);
  if (dma->ready != 1)
    return 0;
  if (dma->request == scePadReqStateBusy)
    return 0;
  switch (term) {
    case InfoModeCurID:
      if (dma->mode == 0xf3)
        return 0;
      return dma->mode >> 4;
    case InfoModeCurExID:
      if (dma->stage == 1)
        return 0;
      return dma->modes[dma->mode_offset];
    case InfoModeCurExOffs:
      if (dma->stage == 1)
        return 0;
      return dma->mode_offset;
    case InfoModeIdTable:
      if (dma->stage == 1)
        return 0;
      if (offset == -1)
        return dma->mode_count;
      if (offset < dma->mode_count)
        return dma->modes[offset];
  }
  return 0;
}

int scePadSetMainMode(int port, int slot, int offset, int lock) {
  buffer[0] = PAD_RPC_SET_MAIN_MODE;
  buffer[1] = port;
  buffer[2] = slot;
  buffer[3] = offset;
  buffer[4] = lock;
  if (CALL() < 0)
    return 0;
  if (buffer[5] == 1)
    scePadSetReqState(port, slot, scePadReqStateBusy);
  return buffer[5];
}

int scePadSetActDirect(int port, int slot, const unsigned char* data) {
  PadOutput* output;
  int i;
  if (scePadGetDmaStr(port, slot)->ready != 1)
    return 0;
  output = PadInfo[port][slot].output;
  for (i = 0; i < 6; i++)
    output->data[i] = data[i];
  output->length = 6;
  output->command = 1;
  _send_to_iop(port, slot);
  return 1;
}

int scePadSetActAlign(int port, int slot, const unsigned char* data) {
  int i;
  int* request = buffer;
  unsigned char* bytes;
  request[0] = PAD_RPC_SET_ACT_ALIGN;
  request[1] = port;
  request[2] = slot;
  bytes = (unsigned char*)&request[3];
  for (i = 0; i < 6; i++)
    bytes[i] = data[i];
  if (sceSifCallRpc(&padsif[0], 1, 0, request, 128, request, 128, 0, 0) < 0)
    return 0;
  if (request[5] == 1)
    scePadSetReqState(port, slot, scePadReqStateBusy);
  return request[5];
}

static int scePadGetButtonMask(int port, int slot) {
  PadDma* dma;
  if (!PadInfo[port][slot].open)
    return 0;
  dma = scePadGetDmaStr(port, slot);
  if (dma->ready != 1)
    return 0;
  if (dma->stage < 2)
    return 0;
  if (dma->button_stage < 2)
    return 0;
  return (unsigned long)dma->buttons[0] + ((unsigned long)dma->buttons[1] << 8) +
         ((unsigned long)dma->buttons[2] << 16) + ((unsigned long)dma->buttons[3] << 24);
}

static int scePadSetButtonInfo(int port, int slot, int buttons) {
  buffer[0] = PAD_RPC_SET_BUTTON_INFO;
  buffer[1] = port;
  buffer[2] = slot;
  buffer[3] = buttons;
  if (CALL() < 0)
    return 0;
  if (buffer[4] == 1)
    scePadSetReqState(port, slot, scePadReqStateBusy);
  return buffer[4];
}

int scePadInfoPressMode(int port, int slot) {
  if (!PadInfo[port][slot].open)
    return 0;
  return scePadGetButtonMask(port, slot) == 0x3ffff;
}

int scePadEnterPressMode(int port, int slot) {
  if (!PadInfo[port][slot].open)
    return 0;
  return scePadSetButtonInfo(port, slot, 0xfff);
}

int scePadExitPressMode(int port, int slot) {
  if (!PadInfo[port][slot].open)
    return 0;
  return scePadSetButtonInfo(port, slot, 0);
}

static int scePadSetVrefParam(int port, int slot, const unsigned char* data) {
  buffer[0] = PAD_RPC_SET_VREF_PARAM;
  buffer[1] = port;
  buffer[2] = slot;
  memcpy((unsigned char*)&buffer[3], data, 12);
  if (CALL() < 0)
    return 0;
  if (buffer[7] == 1)
    scePadSetReqState(port, slot, scePadReqStateBusy);
  return buffer[7];
}

static int scePadGetPortMax(void) {
  buffer[0] = PAD_RPC_GET_PORT_MAX;
  if (CALL() < 0)
    return 0;
  return buffer[3];
}

int scePadGetSlotMax(int port) {
  buffer[0] = PAD_RPC_GET_SLOT_MAX;
  buffer[1] = port;
  if (CALL() < 0)
    return 0;
  return buffer[3];
}

static int scePadGetModVersion(void) {
  buffer[0] = PAD_RPC_GET_MOD_VERSION;
  if (CALL() < 0)
    return 0;
  return buffer[3];
}

int scePadSetWarningLevel(int level) {
  buffer[0] = PAD_RPC_SET_DEBUG;
  buffer[1] = level;
  if (CALL() < 0)
    return 0;
  return buffer[2];
}
