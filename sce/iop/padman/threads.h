#ifndef SCE_IOP_PADMAN_THREADS_H
#define SCE_IOP_PADMAN_THREADS_H
#include "sce/iop/padman/state.h"
// Per-pad worker events.
#define PAD_EVENT_UPDATE 0x1
#define PAD_EVENT_QUERY 0x2
#define PAD_EVENT_SET_MAIN_MODE 0x8
#define PAD_EVENT_SET_ACT_ALIGN 0x10
#define PAD_EVENT_SET_BUTTON_INFO 0x20
#define PAD_EVENT_SET_VREF_PARAM 0x40
#define PAD_EVENT_TRANSFER_START 0x80
#define PAD_EVENT_TRANSFER_DONE 0x100
#define PAD_EVENT_TASK_DONE 0x200
#define PAD_EVENT_CLOSE_DONE 0x400
#define PAD_EVENT_EXIT 0x1000

// Shared vblank/transfer-thread events; PAD_EVENT_EXIT also applies here.
#define PAD_VBLANK_EVENT_TRANSFER_START 0x1
#define PAD_VBLANK_EVENT_TRANSFER_DONE 0x4
#define PAD_VBLANK_EVENT_EXIT_DONE 0x8

#define PAD_THREAD_WAITING 0x10

typedef struct PadVblankState {
  int exiting, event, initialized, stop_transfer;
  int scheduler_thread, transfer_thread;
} PadVblankState;

int pad_update_actuators(PadState* pad);
void pad_wait_clear(int event, u_int bits, int mode, u_int* result);
int pad_vblank_threads_running(PadVblankState* vblank);
void pad_delete_vblank_threads(PadVblankState* vblank);
int pad_threads_idle(PadState* pad);
void pad_delete_threads(PadState* pad);
#endif
