#ifndef SCE_IOP_PADMAN_RUNTIME_H
#define SCE_IOP_PADMAN_RUNTIME_H
#include "sce/iop/padman/threads.h"

typedef struct PadRuntime {
  PadVblankState vblank;
  int reserved;
#if PADMAN_VERSION >= 0x0405
  int reserved1c;
#endif
  int initialized;
#if PADMAN_VERSION >= 0x0403
  int timer_initialized;
#endif
  void* ee_buffer;
  int high_priority, low_priority;
#if PADMAN_VERSION >= 0x0403
  volatile int vblank_flag;
#else
  int reserved34;
#endif
} PadRuntime;

#if PADMAN_VERSION == 0x0403
typedef struct PadPortState {
  int open_slots;
  int capture_count;
  u_int previous_capture, fourth_capture, latest_capture;
  u_int reserved[14];
  volatile u_int* capture_register;
} PadPortState;

extern PadPortState pad_ports[2];
#define PAD_OPEN_SLOTS(port) (pad_ports[port].open_slots)
#else
extern int pad_open_slots[2];
#define PAD_OPEN_SLOTS(port) (pad_open_slots[port])
#endif

extern PadRuntime pad_runtime;
#endif
