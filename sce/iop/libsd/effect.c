#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"

extern volatile u_short* sd_spu_registers;

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
  attr->depth_left = sd_effect_attrs[core].depth_left;
  attr->depth_right = sd_effect_attrs[core].depth_right;
}

extern SdEffectRegisters sd_effect_presets[SD_REV_MODE_MAX];

int sceSdSetEffectAttr(int core, SdEffectAttr* attr) {
  SdEffectRegisters registers;
  int clear;
  int enabled;
  int loaded;
  int delay_changed;
  int previous_mode;
  int mode;
  u_int end;
  int size;
  clear = 0;
  enabled = 0;
  loaded = 0;
  previous_mode = 0;
  delay_changed = 0;
  registers.mask = 0;
  mode = attr->mode;
  sd_current_core = core;
  if (mode & SD_REV_MODE_CLEAR_WA) {
    mode &= ~SD_REV_MODE_CLEAR_WA;
    clear = 1;
    previous_mode = sd_effect_attrs[core].mode;
  }
  if ((u_int)mode >= SD_REV_MODE_MAX)
    return -1;
  sd_effect_attrs[core].mode = mode;
  end = sd_get_effect_end(core);
  size = sd_effect_sizes[mode] * SD_EFFECT_ADDRESS_UNIT - 2;
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
    enabled =
        (sd_spu_registers[sd_current_core * (SPU2_CORE_STRIDE / 2) + (SPU2_ATTR_OFFSET / 2)] >>
         SPU2_ATTR_EFFECT_SHIFT) &
        1;
    if (enabled) {
      u_short value =
          sd_spu_registers[sd_current_core * (SPU2_CORE_STRIDE / 2) + (SPU2_ATTR_OFFSET / 2)];
      sd_spu_registers[sd_current_core * (SPU2_CORE_STRIDE / 2) + (SPU2_ATTR_OFFSET / 2)] =
          value & ~SPU2_ATTR_EFFECT_ENABLE;
      // TODO: needed for matching register allocation.
      asm("" ::"r"(value));
    }
  }
  if (enabled && clear)
    sceSdClearEffectWorkArea(core, 0, previous_mode);
  sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) + (SPU2_EVOLL_OFFSET / 2)] =
      attr->depth_left;
  sd_effect_attrs[core].depth_left = attr->depth_left;
  sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) + (SPU2_EVOLR_OFFSET / 2)] =
      attr->depth_right;
  sd_effect_attrs[core].depth_right = attr->depth_right;
  sd_set_effect_registers(&registers);
  if (loaded)
    sd_set_address((SPU2_EFFECT_START_OFFSET / 2), sd_effect_addresses[core], 1);
  if (clear)
    sceSdClearEffectWorkArea(core, 0, mode);
  if (loaded && enabled)
    sd_spu_registers[sd_current_core * (SPU2_CORE_STRIDE / 2) + (SPU2_ATTR_OFFSET / 2)] |=
        SPU2_ATTR_EFFECT_ENABLE;
  return 0;
}

u_int sd_get_effect_end(int core) {
  sd_current_core = core;
  return ((u_int)sd_spu_registers[core * (SPU2_CORE_STRIDE / 2) + (SPU2_EFFECT_END_OFFSET / 2)]
          << 17) |
         SPU2_EFFECT_END_LOW_MASK;
}
