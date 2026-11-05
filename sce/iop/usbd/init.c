#include "sce/iop/stdio/stdio.h"
#include "sce/iop/usbd/internal.h"

extern const char* usb_version;
int usb_initialize_controller(void);
int usb_initialize_hubs(void);

int usb_initialize(void) {
  printf("USB Driver (%s)", usb_version);
  printf("\n");
  if (usb_initialize_controller() < 0)
    return -1;
  if (usb_initialize_hubs() < 0)
    return -1;
  return 0;
}
