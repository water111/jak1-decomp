#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"

extern u_char sd_effect_clear_buffer[SD_EFFECT_CLEAR_BUFFER_SIZE];
#if LIBSD_VERSION >= 0x0303
// Knowing this buffer exceeds the small-data limit lets the assembler fill the call delay slot.
asm(".extern sd_effect_clear_buffer,1024");
#endif

#if LIBSD_VERSION >= 0x0301
int sceSdClearEffectWorkArea(int core, int channel, u_int effect) {
  int more;
  sceSdCallback volatile callback;
  sceSdTransIntrHandler volatile handler;
  u_int size, end, address, aligned_address, count;
  int region_size;
  int result;
  effect &= 0xff;
  if (effect >= SD_REV_MODE_MAX)
    return SD_ERROR_INVALID_MODE;
  if (!effect)
    return 0;
  if (sd_dma_control((channel << SD_DMA_CORE_SHIFT) | SD_DMA_CHECK_BUSY))
    return SD_ERROR_TRANSFER_BUSY;
  if (sd_transfer_status[channel] != 1)
    return SD_ERROR_TRANSFER_PENDING;
  if (QueryIntrContext())
    return SD_ERROR_INTERRUPT_CONTEXT;
  size = sd_effect_sizes[effect] * SD_EFFECT_ADDRESS_UNIT;
  end = sd_get_effect_end(core);
  aligned_address = 0;
  region_size = size - 1;
  address = (end - region_size) >> 1;
  if (size & 63) {
    size = (size >> 6) << 6;
    end = sd_get_effect_end(core);
    region_size = size - 1;
    aligned_address = (end - region_size) >> 1;
  }
  handler = sd_transfer_handlers[channel];
  callback = sd_transfer_callbacks[channel];
  sd_transfer_handlers[channel] = 0;
  sd_transfer_callbacks[channel] = 0;
  if (aligned_address) {
    result =
        sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, sd_effect_clear_buffer, address << 1, 64);
    if (result < 0)
      goto done;
    result = sceSdVoiceTransStatus(channel, SD_TRANS_STATUS_WAIT);
    if (result < 0)
      goto done;
    address = aligned_address;
  }
  more = 1;
  while (more) {
    count = SD_EFFECT_CLEAR_BUFFER_SIZE;
    if (size <= SD_EFFECT_CLEAR_BUFFER_SIZE) {
      count = size;
      more = 0;
    }
    result =
        sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, sd_effect_clear_buffer, address << 1, count);
    if (result < 0)
      goto done;
    result = sceSdVoiceTransStatus(channel, SD_TRANS_STATUS_WAIT);
    if (result < 0)
      goto done;
    size -= SD_EFFECT_CLEAR_BUFFER_SIZE;
    address += SD_EFFECT_CLEAR_BUFFER_SIZE / 2;
  }
  result = 0;
done:
  sd_transfer_handlers[channel] = handler;
  sd_transfer_callbacks[channel] = callback;
  sd_init_transfer_state();
  return result;
}
#if LIBSD_VERSION >= 0x0303
int sd_continue_effect_clear(int core, void* argument) {
  sd_effect_clear_index[core]++;
  if (sd_effect_clear_count[core] - 1 <= sd_effect_clear_index[core])
    sd_effect_clear_handlers[core] = 0;
  sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_SET_ADDRESS,
                 sd_effect_clear_segments[core][sd_effect_clear_index[core]].address);
  sd_dma_control((core << SD_DMA_CORE_SHIFT) | SD_DMA_WRITE, sd_effect_clear_buffer,
                 sd_effect_clear_segments[core][sd_effect_clear_index[core]].size);
  return 0;
}

int sceSdCleanEffectWorkArea(int core, int channel, int effect_mode) {
  u_int effect = effect_mode & 0xff;
  int result;
  u_int size;
  int remaining;
  u_int end;
  u_int address, next_address;
  SdEffectClearSegment* segment;
  if (effect >= SD_REV_MODE_MAX)
    return SD_ERROR_INVALID_MODE;
  if (!effect)
    return 0;
  if (sd_dma_control((channel << SD_DMA_CORE_SHIFT) | SD_DMA_CHECK_BUSY))
    return SD_ERROR_TRANSFER_BUSY;
  if (sd_transfer_status[channel] != 1)
    return SD_ERROR_TRANSFER_PENDING;
  size = sd_effect_sizes[effect] * SD_EFFECT_ADDRESS_UNIT;
  end = sd_get_effect_end(core);
  remaining = size - 1;
  address = end - remaining;
  if (size & 63) {
    size = (size >> 6) << 6;
    result = sceSdVoiceTrans(channel, SD_TRANS_BY_IO, sd_effect_clear_buffer, address, 64);
    if (result < 0)
      goto done;
    end = sd_get_effect_end(core);
    remaining = size - 1;
    address = end - remaining;
  }
  segment = sd_effect_clear_segments[channel];
  next_address = address + SD_EFFECT_CLEAR_BUFFER_SIZE;
  remaining = size - SD_EFFECT_CLEAR_BUFFER_SIZE;
  sd_effect_clear_count[channel] = 0;
  while (remaining > SD_EFFECT_CLEAR_BUFFER_SIZE) {
    segment->address = next_address;
    segment->size = SD_EFFECT_CLEAR_BUFFER_SIZE;
    segment++;
    next_address += SD_EFFECT_CLEAR_BUFFER_SIZE;
    remaining -= SD_EFFECT_CLEAR_BUFFER_SIZE;
    sd_effect_clear_count[channel]++;
  }
  segment->address = next_address;
  segment->size = remaining;
  sd_effect_clear_count[channel]++;
  sd_effect_clear_handlers[channel] = sd_continue_effect_clear;
  sd_effect_clear_index[channel] = 0;
  result = sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, sd_effect_clear_buffer, address,
                           SD_EFFECT_CLEAR_BUFFER_SIZE);
  if (result >= 0)
    result = 0;
done:
  sd_init_transfer_state();
  return result;
}
#endif

#else
int sceSdClearEffectWorkArea(int core, int channel, u_int effect) {
  int more = 1;
  sceSdCallback volatile callback = 0;
  sceSdTransIntrHandler volatile handler = 0;
  u_int size;
  u_int address;
  u_int aligned_address;
  u_int count;
  int restore_io = 0;
  int region_size;
  int io_mode;
  u_int end;
  if (effect >= SD_REV_MODE_MAX)
    return -1;
  if (!effect)
    return 0;
  size = sd_effect_sizes[effect] * SD_EFFECT_ADDRESS_UNIT;
  end = sd_get_effect_end(core);
  aligned_address = 0;
  region_size = size - 2;
  address = (end - region_size) >> 1;
  if (size & 63) {
    size = (size >> 6) << 6;
    end = sd_get_effect_end(core);
    region_size = size - 2;
    aligned_address = (end - region_size) >> 1;
  }
  io_mode = sd_transfer_active[channel];
  if (io_mode == 1) {
    sd_transfer_active[channel] = 0;
    restore_io = 1;
  }
  if (sd_transfer_handlers[channel]) {
    handler = sd_transfer_handlers[channel];
    sd_transfer_handlers[channel] = 0;
  }
  if (sd_transfer_callbacks[channel]) {
    callback = sd_transfer_callbacks[channel];
    sd_transfer_callbacks[channel] = 0;
  }
  if (aligned_address) {
    sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, sd_effect_clear_buffer, address << 1, 64);
    sceSdVoiceTransStatus(channel, SD_TRANS_STATUS_WAIT);
    address = aligned_address;
  }
  while (more) {
    count = SD_EFFECT_CLEAR_BUFFER_SIZE;
    if (size <= SD_EFFECT_CLEAR_BUFFER_SIZE) {
      count = size;
      more = 0;
    }
    sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, sd_effect_clear_buffer, address << 1, count);
    sceSdVoiceTransStatus(channel, SD_TRANS_STATUS_WAIT);
    size -= SD_EFFECT_CLEAR_BUFFER_SIZE;
    address += SD_EFFECT_CLEAR_BUFFER_SIZE / 2;
  }
  if (restore_io)
    sd_transfer_active[channel] = io_mode;
  if (handler)
    sd_transfer_handlers[channel] = handler;
  if (callback)
    sd_transfer_callbacks[channel] = callback;
  return 0;
}

#endif
