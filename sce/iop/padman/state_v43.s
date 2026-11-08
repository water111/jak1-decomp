.bss
.align 4
.globl pad_states
pad_states:
  .space 3616
.globl pad_ports
pad_ports:
  .space 160
.globl pad_runtime
.globl pad_vblank
pad_runtime:
pad_vblank:
  .space 28
.globl pad_initialized
pad_initialized:
  .space 8
.globl pad_ee_buffer
pad_ee_buffer:
  .space 4
.globl pad_high_priority
pad_high_priority:
  .space 4
.globl pad_low_priority
pad_low_priority:
  .space 8
  .space 4
.globl pad_dma_transfers
pad_dma_transfers:
  .space 256
