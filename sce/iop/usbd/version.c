#include "sce/iop/usbd/internal.h"

#if USBD_DRIVER_VERSION >= 0x010100
const char* usb_version = "Version 1.1.0";
#elif USBD_DRIVER_VERSION >= 0x010000
const char* usb_version = "Version 1.0.0";
#else
const char* usb_version = "Version 0.15.0 Fri Feb 16 21:15:58 JST 2001";
#endif
