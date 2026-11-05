#include "sce/iop/libsd/hardware.h"

u_char sd_silent_adpcm[16] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};

void sd_init_voices(void) {
  volatile int i;
  u_short attr;
  u_short* sample = (u_short*)sd_silent_adpcm;
  volatile u_short* voice0 = (volatile u_short*)(SPU2_BASE);
  volatile u_short* voice1 = (volatile u_short*)(SPU2_BASE + SPU2_CORE_STRIDE);
  volatile u_short* address0 = (volatile u_short*)(SPU2_BASE + 0x1c0);
  volatile u_short* address1 = (volatile u_short*)(SPU2_BASE + SPU2_CORE_STRIDE + 0x1c0);
  SPU2_TRANSFER_ADDR_HIGH(0) = 0;
  SPU2_TRANSFER_ADDR_LOW(0) = 0x2800;
  // BUG: sends 16 u16's, but array is 16 u8's.
  for (i = 0; i < 16; i++)
    SPU2_TRANSFER_DATA(0) = *sample++;
  attr = SPU2_ATTR(0);
  SPU2_ATTR(0) = (attr & ~0x30) | 0x10;
  while (SPU2_STATUS(0) & 0x400) {
  }
  {
    attr = SPU2_ATTR(0);
    SPU2_ATTR(0) = attr & 0xffcf;
  }
  for (i = 0; i < 24; i++) {
    *voice0++ = *voice1++ = 0;
    *voice0++ = *voice1++ = 0;
    *voice0++ = *voice1++ = 0x3fff;
    *voice0++ = *voice1++ = 0;
    *voice0 = *voice1 = 0;
    voice0 += 4;
    voice1 += 4;
    *address0++ = *address1++ = 0;
    *address0 = *address1 = 0x2800;
    address0 += 5;
    address1 += 5;
  }
  SPU2_KEY_ON_LOW(0) = SPU2_KEY_ON_LOW(1) = 0xffff;
  SPU2_KEY_ON_HIGH(0) = SPU2_KEY_ON_HIGH(1) = 0xff;
  for (i = 0; i < 3124; i++) {
  }
  SPU2_KEY_OFF_LOW(0) = SPU2_KEY_OFF_LOW(1) = 0xffff;
  SPU2_KEY_OFF_HIGH(0) = SPU2_KEY_OFF_HIGH(1) = 0xff;
  for (i = 0; i < 3124; i++) {
  }
  SPU2_END_LOW(0) = SPU2_END_HIGH(0) = 0;
}
