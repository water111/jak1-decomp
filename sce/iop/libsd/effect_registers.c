#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"

extern volatile u_short* sd_spu_registers;

void sd_set_effect_registers(SdEffectRegisters* registers) {
  u_int mask = registers->mask;
  int all = mask == 0;
  if (all || (mask & SD_EFFECT_UPDATE_DAPF1))
    sd_set_effect_register(SPU2_EFFECT_DAPF1_OFFSET / 2, registers->dAPF1);
  if (all || (mask & SD_EFFECT_UPDATE_DAPF2))
    sd_set_effect_register(SPU2_EFFECT_DAPF2_OFFSET / 2, registers->dAPF2);
  if (all || (mask & SD_EFFECT_UPDATE_VIIR))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VIIR_OFFSET / 2] = registers->vIIR;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB1))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VCOMB1_OFFSET / 2] = registers->vCOMB1;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB2))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VCOMB2_OFFSET / 2] = registers->vCOMB2;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB3))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VCOMB3_OFFSET / 2] = registers->vCOMB3;
  if (all || (mask & SD_EFFECT_UPDATE_VCOMB4))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VCOMB4_OFFSET / 2] = registers->vCOMB4;
  if (all || (mask & SD_EFFECT_UPDATE_VWALL))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VWALL_OFFSET / 2] = registers->vWALL;
  if (all || (mask & SD_EFFECT_UPDATE_VAPF1))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VAPF1_OFFSET / 2] = registers->vAPF1;
  if (all || (mask & SD_EFFECT_UPDATE_VAPF2))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VAPF2_OFFSET / 2] = registers->vAPF2;
  if (all || (mask & SD_EFFECT_UPDATE_MLSAME))
    sd_set_effect_register(SPU2_EFFECT_MLSAME_OFFSET / 2, registers->mLSAME);
  if (all || (mask & SD_EFFECT_UPDATE_MRSAME))
    sd_set_effect_register(SPU2_EFFECT_MRSAME_OFFSET / 2, registers->mRSAME);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB1))
    sd_set_effect_register(SPU2_EFFECT_MLCOMB1_OFFSET / 2, registers->mLCOMB1);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB1))
    sd_set_effect_register(SPU2_EFFECT_MRCOMB1_OFFSET / 2, registers->mRCOMB1);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB2))
    sd_set_effect_register(SPU2_EFFECT_MLCOMB2_OFFSET / 2, registers->mLCOMB2);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB2))
    sd_set_effect_register(SPU2_EFFECT_MRCOMB2_OFFSET / 2, registers->mRCOMB2);
  if (all || (mask & SD_EFFECT_UPDATE_DLSAME))
    sd_set_effect_register(SPU2_EFFECT_DLSAME_OFFSET / 2, registers->dLSAME);
  if (all || (mask & SD_EFFECT_UPDATE_DRSAME))
    sd_set_effect_register(SPU2_EFFECT_DRSAME_OFFSET / 2, registers->dRSAME);
  if (all || (mask & SD_EFFECT_UPDATE_MLDIFF))
    sd_set_effect_register(SPU2_EFFECT_MLDIFF_OFFSET / 2, registers->mLDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_MRDIFF))
    sd_set_effect_register(SPU2_EFFECT_MRDIFF_OFFSET / 2, registers->mRDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB3))
    sd_set_effect_register(SPU2_EFFECT_MLCOMB3_OFFSET / 2, registers->mLCOMB3);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB3))
    sd_set_effect_register(SPU2_EFFECT_MRCOMB3_OFFSET / 2, registers->mRCOMB3);
  if (all || (mask & SD_EFFECT_UPDATE_MLCOMB4))
    sd_set_effect_register(SPU2_EFFECT_MLCOMB4_OFFSET / 2, registers->mLCOMB4);
  if (all || (mask & SD_EFFECT_UPDATE_MRCOMB4))
    sd_set_effect_register(SPU2_EFFECT_MRCOMB4_OFFSET / 2, registers->mRCOMB4);
  if (all || (mask & SD_EFFECT_UPDATE_DLDIFF))
    sd_set_effect_register(SPU2_EFFECT_DLDIFF_OFFSET / 2, registers->dLDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_DRDIFF))
    sd_set_effect_register(SPU2_EFFECT_DRDIFF_OFFSET / 2, registers->dRDIFF);
  if (all || (mask & SD_EFFECT_UPDATE_MLAPF1))
    sd_set_effect_register(SPU2_EFFECT_MLAPF1_OFFSET / 2, registers->mLAPF1);
  if (all || (mask & SD_EFFECT_UPDATE_MRAPF1))
    sd_set_effect_register(SPU2_EFFECT_MRAPF1_OFFSET / 2, registers->mRAPF1);
  if (all || (mask & SD_EFFECT_UPDATE_MLAPF2))
    sd_set_effect_register(SPU2_EFFECT_MLAPF2_OFFSET / 2, registers->mLAPF2);
  if (all || (mask & SD_EFFECT_UPDATE_MRAPF2))
    sd_set_effect_register(SPU2_EFFECT_MRAPF2_OFFSET / 2, registers->mRAPF2);
  if (all || (mask & SD_EFFECT_UPDATE_VLIN))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VLIN_OFFSET / 2] = registers->vLIN;
  if (all || (mask & SD_EFFECT_UPDATE_VRIN))
    sd_spu_registers[sd_current_core * (SPU2_VOLUME_CORE_STRIDE / 2) +
                     SPU2_EFFECT_VRIN_OFFSET / 2] = registers->vRIN;
}
