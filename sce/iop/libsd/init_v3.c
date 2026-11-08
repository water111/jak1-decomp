#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thevent/thevent.h"

#define IRQ_SPU2 9
#define IRQ_SPU2_DMA0 36
#define IRQ_SPU2_DMA1 40

SdEffectAttr sd_effect_attrs[2] = {{0}, {0}};
volatile int sd_transfer_pending[2] = {0, 0};
volatile int sd_transfer_events[2] = {0, 0};
int sd_initialized = 0;
sceSdCallback sd_irq_callback = 0;
sceSdSpu2IntrHandler volatile sd_spu2_handler = 0;
void* sd_spu2_argument = 0;
sceSdTransIntrHandler volatile sd_transfer_handlers[2] = {0, 0};
#if LIBSD_VERSION >= 0x0303
sceSdTransIntrHandler volatile sd_effect_clear_handlers[2] = {0, 0};
#endif
SdTransferInterrupt sd_transfer_interrupts[2] = {{0, 0}, {1, 0}};

int sd_spu2_interrupt(void* argument) {
  u_int cores;
  if (sd_spu2_handler || sd_irq_callback) {
    while ((cores = (SPU2_REG(0, SPU2_SPDIF_IRQ_OFFSET) & 0xc) >> 2)) {
      if (cores & 1)
        SPU2_ATTR(0) &= ~(1 << SPU2_ATTR_IRQ_SHIFT);
      if (cores & 2)
        SPU2_ATTR(1) &= ~(1 << SPU2_ATTR_IRQ_SHIFT);
      if (sd_spu2_handler)
        sd_spu2_handler(cores, sd_spu2_argument);
      else if (sd_irq_callback)
        sd_irq_callback(0);
    }
  }
  return 1;
}

#if LIBSD_VERSION >= 0x0303
u_char sd_silent_adpcm[16] = {0, 7};
#else
u_char sd_silent_adpcm[16] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
#endif

void sd_init_voices(void) {
  volatile int i;
  u_short attr;
  u_short* sample = (u_short*)sd_silent_adpcm;
  volatile u_short* voice0 = (volatile u_short*)(SPU2_BASE);
  volatile u_short* voice1 = (volatile u_short*)(SPU2_BASE + SPU2_CORE_STRIDE);
#if LIBSD_VERSION >= 0x0303
  volatile u_short* address0 = (volatile u_short*)(SPU2_BASE + 0x1c0);
#else
  // Matching: keep the voice-address pointer in the original register.
  register volatile u_short* address0 asm("$19") = (volatile u_short*)(SPU2_BASE + 0x1c0);
#endif
  volatile u_short* address1 = (volatile u_short*)(SPU2_BASE + SPU2_CORE_STRIDE + 0x1c0);
#if LIBSD_VERSION >= 0x0303
  SPU2_ATTR(0) &= ~SPU2_ATTR_TRANSFER_MASK;
#endif
  SPU2_TRANSFER_ADDR_HIGH(0) = 0;
  SPU2_TRANSFER_ADDR_LOW(0) = 0x2800;
  for (i = 0; i < (int)(sizeof(sd_silent_adpcm) / sizeof(*sample)); i++)
    SPU2_TRANSFER_DATA(0) = *sample++;
  attr = SPU2_ATTR(0);
  SPU2_ATTR(0) = (attr & ~0x30) | 0x10;
#if LIBSD_VERSION >= 0x0303
  i = 0;
#endif
  attr = SPU2_STATUS(0);
  while (attr & SPU2_STATUS_IO_BUSY) {
    sd_delay(1);
#if LIBSD_VERSION >= 0x0303
    if (++i > SPU2_DREQ_TIMEOUT)
      break;
#endif
    attr = SPU2_STATUS(0);
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
  sd_delay(3);
  SPU2_KEY_OFF_LOW(0) = SPU2_KEY_OFF_LOW(1) = 0xffff;
  SPU2_KEY_OFF_HIGH(0) = SPU2_KEY_OFF_HIGH(1) = 0xff;
  sd_delay(3);
  SPU2_END_LOW(0) = SPU2_END_HIGH(0) = 0;
#if LIBSD_VERSION >= 0x0303
  SPU2_END_LOW(1) = SPU2_END_HIGH(1) = 0;
#endif
}

#if LIBSD_VERSION >= 0x0303
int sd_init_state(u_int mode) {
  struct EventFlagParam event;
  int interrupt_mode;
  int result;
  DisableIntr(IRQ_SPU2_DMA0, &interrupt_mode);
  DisableIntr(IRQ_SPU2_DMA1, &interrupt_mode);
  DisableIntr(IRQ_SPU2, &interrupt_mode);
  ReleaseIntrHandler(IRQ_SPU2_DMA0);
  ReleaseIntrHandler(IRQ_SPU2_DMA1);
  ReleaseIntrHandler(IRQ_SPU2);
  sd_transfer_active[0] = 0;
  sd_transfer_active[1] = 0;
  sd_transfer_status[0] = 1;
  sd_transfer_status[1] = 1;
  sd_transfer_pending[0] = 0;
  sd_transfer_pending[1] = 0;
  sd_spu2_handler = 0;
  sd_transfer_handlers[0] = 0;
  sd_transfer_handlers[1] = 0;
  sd_effect_clear_handlers[0] = 0;
  sd_effect_clear_handlers[1] = 0;
  sd_transfer_interrupts[0].mode = 0;
  sd_transfer_interrupts[1].mode = 1;
  sd_transfer_interrupts[0].argument = 0;
  sd_transfer_interrupts[1].argument = 0;
  sd_spu2_argument = 0;
  sd_irq_callback = 0;
  sd_transfer_callbacks[0] = 0;
  sd_transfer_callbacks[1] = 0;
  if (!(mode & 0xf)) {
    bzero(sd_effect_attrs, sizeof(sd_effect_attrs));
    sd_effect_addresses[0] = 0x1dfff0;
    sd_effect_addresses[1] = 0x1ffff0;
    SPU2_REG(0, SPU2_EFFECT_START_OFFSET) = 0xe;
    SPU2_REG(0, SPU2_EFFECT_START_OFFSET + 2) = 0xfff8;
    SPU2_REG(1, SPU2_EFFECT_START_OFFSET) = 0xf;
    SPU2_REG(1, SPU2_EFFECT_START_OFFSET + 2) = 0xfff8;
  }
  result = 0;
  event.attr = 2;
  event.initPattern = SD_TRANSFER_EVENT_DONE;
  event.option = 0;
  if (sd_transfer_events[0] > 0) {
    if (QueryIntrContext())
      iSetEventFlag(sd_transfer_events[0], SD_TRANSFER_EVENT_DONE);
    else
      SetEventFlag(sd_transfer_events[0], SD_TRANSFER_EVENT_DONE);
  } else {
    sd_transfer_events[0] = CreateEventFlag(&event);
    if (sd_transfer_events[0] <= 0)
      result = SD_ERROR_EVENT_FLAG;
  }
  if (sd_transfer_events[1] > 0) {
    if (QueryIntrContext())
      iSetEventFlag(sd_transfer_events[1], SD_TRANSFER_EVENT_DONE);
    else
      SetEventFlag(sd_transfer_events[1], SD_TRANSFER_EVENT_DONE);
  } else {
    sd_transfer_events[1] = CreateEventFlag(&event);
    if (sd_transfer_events[1] <= 0)
      result = SD_ERROR_EVENT_FLAG;
  }
  return result;
}

#else
int sd_init_state(u_int mode) {
  struct EventFlagParam event;
  int interrupt_mode;
  int core;
  if (!(mode & 0xf))
    sd_reset(0);
  sd_transfer_status[0] = 1;
  sd_transfer_status[1] = 1;
  sd_transfer_pending[0] = 0;
  sd_transfer_pending[1] = 0;
  event.attr = 2;
  event.initPattern = SD_TRANSFER_EVENT_DONE;
  event.option = 0;
  if (sd_transfer_events[0] > 0) {
    if (QueryIntrContext())
      iSetEventFlag(sd_transfer_events[0], SD_TRANSFER_EVENT_DONE);
    else
      SetEventFlag(sd_transfer_events[0], SD_TRANSFER_EVENT_DONE);
  } else {
    sd_transfer_events[0] = CreateEventFlag(&event);
    if (sd_transfer_events[0] <= 0)
      return SD_ERROR_EVENT_FLAG;
  }
  if (sd_transfer_events[1] > 0) {
    if (QueryIntrContext())
      iSetEventFlag(sd_transfer_events[1], SD_TRANSFER_EVENT_DONE);
    else
      SetEventFlag(sd_transfer_events[1], SD_TRANSFER_EVENT_DONE);
  } else {
    sd_transfer_events[1] = CreateEventFlag(&event);
    if (sd_transfer_events[1] <= 0)
      return SD_ERROR_EVENT_FLAG;
  }
  sd_transfer_active[0] = 0;
  sd_transfer_active[1] = 0;
  sd_transfer_callbacks[0] = 0;
  sd_transfer_callbacks[1] = 0;
  sd_spu2_handler = 0;
  sd_transfer_handlers[0] = 0;
  sd_transfer_handlers[1] = 0;
  sd_irq_callback = 0;
  sd_spu2_argument = 0;
  sd_transfer_interrupts[0].mode = 0;
  sd_transfer_interrupts[1].mode = 1;
  sd_transfer_interrupts[0].argument = 0;
  sd_transfer_interrupts[1].argument = 0;
  bzero(sd_effect_attrs, sizeof(sd_effect_attrs));
  if (!(mode & 0xf)) {
    sd_effect_addresses[0] = 0x1dfff0;
    sd_effect_addresses[1] = 0x1ffff0;
    SPU2_REG(0, SPU2_EFFECT_START_OFFSET) = 0xe;
    SPU2_REG(0, SPU2_EFFECT_START_OFFSET + 2) = 0xfff8;
    SPU2_REG(1, SPU2_EFFECT_START_OFFSET) = 0xf;
    SPU2_REG(1, SPU2_EFFECT_START_OFFSET + 2) = 0xfff8;
  }
  DisableIntr(IRQ_SPU2_DMA0, &interrupt_mode);
  DisableIntr(IRQ_SPU2_DMA1, &interrupt_mode);
  DisableIntr(IRQ_SPU2, &interrupt_mode);
  ReleaseIntrHandler(IRQ_SPU2_DMA0);
  ReleaseIntrHandler(IRQ_SPU2_DMA1);
  ReleaseIntrHandler(IRQ_SPU2);
  return 0;
}

#endif

void sd_init_module_state(void) {
  sd_initialized = 0;
  sd_transfer_events[0] = 0;
  sd_transfer_events[1] = 0;
}

int sceSdInit(u_int mode) {
  int result;
  sd_init_hardware();
#if LIBSD_VERSION >= 0x0303
  if (!(mode & 0xf))
    sd_reset(0);
  result = sd_init_state(mode);
#else
  result = sd_init_state(mode);
  if (result < 0)
    return result;
#endif
  sd_init_voices();
  sd_init_core_volume(mode & 0xf);
  EnableIntr(IRQ_SPU2_DMA0);
  EnableIntr(IRQ_SPU2_DMA1);
  EnableIntr(IRQ_SPU2);
  RegisterIntrHandler(IRQ_SPU2_DMA0, 1, sd_transfer_interrupt, &sd_transfer_interrupts[0]);
  RegisterIntrHandler(IRQ_SPU2_DMA1, 1, sd_transfer_interrupt, &sd_transfer_interrupts[1]);
  RegisterIntrHandler(IRQ_SPU2, 1, sd_spu2_interrupt, sd_spu2_argument);
  sd_initialized = 1;
  sd_init_transfer_state();
#if LIBSD_VERSION >= 0x0303
  return result;
#else
  return 0;
#endif
}

int sceSdQuit(void) {
  int interrupt_mode;
  sd_dma_control(SD_DMA_STOP);
  sd_dma_control((1 << SD_DMA_CORE_SHIFT) | SD_DMA_STOP);
  if (sd_transfer_events[0] > 0)
    DeleteEventFlag(sd_transfer_events[0]);
  if (sd_transfer_events[1] > 0)
    DeleteEventFlag(sd_transfer_events[1]);
  DisableIntr(IRQ_SPU2_DMA1, &interrupt_mode);
  DisableIntr(IRQ_SPU2_DMA0, &interrupt_mode);
  DisableIntr(IRQ_SPU2, &interrupt_mode);
  ReleaseIntrHandler(IRQ_SPU2_DMA1);
  ReleaseIntrHandler(IRQ_SPU2_DMA0);
  ReleaseIntrHandler(IRQ_SPU2);
  sd_init_transfer_state();
  return 0;
}
