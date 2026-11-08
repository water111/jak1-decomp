#include "sce/iop/libsd/internal.h"

int sceSdBlockTrans(short channel, u_short mode, void* memory, u_int size, ...) {
#if LIBSD_VERSION >= 0x0301
  int result = SD_ERROR_INVALID_MODE;
  volatile int reset;
  int transfer_offset;
#else
  int result = -1;
#endif
  int core = channel & SD_CORE_MASK;
  char* args = __builtin_next_arg(size);
#if LIBSD_VERSION >= 0x0301
  int busy = sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_CHECK_BUSY);
#endif
  switch (mode & SD_TRANS_MODE_MASK) {
    case SD_TRANS_MODE_WRITE:
#if LIBSD_VERSION >= 0x0301
      if (busy)
        return SD_ERROR_TRANSFER_BUSY;
#endif
#if LIBSD_VERSION >= 0x0301
      transfer_offset = core * sizeof(SdTransferInterrupt);
#endif
      sd_transfer_interrupts[core].mode = core | SD_TRANSFER_BLOCK;
      if (mode & SD_BLOCK_LOOP) {
        sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK | SD_TRANSFER_LOOP);
        size >>= 1;
      }
#if LIBSD_VERSION >= 0x0301
      result = sd_block_restart(memory, size, channel, mode, 0);
      goto transfer_done;
#else
      result = size;
      if (sd_block_write(memory, size, channel, mode) < 0)
        result = -1;
#endif
      break;
    case SD_TRANS_MODE_READ:
#if LIBSD_VERSION >= 0x0301
      if (busy)
        return SD_ERROR_TRANSFER_BUSY;
#endif
#if LIBSD_VERSION >= 0x0301
      transfer_offset = core * sizeof(SdTransferInterrupt);
#endif
      sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK_READ);
      if (mode & SD_BLOCK_LOOP) {
        sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK_READ | SD_TRANSFER_LOOP);
        size >>= 1;
      }
#if LIBSD_VERSION >= 0x0301
      result = sd_block_read(memory, size, core, mode);
      goto transfer_done;
#else
      result = size;
      if (sd_block_read(memory, size, core, mode) < 0)
        result = -1;
#endif
      break;
    case SD_TRANS_MODE_WRITE_FROM: {
#if LIBSD_VERSION >= 0x0301
      void* start;
      if (busy)
        return SD_ERROR_TRANSFER_BUSY;
#else
      void* start = *(void**)(((u_int)args + 3) & ~3);
#endif
#if LIBSD_VERSION >= 0x0301
      transfer_offset = core * sizeof(SdTransferInterrupt);
#endif
      sd_transfer_interrupts[core].mode = core | SD_TRANSFER_BLOCK;
      if (mode & SD_BLOCK_LOOP) {
        sd_transfer_interrupts[core].mode = core | (SD_TRANSFER_BLOCK | SD_TRANSFER_LOOP);
        size >>= 1;
      }
#if LIBSD_VERSION >= 0x0301
      start = *(void**)(((u_int)args + 3) & ~3);
#endif
      result = sd_block_restart(memory, size, channel, mode, start);
#if LIBSD_VERSION >= 0x0301
    transfer_done:
      if (result < 0) {
        ((SdTransferInterrupt*)((char*)sd_transfer_interrupts + transfer_offset))->mode = core;
        reset = core;
      }
      return result;
#endif
      break;
    }
    case SD_TRANS_MODE_STOP:
#if LIBSD_VERSION >= 0x0301
      sd_transfer_interrupts[core].mode = core;
      reset = core;
      result = sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_STOP);
#else
      result = sd_stop_transfer(core);
#endif
      break;
#if LIBSD_VERSION >= 0x0301
    default:
      return result;
#endif
  }
  return result;
}

#if LIBSD_VERSION >= 0x0301
u_int sceSdBlockTransStatus(short core, short wait) {
  return sd_block_status(core);
}
#endif
