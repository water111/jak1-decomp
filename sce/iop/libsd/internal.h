#ifndef SCE_IOP_LIBSD_INTERNAL_H
#define SCE_IOP_LIBSD_INTERNAL_H

#include "sce/iop/libsd/libsd.h"

#define SD_P_CORE_ATTR 0x2300
#define SD_CORE_MASK 1
#define SD_CORE_ATTR_MASK 0xe
#define SD_TRANS_MODE_MASK 3
#define SD_TRANSFER_EVENT_DONE 1
#define SD_TRANSFER_BLOCK 0x100
#define SD_TRANSFER_READ 0x200
#define SD_TRANSFER_LOOP 0x1000
#define SD_TRANSFER_CORE_MASK 0xff
#define SD_EFFECT_CLEAR_BUFFER_SIZE 1024
#define SD_EFFECT_ADDRESS_UNIT 8
#define SD_EFFECT_DEFAULT_DELAY_FEEDBACK 128
#define SD_EFFECT_FEEDBACK_SCALE 0x102
#define SD_EFFECT_DELAY_LEFT_SCALE 64
#define SD_EFFECT_DELAY_RIGHT_SCALE 32

#define SD_BWRITE_IOP 5
#define SD_BWRITE_EE 6
#define SD_BRETURN_EE 7
#define SD_SPDIF_OUTPUT_MASK 0xf
#define SD_SPDIF_MEDIA_MASK 0xf00

#define SD_SPDIF_MEDIA_ZERO 0x400
#define SD_BLOCK_SOURCE_MASK 0xf00
#define SD_BLOCK_COUNT_MASK 0xf000
#define SD_DMA_PREPARE_READ 0
#define SD_DMA_PREPARE_WRITE 1
#define SD_DMA_SET_ADDRESS 2
#define SD_DMA_START 3
#define SD_DMA_CHECK_BUSY 4
#define SD_DMA_COMMAND_MASK 0xf
#define SD_DMA_CORE_SHIFT 4

extern sceSdCallback volatile sd_transfer_callbacks[2];
extern sceSdTransIntrHandler volatile sd_transfer_handlers[2];
extern sceSdCallback sd_irq_callback;
extern sceSdSpu2IntrHandler volatile sd_spu2_handler;
extern void* sd_spu2_argument;

typedef struct {
  u_int mode;
  void* argument;
} SdTransferInterrupt;

extern SdTransferInterrupt sd_transfer_interrupts[2];

extern SdEffectAttr sd_effect_attrs[2];
extern volatile int sd_transfer_status[2];
extern int sd_transfer_pending[2];
extern volatile int sd_transfer_events[2];
extern int sd_transfer_active[2];
extern u_int sd_effect_addresses[2];
extern int sd_effect_sizes[];
extern int sd_current_core;
extern int sd_initialized;

int sd_reset(int mode);
u_int sd_get_effect_end(int core);
void sd_set_address(int reg, u_int value, int flag);
void sd_init_hardware(void);
void sd_init_core_volume(int mode);
void sd_init_voices(void);
int sd_transfer_interrupt(void* argument);
u_int sd_stop_transfer(u_int core);

extern volatile int sd_block_buffer[2];
extern void* sd_block_address[2];
extern volatile int sd_block_size[2];
void sd_set_dma_read(int core);
void sd_set_dma_write(int core);
void sd_delay(int count);

#define SD_EFFECT_UPDATE_DAPF1 0x1
#define SD_EFFECT_UPDATE_DAPF2 0x2
#define SD_EFFECT_UPDATE_VIIR 0x4
#define SD_EFFECT_UPDATE_VCOMB1 0x8
#define SD_EFFECT_UPDATE_VCOMB2 0x10
#define SD_EFFECT_UPDATE_VCOMB3 0x20
#define SD_EFFECT_UPDATE_VCOMB4 0x40
#define SD_EFFECT_UPDATE_VWALL 0x80
#define SD_EFFECT_UPDATE_VAPF1 0x100
#define SD_EFFECT_UPDATE_VAPF2 0x200
#define SD_EFFECT_UPDATE_MLSAME 0x400
#define SD_EFFECT_UPDATE_MRSAME 0x800
#define SD_EFFECT_UPDATE_MLCOMB1 0x1000
#define SD_EFFECT_UPDATE_MRCOMB1 0x2000
#define SD_EFFECT_UPDATE_MLCOMB2 0x4000
#define SD_EFFECT_UPDATE_MRCOMB2 0x8000
#define SD_EFFECT_UPDATE_DLSAME 0x10000
#define SD_EFFECT_UPDATE_DRSAME 0x20000
#define SD_EFFECT_UPDATE_MLDIFF 0x40000
#define SD_EFFECT_UPDATE_MRDIFF 0x80000
#define SD_EFFECT_UPDATE_MLCOMB3 0x100000
#define SD_EFFECT_UPDATE_MRCOMB3 0x200000
#define SD_EFFECT_UPDATE_MLCOMB4 0x400000
#define SD_EFFECT_UPDATE_MRCOMB4 0x800000
#define SD_EFFECT_UPDATE_DLDIFF 0x1000000
#define SD_EFFECT_UPDATE_DRDIFF 0x2000000
#define SD_EFFECT_UPDATE_MLAPF1 0x4000000
#define SD_EFFECT_UPDATE_MRAPF1 0x8000000
#define SD_EFFECT_UPDATE_MLAPF2 0x10000000
#define SD_EFFECT_UPDATE_MRAPF2 0x20000000
#define SD_EFFECT_UPDATE_VLIN 0x40000000
#define SD_EFFECT_UPDATE_VRIN 0x80000000

typedef struct {
  u_int mask;
  u_short dAPF1;
  u_short dAPF2;
  u_short vIIR;
  u_short vCOMB1;
  u_short vCOMB2;
  u_short vCOMB3;
  u_short vCOMB4;
  u_short vWALL;
  u_short vAPF1;
  u_short vAPF2;
  u_short mLSAME;
  u_short mRSAME;
  u_short mLCOMB1;
  u_short mRCOMB1;
  u_short mLCOMB2;
  u_short mRCOMB2;
  u_short dLSAME;
  u_short dRSAME;
  u_short mLDIFF;
  u_short mRDIFF;
  u_short mLCOMB3;
  u_short mRCOMB3;
  u_short mLCOMB4;
  u_short mRCOMB4;
  u_short dLDIFF;
  u_short dRDIFF;
  u_short mLAPF1;
  u_short mRAPF1;
  u_short mLAPF2;
  u_short mRAPF2;
  u_short vLIN;
  u_short vRIN;
} SdEffectRegisters;

void sd_set_effect_register(int reg, int value);
void sd_set_effect_registers(SdEffectRegisters* registers);

int sd_block_write(void* memory, int size, u_int core, u_int mode);
int sd_block_read(void* memory, int size, int core, u_int mode);
int sd_block_restart(void* memory, u_int size, u_int core, u_int mode, void* start);
u_int sd_block_status(u_int core);
int sd_voice_write(int core, void* memory, u_int size);
int sd_voice_read(int core, void* memory, int size);

#endif
