#ifndef SCE_IOP_989SND_EXPORT_GP_H
#define SCE_IOP_989SND_EXPORT_GP_H

// save/restore gp around callbacks from another IOP module.
#define SND_SAVE_GP(saved) __asm__("move %0,$28\n\tla $28,_gp" : "=r"(saved) : : "$28", "memory")
#define SND_RESTORE_GP(saved) __asm__("move $28,%0" : : "r"(saved) : "$28", "memory")

#endif
