#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/libsd.h"

volatile u_short* const sd_registers[] = {
    (volatile u_short*)(SPU2_BASE + 0x0),
    (volatile u_short*)(SPU2_BASE + 0x2),
    (volatile u_short*)(SPU2_BASE + 0x4),
    (volatile u_short*)(SPU2_BASE + 0x6),
    (volatile u_short*)(SPU2_BASE + 0x8),
    (volatile u_short*)(SPU2_BASE + 0xa),
    (volatile u_short*)(SPU2_BASE + 0xc),
    (volatile u_short*)(SPU2_BASE + 0xe),
    (volatile u_short*)(SPU2_BASE + SPU2_MIX_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_MVOLL_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_MVOLR_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_EVOLL_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_EVOLR_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_AVOLL_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_AVOLR_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_BVOLL_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_BVOLR_OFFSET),
    (volatile u_short*)(SPU2_BASE + 0x770),
    (volatile u_short*)(SPU2_BASE + 0x772),
    (volatile u_short*)(SPU2_BASE + SPU2_PMON_LOW_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_NON_LOW_OFFSET),
    (volatile u_short*)(SPU2_BASE + 0x1a0),
    (volatile u_short*)(SPU2_BASE + SPU2_KEY_OFF_LOW_OFFSET),
    (volatile u_short*)(SPU2_BASE + 0x340),
    (volatile u_short*)(SPU2_BASE + SPU2_VOICE_MIX_L_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_VOICE_MIX_EL_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_VOICE_MIX_R_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_VOICE_MIX_ER_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_EFFECT_START_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_EFFECT_END_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_HIGH_OFFSET),
    (volatile u_short*)(SPU2_BASE + 0x19c),
    (volatile u_short*)(SPU2_BASE + 0x1c0),
    (volatile u_short*)(SPU2_BASE + 0x1c4),
    (volatile u_short*)(SPU2_BASE + 0x1c8),
    (volatile u_short*)(SPU2_BASE + SPU2_ATTR_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_ADDR_HIGH_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_TRANSFER_DATA_OFFSET),
    (volatile u_short*)(SPU2_BASE + 0x1ae),
    (volatile u_short*)(SPU2_BASE + SPU2_ADMAS_OFFSET),
    (volatile u_short*)(SPU2_BASE + SPU2_STATUS_OFFSET),
};

void sceSdSetParam(u_short entry, u_short value) {
  int core = (entry & 1) * (SPU2_CORE_STRIDE -
                            ((entry & 0x80) >> 7) * (SPU2_CORE_STRIDE - SPU2_VOLUME_CORE_STRIDE));
  *(u_short*)((u_int)sd_registers[entry >> 8] + core + (entry & 0x3e) * 8) = value;
}

u_short sceSdGetParam(u_short entry) {
  int core = (entry & 1) * (SPU2_CORE_STRIDE -
                            ((entry & 0x80) >> 7) * (SPU2_CORE_STRIDE - SPU2_VOLUME_CORE_STRIDE));
  return *(u_short*)((u_int)sd_registers[entry >> 8] + core + (entry & 0x3e) * 8);
}

void sceSdSetSwitch(u_short entry, u_int value) {
  int index = entry >> 8;
  entry &= 1;
  sd_registers[index][entry * (SPU2_CORE_STRIDE / sizeof(u_short))] = value;
  sd_registers[index][entry * (SPU2_CORE_STRIDE / sizeof(u_short)) + 1] = (value >> 16) & 255;
}

u_int sceSdGetSwitch(u_short entry) {
  int index = entry >> 8;
  entry &= 1;
  return sd_registers[index][entry * (SPU2_CORE_STRIDE / sizeof(u_short))] |
         (sd_registers[index][entry * (SPU2_CORE_STRIDE / sizeof(u_short)) + 1] << 16);
}

void sceSdSetAddr(u_short entry, u_int value) {
  u_short* reg = (u_short*)((u_int)sd_registers[entry >> 8] + (entry & 1) * SPU2_CORE_STRIDE +
                            (entry & 0x3e) * 6);
  reg[0] = value >> 17;
  if ((entry & 0xff00) != 0x1d00)
    reg[1] = (value >> 1) & 0xfff8;
}

u_int sceSdGetAddr(u_short entry) {
  volatile u_short* reg = (volatile u_short*)((u_int)sd_registers[entry >> 8] +
                                              (entry & 1) * SPU2_CORE_STRIDE + (entry & 0x3e) * 6);
  u_int high = reg[0] << 17;
  u_int low = 0x1ffff;
  if ((entry & 0xff00) != 0x1d00) {
    low = reg[1] << 1;
    if ((entry & 0xff00) == 0x2100 || (entry & 0xff00) == 0x2200) {
      u_int next_high = reg[0] << 17;
      u_int next_low = reg[1] << 1;
      if (high == next_high || low != next_low) {
        high = next_high;
        low = next_low;
      }
    }
  }
  return high | low;
}
