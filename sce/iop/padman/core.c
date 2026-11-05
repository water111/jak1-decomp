#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/padman/commands.h"
#include "sce/iop/padman/exchange.h"
#include "sce/iop/padman/padman.h"
#include "sce/iop/padman/rpc.h"
#include "sce/iop/padman/runtime.h"
#include "sce/iop/padman/state.h"
#include "sce/iop/padman/threads.h"
#include "sce/iop/padman/transfer.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/ctype.h"
#include "sce/iop/sysclib/stdlib.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/sysmem/sysmem.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thevent/thevent.h"
#include "sce/iop/types.h"

char pad_build_id[16] = "PsIIpadman  2200";
struct ModuleInfo Module = {"padman", 0x0402};
int pad_debug = 1;
volatile int pad_vblank_flag = 0;
int pad_frame = 0;
u_int pad_dma_id = 0;
u_int pad_vblank_timeout = 0;
int pad_poll_frame = 0;

extern volatile int pad_vblank_flag;

int pad_vblank_end(void) {
  pad_vblank_flag = 1;
  return 1;
}

extern int iReferThreadStatus(int id, struct ThreadInfo* info);
extern void ExitThread(void);

int pad_update_actuators(PadState* pad) {
  int current = pad->ee_commands[0].frame < pad->ee_commands[1].frame;
  PadEECommand* command = &pad->ee_commands[current];
  if (command->command) {
    padSetActDirect(pad->port, pad->slot, command->data);
    command->command = 0;
  }
  return current;
}

void pad_wait_clear(int event, u_int bits, int mode, u_int* result) {
  struct ThreadInfo status;
  u_int events;
  WaitEventFlag(event, bits | PAD_EVENT_EXIT, EW_OR, &events);
  if (events & PAD_EVENT_EXIT) {
    ReferThreadStatus(0, &status);
    SetEventFlag(event, PAD_EVENT_EXIT);
    ExitThread();
  }
  ClearEventFlag(event, ~bits);
}

int pad_vblank_threads_running(PadVblankState* vblank) {
  struct ThreadInfo status;
  int running = 0;
  if (iReferThreadStatus(vblank->scheduler_thread, &status) == 0 &&
      !(status.status & PAD_THREAD_WAITING))
    ++running;
  if (iReferThreadStatus(vblank->transfer_thread, &status) == 0 &&
      !(status.status & PAD_THREAD_WAITING))
    ++running;
  return running;
}

void pad_delete_vblank_threads(PadVblankState* vblank) {
  DeleteThread(vblank->scheduler_thread);
  DeleteThread(vblank->transfer_thread);
  DeleteEventFlag(vblank->event);
}

int pad_threads_idle(PadState* pad) {
  struct ThreadInfo status;
  int running = 0;
  if (ReferThreadStatus(pad->update_thread, &status) == 0 && !(status.status & PAD_THREAD_WAITING))
    ++running;
  if (ReferThreadStatus(pad->query_thread, &status) == 0 && !(status.status & PAD_THREAD_WAITING))
    ++running;
  if (ReferThreadStatus(pad->main_mode_thread, &status) == 0 &&
      !(status.status & PAD_THREAD_WAITING))
    ++running;
  if (ReferThreadStatus(pad->act_align_thread, &status) == 0 &&
      !(status.status & PAD_THREAD_WAITING))
    ++running;
  if (ReferThreadStatus(pad->button_info_thread, &status) == 0 &&
      !(status.status & PAD_THREAD_WAITING))
    ++running;
  if (ReferThreadStatus(pad->vref_thread, &status) == 0 && !(status.status & PAD_THREAD_WAITING))
    ++running;
  return running <= 0;
}

void pad_delete_threads(PadState* pad) {
  DeleteThread(pad->update_thread);
  DeleteThread(pad->query_thread);
  DeleteThread(pad->main_mode_thread);
  DeleteThread(pad->act_align_thread);
  DeleteThread(pad->button_info_thread);
  DeleteThread(pad->vref_thread);
}

extern int pad_open_slots[2];
extern u_int pad_dma_id;
extern int pad_frame;
extern sceSifDmaData pad_dma_transfers[16];
#define SIF_SB_STAT_ADDRESS 0xbd000040
static volatile u_int* const sif_status = (volatile u_int*)SIF_SB_STAT_ADDRESS;
#define SIF_SB_STAT (*sif_status)

void pad_dma_report(void) {
  int port, slot, count = 0;
  int buffer;
  PadState* state;
  int intr = 0;
  int result = sceSifDmaStat(pad_dma_id);
  if (result >= 0) {
    if (pad_frame % 240 == 0 && pad_debug) {
      Kprintf("padman: DMA Busy ID = %08x ret = %d\n", pad_dma_id, result);
      Kprintf("        SB_STAT = %08x\n", SIF_SB_STAT);
    }
    return;
  }
  for (port = 0; port < 2; port++) {
    slot = 0;
    for (; slot < 4; slot++) {
      int* open = &pad_open_slots[port];
      if ((*open >> slot) & 1) {
        PadState* pad = &pad_states[port][slot];
        PadEEReport* report = (PadEEReport*)pad;
        buffer = pad->frame & 1;
        report->frame = pad->frame++;
        report->find_retries = pad->find_retries;
        report->mode_config = pad->mode_config;
        report->current_id = pad->current_id;
        report->model = pad->model;
        report->button_ready = pad->button_ready;
        report->mode_count = pad->mode_count;
        report->current_mode = pad->current_mode;
        report->actuator_count = pad->actuator_count;
        report->combination_count = pad->combination_count;
        report->button_info_ready = pad->button_info_ready;
        report->mode = pad->mode;
        report->lock = pad->lock;
        report->actuator_direct_size = pad->actuator_direct_size;
        report->state = pad->state;
        report->request_state = pad->request_state;
        report->current_task = pad->current_task;
        report->run_task = pad->run_task;
        memcpy(report->actuator_direct, pad->actuator_direct, 8);
        // The original report copies six alignment bytes and the following state word.
        memcpy(report->actuator_align, pad->actuator_align, 8);
        memcpy(report->actuator_info, pad->actuator_info, 16);
        memcpy(report->combination_info, pad->combination_info, 16);
        memcpy(report->modes, pad->modes, 8);
        memcpy(report->button_mask, pad->button_mask, 4);
        memcpy(report->button_info, pad->button_info, 4);
        report->status_bit = pad->status_bit;
        state = pad;
        if (pad->button_ready == 1)
          report->length = padGetInBuffer(port, slot, report->data);
        else
          report->length = 0;
        pad_dma_transfers[count].addr = (u_int)state->ee_buffer;
        if (buffer)
          pad_dma_transfers[count].addr += 128;
        pad_dma_transfers[count].data = (u_int)pad;
        pad_dma_transfers[count].size = 128;
        pad_dma_transfers[count].mode = 0;
        count++;
      }
    }
  }
  if (count) {
    int dma;
    CpuSuspendIntr(&intr);
    dma = sceSifSetDma(pad_dma_transfers, count);
    CpuResumeIntr(intr);
    pad_dma_id = dma;
    if (dma == 0 && pad_debug)
      Kprintf("padman: sceSifSetDma faild\n");
  }
}

void pad_shift_response(u_char* buffer) {
  u_char original[PAD_PACKET_SIZE];
  int i;
  for (i = 0; i < PAD_PACKET_SIZE; ++i)
    original[i] = buffer[i];
  buffer[0] = 0xff;
  for (i = 1; i < PAD_PACKET_SIZE; ++i)
    buffer[i] = original[i - 1];
}

extern int pad_frame;
extern u_int pad_vblank_timeout;
extern volatile int pad_vblank_flag;
extern volatile PadVblankState pad_vblank;

int pad_vblank_start(volatile PadVblankState* vblank) {
  pad_vblank_flag = 0;
  pad_frame++;
  pad_vblank_timeout++;
  if (vblank->initialized == 1) {
    if (vblank->stop_transfer == 0) {
      vblank->stop_transfer = 1;
      iSetEventFlag(vblank->event, PAD_VBLANK_EVENT_TRANSFER_START);
    } else if (pad_vblank_timeout >= 60) {
      if (pad_debug)
        Kprintf("padman: *** VBLANK OVERLAP ***\n");
      pad_vblank_timeout = 0;
    }
  }
  if (vblank->exiting == 1 && pad_vblank_threads_running((PadVblankState*)vblank) == 0)
    iSetEventFlag(vblank->event, PAD_VBLANK_EVENT_EXIT_DONE);
  return 1;
}

void pad_transfer_thread(void) {
  while (1) {
    pad_wait_clear(pad_vblank.event, PAD_VBLANK_EVENT_TRANSFER_START, EW_CLEAR, 0);
    pad_transfer_all();
    SetEventFlag(pad_vblank.event, PAD_VBLANK_EVENT_TRANSFER_DONE);
  }
}

extern int pad_poll_frame;
extern int pad_open_slots[2];
extern volatile PadVblankState pad_vblank;
extern void pad_dma_report(void);

void pad_scheduler_thread(void) {
  int port, slot;
  PadState* pad;
  while (1) {
    pad_poll_frame++;
    if (pad_poll_frame % 30 == 0)
      sio2_call_callback4();
    for (port = 0; port < 2; port++) {
      int* open = &pad_open_slots[port];
      for (slot = 0; slot < 4; slot++) {
        pad_transfer_set_active(port, slot, 0);
        if ((*open >> slot) & 1) {
          pad = &pad_states[port][slot];
          pad->status_bit = pad_transfer_get_connected(port, slot);
          if (pad->run_task != 0) {
            if (pad->run_task == 3) {
              pad->current_task = 3;
              pad->run_task = 0;
              pad->request_state = 2;
              SetEventFlag(pad->event, PAD_EVENT_EXIT);
            } else if (pad->current_task == 1) {
              StartThread(pad->task_thread, 0);
              pad->current_task = pad->run_task;
              pad->run_task = 0;
              pad->request_state = 2;
            } else {
              pad->run_task = 0;
              pad->request_state = 1;
            }
          }
          switch (pad->current_task) {
            case 1:
              SetEventFlag(pad->event, PAD_EVENT_UPDATE);
              WaitEventFlag(pad->event, PAD_EVENT_TRANSFER_START, EW_CLEAR, 0);
              pad_transfer_set_active(port, slot, 1);
              break;
            case 2:
              pad->button_ready = 0;
              SetEventFlag(pad->event, PAD_EVENT_QUERY);
              WaitEventFlag(pad->event, PAD_EVENT_TRANSFER_START, EW_CLEAR, 0);
              pad_transfer_set_active(port, slot, 1);
              break;
            case 3:
              pad->button_ready = 0;
              if (pad_threads_idle(pad) == 1) {
                pad->current_task = 0;
                pad->request_state = 0;
                *open ^= 1 << slot;
                pad_delete_threads(pad);
                if (pad->wait_for_close)
                  SetEventFlag(pad->event, PAD_EVENT_CLOSE_DONE);
                else {
                  DeleteEventFlag(pad->event);
                  pad->event = 0;
                }
              }
              continue;
            case 4:
              pad->button_ready = 0;
              SetEventFlag(pad->event, PAD_EVENT_SET_MAIN_MODE);
              WaitEventFlag(pad->event, PAD_EVENT_TRANSFER_START, EW_CLEAR, 0);
              pad_transfer_set_active(port, slot, 1);
              break;
            case 5:
              pad->button_ready = 0;
              SetEventFlag(pad->event, PAD_EVENT_SET_ACT_ALIGN);
              WaitEventFlag(pad->event, PAD_EVENT_TRANSFER_START, EW_CLEAR, 0);
              pad_transfer_set_active(port, slot, 1);
              break;
            case 6:
              pad->button_ready = 0;
              SetEventFlag(pad->event, PAD_EVENT_SET_BUTTON_INFO);
              WaitEventFlag(pad->event, PAD_EVENT_TRANSFER_START, EW_CLEAR, 0);
              pad_transfer_set_active(port, slot, 1);
              break;
            case 7:
              pad->button_ready = 0;
              SetEventFlag(pad->event, PAD_EVENT_SET_VREF_PARAM);
              WaitEventFlag(pad->event, PAD_EVENT_TRANSFER_START, EW_CLEAR, 0);
              pad_transfer_set_active(port, slot, 1);
              break;
            default:
              continue;
          }
        }
      }
    }
    pad_vblank.stop_transfer = 0;
    pad_wait_clear(pad_vblank.event, PAD_VBLANK_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
    if (pad_open_slots[0] || pad_open_slots[1]) {
      for (port = 0; port < 2; port++) {
        for (slot = 0; slot < 4; slot++) {
          pad = &pad_states[port][slot];
          if (pad_transfer_get_active(port, slot) == 1) {
            SetEventFlag(pad->event, PAD_EVENT_TRANSFER_DONE);
            WaitEventFlag(pad->event, PAD_EVENT_TASK_DONE, EW_CLEAR, 0);
          }
        }
      }
    }
    pad_dma_report();
  }
}

#define PAD_ID_CONFIG 0xf3
#define PAD_RESPONSE_ACK 0x5a

int pad_exchange_read(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  int i;
  pad_transfer_set_control1(
      port, slot, pad_command_port_control1(pad->current_id, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot,
                            pad_command_port_control2(pad->current_id, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(pad->current_id));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(pad->current_id));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(pad->current_id));
  for (i = 0; i < PAD_PACKET_SIZE; ++i) {
    pad->input[i] = 0;
    pad->output[i] = 0;
  }
  pad_command_read(pad->current_id, pad->input);
  for (i = 0; i < pad->actuator_direct_size; ++i) {
    if (pad->actuator_align[i] != 0xff)
      pad->input[i + 3] = pad->actuator_direct[i];
    else
      pad->input[i + 3] = 0;
  }
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    success = 1;
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_find(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot, pad_command_port_control1(0, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(0, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(0));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(0));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(0));
  pad_command_read(0, pad->input);
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] && pad_command_supported(pad->output[1]) == 1) {
      int id = pad->output[1];
      if (id && pad_command_supported(id) == 1) {
        pad->current_id = id;
        success = 1;
      }
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_enter_config(int command_id, PadState* pad) {
  u_char id = command_id;
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot, pad_command_port_control1(id, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(id, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(id));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(id));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(id));
  pad_command_enter_config(id, pad->input);
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    success = 1;
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_exit_config(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_exit_config(PAD_ID_CONFIG, pad->input);
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    success = 1;
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_main_mode(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_set_main_mode(PAD_ID_CONFIG, pad->input);
  pad->input[3] = pad->mode;
  pad->input[4] = pad->lock;
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_act_align(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  int i;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_set_actuator_align(PAD_ID_CONFIG, pad->input);
  for (i = 0; i < 6; ++i)
    pad->input[i + 3] = pad->actuator_align[i];
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_model(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_query_model(PAD_ID_CONFIG, pad->input);
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      pad->model = pad->output[3];
      pad->mode_count = pad->output[4];
      pad->current_mode = pad->output[5];
      pad->actuator_count = pad->output[6];
      pad->combination_count = pad->output[7];
      if (pad->actuator_count > 3)
        pad->actuator_count = 4;
      if (pad->combination_count > 3)
        pad->combination_count = 4;
      if (pad->mode_count > 3)
        pad->mode_count = 4;
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_actuator(int index, PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_query_actuator(PAD_ID_CONFIG, pad->input);
  pad->input[3] = index;
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      pad->actuator_info[index][0] = pad->output[5];
      pad->actuator_info[index][1] = pad->output[6];
      pad->actuator_info[index][2] = pad->output[7];
      pad->actuator_info[index][3] = pad->output[8];
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_combination(int index, PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_query_combination(PAD_ID_CONFIG, pad->input);
  pad->input[3] = index;
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      pad->combination_info[index][0] = pad->output[5];
      pad->combination_info[index][1] = pad->output[6];
      pad->combination_info[index][2] = pad->output[7];
      pad->combination_info[index][3] = pad->output[8];
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_mode(int index, PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_query_mode(PAD_ID_CONFIG, pad->input);
  pad->input[3] = index;
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      pad->modes[index] = (pad->output[5] << 8) + pad->output[6];
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_vref(int index, PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, 9);
  pad_transfer_set_output_size(port, slot, 9);
  pad_command_set_vref(PAD_ID_CONFIG, pad->input);
  pad->input[3] = index;
  pad->input[4] = pad->vref[index];
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_button_mask(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_query_button_mask(PAD_ID_CONFIG, pad->input);
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[8] == PAD_RESPONSE_ACK) {
      pad->button_mask[0] = pad->output[3];
      pad->button_mask[1] = pad->output[4];
      pad->button_mask[2] = pad->output[5];
      pad->button_mask[3] = pad->output[6];
    } else {
      pad->button_mask[0] = 0;
      pad->button_mask[1] = 0;
      pad->button_mask[2] = 0;
      pad->button_mask[3] = 0;
    }
    success = 1;
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

int pad_exchange_button_info(PadState* pad) {
  int port = pad->port;
  int slot = pad->slot;
  int success = 0;
  int i;
  pad_transfer_set_control1(port, slot,
                            pad_command_port_control1(PAD_ID_CONFIG, pad->status_bit, pad->model));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(PAD_ID_CONFIG, pad->status_bit));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(PAD_ID_CONFIG));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(PAD_ID_CONFIG));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(PAD_ID_CONFIG));
  pad_command_set_button_info(PAD_ID_CONFIG, pad->input);
  for (i = 0; i < 4; ++i)
    pad->input[i + 3] = pad->button_info[i];
  pad_transfer_set_input(port, slot, 0, pad->input);
  SetEventFlag(pad->event, PAD_EVENT_TRANSFER_START);
  pad_wait_clear(pad->event, PAD_EVENT_TRANSFER_DONE, EW_CLEAR, 0);
  if (pad_transfer_get_error(port, slot) == 0) {
    pad_transfer_get_output(port, slot, 0, pad->output);
    if (pad->status_bit == 1)
      pad_shift_response(pad->output);
    if (pad->output[1] == PAD_ID_CONFIG) {
      success = 1;
    }
  }
  SetEventFlag(pad->event, PAD_EVENT_TASK_DONE);
  return success;
}

#define PAD_MAX_CURRENT_UNITS 60
extern int pad_open_slots[PAD_PORT_COUNT];

int pad_other_actuator_current(int port, int slot) {
  int total = 0;
  int p, s, i;
  int* open;
  PadState* pad;
  u_int actuator;
  for (p = 0; p < padGetPortMax(); ++p) {
    open = &pad_open_slots[p];
    for (s = 0; s < padGetSlotMax(p); ++s) {
      if ((port != p || slot != s) && ((*open >> s) & 1)) {
        pad = &pad_states[p][s];
        if (pad->current_id) {
          for (i = 0; i < pad->actuator_direct_size; ++i) {
            actuator = pad->actuator_align[i];
            if (actuator != 0xff && pad->actuator_direct[actuator])
              total += pad->actuator_info[actuator][3];
          }
        }
      }
    }
  }
  return total;
}

int pad_check_actuator_power(int port, int slot, u_char* values) {
  int total = pad_other_actuator_current(port, slot);
  PadState* pad = &pad_states[port][slot];
  int i;
  u_int actuator;
  for (i = 0; i < 6; ++i) {
    actuator = pad->actuator_align[i];
    if (actuator != 0xff && values[actuator]) {
      int next_total = total + pad->actuator_info[actuator][3];
      if (next_total > PAD_MAX_CURRENT_UNITS) {
        if (pad_debug)
          Kprintf("padman: Over Consumpt Max [%d][%d]\n", port, slot);
        values[actuator] = 0;
      } else {
        total = next_total;
      }
    }
  }
  return 1;
}

void pad_update_thread(void) {
  struct ThreadInfo info;
  PadState* pad;
  int port, slot, i;
  int errors = 0;
  ReferThreadStatus(0, &info);
  pad = (PadState*)info.option;
  port = pad->port;
  slot = pad->slot;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_UPDATE, EW_CLEAR, 0);
    pad_update_actuators(pad);
    if (pad_exchange_read(pad) == 1) {
      errors = 0;
      if (pad->output[2] != 0x5a) {
        pad->button_ready = 0;
        pad->state = 5;
        pad->current_task = 2;
        StartThread(pad->query_thread, 0);
        continue;
      }
      if (pad->output[1] != pad->current_id) {
        pad->button_ready = 0;
        pad->state = 5;
        pad->current_task = 2;
        StartThread(pad->query_thread, 0);
        continue;
      }
      if (pad->output[1] == 0xf3) {
        pad->button_ready = 0;
        pad->state = 5;
        pad->current_task = 2;
        StartThread(pad->query_thread, 0);
        continue;
      }
      for (i = 0; i < 32; ++i)
        pad->button_status[i] = pad->output[i];
      pad->button_ready = 1;
      if (pad->mode_config == 1)
        pad->state = 2;
      else
        pad->state = 6;
      if (pad->request_state == PAD_REQUEST_BUSY && pad->run_task == 0)
        pad->request_state = 0;
      continue;
    } else {
      pad->button_ready = 0;
      pad->state = 7;
      pad->find_retries++;
      errors += pad_transfer_get_error(port, slot);
      if (errors < 10)
        continue;
    }
    errors = 0;
    pad->current_task = 2;
    StartThread(pad->query_thread, 0);
  }
}

extern void ExitThread(void);

void pad_query_thread(void) {
  struct ThreadInfo info;
  PadState* pad;
  int retries, i;
  u_char id;
  u_char index;
  ReferThreadStatus(0, &info);
  pad = (PadState*)info.option;
  pad->current_id = 0;
  pad->mode_config = 0;
  pad->model = 0;
  pad->mode_count = 0;
  pad->actuator_count = 0;
  pad->combination_count = 0;
  pad->button_ready = 0;
  pad->actuator_direct_size = 0;
  pad->find_retries = 0;
  for (i = 0; i < 4; i++) {
    pad->button_info[i] = 0;
    pad->button_mask[i] = 0;
  }
  pad->actuator_align[0] = 0;
  pad->actuator_align[1] = 1;
  i = 2;
  do {
    pad->actuator_align[i] = 0xff;
    i++;
  } while (i < 6);
  for (i = 0; i < 6; i++)
    pad->actuator_direct[i] = 0;
  pad->current_id = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
    if (pad_exchange_find(pad) == 1 && pad->current_id != 0xff) {
      id = pad->current_id;
      pad->current_id = 0;
      pad->state = 5;
      break;
    }
    pad->state = 0;
    pad->disconnected = 1;
  }

  if (pad->disconnected) {
    for (i = 0; i < 6; i++)
      pad->actuator_align[i] = 0xff;
  }
  do {
    retries = 0;
  retry_config:
    pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
    if (pad_exchange_enter_config(id, pad) == 1) {
      pad->mode_config = 2;
      break;
    }
    if (++retries < 10)
      goto retry_config;
    pad->mode_config = 1;
    pad->current_id = id;
    pad->current_task = 1;
    ExitThread();
  } while (0);
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
    if (pad_exchange_model(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->current_id = 0;
      pad->current_task = 1;
      ExitThread();
    }
  }
  if (pad->disconnected == 1 && pad->current_mode != 0) {
    pad->mode = 0;
    pad->lock = 0;
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
      if (pad_exchange_main_mode(pad) == 1)
        break;
      if (++retries >= 10) {
        pad->request_state = 1;
        pad->current_task = 1;
        ExitThread();
      }
    }
    pad->current_id = 0;
    pad->current_task = 1;
    pad->disconnected = 0;
    ExitThread();
  }
  for (index = 0; index < pad->actuator_count; index++) {
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
      if (pad_exchange_actuator(index, pad) == 1)
        break;
      if (++retries >= 10) {
        pad->current_id = 0;
        pad->current_task = 1;
        ExitThread();
      }
    }
  }
  for (index = 0; index < pad->combination_count; index++) {
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
      if (pad_exchange_combination(index, pad) == 1)
        break;
      if (++retries >= 10) {
        pad->current_id = 0;
        pad->current_task = 1;
        ExitThread();
      }
    }
  }
  for (index = 0; index < pad->mode_count; index++) {
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
      if (pad_exchange_mode(index, pad) == 1)
        break;
      if (++retries >= 10) {
        pad->current_id = 0;
        pad->current_task = 1;
        ExitThread();
      }
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
    if (pad_exchange_act_align(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = 1;
      ExitThread();
    }
  }
  if (pad->mode_config == 2 && (pad->model & 2)) {
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
      if (pad_exchange_button_mask(pad) == 1)
        break;
      if (++retries >= 10) {
        pad->current_id = 0;
        pad->current_task = 1;
        ExitThread();
      }
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
    if (pad_exchange_exit_config(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->current_id = 0;
      pad->current_task = 1;
      ExitThread();
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_QUERY, EW_CLEAR, 0);
    if (pad_exchange_find(pad) == 1) {
      id = pad->current_id;
      break;
    }
    if (++retries >= 10) {
      pad->current_id = 0;
      pad->current_task = 1;
      ExitThread();
    }
  }
  pad->state = 5;
  pad->current_id = 0;
  pad->current_task = 1;
  pad->disconnected = 0;
  pad->current_id = id;
  ExitThread();
}

extern void ExitThread(void);

void pad_main_mode_thread(void) {
  struct ThreadInfo info;
  PadState* pad;
  int retries = 0;
  ReferThreadStatus(0, &info);
  pad = (PadState*)info.option;
  pad->button_ready = 0;
  pad->state = 5;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_MAIN_MODE, EW_CLEAR, 0);
    if (pad_exchange_enter_config(pad->current_id, pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_MAIN_MODE, EW_CLEAR, 0);
    if (pad_exchange_main_mode(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_MAIN_MODE, EW_CLEAR, 0);
    if (pad_exchange_exit_config(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  pad->current_task = PAD_TASK_UPDATE;
  ExitThread();
}

void pad_act_align_thread(void) {
  struct ThreadInfo info;
  PadState* pad;
  int retries = 0;
  ReferThreadStatus(0, &info);
  pad = (PadState*)info.option;
  pad->button_ready = 0;
  pad->state = 5;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_ACT_ALIGN, EW_CLEAR, 0);
    if (pad_exchange_enter_config(pad->current_id, pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_ACT_ALIGN, EW_CLEAR, 0);
    if (pad_exchange_act_align(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_ACT_ALIGN, EW_CLEAR, 0);
    if (pad_exchange_exit_config(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  pad->current_task = PAD_TASK_UPDATE;
  ExitThread();
}

void pad_button_info_thread(void) {
  struct ThreadInfo info;
  PadState* pad;
  int retries = 0;
  int index;
  ReferThreadStatus(0, &info);
  pad = (PadState*)info.option;
  pad->button_ready = 0;
  pad->state = 5;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_BUTTON_INFO, EW_CLEAR, 0);
    if (pad_exchange_enter_config(pad->current_id, pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_BUTTON_INFO, EW_CLEAR, 0);
    if (pad_exchange_button_info(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  for (index = 0; index < 12; index++) {
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_SET_BUTTON_INFO, EW_CLEAR, 0);
      if (pad_exchange_vref(index, pad) == 1)
        break;
      if (++retries >= 10) {
        pad->request_state = 1;
        pad->current_task = PAD_TASK_UPDATE;
        ExitThread();
      }
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_BUTTON_INFO, EW_CLEAR, 0);
    if (pad_exchange_exit_config(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  pad->button_info_ready = 1;
  pad->current_task = PAD_TASK_UPDATE;
  ExitThread();
}

void pad_vref_thread(void) {
  struct ThreadInfo info;
  PadState* pad;
  int retries = 0;
  int index;
  ReferThreadStatus(0, &info);
  pad = (PadState*)info.option;
  pad->button_ready = 0;
  pad->state = 5;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_VREF_PARAM, EW_CLEAR, 0);
    if (pad_exchange_enter_config(pad->current_id, pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  for (index = 0; index < 12; index++) {
    retries = 0;
    while (1) {
      pad_wait_clear(pad->event, PAD_EVENT_SET_VREF_PARAM, EW_CLEAR, 0);
      if (pad_exchange_vref(index, pad) == 1)
        break;
      if (++retries >= 10) {
        pad->request_state = 1;
        pad->current_task = PAD_TASK_UPDATE;
        ExitThread();
      }
    }
  }
  retries = 0;
  while (1) {
    pad_wait_clear(pad->event, PAD_EVENT_SET_VREF_PARAM, EW_CLEAR, 0);
    if (pad_exchange_exit_config(pad) == 1)
      break;
    if (++retries >= 10) {
      pad->request_state = 1;
      pad->current_task = PAD_TASK_UPDATE;
      ExitThread();
    }
  }
  pad->current_task = PAD_TASK_UPDATE;
  ExitThread();
}

extern volatile int pad_vblank_flag;
extern int padman_stub[];

int pad_parse_options(int argc, char** argv) {
  int high = 20, low = 46, i, priority;
  char* p;
  for (i = 1; i < argc; i++) {
    if (strncmp("thpri=", argv[i], 6) == 0) {
      p = argv[i] + 6;
      if (look_ctype_table(*p) & 4) {
        priority = strtol(p, 0, 10);

        if ((u_int)(priority - 9) > 114) {
          printf("PADMAN : invalid priority %d\n", priority);
          return 0;
        }
        high = priority;
      }
      while (look_ctype_table(*p) & 4)
        ++p;
      if (*p == ',') {
        if (look_ctype_table(p[1]) & 4) {
          priority = strtol(p + 1, 0, 10);

          if ((u_int)(priority - 9) > 114) {
            printf(" SDR driver error: invalid priority %d\n", priority);
            return 0;
          }
          low = priority;
        }
      }
      if (low < high) {
        printf("PADMAN : high prio thread must be higher than low prio thread\n");
        low = high;
      }
    }
  }
  pad_runtime.high_priority = high;
  pad_runtime.low_priority = low;
  return 1;
}

int start(int argc, char** argv) {
  pad_runtime.initialized = 0;
  pad_vblank_flag = 0;
  pad_runtime.high_priority = 20;
  pad_runtime.low_priority = 46;
  if (argc > 1)
    pad_parse_options(argc, argv);
  if ((pad_runtime.high_priority != 20 || pad_runtime.low_priority != 46) && pad_debug)
    Kprintf("padman: thread priority: high=%d, low=%d\n", pad_runtime.high_priority,
            pad_runtime.low_priority);
  if (RegisterLibraryEntries(padman_stub) != 0) {
    if (pad_debug)
      Kprintf("padman: RegisterLibraryEntries Faild\n");
    return 1;
  }
  if (!pad_rpc_init(pad_runtime.low_priority)) {
    if (pad_debug)
      Kprintf("padman: sif_init Faild\n");
    return 1;
  }
  return 0;
}

#define pad_initialized pad_runtime.initialized
#define pad_high_priority pad_runtime.high_priority
#define pad_low_priority pad_runtime.low_priority
extern int pad_open_slots[2];
#define pad_ee_buffer pad_runtime.ee_buffer
#define pad_vblank pad_runtime.vblank
extern int RegisterVblankHandler(int edge, int priority, void* callback, void* arg);
extern int ReleaseVblankHandler(int edge, void* callback);
extern void pad_transfer_thread(void), pad_scheduler_thread(void);
extern int pad_vblank_start(volatile PadVblankState*), pad_vblank_end(void);
extern void pad_command_init_find(void);
extern void pad_command_init_mouse(void);
extern void pad_command_init_negicon(void);
extern void pad_command_init_konami_gun(void);
extern void pad_command_init_digital(void);
extern void pad_command_init_joystick(void);
extern void pad_command_init_namco_gun(void);
extern void pad_command_init_analog(void);
extern void pad_command_init_jogcon(void);
extern void pad_command_init_config(void);

int padInit(void* ee_buffer) {
  struct EventFlagParam event;
  struct ThreadParam thread;
  int intr;
  if (pad_initialized == 1)
    padEnd();
  pad_debug = 1;
  pad_vblank.initialized = 0;
  pad_vblank.stop_transfer = 1;
  pad_vblank.exiting = 0;
  pad_ee_buffer = ee_buffer;
  pad_command_reset();
  pad_command_init_find();
  pad_command_init_mouse();
  pad_command_init_negicon();
  pad_command_init_konami_gun();
  pad_command_init_digital();
  pad_command_init_joystick();
  pad_command_init_namco_gun();
  pad_command_init_analog();
  pad_command_init_jogcon();
  pad_command_init_config();
  pad_transfer_init();
  pad_open_slots[0] = 0;
  pad_open_slots[1] = 0;
  event.attr = EA_MULTI;
  event.initPattern = 0;
  *(volatile int*)&pad_vblank.event = CreateEventFlag(&event);
  if (!pad_vblank.event)
    goto fail;
  {
    thread.attr = TH_C;
    thread.entry = pad_transfer_thread;
    thread.stackSize = 2048;
    thread.initPriority = pad_high_priority;
    pad_vblank.transfer_thread = CreateThread(&thread);
    if (!pad_vblank.transfer_thread)
      goto fail;
    {
      StartThread(pad_vblank.transfer_thread, 0);
      thread.attr = TH_C;
      thread.entry = pad_scheduler_thread;
      thread.stackSize = 4096;
      thread.initPriority = pad_low_priority;
      pad_vblank.scheduler_thread = CreateThread(&thread);
      if (!pad_vblank.scheduler_thread) {
      fail:
        return 0;
      }
      {
        StartThread(pad_vblank.scheduler_thread, 0);
        CpuSuspendIntr(&intr);
        RegisterVblankHandler(0, 0x10, pad_vblank_start, (void*)&pad_vblank);
        RegisterVblankHandler(1, 0x10, pad_vblank_end, (void*)&pad_vblank);
        CpuEnableIntr(intr);
        pad_vblank.initialized = 1;
        pad_initialized = 1;
        return 1;
      }
    }
  }
}

int padEnd(void) {
  int port, slot;
  if (!pad_initialized)
    return 1;
  {
    for (port = 0; port < 2; port++) {
      int* open = &pad_open_slots[port];
      for (slot = 0; slot < 4; slot++) {
        if ((*open >> slot) & 1)
          padPortClose(port, slot, 1);
      }
    }
    SetEventFlag(pad_vblank.event, PAD_EVENT_EXIT);
    *(volatile int*)&pad_vblank.exiting = 1;
    WaitEventFlag(*(volatile int*)&pad_vblank.event, PAD_VBLANK_EVENT_EXIT_DONE, (EW_OR | EW_CLEAR),
                  0);
    pad_delete_vblank_threads((PadVblankState*)&pad_vblank);
    while (ReleaseVblankHandler(0, pad_vblank_start) != 0) {
    }
    while (ReleaseVblankHandler(1, pad_vblank_end) != 0) {
    }
    pad_initialized = 0;
  }
  return 1;
}

#undef pad_initialized
#undef pad_high_priority
#undef pad_low_priority
#undef pad_ee_buffer
#undef pad_vblank

extern int pad_open_slots[2], pad_low_priority;
extern void pad_update_thread(void);
extern void pad_query_thread(void);
extern void pad_main_mode_thread(void);
extern void pad_act_align_thread(void);
extern void pad_button_info_thread(void);
extern void pad_vref_thread(void);

int padPortOpen(int port, int slot, void* ee_buffer, void** iop_buffer) {
  struct EventFlagParam event;
  struct ThreadParam thread;
  PadState* pad;
  int *open, *priority;
  if ((u_int)port >= 2 || (u_int)slot >= 4)
    return 0;
  pad = &pad_states[port][slot];
  *iop_buffer = pad->ee_commands;
  open = &pad_open_slots[port];
  pad->ee_commands[0].frame = 0;
  pad->ee_commands[1].frame = 0;
  pad->ee_commands[0].command = 0;
  pad->ee_commands[1].command = 0;
  pad->ee_commands[0].reserved = 0;
  pad->ee_commands[1].reserved = 0;
  if ((*open >> slot) & 1)
    return 0;
  pad->port = port;
  pad->slot = slot;
  *open |= 1 << slot;
  pad->state = 5;
  pad->current_id = 0;
  pad->request_state = 0;
  pad->frame = 0;
  pad->ee_buffer = ee_buffer;
  pad->button_ready = 0;
  pad->actuator_direct_size = 0;
  pad->button_info_ready = 0;
  pad->current_task = 0;
  pad->run_task = 0;
  pad->reserved1c0 = 0;
  pad->disconnected = 1;
  pad->status_bit = 0;
  event.attr = EA_MULTI;
  event.initPattern = 0;
  pad->event = CreateEventFlag(&event);
  if (pad->event) {
    priority = &pad_low_priority;
    thread.option = (u_int)pad;
    thread.attr = TH_C;
    thread.entry = pad_update_thread;
    thread.stackSize = 1536;
    thread.initPriority = *priority;
    pad->update_thread = CreateThread(&thread);
    if (pad->update_thread) {
      thread.option = (u_int)pad;
      thread.attr = TH_C;
      thread.entry = pad_query_thread;
      thread.stackSize = 1536;
      thread.initPriority = *priority;
      pad->query_thread = CreateThread(&thread);
      if (pad->query_thread) {
        thread.option = (u_int)pad;
        thread.attr = TH_C;
        thread.entry = pad_main_mode_thread;
        thread.stackSize = 1024;
        thread.initPriority = *priority;
        pad->main_mode_thread = CreateThread(&thread);
        if (pad->main_mode_thread) {
          thread.option = (u_int)pad;
          thread.attr = TH_C;
          thread.entry = pad_act_align_thread;
          thread.stackSize = 1024;
          thread.initPriority = *priority;
          pad->act_align_thread = CreateThread(&thread);
          if (pad->act_align_thread) {
            thread.option = (u_int)pad;
            thread.attr = TH_C;
            thread.entry = pad_button_info_thread;
            thread.stackSize = 1024;
            thread.initPriority = *priority;
            pad->button_info_thread = CreateThread(&thread);
            if (pad->button_info_thread) {
              thread.option = (u_int)pad;
              thread.attr = TH_C;
              thread.entry = pad_vref_thread;
              thread.stackSize = 1024;
              thread.initPriority = *priority;
              pad->vref_thread = CreateThread(&thread);
              if (pad->vref_thread) {
                if (pad->current_task != 0 && pad->current_task != 1)
                  return 0;
                {
                  pad->current_task = 1;
                  StartThread(pad->update_thread, 0);
                  return 1;
                }
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

int padPortClose(int port, int slot, int wait) {
  int result = 0;
  u_int events[1];
  PadState* pad;
  int* open;
  open = &pad_open_slots[port];
  pad = &pad_states[port][slot];
  events[0] = 0;
  if (!((*open >> slot) & 1))
    return result;
  {
    if (pad->request_state == 2)
      return result;
    pad->run_task = 3;
    pad->task_thread = 0;
    pad->request_state = 2;
    pad->wait_for_close = wait;
    if (wait) {
      WaitEventFlag(pad->event, PAD_EVENT_CLOSE_DONE, EW_CLEAR, events);
      DeleteEventFlag(pad->event);
      pad->event = 0;
    }
    result = 1;
  }
  return result;
}

int padGetInBuffer(int port, int slot, u_char* buffer) {
  PadState* pad = &pad_states[port][slot];
  int i;
  u_short buttons;
  if (pad->button_ready == 1) {
    buffer[0] = 0;
    buffer[1] = pad->current_id;
    buffer[31] = pad->button_status[2];
    for (i = 2; i < 31; i++)
      buffer[i] = pad->button_status[i + 1];
    if (pad->current_id == 0x12) {
      buffer[2] = 0xff;
      buffer[3] = 0xff;
      buffer[4] = 0;
      buffer[5] = 0;
    }
    if (pad->current_id == 0x79) {
      buttons = ~((buffer[2] << 8) | buffer[3]);
      if (buttons & 8192) {
        if (buffer[8] == 0)
          buffer[8] = 1;
      } else {
        if (buffer[8] != 0)
          buffer[8] = 0;
      }
      if (buttons & 32768) {
        if (buffer[9] == 0)
          buffer[9] = 1;
      } else {
        if (buffer[9] != 0)
          buffer[9] = 0;
      }
      if (buttons & 4096) {
        if (buffer[10] == 0)
          buffer[10] = 1;
      } else {
        if (buffer[10] != 0)
          buffer[10] = 0;
      }
      if (buttons & 16384) {
        if (buffer[11] == 0)
          buffer[11] = 1;
      } else {
        if (buffer[11] != 0)
          buffer[11] = 0;
      }
      if (buttons & 16) {
        if (buffer[12] == 0)
          buffer[12] = 1;
      } else {
        if (buffer[12] != 0)
          buffer[12] = 0;
      }
      if (buttons & 32) {
        if (buffer[13] == 0)
          buffer[13] = 1;
      } else {
        if (buffer[13] != 0)
          buffer[13] = 0;
      }
      if (buttons & 64) {
        if (buffer[14] == 0)
          buffer[14] = 1;
      } else {
        if (buffer[14] != 0)
          buffer[14] = 0;
      }
      if (buttons & 128) {
        if (buffer[15] == 0)
          buffer[15] = 1;
      } else {
        if (buffer[15] != 0)
          buffer[15] = 0;
      }
      if (buttons & 4) {
        if (buffer[16] == 0)
          buffer[16] = 1;
      } else {
        if (buffer[16] != 0)
          buffer[16] = 0;
      }
      if (buttons & 8) {
        if (buffer[17] == 0)
          buffer[17] = 1;
      } else {
        if (buffer[17] != 0)
          buffer[17] = 0;
      }
      if (buttons & 1) {
        if (buffer[18] == 0)
          buffer[18] = 1;
      } else {
        if (buffer[18] != 0)
          buffer[18] = 0;
      }
      if (buttons & 2) {
        if (buffer[19] == 0)
          buffer[19] = 1;
      } else {
        if (buffer[19] != 0)
          buffer[19] = 0;
      }
    }
    return 32;
  }
  buffer[0] = 0xff;
  for (i = 1; i < 32; i++)
    buffer[i] = 0;
  return 0;
}

extern struct ModuleInfo Module;

int padGetModeConfig(int port, int slot) {
  PadState* pad = &pad_states[port][slot];
  return pad->mode_config;
}

int padInfoAct(int port, int slot, int actuator, int field) {
  PadState* pad = &pad_states[port][slot];
  if (pad->current_task != PAD_TASK_UPDATE || pad->mode_config < PAD_CONFIG_READY)
    return 0;
  if (actuator == -1)
    return pad->actuator_count;
  if (actuator >= pad->actuator_count)
    goto invalid;
  if (field == 1)
    return pad->actuator_info[actuator][0];
  if (field == 2)
    return pad->actuator_info[actuator][1];
  if (field == 3)
    return pad->actuator_info[actuator][2];
  if (field != 4) {
  invalid:
    return -1;
  }
  return pad->actuator_info[actuator][3];
}

int padInfoComb(int port, int slot, int combination, int field) {
  PadState* pad = &pad_states[port][slot];
  if (pad->current_task != PAD_TASK_UPDATE)
    return 0;
  if (pad->mode_config < PAD_CONFIG_READY)
    goto invalid;
  if (combination == -1)
    return pad->combination_count;
  if (combination >= pad->combination_count)
    goto invalid;
  if (field == -1)
    return pad->combination_info[combination][0];
  if (field == 0)
    return pad->combination_info[combination][1];
  if (field == 1)
    return pad->combination_info[combination][2];
  if (field != 2) {
  invalid:
    return -1;
  }
  return pad->combination_info[combination][3];
}

int padInfoMode(int port, int slot, int term, int offset) {
  PadState* pad = &pad_states[port][slot];
  if (pad->current_task != PAD_TASK_UPDATE || pad->request_state == PAD_REQUEST_BUSY)
    goto unavailable;
  switch (term) {
    case 1:
      if (pad->current_id != 0xf3)
        return pad->current_id >> 4;
      goto unavailable;
    case 2:
      if (pad->mode_config == 1)
        goto unavailable;
      return pad->modes[pad->current_mode];
    case 3:
      if (pad->mode_config == 1)
        goto unavailable;
      return pad->current_mode;
    case 4:
      if (pad->mode_config == 1)
        goto unavailable;
      if (offset == -1)
        return pad->mode_count;
      if (offset < pad->mode_count)
        return pad->modes[offset];
    unavailable:
      return 0;
  }
  return -1;
}

int padSetActDirect(int port, int slot, u_char* values) {
  PadState* pad = &pad_states[port][slot];
  int i;
  if (pad->current_task != PAD_TASK_UPDATE)
    return 0;
  {
    pad_check_actuator_power(port, slot, values);
    for (i = 0; i < 6; ++i)
      pad->actuator_direct[i] = values[i];
    pad->actuator_direct_size = 6;
    return 1;
  }
}

int padSetMainMode(int port, int slot, int mode, int lock) {
  PadState* pad = &pad_states[port][slot];
  if (pad->current_task != PAD_TASK_UPDATE || pad->mode_config < PAD_CONFIG_READY ||
      mode >= pad->mode_count)
    return 0;
  {
    if (pad->request_state == PAD_REQUEST_BUSY)
      return 0;
    pad->mode = mode;
    pad->lock = lock;
    pad->run_task = PAD_TASK_MAIN_MODE;
    pad->request_state = PAD_REQUEST_BUSY;
    pad->task_thread = pad->main_mode_thread;
    return 1;
  }
}

int padSetActAlign(int port, int slot, const u_char* values) {
  PadState* pad = &pad_states[port][slot];
  int i;
  if (pad->current_task != PAD_TASK_UPDATE) {
  unavailable:
    return 0;
  }
  if (pad->mode_config < PAD_CONFIG_READY)
    goto unavailable;
  if (pad->request_state == PAD_REQUEST_BUSY)
    goto unavailable;
  for (i = 0; i < 6; ++i)
    pad->actuator_align[i] = values[i];
  pad->request_state = PAD_REQUEST_BUSY;
  pad->run_task = PAD_TASK_ACT_ALIGN;
  pad->task_thread = pad->act_align_thread;
  return 1;
}

int padSetVrefParam(int port, int slot, const u_char* values) {
  PadState* pad = &pad_states[port][slot];
  int i;
  if (pad->current_task != PAD_TASK_UPDATE) {
  unavailable:
    return 0;
  }
  if (pad->mode_config < PAD_CONFIG_READY)
    goto unavailable;
  if (pad->model < 2)
    goto unavailable;
  if (pad->request_state == PAD_REQUEST_BUSY)
    goto unavailable;
  for (i = 0; i < 12; ++i)
    pad->vref[i] = values[i];
  pad->run_task = PAD_TASK_VREF;
  pad->request_state = PAD_REQUEST_BUSY;
  pad->task_thread = pad->vref_thread;
  return 1;
}

int padSetButtonInfo(int port, int slot, u_int buttons) {
  PadState* pad = &pad_states[port][slot];
  int i;
  if (pad->current_task != PAD_TASK_UPDATE || pad->mode_config < PAD_CONFIG_READY ||
      !(pad->model & 2) || pad->request_state == PAD_REQUEST_BUSY)
    return 0;
  buttons = (buttons << 6) | 0x3f;
  pad->button_info[0] = buttons;
  pad->button_info[1] = buttons >> 8;
  pad->button_info[2] = buttons >> 16;
  pad->button_info[3] = buttons >> 24;
  for (i = 0; i < 12; ++i)
    pad->vref[i] = 2;
  pad->run_task = PAD_TASK_BUTTON_INFO;
  pad->request_state = PAD_REQUEST_BUSY;
  pad->task_thread = pad->button_info_thread;
  return 1;
}

u_int padGetButtonMask(int port, int slot) {
  PadState* pad = &pad_states[port][slot];
  if (pad->current_task != PAD_TASK_UPDATE) {
  unavailable:
    return 0;
  }
  if (pad->mode_config < PAD_CONFIG_READY)
    goto unavailable;
  if (pad->model < 2)
    goto unavailable;
  return pad->button_mask[0] + (pad->button_mask[1] << 8) + (pad->button_mask[2] << 16) +
         (pad->button_mask[3] << 24);
}

int padGetPortMax(void) {
  return PAD_PORT_COUNT;
}

int padGetSlotMax(int port) {
  return sio2_call_callback2();
}

u_int padGetModVersion(void) {
  return Module.version;
}

int padSetDebug(int enabled) {
  pad_debug = enabled;
  return 1;
}
