#include "sce/iop/libsd/internal.h"

int sceSdBlockTrans(short channel, u_short mode, void* memory, u_int size, ...) {
  int result = -1;
  int core = channel & SD_CORE_MASK;
  char* args = __builtin_next_arg(size);
  switch (mode & SD_TRANS_MODE_MASK) {
    case SD_TRANS_MODE_WRITE:
      sd_transfer_interrupts[core].mode = core | SD_TRANSFER_BLOCK;
      if (mode & SD_BLOCK_LOOP) {
        sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK | SD_TRANSFER_LOOP);
        size >>= 1;
      }
      result = size;
      if (sd_block_write(memory, size, channel, mode) < 0)
        result = -1;
      break;
    case SD_TRANS_MODE_READ:
      sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK | SD_TRANSFER_READ);
      if (mode & SD_BLOCK_LOOP) {
        sd_transfer_interrupts[core].mode =
            core | (SD_TRANSFER_BLOCK | SD_TRANSFER_READ | SD_TRANSFER_LOOP);
        size >>= 1;
      }
      result = size;
      if (sd_block_read(memory, size, core, mode) < 0)
        result = -1;
      break;
    case SD_TRANS_MODE_WRITE_FROM: {
      void* start = *(void**)(((u_int)args + 3) & ~3);
      sd_transfer_interrupts[core].mode = core | SD_TRANSFER_BLOCK;
      if (mode & SD_BLOCK_LOOP) {
        sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK | SD_TRANSFER_LOOP);
        size >>= 1;
      }
      result = sd_block_restart(memory, size, channel, mode, start);
      break;
    }
    case SD_TRANS_MODE_STOP:
      result = sd_stop_transfer(core);
      break;
  }
  return result;
}
