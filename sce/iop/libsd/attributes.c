#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"

#if LIBSD_VERSION >= 0x0301
volatile u_int sd_spdif_mode = 0;
#else
extern u_int sd_spdif_mode;
#endif

short sd_core_attr_bits[] = {SPU2_ATTR_EFFECT_SHIFT, SPU2_ATTR_IRQ_SHIFT, SPU2_ATTR_MUTE_SHIFT,
                             SPU2_ATTR_NOISE_SHIFT};

#if LIBSD_VERSION >= 0x0301
int sd_set_spdif_mode(int value) {
  u_short output = SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET);
  u_short mode = SPU2_REG(0, SPU2_SPDIF_MODE_OFFSET);
  volatile u_int status;
  output &= 0xfe57;
  mode &= ~2;
  switch (value & SD_SPDIF_OUTPUT_MASK) {
    case SD_SPDIF_OUT_PCM:
      output |= 0x20;
      break;
    case SD_SPDIF_OUT_BITSTREAM:
      output |= 0x100;
      mode |= 2;
      break;
    case SD_SPDIF_OUT_OFF:
      break;
    case 3:
      output |= 0x100;
      break;
    default:
      return SD_ERROR_INVALID_MODE;
  }
  if (value & SD_SPDIF_COPY_PROHIBIT)
    mode |= 0x8000;
  else
    mode &= 0x7fff;
  if ((value & SD_SPDIF_MEDIA_MASK) == SD_SPDIF_MEDIA_DVD) {
    SPU2_REG(0, SPU2_SPDIF_MEDIA_OFFSET) = 0x200;
#if LIBSD_VERSION >= 0x0303
    mode = (mode & 0xc0ff) | 0x1900;
#else
    mode |= 0x1800;
#endif
  } else if ((value & SD_SPDIF_MEDIA_MASK) == SD_SPDIF_MEDIA_ZERO) {
    SPU2_REG(0, SPU2_SPDIF_MEDIA_OFFSET) = 0;
#if LIBSD_VERSION >= 0x0303
    mode = (mode & 0xc0ff) | 0x100;
#else
    mode &= 0xe7ff;
#endif
  } else {
    SPU2_REG(0, SPU2_SPDIF_MEDIA_OFFSET) = 0x200;
#if LIBSD_VERSION >= 0x0303
    mode = (mode & 0xc0ff) | 0x900;
#else
    mode = (mode & 0xe7ff) | 0x800;
#endif
  }
  SPU2_REG(0, SPU2_SPDIF_OUTPUT_OFFSET) = output;
  SPU2_REG(0, SPU2_SPDIF_MODE_OFFSET) = mode;
  sd_spdif_mode = value;
  status = sd_spdif_mode;
  return 0;
}
#else
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

#endif

#if LIBSD_VERSION >= 0x0303
void sd_set_core_attr_bit(u_short entry, u_short value) {
  short bit = sd_core_attr_bits[((entry & SD_CORE_ATTR_MASK) >> 1) - 1];
  int intr_state;
  short current;
  CpuSuspendIntr(&intr_state);
  current = sceSdGetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR);
  sceSdSetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR,
                (current & ~(1 << bit)) | ((value & 1) << bit));
  CpuResumeIntr(intr_state);
}

void sceSdSetCoreAttr(u_short entry, u_short value) {
  int intr_state;
  short current;
  if ((entry & 0xfffe) == SD_C_SPDIF_MODE)
    sd_set_spdif_mode(value);
  else if ((entry & 0xfffe) == SD_C_NOISE_CLK) {
    CpuSuspendIntr(&intr_state);
    current = sceSdGetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR);
    sceSdSetParam((entry & SD_CORE_MASK) | SD_P_CORE_ATTR,
                  (current & ~SPU2_ATTR_NOISE_MASK) |
                      ((value & SPU2_NOISE_CLOCK_MASK) << SPU2_ATTR_NOISE_SHIFT));
    CpuResumeIntr(intr_state);
  } else
    sd_set_core_attr_bit(entry, value);
}
#else
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

#endif

u_short sceSdGetCoreAttr(u_short entry) {
#if LIBSD_VERSION >= 0x0301
  u_int result;
#endif
  u_short current = SPU2_ATTR(entry & SD_CORE_MASK);
  switch (entry & SD_CORE_ATTR_MASK) {
    case SD_C_EFFECT_ENABLE:
#if LIBSD_VERSION >= 0x0301
      result = (current >> SPU2_ATTR_EFFECT_SHIFT) & 1;
      break;
#else
      return (current >> SPU2_ATTR_EFFECT_SHIFT) & 1;
#endif
    case SD_C_IRQ_ENABLE:
#if LIBSD_VERSION >= 0x0301
      result = (current >> SPU2_ATTR_IRQ_SHIFT) & 1;
      break;
#else
      return (current >> SPU2_ATTR_IRQ_SHIFT) & 1;
#endif
    case SD_C_MUTE_ENABLE:
#if LIBSD_VERSION >= 0x0301
      result = (current >> SPU2_ATTR_MUTE_SHIFT) & 1;
      break;
#else
      return (current >> SPU2_ATTR_MUTE_SHIFT) & 1;
#endif
    case SD_C_NOISE_CLK:
#if LIBSD_VERSION >= 0x0301
      result = (current >> SPU2_ATTR_NOISE_SHIFT) & SPU2_NOISE_CLOCK_MASK;
      break;
#else
      return (current >> SPU2_ATTR_NOISE_SHIFT) & SPU2_NOISE_CLOCK_MASK;
#endif
    case SD_C_SPDIF_MODE:
#if LIBSD_VERSION >= 0x0301
      result = sd_spdif_mode;
      break;
#else
      return sd_spdif_mode;
#endif
    default:
#if LIBSD_VERSION >= 0x0301
      result = 0;
      break;
#else
      return 0;
#endif
  }
#if LIBSD_VERSION >= 0x0301
  return result;
#endif
}
