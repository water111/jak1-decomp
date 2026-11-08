.section .bss.mcman,"aw",@nobits
.align 2

.globl mcman_write_dma
mcman_write_dma:
  .space 1584

.globl mcman_read_dma
mcman_read_dma:
  .space 1584

  .space 24

.globl mcman_modified_time
mcman_modified_time:
  .space 8

  .space 480

.globl mcman_directory_buffer
mcman_directory_buffer:
  .space 512

.globl mcman_open_entry
mcman_open_entry:
  .space 512

.globl mcman_open_parent
mcman_open_parent:
  .space 512

.globl mcman_directory_index
mcman_directory_index:
  .space 4

.globl mcman_directory_length
mcman_directory_length:
  .space 4

.globl mcman_directory_path
mcman_directory_path:
  .space 1024

.globl mcman_directory_pattern
mcman_directory_pattern:
  .space 4

.globl mcman_rename_entry
mcman_rename_entry:
  .space 4

.globl mcman_rename_source
mcman_rename_source:
  .space 512

.globl mcman_rename_target
mcman_rename_target:
  .space 512

.globl mcman_ps1_directory_cluster
mcman_ps1_directory_cluster:
  .space 4

  .space 4

.globl mcman_ps1_directory_name
mcman_ps1_directory_name:
  .space 64

.globl mcman_ps1_packet
mcman_ps1_packet:
  .space 148

  .space 4

.globl mcman_ps1_tx
mcman_ps1_tx:
  .space 144

.globl mcman_ps1_rx
mcman_ps1_rx:
  .space 144

.globl mcman_ps1_page
mcman_ps1_page:
  .space 128

.globl mcman_ps1_semaphore
mcman_ps1_semaphore:
  .space 4

  .space 4

.globl mcman_ps1_delay
mcman_ps1_delay:
  .space 8

.globl mcman_cache_buffer
mcman_cache_buffer:
  .space 36864

.globl mcman_entry_storage
mcman_entry_storage:
  .space 576

.globl mcman_order_storage
mcman_order_storage:
  .space 144

.globl mcman_backupbuf
mcman_backupbuf:
  .space 16384

.globl mcman_fat_cache
mcman_fat_cache:
  .space 16416

.globl mcman_cache_entries
mcman_cache_entries:
  .space 4

.globl mcman_cache_order
mcman_cache_order:
  .space 4

  .space 8

.globl mcman_io_card
mcman_io_card:

.globl mcman_io_port
mcman_io_port:
  .space 4

.globl mcman_io_slot
mcman_io_slot:
  .space 4

.globl mcman_io_semaphore
mcman_io_semaphore:
  .space 4

  .space 4

.globl mcman_raw_write_dma
mcman_raw_write_dma:
  .space 1584
.globl mcman_raw_read_dma
mcman_raw_read_dma:
  .space 1584

.globl mcman_pagebuf
mcman_pagebuf:
  .space 1056

.globl mcman_handles
mcman_handles:
  .space 144

.globl mcman_devices
mcman_devices:
  .space 3104

.globl mcman_sio2_packet
mcman_sio2_packet:
  .space 148

  .space 12

.globl mcman_eccdata
mcman_eccdata:
  .space 512

.globl mcman_pagedata
mcman_pagedata:
  .space 128

.globl mcman_badblock_state
mcman_badblock_state:

.globl mcman_badblock_port
mcman_badblock_port:
  .space 4

.globl mcman_badblock_slot
mcman_badblock_slot:
  .space 4

.globl mcman_badblock
mcman_badblock:
  .space 4

.globl mcman_replacement_clusters
mcman_replacement_clusters:
  .space 64

  .space 4

.globl mcman_raw_packet
mcman_raw_packet:
  .space 148
