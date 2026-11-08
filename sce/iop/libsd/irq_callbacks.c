#include "sce/iop/libsd/internal.h"

sceSdCallback sceSdSetIRQCallback(sceSdCallback callback) {
  sceSdCallback previous = sd_irq_callback;
  sd_irq_callback = callback;
#if LIBSD_VERSION >= 0x0301
  { sceSdCallback volatile readback = sd_irq_callback; }
#endif
  return previous;
}

sceSdSpu2IntrHandler sceSdSetSpu2IntrHandler(sceSdSpu2IntrHandler handler, void* argument) {
  sceSdSpu2IntrHandler previous = sd_spu2_handler;
  sd_spu2_handler = handler;
  sd_spu2_argument = argument;
#if LIBSD_VERSION >= 0x0301
  { void* volatile readback = sd_spu2_argument; }
#endif
  return previous;
}

#if LIBSD_VERSION >= 0x0301
void* sceSdGetSpu2IntrHandlerArgument(void) {
  return sd_spu2_argument;
}
#endif
