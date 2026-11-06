#include "sce/iop/sdrdrv/internal.h"
#include "sce/iop/sysclib/string.h"

#define SDR_CALLBACK_TRANSFER_0 0x100
#define SDR_CALLBACK_TRANSFER_1 0x200
#define SDR_CALLBACK_SPU2 0x400
#define SDR_CALLBACK_TRANSFER_0_2400 11
#define SDR_CALLBACK_TRANSFER_1_2400 12
#define SDR_CALLBACK_SPU2_2400 13

struct SdrCallbackPacket {
  volatile unsigned int events;
  volatile unsigned int cores;
  unsigned int reserved[14];
};

static struct SdrCallbackPacket callback_packet;
#if SDRDRV_SDK >= 2700
static struct SdrCallbackPacket callback_snapshot;
#endif
sceSifClientData sdr_callback_client;

void sdr_callback_loop(void) {
#if SDRDRV_SDK >= 2700
  int intr_state;
#endif
  for (;;) {
    if (callback_packet.events) {
#if SDRDRV_SDK >= 2700
    retry:
      CpuSuspendIntr(&intr_state);
      memcpy(&callback_snapshot, &callback_packet, sizeof(callback_packet));
      CpuResumeIntr(intr_state);
      sceSifCallRpc(&sdr_callback_client, 0, 0, &callback_snapshot, SDR_CALLBACK_PACKET_SIZE, 0, 0,
                    0, 0);
      CpuSuspendIntr(&intr_state);
      if (callback_packet.events != callback_snapshot.events) {
        callback_packet.events &= ~callback_snapshot.events;
        CpuResumeIntr(intr_state);
        // Send newly pending events before sleeping.
        goto retry;
      }
      callback_packet.events = 0;
      iCancelWakeupThread(0);
      CpuResumeIntr(intr_state);
#else
      sceSifCallRpc(&sdr_callback_client, 0, 0, &callback_packet, SDR_CALLBACK_PACKET_SIZE, 0, 0, 0,
                    0);
      callback_packet.events = 0;
#endif
    }
    SleepThread();
  }
}

int sdr_transfer0_callback(int channel, void* argument) {
#if SDRDRV_SDK >= 2700
  callback_packet.events |= SDR_CALLBACK_TRANSFER_0;
#else
  callback_packet.events = SDR_CALLBACK_TRANSFER_0_2400;
#endif
  iWakeupThread(sdr_callback_tid);
  return 1;
}

int sdr_transfer1_callback(int channel, void* argument) {
#if SDRDRV_SDK >= 2700
  callback_packet.events |= SDR_CALLBACK_TRANSFER_1;
#else
  callback_packet.events = SDR_CALLBACK_TRANSFER_1_2400;
#endif
  iWakeupThread(sdr_callback_tid);
  return 1;
}

int sdr_spu2_callback(int cores, void* argument) {
#if SDRDRV_SDK >= 2700
  callback_packet.events |= SDR_CALLBACK_SPU2;
#else
  callback_packet.events = SDR_CALLBACK_SPU2_2400;
#endif
  callback_packet.cores = cores;
  iWakeupThread(sdr_callback_tid);
  return 1;
}

int sdr_callback_thread(void) {
  int delay;
  callback_packet.events = 0;
  do {
    if (sceSifBindRpc(&sdr_callback_client, SDR_RPC_CALLBACK, 0) < 0) {
      printf("error \n");
      for (;;) {
      }
    }
    delay = 10000;
    while (delay--)
      ;
  } while (!sdr_callback_client.serve);
  sdr_callback_loop();
  return 0;
}
