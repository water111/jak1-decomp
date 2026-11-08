#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/thevent/thevent.h"

extern volatile u_short* sd_spu_registers;

#if LIBSD_VERSION >= 0x0303
#define CORE_REG(core, offset) SPU2_REG(core, offset)
#else
#define CORE_REG(core, offset) sd_spu_registers[(core) * (SPU2_CORE_STRIDE / 2) + (offset) / 2]
#endif
#define DMA_ADDRESS(core) \
  (*(volatile u_int*)(SPU2_DMA_ADDRESS_BASE + (core) * SPU2_DMA_CORE_STRIDE))
#define DMA_CONTROL(core) \
  (*(volatile u_int*)(SPU2_DMA_CONTROL_BASE + (core) * SPU2_DMA_CORE_STRIDE))
#define DMA_WORDS(core) \
  (*(volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + (core) * SPU2_DMA_CORE_STRIDE))
#define DMA_BLOCKS(core) \
  (*(volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + 2 + (core) * SPU2_DMA_CORE_STRIDE))
#define NEXT_WORD(args) (*(u_int*)((args += 4) - 4))

void sd_init_transfer_state(void) {
  volatile int reset = 0;
  reset = 0;
  reset = 0;
  reset = 0;
}

int sd_reset(int mode) {
  u_int timeout;
  SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET) = 0;
  sd_delay(2);
  SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET) = SPU2_SPDIF_RESET;
  sd_delay(1);
  SPU2_REG(0, SPU2_MVOLL_OFFSET) = 0;
  SPU2_REG(0, SPU2_MVOLR_OFFSET) = 0;
  SPU2_REG(0, SPU2_MVOLL_OFFSET + SPU2_VOLUME_CORE_STRIDE) = 0;
  SPU2_REG(0, SPU2_MVOLR_OFFSET + SPU2_VOLUME_CORE_STRIDE) = 0;
  SPU2_ADMAS(0) = 0;
  SPU2_ADMAS(1) = 0;
  SPU2_ATTR(0) = 0;
  SPU2_ATTR(1) = 0;
  sd_delay(1);
  SPU2_ATTR(0) = SPU2_ATTR_ENABLE;
  SPU2_ATTR(1) = SPU2_ATTR_ENABLE;
  SPU2_REG(0, SPU2_MVOLL_OFFSET) = 0;
  SPU2_REG(0, SPU2_MVOLR_OFFSET) = 0;
  SPU2_REG(0, SPU2_MVOLL_OFFSET + SPU2_VOLUME_CORE_STRIDE) = 0;
  SPU2_REG(0, SPU2_MVOLR_OFFSET + SPU2_VOLUME_CORE_STRIDE) = 0;
  timeout = 0;
  while ((SPU2_STATUS(0) & SPU2_STATUS_RESET_MASK) || (SPU2_STATUS(1) & SPU2_STATUS_RESET_MASK)) {
    if (++timeout > SPU2_POLL_TIMEOUT)
      break;
    sd_delay(1);
  }
  SPU2_KEY_OFF_LOW(0) = 0xffff;
  SPU2_KEY_OFF_HIGH(0) = 0xff;
  SPU2_KEY_OFF_LOW(1) = 0xffff;
  SPU2_KEY_OFF_HIGH(1) = 0xff;
  SPU2_REG(0, SPU2_PMON_LOW_OFFSET) = 0;
  SPU2_REG(0, SPU2_PMON_HIGH_OFFSET) = 0;
  SPU2_REG(1, SPU2_PMON_LOW_OFFSET) = 0;
  SPU2_REG(1, SPU2_PMON_HIGH_OFFSET) = 0;
  SPU2_REG(0, SPU2_NON_LOW_OFFSET) = 0;
  SPU2_REG(0, SPU2_NON_HIGH_OFFSET) = 0;
  SPU2_REG(1, SPU2_NON_LOW_OFFSET) = 0;
  SPU2_REG(1, SPU2_NON_HIGH_OFFSET) = 0;
  return 0;
}

void sd_set_dma_write(int core) {
  *(volatile u_int*)(SPU2_DMA_DELAY_ADDRESS + core * SPU2_CORE_STRIDE) &= 0xf0ffffff;
}

void sd_set_dma_read(int core) {
  volatile u_int* delay = (volatile u_int*)(SPU2_DMA_DELAY_ADDRESS + core * SPU2_CORE_STRIDE);
  *delay = (*delay & 0xf0ffffff) | 0x2000000;
}

void sd_busy_wait(int unused) {
  volatile int i;
  volatile int value = 13;
  for (i = 0; i < 120; i++)
    value *= 13;
}

void sd_delay(int count) {
  int i;
  for (i = 0; i < count; i++)
    sd_busy_wait(1);
}

#if LIBSD_VERSION >= 0x0303
#define DMA_CORE_REG(offset) (((volatile u_short*)(SPU2_BASE + (offset)))[core_offset])
#define DMA_CORE_STATUS DMA_CORE_REG(SPU2_STATUS_OFFSET)
#else
#define DMA_CORE_REG(offset) CORE_REG(core, offset)
#define DMA_CORE_STATUS SPU2_STATUS(core)
#endif

int sd_dma_control(int command, ...) {
  int core = command >> SD_DMA_CORE_SHIFT;
  int core_offset = core * (SPU2_CORE_STRIDE / 2);
  int dma_word_offset = core * (SPU2_DMA_CORE_STRIDE / sizeof(u_int));
  int dma_half_offset = core * (SPU2_DMA_CORE_STRIDE / sizeof(u_short));
  volatile u_int* control = &((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_word_offset];
  u_int result = 0;
  volatile u_short status;
  u_short attr;
  char* args = __builtin_next_arg(command);
  u_int memory, size, blocks, first_buffer, first_address, buffer, address, timeout;
  int block_transfer;
#if LIBSD_VERSION >= 0x0303
  int intr_state, resume_state;
#endif
  switch (command & SD_DMA_COMMAND_MASK) {
    case SD_DMA_CHECK_BUSY:
      attr = DMA_CORE_REG(SPU2_ATTR_OFFSET);
      if ((attr & SPU2_ATTR_TRANSFER_MASK) || (*control & SPU2_DMA_START) ||
          ((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[core_offset])
        result = -1;
      break;
    case SD_DMA_SET_ADDRESS: {
#if LIBSD_VERSION >= 0x0303
      volatile u_short* low = (volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_LOW_OFFSET);
      volatile u_short* high = (volatile u_short*)SPU2_BASE;
#endif
      // Convert bytes to halfwords and align to 16 bytes. Keep the original temporary registers.
      register u_int raw_address asm("$2") =
          ((u_int*)(args = (char*)(((int)args + 3) & ~3) + 4))[-1];
#if LIBSD_VERSION >= 0x0303
      asm("ori %2,%2,%4\n\tsrl $3,%1,1\n\tli %1,-8\n\tand %0,$3,%1"
          : "=r"(memory), "+r"(raw_address), "+r"(high)
          : "r"(low), "i"(SPU2_TRANSFER_ADDR_HIGH_OFFSET)
          : "$3");
#else
      asm("srl $3,%1,1\n\tli %1,-8\n\tand %0,$3,%1"
          : "=r"(memory), "+r"(raw_address)
          :
          : "$3", "memory");
#endif
#if LIBSD_VERSION >= 0x0303
      low[core_offset] = memory;
      high[core_offset] = memory >> 16;
      status = high[core_offset];
#else
      DMA_CORE_REG(SPU2_TRANSFER_ADDR_LOW_OFFSET) = memory;
      DMA_CORE_REG(SPU2_TRANSFER_ADDR_HIGH_OFFSET) = memory >> 16;
      status = DMA_CORE_REG(SPU2_TRANSFER_ADDR_HIGH_OFFSET);
#endif
      break;
    }
    case SD_DMA_WRITE:
#if LIBSD_VERSION >= 0x0303
      CpuSuspendIntr(&intr_state);
#endif
      attr = DMA_CORE_REG(SPU2_ATTR_OFFSET);
#if LIBSD_VERSION >= 0x0303
      resume_state = intr_state;
#endif
      DMA_CORE_REG(SPU2_ATTR_OFFSET) = (attr & ~SPU2_ATTR_TRANSFER_MASK) | SPU2_ATTR_DMA_WRITE;
#if LIBSD_VERSION >= 0x0303
      CpuResumeIntr(resume_state);
#endif
      sd_set_dma_write(core);
      memory = ((u_int*)(args = (char*)(((int)args + 3) & ~3) + 4))[-1];
      size = ((u_int*)(args = (char*)(((int)args + 3) & ~3) + 4))[-1];
      blocks = (size / SPU2_DMA_BLOCK_BYTES) + ((size % SPU2_DMA_BLOCK_BYTES) ? 1 : 0);
      ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_word_offset] = memory;
      ((volatile u_short*)SPU2_DMA_BLOCK_SIZE_BASE)[dma_half_offset] = SPU2_DMA_BLOCK_WORDS;
      ((volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + 2))[dma_half_offset] = blocks;
      *control = SPU2_DMA_WRITE;
      status = *control;
      result = blocks * SPU2_DMA_BLOCK_BYTES;
      break;
    case SD_DMA_READ:
#if LIBSD_VERSION >= 0x0303
      CpuSuspendIntr(&intr_state);
#endif
      attr = DMA_CORE_REG(SPU2_ATTR_OFFSET);
#if LIBSD_VERSION >= 0x0303
      resume_state = intr_state;
#endif
      DMA_CORE_REG(SPU2_ATTR_OFFSET) = attr | SPU2_ATTR_DMA_READ;
#if LIBSD_VERSION >= 0x0303
      CpuResumeIntr(resume_state);
#endif
      sd_set_dma_read(core);
      memory = ((u_int*)(args = (char*)(((int)args + 3) & ~3) + 4))[-1];
      size = ((u_int*)(args = (char*)(((int)args + 3) & ~3) + 4))[-1];
      blocks = (size / SPU2_DMA_BLOCK_BYTES) + ((size % SPU2_DMA_BLOCK_BYTES) ? 1 : 0);
      ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_word_offset] = memory;
      ((volatile u_short*)SPU2_DMA_BLOCK_SIZE_BASE)[dma_half_offset] = SPU2_DMA_BLOCK_WORDS;
      ((volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + 2))[dma_half_offset] = blocks;
      *control = SPU2_DMA_READ;
      status = *control;
      result = blocks * SPU2_DMA_BLOCK_BYTES;
      break;
    case SD_DMA_STOP:
      result = 0;
      buffer = 0;
      address = 0;
      if (*control & SPU2_DMA_START) {
        first_buffer = sd_block_buffer[core];
        first_address = ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_word_offset];
        buffer = sd_block_buffer[core];
        address = ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_word_offset];
        if (first_buffer == buffer && first_address != address)
          address = first_address;
        *control &= ~SPU2_DMA_START;
        attr = DMA_CORE_REG(SPU2_ATTR_OFFSET);
        if (attr & SPU2_ATTR_TRANSFER_MASK) {
          timeout = 0;
          while (!(DMA_CORE_STATUS & SPU2_STATUS_DREQ)) {
            if (++timeout > SPU2_DREQ_TIMEOUT)
              break;
          }
        }
      }
      attr = DMA_CORE_REG(SPU2_ATTR_OFFSET);
      if (attr & SPU2_ATTR_TRANSFER_MASK) {
#if LIBSD_VERSION >= 0x0303
        CpuSuspendIntr(&intr_state);
        attr = DMA_CORE_REG(SPU2_ATTR_OFFSET);
        resume_state = intr_state;
        DMA_CORE_REG(SPU2_ATTR_OFFSET) = attr & ~SPU2_ATTR_TRANSFER_MASK;
        CpuResumeIntr(resume_state);
#else
        DMA_CORE_REG(SPU2_ATTR_OFFSET) = attr & ~SPU2_ATTR_TRANSFER_MASK;
#endif
        timeout = 0;
        while (DMA_CORE_REG(SPU2_ATTR_OFFSET) & SPU2_ATTR_TRANSFER_MASK) {
          if (++timeout > SPU2_POLL_TIMEOUT)
            break;
        }
      }
      block_transfer = 0;
      if (((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[core_offset] & 7) {
        block_transfer = 1;
        ((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[core_offset] = 0;
      }
      if (QueryIntrContext())
        iSetEventFlag(sd_transfer_events[core], SD_TRANSFER_EVENT_DONE);
      else
        SetEventFlag(sd_transfer_events[core], SD_TRANSFER_EVENT_DONE);
      sd_transfer_pending[core] = 0;
      sd_transfer_status[core] = 1;
#if LIBSD_VERSION >= 0x0303
      sd_effect_clear_handlers[core] = 0;
#endif
      status = sd_transfer_status[core];
      if (address) {
        result = address & 0xffffff;
        if (block_transfer)
          result |= buffer << 24;
      }
      break;
  }
  return result;
}

#define VOICE_REG(offset) (((volatile u_short*)(SPU2_BASE + (offset)))[core_offset])
#define CORE_ATTR(core) CORE_REG(core, SPU2_ATTR_OFFSET)

int sd_voice_write_io(void* memory, u_int size, int core) {
  u_short* source = memory;
  int core_offset = core * (SPU2_CORE_STRIDE / 2);
  volatile int status;
  u_short attr;
#if LIBSD_VERSION >= 0x0303
  int intr_state;
#endif
  while (size) {
    int count;
    int i;
    u_int timeout;
    if (size <= SPU2_DMA_BLOCK_BYTES)
      count = size;
    else
      count = SPU2_DMA_BLOCK_BYTES;
    for (i = 0; i < count; i += 2)
      ((volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_DATA_OFFSET))[core_offset] = *source++;
#if LIBSD_VERSION >= 0x0303
    CpuSuspendIntr(&intr_state);
#endif
    attr = ((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset];
    ((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset] =
        (attr & ~SPU2_ATTR_TRANSFER_MASK) | SPU2_ATTR_IO_WRITE;
#if LIBSD_VERSION >= 0x0303
    CpuResumeIntr(intr_state);
#endif
    timeout = 0;
    while (((volatile u_short*)(SPU2_BASE + SPU2_STATUS_OFFSET))[core_offset] &
           SPU2_STATUS_IO_BUSY) {
      if (++timeout > SPU2_POLL_TIMEOUT)
        break;
      sd_delay(1);
    }
    size -= count;
  }
#if LIBSD_VERSION >= 0x0303
  CpuSuspendIntr(&intr_state);
#endif
  attr = ((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset];
  ((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset] =
      attr & ~SPU2_ATTR_TRANSFER_MASK;
#if LIBSD_VERSION >= 0x0303
  CpuResumeIntr(intr_state);
#endif
  sd_transfer_status[core] = 1;
  status = sd_transfer_status[core];
  return 0;
}

#if LIBSD_VERSION >= 0x0303
#define INTERRUPT_ATTR (((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset])
#else
#define INTERRUPT_ATTR CORE_ATTR(core)
#endif

int sd_transfer_interrupt(void* argument) {
  SdTransferInterrupt* transfer = argument;
  u_int write;
  u_int core;
  u_int mode;
#if LIBSD_VERSION >= 0x0303
  int core_offset, dma_offset;
#endif
  if ((transfer->mode & SD_TRANSFER_DIRECTION_MASK) == SD_TRANSFER_DIRECTION_WRITE)
    write = 1;
  else if ((transfer->mode & SD_TRANSFER_DIRECTION_MASK) == SD_TRANSFER_DIRECTION_READ)
    write = 0;
  else
    return 1;
  mode = *(u_int*)argument;
  core = mode & SD_TRANSFER_CORE_MASK;
#if LIBSD_VERSION >= 0x0303
  core_offset = core * (SPU2_CORE_STRIDE / 2);
  dma_offset = core * (SPU2_DMA_CORE_STRIDE / sizeof(u_short));
#endif
  if ((mode & SD_TRANSFER_TYPE_MASK) == SD_TRANSFER_TYPE_VOICE) {
    int timeout = 0;
    u_int attr_timeout;
    while (!(SPU2_STATUS(core) & SPU2_STATUS_DREQ)) {
      if (++timeout > SPU2_DREQ_TIMEOUT)
        break;
    }
    INTERRUPT_ATTR &= ~SPU2_ATTR_TRANSFER_MASK;
    attr_timeout = 0;
    while (INTERRUPT_ATTR & SPU2_ATTR_TRANSFER_MASK) {
      if (++attr_timeout > SPU2_POLL_TIMEOUT)
        break;
    }
    if (!write)
      FlushDcache();
#if LIBSD_VERSION >= 0x0303
    if (sd_effect_clear_handlers[core]) {
      sd_effect_clear_handlers[core](core, 0);
      return 1;
    }
#endif
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
  } else if ((mode & SD_TRANSFER_TYPE_MASK) == SD_TRANSFER_TYPE_BLOCK) {
    volatile u_int status;
#if LIBSD_VERSION < 0x0303
    int dma_offset = core * (SPU2_DMA_CORE_STRIDE / sizeof(u_short));
#endif
    if (!(mode & SD_TRANSFER_LOOP)) {
      SPU2_ATTR(core) &= ~SPU2_ATTR_TRANSFER_MASK;
      SPU2_ADMAS(core) = 0;
      status = SPU2_ADMAS(core);
    } else {
      int buffer = sd_block_buffer[core];
      int size = sd_block_size[core];
      u_int address = (u_int)sd_block_address[core] + (1 - buffer) * size;
      ((volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + 2))[dma_offset] =
          sd_block_size[core] / SPU2_DMA_BLOCK_BYTES +
          (sd_block_size[core] % SPU2_DMA_BLOCK_BYTES > 0);
      sd_block_buffer[core] = 1 - buffer;
      ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_offset / 2] = address;
      ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2] = write | SPU2_DMA_READ;
      status = ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2];
    }
    if (!write)
      FlushDcache();
    if (sd_transfer_handlers[core])
      sd_transfer_handlers[core](core, transfer->argument);
    else if (sd_transfer_callbacks[core])
      ((int (*)(void))sd_transfer_callbacks[core])();
  }
  return 1;
}

u_int sd_block_status(u_int core) {
  int core_offset = core * (SPU2_CORE_STRIDE / 2);
  u_int first_buffer = sd_block_buffer[core];
  volatile u_int* dma_address = &DMA_ADDRESS(core);
  u_int first_address = *dma_address;
  u_int buffer = sd_block_buffer[core];
  u_int address = *dma_address;
  if (first_buffer == buffer && first_address != address)
    address = first_address;
  if (!(((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[core_offset] & 7))
    address = 0;
  return (buffer << 24) | (address & SPU2_DMA_ADDRESS_MASK);
}

int sd_block_restart(void* memory, u_int size, u_int core, u_int mode, void* start) {
  u_int offset;
  int remaining;
  volatile u_int status;
  int dma_offset;
#if LIBSD_VERSION >= 0x0303
  int intr_state;
#endif
  int core_offset;
  core &= SD_CORE_MASK;
  core_offset = core * (SPU2_CORE_STRIDE / 2);
  sd_block_address[core] = memory;
  sd_block_buffer[core] = 0;
  sd_block_size[core] = size;
  dma_offset = core * (SPU2_DMA_CORE_STRIDE / sizeof(u_short));
  if (!start) {
    start = memory;
    remaining = size;
  } else {
    offset = (u_int)start - (u_int)memory;
    if (offset >= size) {
      offset -= size;
      if (!(mode & SD_BLOCK_LOOP) || offset >= size)
        return SD_ERROR_INVALID_MODE;
      sd_block_buffer[core]++;
    }
    remaining = size - offset;
    if (remaining % SPU2_BLOCK_BUFFER_BYTES > 0) {
      remaining = (remaining / SPU2_BLOCK_BUFFER_BYTES + 1) * SPU2_BLOCK_BUFFER_BYTES;
      start = (char*)memory + (sd_block_buffer[core] * size + (size - remaining));
    }
  }
#if LIBSD_VERSION >= 0x0303
  CpuSuspendIntr(&intr_state);
#endif
  ((volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET))[core_offset] &= ~SPU2_ATTR_TRANSFER_MASK;
#if LIBSD_VERSION >= 0x0303
  CpuResumeIntr(intr_state);
#endif
  ((volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_HIGH_OFFSET))[core_offset] = 0;
  ((volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_LOW_OFFSET))[core_offset] = 0;
  ((volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET))[core_offset] = 1 << core;
  sd_set_dma_write(core);
  ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_offset / 2] = (u_int)start;
  ((volatile u_short*)SPU2_DMA_BLOCK_SIZE_BASE)[dma_offset] = SPU2_DMA_BLOCK_WORDS;
  ((volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + 2))[dma_offset] =
      remaining / SPU2_DMA_BLOCK_BYTES + (remaining % SPU2_DMA_BLOCK_BYTES > 0);
  ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2] = SPU2_DMA_WRITE;
  status = ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2];
  return size;
}

int sd_block_read(void* memory, int size, int core, u_int mode) {
  u_short attr;
  volatile u_int status;
  int dma_offset;
#if LIBSD_VERSION >= 0x0303
  int intr_state;
  int resume_state;
#endif
  u_int channel = core & SD_CORE_MASK;
  dma_offset = channel * (SPU2_DMA_CORE_STRIDE / sizeof(u_short));
  sd_block_address[channel] = memory;
  sd_block_buffer[channel] = 0;
  sd_block_size[channel] = size;
#if LIBSD_VERSION >= 0x0303
  CpuSuspendIntr(&intr_state);
#endif
#if LIBSD_VERSION >= 0x0303
  attr = SPU2_ATTR(channel);
  resume_state = intr_state;
  SPU2_ATTR(channel) = attr & ~SPU2_ATTR_TRANSFER_MASK;
#else
  attr = SPU2_ATTR(channel);
  SPU2_ATTR(channel) = attr & ~SPU2_ATTR_TRANSFER_MASK;
#endif
#if LIBSD_VERSION >= 0x0303
  CpuResumeIntr(resume_state);
#endif
  SPU2_TRANSFER_ADDR_HIGH(channel) = 0;
  SPU2_TRANSFER_ADDR_LOW(channel) = ((mode & SD_BLOCK_SOURCE_MASK) << 1) + SPU2_BLOCK_READ_BASE;
  SPU2_TRANSFER_MODE(channel) = (mode & SD_BLOCK_COUNT_MASK) >> 11;
  sd_delay(3);
  SPU2_ADMAS(channel) = SPU2_ADMAS_READ;
  sd_set_dma_read(channel);
  ((volatile u_int*)SPU2_DMA_ADDRESS_BASE)[dma_offset / 2] = (u_int)memory;
  ((volatile u_short*)SPU2_DMA_BLOCK_SIZE_BASE)[dma_offset] = SPU2_DMA_BLOCK_WORDS;
  ((volatile u_short*)(SPU2_DMA_BLOCK_SIZE_BASE + 2))[dma_offset] =
      sd_block_size[channel] / SPU2_DMA_BLOCK_BYTES +
      (sd_block_size[channel] % SPU2_DMA_BLOCK_BYTES > 0);
  ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2] = SPU2_DMA_READ;
  status = ((volatile u_int*)SPU2_DMA_CONTROL_BASE)[dma_offset / 2];
  return size;
}
