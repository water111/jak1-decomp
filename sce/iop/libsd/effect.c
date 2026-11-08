#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/sysclib/string.h"

extern volatile u_short* sd_spu_registers;

#if LIBSD_VERSION >= 0x0301
#define EFFECT_CORE core
#else
#define EFFECT_CORE sd_current_core
#endif

void sd_copy_effect(void* destination, const void* source, int size) {
  u_char* out = destination;
  const u_char* in = source;
  while (size--)
    *out++ = *in++;
}

void sceSdGetEffectAttr(int core, SdEffectAttr* attr) {
  attr->core = core;
  attr->mode = sd_effect_attrs[core].mode;
  attr->delay = sd_effect_attrs[core].delay;
  attr->feedback = sd_effect_attrs[core].feedback;
#if LIBSD_VERSION >= 0x0301
  attr->depth_left = SPU2_REG(0, SPU2_EVOLL_OFFSET + core * SPU2_VOLUME_CORE_STRIDE);
  attr->depth_right = SPU2_REG(0, SPU2_EVOLR_OFFSET + core * SPU2_VOLUME_CORE_STRIDE);
#else
  attr->depth_left = sd_effect_attrs[core].depth_left;
  attr->depth_right = sd_effect_attrs[core].depth_right;
#endif
}

extern SdEffectRegisters sd_effect_presets[SD_REV_MODE_MAX];

#if LIBSD_VERSION >= 0x0303
#define EFFECT_ATTR (*control)
#define EFFECT_CHANNEL channel
#else
#define EFFECT_ATTR sd_spu_registers[EFFECT_CORE * (SPU2_CORE_STRIDE / 2) + SPU2_ATTR_OFFSET / 2]
#define EFFECT_CHANNEL 0
#endif

int sceSdSetEffectAttr(int core, SdEffectAttr* attr) {
  SdEffectRegisters registers;
  int clear;
  int enabled;
  int loaded;
#if LIBSD_VERSION < 0x0303
  int delay_changed;
#endif
  int previous_mode;
#if LIBSD_VERSION >= 0x0303
  int mode;
  int core_offset;
  int delay_changed;
  volatile u_short* control;
  int channel;
  int intr_state;
#else
  int mode;
#endif
  u_int end;
  int size;
#if LIBSD_VERSION >= 0x0301
  int result;
  volatile u_int status;
  u_short attr_value;
#endif
  clear = 0;
#if LIBSD_VERSION >= 0x0303
  channel = 0;
#endif
  enabled = 0;
  loaded = 0;
  previous_mode = 0;
  delay_changed = 0;
  registers.mask = 0;
#if LIBSD_VERSION >= 0x0303
  control = &SPU2_ATTR(core);
#endif
  mode = attr->mode;
#if LIBSD_VERSION < 0x0301
  sd_current_core = core;
#endif
  if (mode & SD_REV_MODE_CLEAR_WA) {
#if LIBSD_VERSION < 0x0303
    mode &= ~SD_REV_MODE_CLEAR_WA;
#endif
    clear = 1;
    previous_mode = sd_effect_attrs[core].mode;
#if LIBSD_VERSION >= 0x0303
    if (mode & SD_REV_MODE_CLEAR_CHANNEL_1)
      channel = 1;
#endif
  }
#if LIBSD_VERSION >= 0x0303
  mode &= 0xff;
#endif
  if ((u_int)mode >= SD_REV_MODE_MAX)
#if LIBSD_VERSION >= 0x0301
    return SD_ERROR_INVALID_MODE;
#else
    return -1;
#endif
#if LIBSD_VERSION >= 0x0303
  core_offset = core * (SPU2_CORE_STRIDE / 2);
#endif
  sd_effect_attrs[core].mode = mode;
  end = sd_get_effect_end(core);
#if LIBSD_VERSION >= 0x0301
  size = sd_effect_sizes[mode] * SD_EFFECT_ADDRESS_UNIT - 1;
#else
  size = sd_effect_sizes[mode] * SD_EFFECT_ADDRESS_UNIT - 2;
#endif
  sd_effect_addresses[core] = end - size;
  sd_copy_effect(&registers, &sd_effect_presets[sd_effect_attrs[core].mode], sizeof(registers));
  loaded = 1;
  switch (mode) {
    case SD_REV_MODE_ECHO:
      sd_effect_attrs[core].delay = sd_effect_attrs[core].feedback =
          SD_EFFECT_DEFAULT_DELAY_FEEDBACK;
      break;
    case SD_REV_MODE_DELAY:
      sd_effect_attrs[core].feedback = 0;
      sd_effect_attrs[core].delay = SD_EFFECT_DEFAULT_DELAY_FEEDBACK;
      break;
    default:
      sd_effect_attrs[core].delay = sd_effect_attrs[core].feedback = 0;
  }
  switch (mode) {
    case SD_REV_MODE_ECHO:
    case SD_REV_MODE_DELAY: {
      int delay;
      delay_changed = 1;
      if (!loaded) {
        sd_copy_effect(&registers, &sd_effect_presets[sd_effect_attrs[core].mode],
                       sizeof(registers));
        registers.mask =
            (SD_EFFECT_UPDATE_MLSAME | SD_EFFECT_UPDATE_MRSAME | SD_EFFECT_UPDATE_MLCOMB1 |
             SD_EFFECT_UPDATE_DLSAME | SD_EFFECT_UPDATE_MLAPF1 | SD_EFFECT_UPDATE_MRAPF1);
      }
      delay = attr->delay;
      sd_effect_attrs[core].delay = delay;
      registers.mLSAME = (delay + 1) * SD_EFFECT_DELAY_LEFT_SCALE - registers.dAPF1;
      delay = (delay + 1) * SD_EFFECT_DELAY_RIGHT_SCALE;
      registers.mRSAME = delay - registers.dAPF2;
      registers.mLCOMB1 = registers.mRCOMB1 + delay;
      registers.dLSAME = registers.dRSAME + delay;
      registers.mLAPF1 = registers.mLAPF2 + delay;
      registers.mRAPF1 = registers.mRAPF2 + delay;
      break;
    }
  }
  switch (mode) {
    case SD_REV_MODE_ECHO:
    case SD_REV_MODE_DELAY: {
      int feedback;
      if (!loaded) {
        if (!delay_changed) {
          sd_copy_effect(&registers, &sd_effect_presets[sd_effect_attrs[core].mode],
                         sizeof(registers));
          registers.mask = SD_EFFECT_UPDATE_VWALL;
        } else {
          registers.mask |= SD_EFFECT_UPDATE_VWALL;
        }
      }
      feedback = attr->feedback;
      sd_effect_attrs[core].feedback = feedback;
      registers.vWALL = feedback * SD_EFFECT_FEEDBACK_SCALE;
      break;
    }
  }
  if (loaded) {
    enabled = (EFFECT_ATTR >> SPU2_ATTR_EFFECT_SHIFT) & 1;
    if (enabled) {
#if LIBSD_VERSION >= 0x0303
      CpuSuspendIntr(&intr_state);
#endif
#if LIBSD_VERSION >= 0x0301
      attr_value =
#else
      u_short value =
#endif
          EFFECT_ATTR;
      EFFECT_ATTR =
#if LIBSD_VERSION >= 0x0301
          attr_value & ~SPU2_ATTR_EFFECT_ENABLE;
#else
          value & ~SPU2_ATTR_EFFECT_ENABLE;
#endif
#if LIBSD_VERSION >= 0x0303
      CpuResumeIntr(intr_state);
#endif
#if LIBSD_VERSION < 0x0301
      // TODO: needed for matching register allocation.
      asm("" ::"r"(value));
#endif
    }
  }
#if LIBSD_VERSION >= 0x0301
  if (enabled && clear) {
    result = sceSdClearEffectWorkArea(core, EFFECT_CHANNEL, previous_mode);
    if (result < 0)
      goto restore;
  }
  SPU2_REG(0, SPU2_EVOLL_OFFSET + core * SPU2_VOLUME_CORE_STRIDE) = attr->depth_left;
  SPU2_REG(0, SPU2_EVOLR_OFFSET + core * SPU2_VOLUME_CORE_STRIDE) = attr->depth_right;
  sd_set_effect_registers(core, &registers);
  if (loaded) {
#if LIBSD_VERSION >= 0x0303
    ((volatile u_short*)(SPU2_BASE + SPU2_EFFECT_START_OFFSET))[core_offset] =
        sd_effect_addresses[core] >> 17;
    ((volatile u_short*)(SPU2_BASE + SPU2_EFFECT_START_OFFSET + 2))[core_offset] =
        sd_effect_addresses[core] >> 1;
    status = ((volatile u_short*)(SPU2_BASE + SPU2_EFFECT_START_OFFSET + 2))[core_offset];
#else
    SPU2_REG(core, SPU2_EFFECT_START_OFFSET) = sd_effect_addresses[core] >> 17;
    SPU2_REG(core, SPU2_EFFECT_START_OFFSET + 2) = sd_effect_addresses[core] >> 1;
    status = SPU2_REG(core, SPU2_EFFECT_START_OFFSET + 2);
#endif
  }
  result = 0;
  if (clear)
    result = sceSdClearEffectWorkArea(core, EFFECT_CHANNEL, mode);
restore:
  if (loaded && enabled) {
#if LIBSD_VERSION >= 0x0303
    CpuSuspendIntr(&intr_state);
#endif
    attr_value = EFFECT_ATTR;
    EFFECT_ATTR = attr_value | SPU2_ATTR_EFFECT_ENABLE;
    status = EFFECT_ATTR;
#if LIBSD_VERSION >= 0x0303
    CpuResumeIntr(intr_state);
#endif
  }
  sd_init_transfer_state();
  return result;
#else
  if (enabled && clear)
    sceSdClearEffectWorkArea(core, EFFECT_CHANNEL, previous_mode);
  sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + (SPU2_EVOLL_OFFSET / 2)] =
      attr->depth_left;
  sd_effect_attrs[core].depth_left = attr->depth_left;
  sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + (SPU2_EVOLR_OFFSET / 2)] =
      attr->depth_right;
  sd_effect_attrs[core].depth_right = attr->depth_right;
#if LIBSD_VERSION >= 0x0301
  sd_set_effect_registers(core, &registers);
#else
  sd_set_effect_registers(&registers);
#endif
  if (loaded)
    sd_set_address((SPU2_EFFECT_START_OFFSET / 2), sd_effect_addresses[core], 1);
  if (clear)
    sceSdClearEffectWorkArea(core, EFFECT_CHANNEL, mode);
  if (loaded && enabled)
    EFFECT_ATTR |= SPU2_ATTR_EFFECT_ENABLE;
  return 0;
#endif
}

u_int sd_get_effect_end(int core) {
#if LIBSD_VERSION < 0x0301
  sd_current_core = core;
#endif
#if LIBSD_VERSION >= 0x0303
  return ((u_int)SPU2_REG(core, SPU2_EFFECT_END_OFFSET)
#else
  return ((u_int)sd_spu_registers[core * (SPU2_CORE_STRIDE / 2) + (SPU2_EFFECT_END_OFFSET / 2)]
#endif
          << 17) |
         SPU2_EFFECT_END_LOW_MASK;
}

#if LIBSD_VERSION >= 0x0303
int sceSdSetEffectMode(int core, SdEffectAttr* attr) {
  SdEffectRegisters registers;
  u_int mode;
  int result;
  int region_size;
  u_int end;
  volatile u_short* control;
  int clear = 0;
  int channel;
  int enabled;
  int intr_state;
  volatile u_int status;
  registers.mask = 0;
  mode = attr->mode;
  channel = 0;
  control = &SPU2_ATTR(core);
  if (mode & SD_REV_MODE_CLEAR_WA) {
    clear = 1;
    if (mode & SD_REV_MODE_CLEAR_CHANNEL_1)
      channel = 1;
  }
  mode &= 0xff;
  if ((u_int)mode >= SD_REV_MODE_MAX)
    return SD_ERROR_INVALID_MODE;
  sd_effect_attrs[core].mode = mode;
  sd_effect_attrs[core].delay = 0;
  sd_effect_attrs[core].feedback = 0;
  end = sd_get_effect_end(core);
  region_size = sd_effect_sizes[mode] * SD_EFFECT_ADDRESS_UNIT - 1;
  sd_effect_addresses[core] = end - region_size;
  memcpy(&registers, &sd_effect_presets[mode], sizeof(registers));
  enabled = (*control >> SPU2_ATTR_EFFECT_SHIFT) & 1;
  if (enabled) {
    CpuSuspendIntr(&intr_state);
    *control &= ~SPU2_ATTR_EFFECT_ENABLE;
    CpuResumeIntr(intr_state);
  }
  SPU2_REG(0, SPU2_EVOLL_OFFSET + core * SPU2_VOLUME_CORE_STRIDE) = 0;
  SPU2_REG(0, SPU2_EVOLR_OFFSET + core * SPU2_VOLUME_CORE_STRIDE) = 0;
  sd_set_effect_registers(core, &registers);
  SPU2_REG(core, SPU2_EFFECT_START_OFFSET) = sd_effect_addresses[core] >> 17;
  SPU2_REG(core, SPU2_EFFECT_START_OFFSET + 2) = sd_effect_addresses[core] >> 1;
  if (enabled) {
    CpuSuspendIntr(&intr_state);
    *control |= SPU2_ATTR_EFFECT_ENABLE;
    CpuResumeIntr(intr_state);
    status = *control;
  }
  result = 0;
  if (clear)
    result = sceSdCleanEffectWorkArea(core, channel, mode);
  return result;
}

int sceSdSetEffectModeParams(int core, SdEffectAttr* attr) {
  SdEffectRegisters registers;
  int mode, delay;
  u_int requested_mode = attr->mode & 0xff;
  if (requested_mode >= SD_REV_MODE_MAX)
    return SD_ERROR_INVALID_MODE;
  mode = sd_effect_attrs[core].mode;
  if (mode != requested_mode)
    return SD_ERROR_INVALID_MODE;
  switch (mode) {
    case SD_REV_MODE_ECHO:
    case SD_REV_MODE_DELAY:
      memcpy(&registers, &sd_effect_presets[mode], sizeof(registers));
      registers.mask = SD_EFFECT_UPDATE_MLSAME | SD_EFFECT_UPDATE_MRSAME |
                       SD_EFFECT_UPDATE_MLCOMB1 | SD_EFFECT_UPDATE_DLSAME |
                       SD_EFFECT_UPDATE_MLAPF1 | SD_EFFECT_UPDATE_MRAPF1 | SD_EFFECT_UPDATE_VWALL;
      sd_effect_attrs[core].delay = attr->delay;
      sd_effect_attrs[core].feedback = attr->feedback;
      delay = sd_effect_attrs[core].delay + 1;
      registers.mLSAME = delay * SD_EFFECT_DELAY_LEFT_SCALE - registers.dAPF1;
      delay *= SD_EFFECT_DELAY_RIGHT_SCALE;
      registers.mRSAME = delay - registers.dAPF2;
      registers.mLCOMB1 = registers.mRCOMB1 + delay;
      registers.dLSAME = registers.dRSAME + delay;
      registers.mLAPF1 = registers.mLAPF2 + delay;
      registers.mRAPF1 = registers.mRAPF2 + delay;
      registers.vWALL = sd_effect_attrs[core].feedback * SD_EFFECT_FEEDBACK_SCALE;
      sd_set_effect_registers(core, &registers);
      break;
  }
  SPU2_REG(0, SPU2_EVOLL_OFFSET + core * SPU2_VOLUME_CORE_STRIDE) = attr->depth_left;
  SPU2_REG(0, SPU2_EVOLR_OFFSET + core * SPU2_VOLUME_CORE_STRIDE) = attr->depth_right;
  sd_init_transfer_state();
  return 0;
}
#endif
