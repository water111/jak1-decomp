#include "sce/iop/libsd/internal.h"

sceSdCallback sceSdSetTransCallback(u_short core, sceSdCallback callback) {
  sceSdCallback previous = sd_transfer_callbacks[core];
  sd_transfer_callbacks[core] = callback;
  return previous;
}

sceSdTransIntrHandler sceSdSetTransIntrHandler(int core,
                                               sceSdTransIntrHandler handler,
                                               void* argument) {
  sceSdTransIntrHandler previous = sd_transfer_handlers[core];
  sd_transfer_handlers[core] = handler;
  sd_transfer_interrupts[core].argument = argument;
  return previous;
}
