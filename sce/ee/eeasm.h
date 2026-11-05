#ifndef SCE_EE_EEASM_H
#define SCE_EE_EEASM_H

#define VPU_STAT_VU1_BUSY 0x100

#define EE_GET_VPU_STAT()                           \
  ({                                                \
    unsigned status;                                \
    __asm__ volatile("cfc2 %0,$29" : "=r"(status)); \
    status;                                         \
  })

#define EE_RESET_VU1(status)                                                           \
  __asm__ volatile("sync.l\n\tcfc2 %0,$28\n\tori %0,%0,0x200\n\tctc2 %0,$28\n\tsync.p" \
                   : "+r"(status))

#define EE_SYNC_L() __asm__ volatile("sync.l")

#endif
