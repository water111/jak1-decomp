#ifndef SCE_IOP_PADMAN_STATE_H
#define SCE_IOP_PADMAN_STATE_H
#include "sce/iop/padman/transfer.h"
#define PAD_TASK_UPDATE 1
#define PAD_TASK_MAIN_MODE 4
#define PAD_TASK_ACT_ALIGN 5
#define PAD_TASK_BUTTON_INFO 6
#define PAD_TASK_VREF 7
#define PAD_REQUEST_BUSY 2
#define PAD_CONFIG_READY 2

typedef struct PadEECommand {
  u_int frame, command, reserved;
  u_char data[20];
} PadEECommand;

// The first 128 bytes are sent to the EE as one frame report.
typedef struct PadEEReport {
  u_char data[32];
  u_char actuator_direct[8], actuator_align[8];
  u_char actuator_info[4][4], combination_info[4][4];
  u_short modes[4];
  u_int frame, find_retries, length;
  u_char mode_config, current_id, model, button_ready;
  u_char mode_count, current_mode, actuator_count, combination_count;
  u_char button_info_ready, mode, lock, actuator_direct_size;
  u_char state, request_state, current_task, run_task, status_bit;
  u_char button_info[4], button_mask[4], reserved[3];
} PadEEReport;

typedef struct PadState {
  PadEEReport report;
  PadEECommand ee_commands[2];
  u_char mode_config, current_id, model, mode_count;
  u_char current_mode, actuator_count, combination_count, disconnected;
  u_char actuator_info[4][4];
  u_char combination_info[4][4];
  u_short modes[4];
  u_char button_info[4], button_mask[4], vref[12];
  u_short reserved104, button_info_ready;
  u_char input[32], output[32], button_status[32];
  u_short button_ready;
  u_char mode, lock;
  u_char actuator_direct[8];
  u_short actuator_direct_size;
  u_char actuator_align[6];
  u_short state, request_state;
  u_int frame;
  void* ee_buffer;
  int find_retries;
  int update_thread, query_thread, reserved_thread;
  int main_mode_thread, act_align_thread, button_info_thread, vref_thread;
  int event;
  short port, slot;
  int current_task, run_task, task_thread;
  u_int status_bit;
  u_int reserved1c0, wait_for_close;
} PadState;

extern PadState pad_states[PAD_PORT_COUNT][PAD_SLOT_COUNT];
extern int pad_debug;
int pad_other_actuator_current(int port, int slot);
int pad_check_actuator_power(int port, int slot, u_char* values);
#endif
