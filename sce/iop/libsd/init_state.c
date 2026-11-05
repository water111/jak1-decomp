#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/hardware.h"
#include "sce/iop/libsd/internal.h"
#include "sce/iop/thevent/thevent.h"

#define IRQ_SPU2 9
#define IRQ_SPU2_DMA0 36
#define IRQ_SPU2_DMA1 40

int sd_spu2_interrupt(void* argument) {
  if (sd_spu2_handler)
    sd_spu2_handler((SPU2_REG(0, SPU2_SPDIF_IRQ_OFFSET) & 0xc) >> 2, sd_spu2_argument);
  else if (sd_irq_callback)
    sd_irq_callback(0);
  return 1;
}

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
  event.initPattern = 0;
  event.option = 0;
  sd_transfer_events[0] = CreateEventFlag(&event);
  if (sd_transfer_events[0] <= 0)
    return -1;
  sd_transfer_events[1] = CreateEventFlag(&event);
  if (sd_transfer_events[1] <= 0)
    return -1;
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
  for (core = 0; core < 2; core++) {
    sd_effect_attrs[core].mode = 0;
    sd_effect_attrs[core].depth_left = 0;
    sd_effect_attrs[core].depth_right = 0;
    sd_effect_attrs[core].delay = 0;
    sd_effect_attrs[core].feedback = 0;
  }
  if (!(mode & 0xf)) {
    u_int* address;
    core = 0;
    address = sd_effect_addresses;
    for (; core < 2; core++) {
      u_int end = sd_get_effect_end(core);
      int size = sd_effect_sizes[0] * 8 - 2;
      sd_current_core = core;
      end -= size;
      *address = end;

      // TODO needed for getting the store above and call right
      asm("" : "=r"(end) : "0"(end));
      asm("" : "=r"(end) : "0"(end));

      sd_set_address(0x170, end, 1);
      address++;
    }
  }
  sd_transfer_active[0] = 0;
  sd_transfer_active[1] = 0;
  DisableIntr(IRQ_SPU2_DMA0, &interrupt_mode);
  DisableIntr(IRQ_SPU2_DMA1, &interrupt_mode);
  DisableIntr(IRQ_SPU2, &interrupt_mode);
  ReleaseIntrHandler(IRQ_SPU2_DMA0);
  ReleaseIntrHandler(IRQ_SPU2_DMA1);
  ReleaseIntrHandler(IRQ_SPU2);
  return 0;
}

int sceSdInit(u_int mode) {
  sd_init_hardware();
  if (sd_init_state(mode) < 0)
    return -1;
  sd_init_voices();
  sd_init_core_volume(mode & 0xf);
  EnableIntr(IRQ_SPU2_DMA0);
  EnableIntr(IRQ_SPU2_DMA1);
  EnableIntr(IRQ_SPU2);
  RegisterIntrHandler(IRQ_SPU2_DMA0, 1, sd_transfer_interrupt, &sd_transfer_interrupts[0]);
  RegisterIntrHandler(IRQ_SPU2_DMA1, 1, sd_transfer_interrupt, &sd_transfer_interrupts[1]);
  RegisterIntrHandler(IRQ_SPU2, 1, sd_spu2_interrupt, sd_spu2_argument);
  sd_initialized = 1;
  return 0;
}

int sceSdQuit(void) {
  int interrupt_mode;
  sd_stop_transfer(0);
  sd_stop_transfer(1);
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
  return 0;
}
