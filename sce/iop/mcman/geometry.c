#include "sce/iop/mcman/internal.h"

int mcman_spare_size(int port, int slot) {
  return mcman_devices[port][slot].page_size / 32;
}
