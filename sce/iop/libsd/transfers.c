#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thevent/thevent.h"

extern volatile u_short* sd_spu_registers;
extern volatile u_int* sd_dma_priority;
extern int sd_voice_addresses[2];

#define RESET_REG(offset) sd_spu_registers[sd_current_core * (SPU2_CORE_STRIDE / 2) + (offset) / 2]
#define VOLUME_REG(offset) \
  sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) + (offset) / 2]

int sd_reset(int mode) {
  int i;
  SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET) = 0;
  sd_delay(2);
  SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET) = SPU2_SPDIF_RESET;
  sd_delay(1);
  *sd_dma_priority |= SPU2_DMA_PRIORITY_ENABLE;
  VOLUME_REG(SPU2_MVOLL_OFFSET) = 0;
  VOLUME_REG(SPU2_MVOLR_OFFSET) = 0;
  VOLUME_REG(SPU2_CORE_STRIDE + SPU2_MVOLL_OFFSET) = 0;
  VOLUME_REG(SPU2_CORE_STRIDE + SPU2_MVOLR_OFFSET) = 0;
  for (sd_current_core = 0; (u_int)sd_current_core < 2; sd_current_core++) {
    u_int timeout;
    sd_transfer_active[sd_current_core] = 0;
    sd_voice_addresses[sd_current_core] = 0;
    ((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[sd_current_core * (SPU2_CORE_STRIDE / 2)] =
        0;
    RESET_REG(SPU2_ATTR_OFFSET) = 0;
    sd_delay(1);
    RESET_REG(SPU2_ATTR_OFFSET) = SPU2_ATTR_ENABLE;
    VOLUME_REG(SPU2_MVOLL_OFFSET) = 0;
    VOLUME_REG(SPU2_MVOLR_OFFSET) = 0;
    timeout = 0;
    while (RESET_REG(SPU2_STATUS_OFFSET) & SPU2_STATUS_RESET_MASK) {
      if (++timeout > SPU2_POLL_TIMEOUT) {
        printf("SPU:T/O [%s]\n", "wait (reset)");
        break;
      }
      sd_delay(1);
    }
    RESET_REG(SPU2_KEY_OFF_LOW_OFFSET) = 0xffff;
    RESET_REG(SPU2_KEY_OFF_HIGH_OFFSET) = 0xffff;
  }
  for (i = 0; i < 2; i++) {
    sd_current_core = 1;
    RESET_REG(SPU2_PMON_LOW_OFFSET) = 0;
    RESET_REG(SPU2_PMON_HIGH_OFFSET) = 0;
    RESET_REG(SPU2_NON_LOW_OFFSET) = 0;
    RESET_REG(SPU2_NON_HIGH_OFFSET) = 0;
  }
  sd_current_core = 0;
  return 0;
}

extern volatile u_short* sd_spu_registers;
extern volatile u_int* sd_dma_delay;

void sd_set_effect_register(int reg, int value) {
  volatile u_short* address =
      (volatile u_short*)((sd_current_core * (SPU2_CORE_STRIDE / 2) + reg) * 2 +
                          (u_int)sd_spu_registers);
  u_int shifted = value << 2;
  address[0] = shifted >> 16;
  address[1] = shifted;
}

void sd_set_address(int reg, u_int value, int bytes) {
  volatile u_short* address;
  if (!bytes) {
    u_int shifted;
    volatile u_short* address =
        (volatile u_short*)((sd_current_core * (SPU2_CORE_STRIDE / 2) + reg) * 2 +
                            (u_int)sd_spu_registers);
    shifted = value << 2;
    address[0] = shifted >> 16;
    address[1] = shifted;
  } else {
    volatile u_short* address =
        (volatile u_short*)((sd_current_core * (SPU2_CORE_STRIDE / 2) + reg) * 2 +
                            (u_int)sd_spu_registers);
    address[0] = value >> 17;
    address[1] = value >> 1;
  }
}

void sd_set_dma_write(int core) {
  volatile u_int* delay = (volatile u_int*)(core * 0x400 + (u_int)sd_dma_delay);
  *delay = (*delay & SPU2_DMA_DELAY_PRESERVE_MASK) | SPU2_DMA_WRITE_DELAY;
}

void sd_set_dma_read(int core) {
  volatile u_int* delay = (volatile u_int*)(core * 0x400 + (u_int)sd_dma_delay);
  *delay = (*delay & SPU2_DMA_DELAY_PRESERVE_MASK) | SPU2_DMA_READ_DELAY;
}

void sd_busy_wait(int unused) {
  volatile int i;
  volatile int value = 13;
  for (i = 0; i < 120; i++)
    value *= 13;
}

void sd_delay(int count) {
  int i;
  if (QueryIntrContext()) {
    for (i = 0; i < count; i++)
      sd_busy_wait(1);
  } else {
    DelayThread(count * 21);
  }
}

extern volatile u_short* sd_spu_registers;
extern volatile u_int* sd_dma_address_register;
extern volatile u_int* sd_dma_size_register;
extern volatile u_int* sd_dma_control_register;
extern int sd_voice_addresses[2];
extern int sd_dma_read_mode;
extern void* volatile sd_dma_memory;
extern int sd_dma_blocks;

#define CORE_REG(core, offset)                                                 \
  (*(volatile u_short*)(((core) * (SPU2_CORE_STRIDE / 2) + (offset) / 2) * 2 + \
                        (u_int)sd_spu_registers))
#define DMA_REG(reg, offset) ((reg)[offset])
#define NEXT_WORD(cursor) (*(u_int*)((cursor = (char*)(((u_int)cursor + 3) & ~3) + 4) - 4))

static inline void sd_store(int* p, u_int x) {
  *p = x;
}

int sd_dma_control(int command, ...) {
  int argument;
  int result = 0;
  int core = command >> SD_DMA_CORE_SHIFT;
  int dma_offset = core * 0x110;
  char* args = __builtin_next_arg(command);
  u_short attr;
  switch (command & SD_DMA_COMMAND_MASK) {
    case SD_DMA_CHECK_BUSY:
      attr = CORE_REG(core, SPU2_ATTR_OFFSET);
      if ((attr & SPU2_ATTR_TRANSFER_MASK) ||
          (DMA_REG(sd_dma_control_register, dma_offset) & SPU2_DMA_START))
        result = -1;
      break;
    case SD_DMA_SET_ADDRESS:
      argument = NEXT_WORD(args);
      sd_store(&sd_voice_addresses[core], argument);
      CORE_REG(core, SPU2_TRANSFER_ADDR_LOW_OFFSET) = (u_short)sd_voice_addresses[core];
      *(volatile short*)&CORE_REG(core, SPU2_TRANSFER_ADDR_HIGH_OFFSET) =
          sd_voice_addresses[core] >> 16;
      break;
    case SD_DMA_PREPARE_WRITE:
      attr = CORE_REG(core, SPU2_ATTR_OFFSET);
      sd_dma_read_mode = 0;
      result = -1;
      if (!(attr & SPU2_ATTR_TRANSFER_MASK) &&
          !(DMA_REG(sd_dma_control_register, dma_offset) & SPU2_DMA_START)) {
        result = 0;
        CORE_REG(core, SPU2_ATTR_OFFSET) = (attr & ~SPU2_ATTR_TRANSFER_MASK) | SPU2_ATTR_DMA_WRITE;
      }
      break;
    case SD_DMA_PREPARE_READ:
      attr = CORE_REG(core, SPU2_ATTR_OFFSET);
      sd_dma_read_mode = 1;
      result = -1;
      if (!(attr & SPU2_ATTR_TRANSFER_MASK) &&
          !(DMA_REG(sd_dma_control_register, dma_offset) & SPU2_DMA_START)) {
        result = 0;
        CORE_REG(core, SPU2_ATTR_OFFSET) = attr | SPU2_ATTR_DMA_READ;
      }
      break;
    case SD_DMA_START: {
      int dma_offset;
      u_int control;
      if (sd_dma_read_mode == 1)
        sd_set_dma_read(core);
      else
        sd_set_dma_write(core);
      argument = NEXT_WORD(args);
      sd_dma_memory = (void*)argument;
      argument = NEXT_WORD(args);
      result = ((u_int)argument >> 6) + (((u_int)argument % SPU2_DMA_BLOCK_BYTES) > 0);
      sd_dma_blocks = result;
      dma_offset = core * 0x110;
      DMA_REG(sd_dma_address_register, dma_offset) = (u_int)sd_dma_memory;
      DMA_REG(sd_dma_size_register, dma_offset) = (result << 16) | SPU2_DMA_BLOCK_WORDS;
      control = SPU2_DMA_WRITE;
      if (sd_dma_read_mode == 1)
        control = SPU2_DMA_READ;
      DMA_REG(sd_dma_control_register, dma_offset) = control;
      result *= SPU2_DMA_BLOCK_BYTES;
      break;
    }
  }
  return result;
}

extern volatile u_short* sd_spu_registers;
extern int sd_voice_addresses[2];
int sd_dma_control(int command, ...);

#define VOICE_REG(offset) (((volatile u_short*)(SPU2_BASE + (offset)))[core_offset])

int sd_voice_write_io(void* memory, u_int size, int core) {
  u_short* source = memory;
  int core_offset;
  u_short attr;
  if (sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_CHECK_BUSY))
    return -1;
  sd_spu_registers[core * (SPU2_CORE_STRIDE / 2) + SPU2_TRANSFER_ADDR_LOW_OFFSET / 2] =
      (u_short)sd_voice_addresses[core];
  sd_spu_registers[core * (SPU2_CORE_STRIDE / 2) + SPU2_TRANSFER_ADDR_HIGH_OFFSET / 2] =
      sd_voice_addresses[core] >> 16;
  core_offset = core * (SPU2_CORE_STRIDE / 2);
  while (size) {
    int count;
    int i;
    u_int timeout;
    if (size <= SPU2_DMA_BLOCK_BYTES)
      count = size;
    else
      count = SPU2_DMA_BLOCK_BYTES;
    for (i = 0; i < count; i += 2)
      VOICE_REG(SPU2_TRANSFER_DATA_OFFSET) = *source++;
    attr = VOICE_REG(SPU2_ATTR_OFFSET);
    VOICE_REG(SPU2_ATTR_OFFSET) = (attr & ~SPU2_ATTR_TRANSFER_MASK) | SPU2_ATTR_IO_WRITE;
    timeout = 0;
    while (VOICE_REG(SPU2_STATUS_OFFSET) & SPU2_STATUS_IO_BUSY) {
      if (++timeout > SPU2_POLL_TIMEOUT) {
        printf("SPU:T/O [%s]\n", "wait (wrdy H -> L)");
        break;
      }
      sd_delay(1);
    }
    size -= count;
  }
  {
    attr = VOICE_REG(SPU2_ATTR_OFFSET);
    VOICE_REG(SPU2_ATTR_OFFSET) = attr & ~SPU2_ATTR_TRANSFER_MASK;
  }
  return 0;
}

extern volatile u_short* sd_spu_registers;
#define CORE_ATTR(core) sd_spu_registers[(core) * (SPU2_CORE_STRIDE / 2) + SPU2_ATTR_OFFSET / 2]

int sd_transfer_interrupt(void* argument) {
  SdTransferInterrupt* transfer = argument;
  u_int mode = transfer->mode;
  int write = 1;
  u_int core = mode & SD_TRANSFER_CORE_MASK;
  if (mode & SD_TRANSFER_READ)
    write = 0;
  if (!(mode & SD_TRANSFER_BLOCK)) {
    int timeout = 0;
    u_int attr_timeout;
    while (!(SPU2_STATUS(core) & SPU2_STATUS_DREQ)) {
      if (++timeout > SPU2_DREQ_TIMEOUT) {
        printf("SPU:T/O [%s]\n", "wait (SPU2_STATX_DREQ)");
        break;
      }
    }
    CORE_ATTR(core) &= ~SPU2_ATTR_TRANSFER_MASK;
    attr_timeout = 0;
    while (CORE_ATTR(core) & SPU2_ATTR_TRANSFER_MASK) {
      if (++attr_timeout > SPU2_POLL_TIMEOUT)
        break;
    }
    iSetEventFlag(sd_transfer_events[core], SD_TRANSFER_EVENT_DONE);
    if (sd_transfer_handlers[core]) {
      sd_transfer_status[core] = 1;
      sd_transfer_handlers[core](core, transfer->argument);
    } else if (sd_transfer_callbacks[core]) {
      sd_transfer_status[core] = 1;
      ((int (*)(void))sd_transfer_callbacks[core])();
    } else {
      sd_transfer_pending[core] = 1;
    }
  } else {
    int dma_offset = core * 0x220;
    if (!(mode & SD_TRANSFER_LOOP)) {
      SPU2_ATTR(core) &= ~SPU2_ATTR_TRANSFER_MASK;
      SPU2_ADMAS(core) = 0;
    } else {
      sd_block_buffer[core] = 1 - sd_block_buffer[core];
      ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_offset / 2] =
          (u_int)sd_block_address[core] + sd_block_buffer[core] * sd_block_size[core];
      SPU2_DMA_BLOCK_COUNT(core) = sd_block_size[core] / SPU2_DMA_BLOCK_BYTES +
                                   ((sd_block_size[core] % SPU2_DMA_BLOCK_BYTES) > 0);
      ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2] = write | SPU2_DMA_READ;
    }
    if (sd_transfer_handlers[core])
      sd_transfer_handlers[core](core, transfer->argument);
    else if (sd_transfer_callbacks[core])
      ((int (*)(void))sd_transfer_callbacks[core])();
  }
  if (!write)
    FlushDcache();
  return 1;
}

u_int sd_stop_transfer(u_int core) {
  int dma_offset;
  u_int address;
  u_int result;
  u_int channel = core & SD_CORE_MASK;
  dma_offset = channel * 0x220;
  if ((SPU2_ADMAS(channel) >> channel) & 1) {
    address = SPU2_DMA_ADDRESS(channel);
    ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2] &= ~SPU2_DMA_START;
    SPU2_ATTR(channel) &= ~SPU2_ATTR_TRANSFER_MASK;
    SPU2_ADMAS(channel) = 0;
    result = (sd_block_buffer[channel] << 24) | (address & SPU2_DMA_ADDRESS_MASK);
  } else {
    result = -1;
  }
  return result;
}

u_int sd_block_status(u_int core) {
  u_int address = 0;
  if ((SPU2_ADMAS(core) >> core) & 1)
    address = SPU2_DMA_ADDRESS(core);
  return (sd_block_buffer[core] << 24) | (address & SPU2_DMA_ADDRESS_MASK);
}

int sd_block_restart(void* memory, u_int size, u_int core, u_int mode, void* start) {
  u_int offset;
  int remaining;
  u_short core_offset;
  core &= SD_CORE_MASK;
  core_offset = core * (SPU2_CORE_STRIDE / 2);
  if (!start)
    start = memory;
  offset = (u_int)start - (u_int)memory;
  sd_block_address[core] = memory;
  sd_block_buffer[core] = 0;
  sd_block_size[core] = size;
  if (offset >= size) {
    if (!(mode & SD_BLOCK_LOOP))
      return -1;
    offset -= size;
    sd_block_buffer[core]++;
  }
  remaining = size - offset;
  if (remaining % SPU2_BLOCK_BUFFER_BYTES > 0) {
    remaining = (remaining / SPU2_BLOCK_BUFFER_BYTES + 1) * SPU2_BLOCK_BUFFER_BYTES;
    start = (char*)memory + (sd_block_buffer[core] * size + (size - remaining));
  }
  ((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset] &= ~SPU2_ATTR_TRANSFER_MASK;
  ((volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_HIGH_OFFSET))[core_offset] = 0;
  ((volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_LOW_OFFSET))[core_offset] = 0;
  ((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[core_offset] = 1 << core;
  sd_set_dma_write(core);
  SPU2_DMA_ADDRESS(core) = (u_int)start;
  SPU2_DMA_BLOCK_SIZE(core) = SPU2_DMA_BLOCK_WORDS;
  SPU2_DMA_BLOCK_COUNT(core) =
      remaining / SPU2_DMA_BLOCK_BYTES + (remaining % SPU2_DMA_BLOCK_BYTES > 0);
  SPU2_DMA_CONTROL(core) = SPU2_DMA_WRITE;
  return size;
}

int sd_block_write(void* memory, int size, u_int core, u_int mode) {
  u_short dma_offset;
  u_short attr;
  core &= SD_CORE_MASK;
  attr = SPU2_ATTR(core);
  dma_offset = core * 0x220;
  if ((attr & SPU2_ATTR_TRANSFER_MASK) ||
      (SPU2_DMA_WORD_AT(SPU2_DMA_CONTROL_BASE, dma_offset) & SPU2_DMA_START))
    return -1;
  sd_block_address[core] = memory;
  sd_block_buffer[core] = 0;
  sd_block_size[core] = size;
  attr = SPU2_ATTR(core);
  SPU2_ATTR(core) = attr & ~SPU2_ATTR_TRANSFER_MASK;
  SPU2_TRANSFER_ADDR_HIGH(core) = 0;
  SPU2_TRANSFER_ADDR_LOW(core) = 0;
  SPU2_ADMAS(core) = 1 << core;
  sd_set_dma_write(core);
  SPU2_DMA_WORD_AT(SPU2_DMA_ADDRESS_BASE, dma_offset) = (u_int)memory;
  SPU2_DMA_BLOCK_SIZE(core) = SPU2_DMA_BLOCK_WORDS;
  SPU2_DMA_BLOCK_COUNT(core) =
      sd_block_size[core] / SPU2_DMA_BLOCK_BYTES + (sd_block_size[core] % SPU2_DMA_BLOCK_BYTES > 0);
  SPU2_DMA_WORD_AT(SPU2_DMA_CONTROL_BASE, dma_offset) = SPU2_DMA_WRITE;
  return size;
}

int sd_block_read(void* memory, int size, int core, u_int mode) {
  u_short dma_offset;
  u_short attr;
  u_int channel = core & SD_CORE_MASK;
  attr = SPU2_ATTR(channel);
  dma_offset = channel * 0x220;
  if ((attr & SPU2_ATTR_TRANSFER_MASK) ||
      (SPU2_DMA_WORD_AT(SPU2_DMA_CONTROL_BASE, dma_offset) & SPU2_DMA_START))
    return -1;
  sd_block_address[channel] = memory;
  sd_block_buffer[channel] = 0;
  sd_block_size[channel] = size;
  attr = SPU2_ATTR(channel);
  SPU2_ATTR(channel) = attr & ~SPU2_ATTR_TRANSFER_MASK;
  SPU2_TRANSFER_ADDR_HIGH(channel) = 0;
  SPU2_TRANSFER_ADDR_LOW(channel) = ((mode & SD_BLOCK_SOURCE_MASK) << 1) + SPU2_BLOCK_READ_BASE;
  SPU2_TRANSFER_MODE(channel) = (mode & SD_BLOCK_COUNT_MASK) >> 11;
  sd_delay(3);
  SPU2_ADMAS(channel) = SPU2_ADMAS_READ;
  sd_set_dma_read(channel);
  SPU2_DMA_WORD_AT(SPU2_DMA_ADDRESS_BASE, dma_offset) = (u_int)memory;
  SPU2_DMA_BLOCK_SIZE(channel) = SPU2_DMA_BLOCK_WORDS;
  SPU2_DMA_BLOCK_COUNT(channel) = sd_block_size[channel] / SPU2_DMA_BLOCK_BYTES +
                                  (sd_block_size[channel] % SPU2_DMA_BLOCK_BYTES > 0);
  SPU2_DMA_WORD_AT(SPU2_DMA_CONTROL_BASE, dma_offset) = SPU2_DMA_READ;
  return size;
}

extern int sd_voice_addresses[2];
int sd_dma_control(int command, ...);
int sd_voice_write_io(void* memory, u_int size, int core);

int sd_voice_write(int core, void* memory, u_int size) {
  if (!sd_transfer_active[core]) {
    int command = core << SD_DMA_CORE_SHIFT;
    if (sd_dma_control(command | SD_DMA_CHECK_BUSY))
      return -1;
    sd_dma_control(command | SD_DMA_SET_ADDRESS, sd_voice_addresses[core]);
    sd_dma_control(command | SD_DMA_PREPARE_WRITE);
    size = sd_dma_control(command | SD_DMA_START, memory, size);
  } else {
    if (sd_voice_write_io(memory, size, core))
      return -1;
  }
  return size;
}

int sd_voice_read(int core, void* memory, int size) {
  int result;
  int command = core << SD_DMA_CORE_SHIFT;
  if (!sd_dma_control(command | SD_DMA_CHECK_BUSY)) {
    sd_dma_control(command | SD_DMA_SET_ADDRESS, sd_voice_addresses[core]);
    sd_dma_control(command | SD_DMA_PREPARE_READ);
    sd_dma_control(command | SD_DMA_START, memory, size);
    result = size;
  } else {
    result = -1;
  }
  return result;
}
