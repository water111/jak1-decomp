#include "sce/iop/sifman/sif.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/libsd.h"
#include "989snd/989snd.h"

static unsigned int dmaid = 0;
static sceSifDmaData cmd;

// Wait for DMA to finish, calling DelayThread(10) and very questionable countdown.
void DMA_Sync(void) {
  int count;

  if (dmaid) {
    if (sceSifDmaStat(dmaid) > 0) {

      // The entire check is duplicated and just reloads the counter when reaching 0.
      // It's possible this printed something on timer reload to indicate a stall.
      count = 10000;
      while (sceSifDmaStat(dmaid) > 0) {
        DelayThread(10);
        count--;
        if (!count) {
          count = 10000;
        }
      }
    }

    count = 10000;
    while (sceSifDmaStat(dmaid) == 0) {
      DelayThread(10);
      count--;
      if (!count) {
        count = 10000;
      }
    }
    dmaid = 0;
  }
}

// Wait for any ongoing DMA transfer to finish, then DMA to EE.
void DMA_SendToEE(void *src_addr, unsigned int size, unsigned int dst_addr) {
  DMA_Sync();
  cmd.data = (unsigned int)src_addr;
  cmd.addr = dst_addr;
  cmd.size = size;
  cmd.mode = 0;

  CpuDisableIntr();
  dmaid = sceSifSetDma(&cmd, 1);
  CpuEnableIntr();

  if (!dmaid) {
    // failed to queue, hang forever...
    for (;;) {
    }
  }
}

static volatile int strobe = 0;

// DMA completion interrupt, sets strobe to 1.
static int intr(int channel, void *userdata) {
  strobe = 1;
  return 0;
}

// DMA to the SPU, spin until completion.
int DMA_SendToSPUAndSync(void *src_addr, unsigned int size, unsigned int dst_addr) {
  int channel = snd_GetFreeSPUDMA();
  unsigned int transferred;

  if (channel == -1) {
    return 0;
  }
  strobe = 0;
  sceSdSetTransIntrHandler(channel, intr, 0);

  size = (size + 63) & ~63;
  transferred = sceSdVoiceTrans(channel, 0, src_addr, dst_addr, size);
  while (!strobe) {
  }

  sceSdSetTransIntrHandler(channel, 0, 0);
  snd_FreeSPUDMA(channel);
  if (transferred < size) {
    return 0;
  }
  return 1;
}
