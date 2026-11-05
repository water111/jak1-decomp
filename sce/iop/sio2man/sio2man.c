#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/dmacman/dmacman.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/ctype.h"
#include "sce/iop/sysclib/stdlib.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thevent/thevent.h"

// SIO2 Registers
#define SIO2_EVENT_PAD_INIT 0x0001
#define SIO2_EVENT_PAD_READY 0x0002
#define SIO2_EVENT_MC_INIT 0x0004
#define SIO2_EVENT_MC_READY 0x0008
#define SIO2_EVENT_MTAP_INIT 0x0010
#define SIO2_EVENT_MTAP_READY 0x0020
#define SIO2_EVENT_INIT_40 0x0040
#define SIO2_EVENT_READY_80 0x0080
#define SIO2_EVENT_INIT_100 0x0100
#define SIO2_EVENT_READY_200 0x0200
#define SIO2_EVENT_TRANSFER 0x0400
#define SIO2_EVENT_TRANSFER_DONE 0x0800
#define SIO2_EVENT_RESET 0x1000
#define SIO2_EVENT_IRQ 0x2000

#define SIO2_EVENT_INIT_MASK                                                              \
  (SIO2_EVENT_PAD_INIT | SIO2_EVENT_MC_INIT | SIO2_EVENT_MTAP_INIT | SIO2_EVENT_INIT_40 | \
   SIO2_EVENT_INIT_100)

#define SIO2_DMA_PRIORITY 3

#define SIO2_CTRL 0xbf808268
#define SIO2_RECV1 0xbf80826c
#define SIO2_SEND1 0xbf808240
#define SIO2_SEND2 0xbf808244
#define SIO2_RECV2 0xbf808270
#define SIO2_SEND3 0xbf808200
#define SIO2_RECV3 0xbf808274
#define SIO2_UNK78 0xbf808278
#define SIO2_UNK7C 0xbf80827c
#define SIO2_DATA_OUT 0xbf808260
#define SIO2_DATA_IN 0xbf808264
#define SIO2_STAT 0xbf808280

#define REG32(address) (*(volatile unsigned int*)(address))
#define REG8(address) (*(unsigned char*)(address))

char sio2_build_id[16] = "PsIIsio2man 2200";
struct ModuleInfo Module = {"sio2man", 0x0204};
static int initialized = 0;

static struct {
  int reserved;
  int event;
  int thread;
  int priority;
  struct Sio2Transfer* transfer;
  int (*slot_callback)(int*);
  int (*callback2)(void);
  int (*callback3)(void);
  void (*callback4)(void);
} state;

extern int sio2man_stub[];

void sio2_debug(const char* format, ...) {
}

// Register read/writes.

void sio2_ctrl_set(unsigned int value) {
  REG32(SIO2_CTRL) = value;
}

unsigned int sio2_ctrl_get(void) {
  return REG32(SIO2_CTRL);
}

unsigned int sio2_recv1_get(void) {
  return REG32(SIO2_RECV1);
}

void sio2_send1_set(int port, unsigned int value) {
  ((unsigned int*)SIO2_SEND1)[port * 2] = value;
}

unsigned int sio2_send1_get(int port) {
  return ((unsigned int*)SIO2_SEND1)[port * 2];
}

void sio2_send2_set(int port, unsigned int value) {
  ((unsigned int*)SIO2_SEND2)[port * 2] = value;
}

unsigned int sio2_send2_get(int port) {
  return ((unsigned int*)SIO2_SEND2)[port * 2];
}

unsigned int sio2_recv2_get(void) {
  return REG32(SIO2_RECV2);
}

void sio2_send3_set(int index, unsigned int value) {
  ((unsigned int*)SIO2_SEND3)[index * 1] = value;
}

unsigned int sio2_send3_get(int index) {
  return ((unsigned int*)SIO2_SEND3)[index * 1];
}

unsigned int sio2_recv3_get(void) {
  return REG32(SIO2_RECV3);
}

void sio2_unk78_set(unsigned int value) {
  REG32(SIO2_UNK78) = value;
}

unsigned int sio2_unk78_get(void) {
  return REG32(SIO2_UNK78);
}

void sio2_unk7c_set(unsigned int value) {
  REG32(SIO2_UNK7C) = value;
}

unsigned int sio2_unk7c_get(void) {
  return REG32(SIO2_UNK7C);
}

void sio2_data_out(unsigned char value) {
  REG8(SIO2_DATA_OUT) = value;
}

unsigned char sio2_data_in(void) {
  return REG8(SIO2_DATA_IN);
}

void sio2_stat_set(unsigned int value) {
  REG32(SIO2_STAT) = value;
}

unsigned int sio2_stat_get(void) {
  return REG32(SIO2_STAT);
}

// Control transfers

void sio2_initialize_controller(void) {
  sio2_ctrl_set(0x3bc);
}

void sio2_reset_transfer_state(void) {
  sio2_ctrl_set(sio2_ctrl_get() | 0xc);
}

void sio2_setup_transfer(struct Sio2Transfer* transfer) {
  int i;
  for (i = 0; i < 4; i++) {
    sio2_send1_set(i, transfer->send1[i]);
    sio2_send2_set(i, transfer->send2[i]);
  }
  for (i = 0; i < 16; i++)
    sio2_send3_set(i, transfer->send3[i]);
  for (i = 0; i < transfer->input_size; i++)
    sio2_data_out(transfer->input[i]);
  if (transfer->input_dma.address) {
    dmac_request(DMA_CHANNEL_SIO2_IN, transfer->input_dma.address, transfer->input_dma.size,
                 transfer->input_dma.count, DMA_TO_DEVICE);
    dmac_transfer(DMA_CHANNEL_SIO2_IN);
  }
  if (transfer->output_dma.address) {
    dmac_request(DMA_CHANNEL_SIO2_OUT, transfer->output_dma.address, transfer->output_dma.size,
                 transfer->output_dma.count, DMA_FROM_DEVICE);
    dmac_transfer(DMA_CHANNEL_SIO2_OUT);
  }
}

void sio2_start_transfer(void) {
  sio2_ctrl_set(sio2_ctrl_get() | 1);
}

void sio2_read_transfer_result(struct Sio2Transfer* transfer) {
  int i;
  transfer->recv1 = sio2_recv1_get();
  transfer->recv2 = sio2_recv2_get();
  transfer->recv3 = sio2_recv3_get();
  for (i = 0; i < transfer->output_size; i++)
    transfer->output[i] = sio2_data_in();
}

void sio2_transfer_thread(void) {
  unsigned int bits;
  for (;;) {
    // Wait for a user to initialize and set the ready bit for that user.
    WaitEventFlag(state.event, SIO2_EVENT_INIT_MASK, EW_OR, &bits);
    if (bits & SIO2_EVENT_PAD_INIT) {
      ClearEventFlag(state.event, ~SIO2_EVENT_PAD_INIT);
      SetEventFlag(state.event, SIO2_EVENT_PAD_READY);
    } else if (bits & SIO2_EVENT_INIT_40) {
      ClearEventFlag(state.event, ~SIO2_EVENT_INIT_40);
      SetEventFlag(state.event, SIO2_EVENT_READY_80);
    } else if (bits & SIO2_EVENT_INIT_100) {
      ClearEventFlag(state.event, ~SIO2_EVENT_INIT_100);
      SetEventFlag(state.event, SIO2_EVENT_READY_200);
    } else if (bits & SIO2_EVENT_MC_INIT) {
      ClearEventFlag(state.event, ~SIO2_EVENT_MC_INIT);
      SetEventFlag(state.event, SIO2_EVENT_MC_READY);
    } else if (bits & SIO2_EVENT_MTAP_INIT) {
      ClearEventFlag(state.event, ~SIO2_EVENT_MTAP_INIT);
      SetEventFlag(state.event, SIO2_EVENT_MTAP_READY);
    } else {
      sio2_debug("SIO2_BASIC_THREAD : why I wakeup ? %08lx\n", bits);
      return;
    }

    // Once user is active, process that user's transfers in a loop until a reset.
    for (;;) {
      WaitEventFlag(state.event, SIO2_EVENT_TRANSFER | SIO2_EVENT_RESET, EW_OR, &bits);
      if (bits & SIO2_EVENT_RESET) {
        ClearEventFlag(state.event, ~SIO2_EVENT_RESET);
        break;
      }
      ClearEventFlag(state.event, ~SIO2_EVENT_TRANSFER);
      sio2_reset_transfer_state();
      sio2_setup_transfer(state.transfer);
      sio2_start_transfer();
      WaitEventFlag(state.event, SIO2_EVENT_IRQ, EW_AND, 0);
      ClearEventFlag(state.event, ~SIO2_EVENT_IRQ);
      sio2_read_transfer_result(state.transfer);
      SetEventFlag(state.event, SIO2_EVENT_TRANSFER_DONE);
    }
  }
}

int sio2_handle_interrupt(void* arg) {
  sio2_stat_set(sio2_stat_get());
  iSetEventFlag(((int*)arg)[1], SIO2_EVENT_IRQ);
  return 1;
}

int sio2_create_event_flag(void) {
  struct EventFlagParam param;
  param.attr = EA_MULTI;
  param.initPattern = 0;
  return CreateEventFlag(&param);
}

int sio2_create_transfer_thread(void) {
  struct ThreadParam param;
  param.attr = TH_C;
  param.entry = sio2_transfer_thread;
  param.stackSize = 0x2000;
  param.initPriority = state.priority;
  return CreateThread(&param);
}

int start(int argc, char** argv) {
  int i;
  int priority;
  int intr_state;
  char* value;
  if (RegisterLibraryEntries(sio2man_stub))
    return 1;
  if (initialized)
    return 1;
  initialized = 1;
  state.priority = 16;
  for (i = 1; i < argc; i++) {
    if (!strncmp("thpri=", argv[i], 6)) {
      priority = -1;
      value = argv[i] + 6;
      if (look_ctype_table(*value) & CTYPE_DIGIT)
        priority = strtol(value, 0, 10);
      if (priority < 9 || priority >= 124) {
        printf("SIO2MAN:invalid priority_main %d\n", priority);
        return 1;
      }
      state.priority = priority;
    }
  }
  state.slot_callback = 0;
  state.callback2 = 0;
  state.callback3 = 0;
  state.callback4 = 0;
  sio2_initialize_controller();
  state.event = sio2_create_event_flag();
  state.thread = sio2_create_transfer_thread();
  CpuSuspendIntr(&intr_state);
  RegisterIntrHandler(IOP_IRQ_SIO2, 1, sio2_handle_interrupt, &state);
  EnableIntr(IOP_IRQ_SIO2);
  CpuResumeIntr(intr_state);
  dmac_set_priority(DMA_CHANNEL_SIO2_IN, SIO2_DMA_PRIORITY);
  dmac_set_priority(DMA_CHANNEL_SIO2_OUT, SIO2_DMA_PRIORITY);
  dmac_enable(DMA_CHANNEL_SIO2_IN);
  dmac_enable(DMA_CHANNEL_SIO2_OUT);
  StartThread(state.thread, 0);
  return 0;
}

void sio2_shutdown(void) {
  int intr_state;
  GetThreadId();
  CpuSuspendIntr(&intr_state);
  DisableIntr(IOP_IRQ_SIO2, 0);
  ReleaseIntrHandler(IOP_IRQ_SIO2);
  CpuResumeIntr(intr_state);
  dmac_disable(DMA_CHANNEL_SIO2_IN);
  dmac_disable(DMA_CHANNEL_SIO2_OUT);
}

void sio2_pad_init(void) {
  SetEventFlag(state.event, SIO2_EVENT_PAD_INIT);
  WaitEventFlag(state.event, SIO2_EVENT_PAD_READY, EW_AND, 0);
  ClearEventFlag(state.event, ~SIO2_EVENT_PAD_READY);
}

void sio2_mc_init(void) {
  SetEventFlag(state.event, SIO2_EVENT_MC_INIT);
  WaitEventFlag(state.event, SIO2_EVENT_MC_READY, EW_AND, 0);
  ClearEventFlag(state.event, ~SIO2_EVENT_MC_READY);
}

void sio2_mtap_init(void) {
  SetEventFlag(state.event, SIO2_EVENT_MTAP_INIT);
  WaitEventFlag(state.event, SIO2_EVENT_MTAP_READY, EW_AND, 0);
  ClearEventFlag(state.event, ~SIO2_EVENT_MTAP_READY);
}

void sio2_init_40(void) {
  SetEventFlag(state.event, SIO2_EVENT_INIT_40);
  WaitEventFlag(state.event, SIO2_EVENT_READY_80, EW_AND, 0);
  ClearEventFlag(state.event, ~SIO2_EVENT_READY_80);
}

void sio2_init_100(void) {
  SetEventFlag(state.event, SIO2_EVENT_INIT_100);
  WaitEventFlag(state.event, SIO2_EVENT_READY_200, EW_AND, 0);
  ClearEventFlag(state.event, ~SIO2_EVENT_READY_200);
}

int sio2_transfer(struct Sio2Transfer* transfer) {
  state.transfer = transfer;
  SetEventFlag(state.event, SIO2_EVENT_TRANSFER);
  WaitEventFlag(state.event, SIO2_EVENT_TRANSFER_DONE, EW_AND, 0);
  ClearEventFlag(state.event, ~SIO2_EVENT_TRANSFER_DONE);
  return 1;
}

void sio2_transfer_reset(void) {
  SetEventFlag(state.event, SIO2_EVENT_RESET);
}

void sio2_set_slot_callback(int (*callback)(int*)) {
  state.slot_callback = callback;
}

void sio2_set_callback2(int (*callback)(void)) {
  state.callback2 = callback;
}

void sio2_set_callback3(int (*callback)(void)) {
  state.callback3 = callback;
}

void sio2_set_callback4(void (*callback)(void)) {
  state.callback4 = callback;
}

int sio2_change_slot(int* slots) {
  int result;
  int i;
  if (state.slot_callback)
    result = state.slot_callback(slots);
  else {
    result = 1;
    for (i = 0; i < 4; i++) {
      if (slots[i] == -1 || slots[i] == 0)
        slots[i + 4] = 1;
      else {
        slots[i + 4] = 0;
        result = 0;
      }
    }
  }
  return result;
}

int sio2_call_callback2(void) {
  int result;
  if (!state.callback2)
    result = 1;
  else
    result = state.callback2();
  return result;
}

int sio2_call_callback3(void) {
  int result;
  if (!state.callback3)
    result = 1;
  else
    result = state.callback3();
  return result;
}

void sio2_call_callback4(void) {
  if (state.callback4)
    state.callback4();
}
