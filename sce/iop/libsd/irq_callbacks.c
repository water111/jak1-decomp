#include "sce/iop/libsd/internal.h"

sceSdCallback sceSdSetIRQCallback(sceSdCallback callback) {
  sceSdCallback previous = sd_irq_callback;
  sd_irq_callback = callback;
  return previous;
}

sceSdSpu2IntrHandler sceSdSetSpu2IntrHandler(sceSdSpu2IntrHandler handler, void* argument) {
  sceSdSpu2IntrHandler previous = sd_spu2_handler;
  sd_spu2_handler = handler;
  sd_spu2_argument = argument;
  return previous;
}
