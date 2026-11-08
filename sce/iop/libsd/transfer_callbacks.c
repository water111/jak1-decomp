#include "sce/iop/libsd/internal.h"

sceSdCallback sceSdSetTransCallback(u_short core, sceSdCallback callback) {
  sceSdCallback previous = sd_transfer_callbacks[core];
  sd_transfer_callbacks[core] = callback;
#if LIBSD_VERSION >= 0x0301
  { sceSdCallback volatile readback = sd_transfer_callbacks[core]; }
#endif
  return previous;
}

sceSdTransIntrHandler sceSdSetTransIntrHandler(int core,
                                               sceSdTransIntrHandler handler,
                                               void* argument) {
  sceSdTransIntrHandler previous = sd_transfer_handlers[core];
  sd_transfer_handlers[core] = handler;
  sd_transfer_interrupts[core].argument = argument;
#if LIBSD_VERSION >= 0x0301
  { void* volatile readback = sd_transfer_interrupts[core].argument; }
#endif
  return previous;
}

#if LIBSD_VERSION >= 0x0301
void* sceSdGetTransIntrHandlerArgument(int core) {
  return sd_transfer_interrupts[core].argument;
}
#endif
