#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/thevent/thevent.h"

int sceSdVoiceTransStatus(short core, short wait) {
  u_int bits;
  if (sd_transfer_active[core] == 1 || sd_transfer_status[core] == 1)
    return 1;
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
  return sd_transfer_status[core];
}
