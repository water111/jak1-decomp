#ifndef SCE_IOP_SDRDRV_INTERNAL_H
#define SCE_IOP_SDRDRV_INTERNAL_H

#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/sdrdrv/sdrdrv.h"
#include "sce/iop/sifcmd/sifrpc.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/thbase/thread.h"

#define SDR_DEFAULT_PRIORITY 24
#define SDR_MIN_PRIORITY 9
#define SDR_MAX_PRIORITY 123
#define SDR_PRIORITY_GUARD 8
#define SDR_INVALID_PRIORITY (-403)
#define SDR_THREAD_STACK_SIZE 0x800
#define SDR_RPC_COMMAND 0x80000701
#define SDR_RPC_CALLBACK 0x80000704
#define SDR_USER_COMMAND_COUNT 16
#define SDR_CALLBACK_PACKET_SIZE 64
#define SDR_COMMAND_MASK 0xfff0
#define SDR_CORE_MASK 0xf

extern int sdr_main_thread_priority;
extern int sdr_callback_thread_priority;
extern int sdr_main_tid;
extern volatile int sdr_callback_tid;
extern unsigned int sdr_command_buffer[16];
#if SDRDRV_SDK >= 2700
extern unsigned int sdr_batch_buffer[384];
#endif
extern sceSifClientData sdr_callback_client;

int sdr_rpc_thread(void);
void* sdr_dispatch(unsigned int command, void* data, int size);
int sdr_callback_thread(void);
int sdr_transfer0_callback(int channel, void* argument);
int sdr_transfer1_callback(int channel, void* argument);
int sdr_spu2_callback(int cores, void* argument);

#endif
