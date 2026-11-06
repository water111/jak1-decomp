#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sdrdrv/internal.h"
#include "sce/iop/sysclib/ctype.h"
#include "sce/iop/sysclib/stdlib.h"
#include "sce/iop/sysclib/string.h"

struct ModuleInfo Module = {"sdr_driver", 0x0401};
extern unsigned int sdrdrv_stub[];
int sdr_main_thread_priority;
int sdr_callback_thread_priority;
int sdr_main_tid;
#if SDRDRV_SDK >= 2700
// Keep this buffer with the module state to preserve the SDK 2700 BSS layout.
unsigned int sdr_batch_buffer[384];
#endif

int start(int argc, char** argv) {
  struct ThreadParam thread;
  int intr_state;
  int result;
  int priority;
  int i;
  char* value;
#if SDRDRV_SDK == 2400
  FlushDcache();
#endif
  CpuSuspendIntr(&intr_state);
  result = RegisterLibraryEntries(sdrdrv_stub);
  CpuResumeIntr(intr_state);
  if (result)
    return 1;
  printf("SDR driver version 4.0.1 (C) SCEI\n");
  sdr_main_thread_priority = SDR_DEFAULT_PRIORITY;
  sdr_callback_thread_priority = SDR_DEFAULT_PRIORITY;
  sdr_main_tid = 0;
  sdr_callback_tid = 0;
  for (i = 1; i < argc; i++) {
    if (!strncmp("thpri=", argv[i], 6)) {
      value = argv[i] + 6;
      if (look_ctype_table(*value) & CTYPE_DIGIT) {
        priority = strtol(value, 0, 10);
        if (priority < SDR_MIN_PRIORITY || priority > SDR_MAX_PRIORITY) {
          printf(" SDR driver error: invalid priority %d\n", priority);
          priority = SDR_DEFAULT_PRIORITY;
        }
        sdr_main_thread_priority = priority;
      }
      while (look_ctype_table(*value) & CTYPE_DIGIT)
        value++;
      if (*value == ',' && (look_ctype_table(value[1]) & CTYPE_DIGIT)) {
        priority = strtol(value + 1, 0, 10);
        if (priority < SDR_MIN_PRIORITY || priority > SDR_MAX_PRIORITY) {
          printf(" SDR driver error: invalid priority %d\n", priority);
          priority = SDR_DEFAULT_PRIORITY;
        }
        sdr_callback_thread_priority = priority;
      }
      if (sdr_main_thread_priority > sdr_callback_thread_priority) {
        printf(" SDR driver ERROR:\n");
        printf("   callback th. priority is higher than main th. priority.\n");
        sdr_callback_thread_priority = sdr_main_thread_priority;
      }
      printf(" SDR driver: thread priority: main=%d, callback=%d\n", sdr_main_thread_priority,
             sdr_callback_thread_priority);
    }
  }
  thread.attr = TH_C;
  thread.entry = sdr_rpc_thread;
  thread.stackSize = SDR_THREAD_STACK_SIZE;
  thread.option = 0;
  thread.initPriority = sdr_main_thread_priority;
  sdr_main_tid = CreateThread(&thread);
  if (sdr_main_tid > 0) {
    StartThread(sdr_main_tid, 0);
    printf(" Exit rsd_main \n");
    return 0;
  }
  return 1;
}

int sceSdrChangeThreadPriority(int main, int callback) {
  struct ThreadInfo info;
  int result;
  if (main < SDR_MIN_PRIORITY || main > SDR_MAX_PRIORITY)
    return SDR_INVALID_PRIORITY;
  if (callback < SDR_MIN_PRIORITY || callback > SDR_MAX_PRIORITY)
    return SDR_INVALID_PRIORITY;
  if (callback < main) {
    printf(" SDR driver ERROR:\n");
    printf("   callback th. priority is higher than main th. priority.\n");
    callback = main;
  }
  ReferThreadStatus(0, &info);
  ChangeThreadPriority(0, SDR_PRIORITY_GUARD);
  if (sdr_main_tid > 0) {
    result = ChangeThreadPriority(sdr_main_tid, main);
    if (result < 0)
      return result;
  }
  if (sdr_callback_tid > 0) {
    result = ChangeThreadPriority(sdr_callback_tid, callback);
    if (result < 0)
      return result;
  } else
    sdr_callback_thread_priority = callback;
  ChangeThreadPriority(0, info.currentPriority);
  return 0;
}
