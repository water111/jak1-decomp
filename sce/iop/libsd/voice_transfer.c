#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/thevent/thevent.h"

#if LIBSD_VERSION >= 0x0301
int sceSdVoiceTrans(short channel,
                    u_short mode,
                    u_char* memory,
                    u_int address,
                    u_int transfer_size) {
  int result;
  int core = channel & SD_CORE_MASK;
  register u_int size asm("$18") = transfer_size;
  if (sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_CHECK_BUSY))
    return SD_ERROR_TRANSFER_BUSY;
  if (sd_transfer_status[core] != 1)
    return SD_ERROR_TRANSFER_PENDING;
  result = SD_ERROR_INVALID_MODE;
  switch (mode & SD_TRANS_MODE_MASK) {
    case SD_TRANS_MODE_WRITE:
      if (!(mode & SD_TRANS_BY_IO)) {
        u_int rounded;
        sd_transfer_interrupts[core].mode = core | SD_TRANSFER_VOICE_WRITE;
        sd_transfer_active[core] = 0;
        sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_SET_ADDRESS, address);
        if (QueryIntrContext())
          iClearEventFlag(sd_transfer_events[core], ~SD_TRANSFER_EVENT_DONE);
        else
          ClearEventFlag(sd_transfer_events[core], ~SD_TRANSFER_EVENT_DONE);
        sd_transfer_status[core] = 0;
        rounded = (size / 64) * 64;
        if (size & 63)
          rounded += 64;
        asm("" : : "r"(size));
        result = sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_WRITE, memory, rounded);
      } else {
        u_int rounded;
        u_int remainder;
        sd_transfer_interrupts[core].mode = core | SD_TRANSFER_VOICE_WRITE;
        sd_transfer_active[core] = 1;
        sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_SET_ADDRESS, address);
        asm("srl $2,%1,6\n\tsll %0,$2,6" : "=r"(rounded) : "r"(size) : "$2");
        remainder = size & 63;
        asm("" : : "r"(size));
        size = remainder ? rounded + 64 : rounded;
        sd_voice_write_io(memory, size, core);
        return size;
      }
      break;
    case SD_TRANS_MODE_READ: {
      u_int rounded, remainder;
      sd_transfer_interrupts[core].mode = core | SD_TRANSFER_VOICE_READ;
      sd_transfer_active[core] = 0;
      sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_SET_ADDRESS, address);
      if (QueryIntrContext())
        iClearEventFlag(sd_transfer_events[core], ~SD_TRANSFER_EVENT_DONE);
      else
        ClearEventFlag(sd_transfer_events[core], ~SD_TRANSFER_EVENT_DONE);
      sd_transfer_status[core] = 0;
      rounded = (size / 64) * 64;
      remainder = size & 63;
      asm("" : : "r"(size));
      if (remainder)
        rounded += 64;
      result = sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_READ, memory, rounded);
      break;
    }
  }
  return result;
}
#else
extern int sd_voice_addresses[2];

int sceSdVoiceTrans(short channel, u_short mode, u_char* memory, u_int address, u_int size) {
  int core = channel & SD_CORE_MASK;
  int result = -1;
  if (sd_transfer_status[core] == 1) {
    if (QueryIntrContext())
      iClearEventFlag(sd_transfer_events[core], ~SD_TRANSFER_EVENT_DONE);
    else
      ClearEventFlag(sd_transfer_events[core], ~SD_TRANSFER_EVENT_DONE);
    sceSdSetAddr(core | SD_A_TSA, address);
    sd_voice_addresses[core] = (int)address >> 1;
    if (!(mode & SD_TRANS_BY_IO))
      sd_transfer_active[core] = 0;
    else
      sd_transfer_active[core] = 1;
    sd_transfer_status[core] = 0;
    if ((mode & SD_TRANS_MODE_MASK) == SD_TRANS_MODE_WRITE) {
      sd_transfer_interrupts[core].mode = core;
      result = sd_voice_write(core, memory, size);
    } else if ((mode & SD_TRANS_MODE_MASK) == SD_TRANS_MODE_READ) {
      sd_transfer_interrupts[core].mode = core | SD_TRANSFER_READ;
      result = sd_voice_read(core, memory, size);
    }
    if (result < 0)
      sd_transfer_status[core] = 1;
  }
  return result;
}

#endif
