#include "sce/iop/libsd/internal.h"

u_int sceSdBlockTransStatus(short core, short wait) {
  return sd_block_status(core);
}
