#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/loadcore/loadcore.h"

struct ModuleInfo Module = {"Sound_Device_Library", LIBSD_VERSION};

int start(void) {
#if LIBSD_VERSION >= 0x0301
  int intr_state;
  int result;
  CpuSuspendIntr(&intr_state);
  result = RegisterLibraryEntries(libsd_stub);
  CpuResumeIntr(intr_state);
  if (result)
    return 1;
  sd_init_bus();
  sd_init_module_state();
  return 0;
#else
  if (RegisterLibraryEntries((void*)libsd_stub))
    return 1;
  sd_init_bus();
  return 0;
#endif
}
