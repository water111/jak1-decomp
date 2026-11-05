#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/thevent/thevent.h"

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
