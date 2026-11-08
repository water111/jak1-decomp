#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/thevent/thevent.h"

int sceSdVoiceTransStatus(short core, short wait) {
  u_int bits;
  if (sd_transfer_active[core] == 1 || sd_transfer_status[core] == 1)
    return 1;
#if LIBSD_VERSION >= 0x0301
  switch (wait) {
    case 1:
      if (QueryIntrContext())
        return SD_ERROR_INTERRUPT_CONTEXT;
      WaitEventFlag(sd_transfer_events[core], SD_TRANSFER_EVENT_DONE, EW_AND, &bits);
      sd_transfer_pending[core] = 0;
      sd_transfer_status[core] = 1;
      break;
    case 0:
      if (sd_transfer_pending[core]) {
        sd_transfer_pending[core] = 0;
        sd_transfer_status[core] = 1;
      }
      break;
  }
#else
  if (wait == 1) {
    if (QueryIntrContext())
      return -1;
    WaitEventFlag(sd_transfer_events[core], 1, EW_AND, &bits);
    ClearEventFlag(sd_transfer_events[core], ~1);
    sd_transfer_status[core] = 1;
  } else if (sd_transfer_pending[core]) {
    sd_transfer_pending[core] = 0;
    sd_transfer_status[core] = 1;
  }
#endif
  return sd_transfer_status[core];
}

#if LIBSD_VERSION >= 0x0301
int sceSdStopTrans(int channel) {
  volatile int core;
  channel &= SD_CORE_MASK;
  sd_transfer_interrupts[channel].mode = channel;
  core = channel;
  return sd_dma_control((channel << SD_DMA_CORE_SHIFT) | SD_DMA_STOP);
}
#endif
