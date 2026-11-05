#include "sce/iop/libsd/internal.h"

extern u_char sd_effect_clear_buffer[SD_EFFECT_CLEAR_BUFFER_SIZE];

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
