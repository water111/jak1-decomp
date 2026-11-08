#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"

extern volatile u_short* sd_spu_registers;

#if LIBSD_VERSION >= 0x0301
void sd_set_effect_register(int core, int reg, int value) {
  volatile u_short* address =
      (volatile u_short*)((core * (SPU2_CORE_STRIDE / 2) + reg) * 2 + (u_int)sd_spu_registers);
  u_int shifted = value << 2;
  address[0] = shifted >> 16;
  address[1] = shifted;
}

#define WRITE_EFFECT(reg, value) sd_set_effect_register(core, reg, value)
#define EFFECT_CORE core
void sd_set_effect_registers(int core, SdEffectRegisters* registers) {
#else
#define WRITE_EFFECT(reg, value) sd_set_effect_register(reg, value)
#define EFFECT_CORE sd_current_core
void sd_set_effect_registers(SdEffectRegisters* registers) {
#endif

  u_int mask = registers->mask;
  int all = mask == 0;
  if (all || (mask & SD_EFFECT_UPDATE_DAPF1))
    WRITE_EFFECT(SPU2_EFFECT_DAPF1_OFFSET / 2, registers->dAPF1);
  if (all || (mask & SD_EFFECT_UPDATE_DAPF2))
    WRITE_EFFECT(SPU2_EFFECT_DAPF2_OFFSET / 2, registers->dAPF2);
  if (all || (mask & SD_EFFECT_UPDATE_VIIR))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VIIR_OFFSET / 2] =
        registers->vIIR;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB1))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VCOMB1_OFFSET / 2] =
        registers->vCOMB1;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB2))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VCOMB2_OFFSET / 2] =
        registers->vCOMB2;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB3))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VCOMB3_OFFSET / 2] =
        registers->vCOMB3;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB4))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VCOMB4_OFFSET / 2] =
        registers->vCOMB4;
  if (all || (mask & SD_EFFECT_UPDATE_VWALL))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VWALL_OFFSET / 2] =
        registers->vWALL;
  if (all || (mask & SD_EFFECT_UPDATE_VAPF1))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VAPF1_OFFSET / 2] =
        registers->vAPF1;
  if (all || (mask & SD_EFFECT_UPDATE_VAPF2))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VAPF2_OFFSET / 2] =
        registers->vAPF2;
  if (all || (mask & SD_EFFECT_UPDATE_MLSAME))
    WRITE_EFFECT(SPU2_EFFECT_MLSAME_OFFSET / 2, registers->mLSAME);
  if (all || (mask & SD_EFFECT_UPDATE_MRSAME))
    WRITE_EFFECT(SPU2_EFFECT_MRSAME_OFFSET / 2, registers->mRSAME);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB1))
    WRITE_EFFECT(SPU2_EFFECT_MLCOMB1_OFFSET / 2, registers->mLCOMB1);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB1))
    WRITE_EFFECT(SPU2_EFFECT_MRCOMB1_OFFSET / 2, registers->mRCOMB1);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB2))
    WRITE_EFFECT(SPU2_EFFECT_MLCOMB2_OFFSET / 2, registers->mLCOMB2);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB2))
    WRITE_EFFECT(SPU2_EFFECT_MRCOMB2_OFFSET / 2, registers->mRCOMB2);
  if (all || (mask & SD_EFFECT_UPDATE_DLSAME))
    WRITE_EFFECT(SPU2_EFFECT_DLSAME_OFFSET / 2, registers->dLSAME);
  if (all || (mask & SD_EFFECT_UPDATE_DRSAME))
    WRITE_EFFECT(SPU2_EFFECT_DRSAME_OFFSET / 2, registers->dRSAME);
  if (all || (mask & SD_EFFECT_UPDATE_MLDIFF))
    WRITE_EFFECT(SPU2_EFFECT_MLDIFF_OFFSET / 2, registers->mLDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_MRDIFF))
    WRITE_EFFECT(SPU2_EFFECT_MRDIFF_OFFSET / 2, registers->mRDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB3))
    WRITE_EFFECT(SPU2_EFFECT_MLCOMB3_OFFSET / 2, registers->mLCOMB3);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB3))
    WRITE_EFFECT(SPU2_EFFECT_MRCOMB3_OFFSET / 2, registers->mRCOMB3);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB4))
    WRITE_EFFECT(SPU2_EFFECT_MLCOMB4_OFFSET / 2, registers->mLCOMB4);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB4))
    WRITE_EFFECT(SPU2_EFFECT_MRCOMB4_OFFSET / 2, registers->mRCOMB4);
  if (all || (mask & SD_EFFECT_UPDATE_DLDIFF))
    WRITE_EFFECT(SPU2_EFFECT_DLDIFF_OFFSET / 2, registers->dLDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_DRDIFF))
    WRITE_EFFECT(SPU2_EFFECT_DRDIFF_OFFSET / 2, registers->dRDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_MLAPF1))
    WRITE_EFFECT(SPU2_EFFECT_MLAPF1_OFFSET / 2, registers->mLAPF1);
  if (all || (mask & SD_EFFECT_UPDATE_MRAPF1))
    WRITE_EFFECT(SPU2_EFFECT_MRAPF1_OFFSET / 2, registers->mRAPF1);
  if (all || (mask & SD_EFFECT_UPDATE_MLAPF2))
    WRITE_EFFECT(SPU2_EFFECT_MLAPF2_OFFSET / 2, registers->mLAPF2);
  if (all || (mask & SD_EFFECT_UPDATE_MRAPF2))
    WRITE_EFFECT(SPU2_EFFECT_MRAPF2_OFFSET / 2, registers->mRAPF2);
  if (all || (mask & SD_EFFECT_UPDATE_VLIN))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VLIN_OFFSET / 2] =
        registers->vLIN;
  if (all || (mask & SD_EFFECT_UPDATE_VRIN))
    sd_spu_registers[EFFECT_CORE * (SPU2_VOLUME_CORE_STRIDE / 2) + SPU2_EFFECT_VRIN_OFFSET / 2] =
        registers->vRIN;
}
