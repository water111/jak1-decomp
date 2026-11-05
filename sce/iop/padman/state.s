# Same odd issue with these variables - not sure how they could be laid out like this in C.
.bss
.align 4
.globl pad_states
pad_states:
  .space 3648
.globl pad_open_slots
pad_open_slots:
  .space 8
.globl pad_runtime
.globl pad_vblank
pad_runtime:
pad_vblank:
  .space 24
  .space 4
.globl pad_initialized
pad_initialized:
  .space 4
.globl pad_ee_buffer
pad_ee_buffer:
  .space 4
.globl pad_high_priority
pad_high_priority:
  .space 4
.globl pad_low_priority
pad_low_priority:
  .space 4
  .space 4
.globl pad_dma_transfers
pad_dma_transfers:
  .space 256
