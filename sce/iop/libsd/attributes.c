#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"

extern u_int sd_spdif_mode;

short sd_core_attr_bits[] = {SPU2_ATTR_EFFECT_SHIFT, SPU2_ATTR_IRQ_SHIFT, SPU2_ATTR_MUTE_SHIFT,
                             SPU2_ATTR_NOISE_SHIFT};

int sd_set_spdif_mode(int value) {
  u_short output = SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET);
  u_short mode = SPU2_REG(0, SPU2_SPDIF_MODE_OFFSET);
  switch (value & SD_SPDIF_OUTPUT_MASK) {
    case SD_SPDIF_OUT_PCM:
      mode &= ~2;
      output = (output & 0xfef7) | 0x20;
      break;
    case SD_SPDIF_OUT_BITSTREAM:
      mode |= 2;
      output = (output & 0xffd7) | 0x100;
      break;
    case SD_SPDIF_OUT_OFF:
      output &= 0xfed7;
      break;
    default:
      return -1;
  }
  if (value & SD_SPDIF_COPY_PROHIBIT)
    mode |= 0x8000;
  else
    mode &= 0x7fff;
  if ((value & SD_SPDIF_MEDIA_MASK) == SD_SPDIF_MEDIA_DVD) {
    SPU2_REG(0, SPU2_SPDIF_MEDIA_OFFSET) = 0x200;
    mode |= 0x1800;
  } else if ((value & SD_SPDIF_MEDIA_MASK) == SD_SPDIF_MEDIA_ZERO) {
    SPU2_REG(0, SPU2_SPDIF_MEDIA_OFFSET) = 0;
    mode &= 0xe7ff;
  } else {
    SPU2_REG(0, SPU2_SPDIF_MEDIA_OFFSET) = 0x200;
    mode = (mode & 0xe7ff) | 0x800;
  }
  SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET) = output;
  SPU2_REG(0, SPU2_SPDIF_MODE_OFFSET) = mode;
  sd_spdif_mode = value;
  return 0;
}

void sd_set_core_attr_bit(u_short entry, u_short value, u_short current) {
  int bit = sd_core_attr_bits[((entry & SD_CORE_ATTR_MASK) >> 1) - 1];
  sceSdSetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR,
                (current & ~(1 << bit)) | ((value & 1) << bit));
}

void sceSdSetCoreAttr(u_short entry, u_short value) {
  short current = sceSdGetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR);
  if ((entry & 0xfffe) == SD_C_SPDIF_MODE)
    sd_set_spdif_mode(value);
  else if ((entry & 0xfffe) == SD_C_NOISE_CLK)
    sceSdSetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR,
                  (current & ~SPU2_ATTR_NOISE_MASK) |
                      ((value & SPU2_NOISE_CLOCK_MASK) << SPU2_ATTR_NOISE_SHIFT));
  else
    sd_set_core_attr_bit(entry, value, current);
}

u_short sceSdGetCoreAttr(u_short entry) {
  u_short current = SPU2_ATTR(entry & SD_CORE_MASK);
  switch (entry & SD_CORE_ATTR_MASK) {
    case SD_C_EFFECT_ENABLE:
      return (current >> SPU2_ATTR_EFFECT_SHIFT) & 1;
    case SD_C_IRQ_ENABLE:
      return (current >> SPU2_ATTR_IRQ_SHIFT) & 1;
    case SD_C_MUTE_ENABLE:
      return (current >> SPU2_ATTR_MUTE_SHIFT) & 1;
    case SD_C_NOISE_CLK:
      return (current >> SPU2_ATTR_NOISE_SHIFT) & SPU2_NOISE_CLOCK_MASK;
    case SD_C_SPDIF_MODE:
      return sd_spdif_mode;
    default:
      return 0;
  }
}
