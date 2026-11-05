#ifndef SCE_IOP_USBD_API_GP_H
#define SCE_IOP_USBD_API_GP_H

// Public calls may arrive with another IRX's global pointer and this module
// needs to swap GP and revert.
#define USB_SAVE_GP(saved) __asm__ volatile("move %0,$gp\n\tla $gp,_gp" : "=r"(saved))
#define USB_RESTORE_GP(saved) __asm__ volatile("move $gp,%0" : : "r"(saved))
#define USB_ERROR_LOCK 261

#endif
