#include "sce/iop/loadcore/loadcore.h"

struct ModuleInfo Module = {"Sound_Device_Library", 0x0104};

extern void libsd_stub(void);
void sd_init_bus(void);

int start(void) {
  if (RegisterLibraryEntries((void*)libsd_stub))
    return 1;
  sd_init_bus();
  return 0;
}
