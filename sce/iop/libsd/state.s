# It doesn't seem possible to recreate these variables in C, these might be created
# by linker scripts or assembly like this.
.bss
.align 2
.globl sd_block_buffer
sd_block_buffer:
  .space 8
.globl sd_block_address
sd_block_address:
  .space 8
.globl sd_block_size
sd_block_size:
  .space 8
  .space 8
.globl sd_spdif_mode
sd_spdif_mode:
  .space 4
  .space 12
.globl sd_current_core
sd_current_core:
  .space 4
  .space 4
.globl sd_transfer_pending
sd_transfer_pending:
  .space 8
.globl sd_transfer_events
sd_transfer_events:
  .space 8
.globl sd_effect_addresses
sd_effect_addresses:
  .space 8
.comm sd_transfer_callbacks,8
.comm sd_batch_value,4
