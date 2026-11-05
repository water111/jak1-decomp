#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/sifman/sif.h"

int sd_transfer_to_ee(void* destination, void* source, u_int size) {
  sceSifDmaData dma;
  int state;
  int id;
  int result;
  dma.data = (u_int)source;
  dma.addr = (u_int)destination;
  dma.size = size;
  dma.mode = 0;
  CpuSuspendIntr(&state);
  id = sceSifSetDma(&dma, 1);
  CpuResumeIntr(state);
  while (sceSifDmaStat(id) >= 0) {
  }
  result = -1;
  if (id)
    result = 0;
  return result;
}

extern u_int sd_batch_value;

int sd_transfer_to_ee(void* destination, void* source, u_int size);

int sceSdProcBatch(sceSdBatch* batch, u_int returns[], u_int count) {
  u_int i;
  for (i = 0; i < count; i++) {
    u_int result = 0;
    switch (batch[i].func) {
      case SD_BSET_PARAM:
        sceSdSetParam(batch[i].entry, batch[i].value);
        break;
      case SD_BGET_PARAM:
        result = sceSdGetParam(batch[i].entry);
        break;
      case SD_BSET_SWITCH:
        sceSdSetSwitch(batch[i].entry, batch[i].value);
        break;
      case SD_BGET_SWITCH:
        result = sceSdGetSwitch(batch[i].entry);
        break;
      case SD_BSET_ADDR:
        sceSdSetAddr(batch[i].entry, batch[i].value);
        break;
      case SD_BGET_ADDR:
        result = sceSdGetAddr(batch[i].entry);
        break;
      case SD_BSET_CORE:
        sceSdSetCoreAttr(batch[i].entry, batch[i].value);
        break;
      case SD_BGET_CORE:
        result = sceSdGetCoreAttr(batch[i].entry);
        break;
      case SD_BWRITE_IOP:
        *(u_int*)batch[i].value = batch[i].entry;
        break;
      case SD_BWRITE_EE:
        sd_batch_value = batch[i].entry;
        sd_transfer_to_ee((void*)batch[i].value, &sd_batch_value, 4);
        break;
      case SD_BRETURN_EE:
        sd_transfer_to_ee((void*)batch[i].value, returns, batch[i].entry);
        break;
      default:
        return -(int)(i + 1);
    }
    if (returns)
      returns[i] = result;
  }
  return i;
}

int sceSdProcBatchEx(sceSdBatch* batch, u_int returns[], u_int count, u_int voices) {
  u_int i, output = 0;
  for (i = 0; i < count; i++, output++) {
    u_int result = 0;
    int voice;
    switch (batch[i].func) {
      case SD_BSET_PARAM:
        if ((batch[i].entry & 0x3e) != 0x3e) {
          sceSdSetParam(batch[i].entry, batch[i].value);
        } else {
          for (voice = 0; voice < 24; voice++) {
            if ((1 << voice) & voices) {
              sceSdSetParam((batch[i].entry & ~0x3e) | (voice << 1), batch[i].value);
              output++;
            }
          }
          output--;
        }
        break;
      case SD_BGET_PARAM:
        if ((batch[i].entry & 0x3e) != 0x3e) {
          result = sceSdGetParam(batch[i].entry);
        } else {
          for (voice = 0; voice < 24; voice++) {
            if ((1 << voice) & voices)
              result = sceSdGetParam((batch[i].entry & ~0x3e) | (voice << 1));
            if (returns)
              returns[output] = result;
            output++;
          }
          output--;
        }
        break;
      case SD_BSET_SWITCH:
        sceSdSetSwitch(batch[i].entry, batch[i].value);
        break;
      case SD_BGET_SWITCH:
        result = sceSdGetSwitch(batch[i].entry);
        break;
      case SD_BSET_ADDR:
        if ((*(u_int*)&batch[i] & 0x7e0000) != 0x7e0000) {
          sceSdSetAddr(batch[i].entry, batch[i].value);
        } else {
          for (voice = 0; voice < 24; voice++) {
            if ((1 << voice) & voices) {
              sceSdSetAddr((batch[i].entry & ~0x3e) | (voice << 1), batch[i].value);
              output++;
            }
          }
          output--;
        }
        break;
      case SD_BGET_ADDR:
        if ((*(u_int*)&batch[i] & 0x7e0000) != 0x7e0000) {
          result = sceSdGetAddr(batch[i].entry);
        } else {
          for (voice = 0; voice < 24; voice++) {
            if ((1 << voice) & voices) {
              result = sceSdGetAddr((batch[i].entry & ~0x3e) | (voice << 1));
              if (returns)
                returns[output] = result;
              output++;
            }
          }
          output--;
        }
        break;
      case SD_BSET_CORE:
        sceSdSetCoreAttr(batch[i].entry, batch[i].value);
        break;
      case SD_BGET_CORE:
        result = sceSdGetCoreAttr(batch[i].entry);
        break;
      case SD_BWRITE_IOP:
        *(u_int*)batch[i].value = batch[i].entry;
        break;
      case SD_BWRITE_EE:
        sd_batch_value = batch[i].entry;
        sd_transfer_to_ee((void*)batch[i].value, &sd_batch_value, 4);
        break;
      case SD_BRETURN_EE:
        sd_transfer_to_ee((void*)batch[i].value, returns, batch[i].entry);
        break;
      default:
        return -(int)(i + 1);
    }
    if (returns)
      returns[output] = result;
  }
  return output;
}
