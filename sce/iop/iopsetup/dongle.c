#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/usbd/usbd.h"

#ifndef IOPSETUP_REVISION
#define IOPSETUP_REVISION 20011031
#endif

#define WIBU_VENDOR_ID 0x064f
#define WIBU_FIRST_PRODUCT_ID 0x0bd7
#define WIBU_SERIAL_BASE 5000000
#define WIBU_BOX_COUNT 2

typedef struct {
  int device_id;
  UsbDeviceDescriptor* device;
  UsbConfigDescriptor* config;
  UsbInterfaceDescriptor* interface;
  int pipe;
  u_char reserved[44];
  u_char receive[8];

  union {
    u_int value;
    u_char bytes[4];
  } serial;

  u_char mask;
  u_char padding[51];
} WibuBox;

u_char wibu_mask = 0;
int wibu_id = 0;
WibuBox s_awibubox[WIBU_BOX_COUNT];

WibuBox* PwibuboxFirstEmpty(void) {
  u_int i;
  WibuBox* box;
  for (i = 0; i < WIBU_BOX_COUNT; i++) {
    box = &s_awibubox[i];
    if (!box->device_id)
      return box;
  }
  return 0;
}

WibuBox* PwibuboxFirstFull(void) {
  u_int i;
  WibuBox* box;
  for (i = 0; i < WIBU_BOX_COUNT; i++) {
    box = &s_awibubox[i];
    if (box->device_id)
      return box;
  }
  return 0;
}

WibuBox* PwibuboxAlloc(int device) {
  WibuBox* box = PwibuboxFirstEmpty();
  if (box)
    box->device_id = device;
  return box;
}

void FreeWibubox(WibuBox* box) {
  memset(box, 0, sizeof(*box));
}

int FWibuDevice(int id, WibuBox* box) {
  UsbDeviceDescriptor* device;
  UsbConfigDescriptor* config;
  UsbInterfaceDescriptor* interface;
  device = sceUsbdScanStaticDescriptor(id, 0, USB_DESCRIPTOR_DEVICE);
  if (!device) {
    printf("wibu_iop: no device descriptor!\n");
    return 0;
  }
  if (device->vendor != WIBU_VENDOR_ID || device->product < WIBU_FIRST_PRODUCT_ID ||
      device->device_class != USB_CLASS_VENDOR || device->subclass != 0 ||
      device->protocol != USB_CLASS_VENDOR || device->configuration_count != 1) {
    printf("wibu_iop: not a WIBU-BOX\n");
    return 0;
  }
  config = sceUsbdScanStaticDescriptor(id, 0, USB_DESCRIPTOR_CONFIGURATION);
  if (!config) {
    printf("wibu_iop: no configuration descriptor!\n");
    return 0;
  }
  if (config->interface_count != 1 || config->value != 1) {
    printf("wibu_iop: bad configuration\n");
    return 0;
  }
  interface = sceUsbdScanStaticDescriptor(id, 0, USB_DESCRIPTOR_INTERFACE);
  if (!interface) {
    printf("wibu_iop: no interface descriptor!\n");
    return 0;
  }
  if (interface->endpoint_count != 0 || interface->interface_class != USB_CLASS_VENDOR ||
      interface->subclass != 0 || interface->protocol != USB_CLASS_VENDOR) {
    printf("wibu_iop: bad interface\n");
    return 0;
  }
  if (box) {
    box->device = device;
    box->config = config;
    box->interface = interface;
  }
  return 1;
}

void WibuSerialCallback(int result, int bytes, void* argument) {
  WibuBox* box = argument;
  if (result) {
    printf("wibu_iop: WibuSerialCallback failed with %d\n", result);
    FreeWibubox(box);
    return;
  }
  if (bytes != 6) {
    printf("wibu_iop: WibuSerialCallback got %d bytes, not 6\n", bytes);
    FreeWibubox(box);
    return;
  }
  box->serial.bytes[0] = box->receive[0];
  box->serial.bytes[1] = box->receive[1];
  box->serial.bytes[2] = box->receive[2];
  box->serial.bytes[3] = box->receive[3];
  box->mask = box->receive[4];
  printf("wibu_iop: got serial number %d-%d\n", box->mask, box->serial.value);
  wibu_mask = box->mask;
  wibu_id = box->serial.value;
}

void WibuRequestSerial(WibuBox* box) {
  int result;
  UsbDeviceRequest request;
  request.request_type = 0xc0;
  request.request = 4;
  request.value = 0;
  request.index = 0;
  request.length = 6;
  result = sceUsbdTransferPipe(box->pipe, box->receive, request.length, &request,
                               WibuSerialCallback, box);
  if (result)
    printf("wibu_iop: serial request returned 0x%x\n", result);
}

int FWibuProbe(int id) {
  printf("wibu_iop: 0x%x probing\n", id);
  if (!PwibuboxFirstEmpty()) {
    printf("wibu_iop: slots filled\n");
    return 0;
  }
  return FWibuDevice(id, 0);
}

int RetWibuAttach(int id) {
  WibuBox* box;
  printf("wibu_iop: 0x%x attaching\n", id);
  box = PwibuboxAlloc(id);
  if (!box) {
    printf("wibu_iop: slots filled\n");
    return -1;
  }
  if (!FWibuDevice(id, box)) {
    FreeWibubox(box);
    return -1;
  }
  box->pipe = sceUsbdOpenPipe(id, 0);
  if (box->pipe < 0) {
    printf("wibu_iop: sceUsbdOpenPipe failed with %d\n", box->pipe);
    FreeWibubox(box);
    return -1;
  }
  sceUsbdSetPrivateData(id, box);
  WibuRequestSerial(box);
  printf("wibu_iop: 0x%x attached\n", id);
  return 0;
}

int RetWibuDetach(int id) {
  WibuBox* box;
  printf("wibu_iop: 0x%x detaching\n", id);
  box = sceUsbdGetPrivateData(id);
  if (!box) {
    printf("wibu_iop: no private data\n");
    return -1;
  }
  FreeWibubox(box);
  printf("wibu_iop: 0x%x detached\n", id);
  return 0;
}

UsbDriver s_uldd = {0, 0, "iop", FWibuProbe, RetWibuAttach, RetWibuDetach};

void wibuInit(void) {
  int result;
  printf("wibu_iop: started...\n");
  result = sceUsbdRegisterLdd(&s_uldd);
  if (result) {
    printf("wibu_iop: sceUsbdRegisterLdd failed with 0x%x\n", result);
    return;
  }
  printf("wibu_iop: usb driver registered...\n");
  result = sceUsbdChangeThreadPriority(15, 16);
  if (result) {
    printf("wibu_iop: sceUsbdChangeThreadPriority failed with 0x%x\n", result);
    sceUsbdUnregisterLdd(&s_uldd);
    return;
  }
}

// allowlist of dongles
static int dongle_table[] = {
    0x1a7, 0x1af, 0x1b0, 0xea7, 0xe63, 0xe5b, 0xe61, 0xeb9, 0xe5d, 0xec0, 0xe55, 0xe51, 0xeb6,
    0xec4, 0xebf, 0xeb8, 0xeb4, 0xe59, 0xe53, 0xe5f, 0xe57, 0xeb5, 0xeb1, 0xec6, 0xeae, 0xec1,
    0xeba, 0xec7, 0xec3, 0xebe, 0xe9f, 0xea0, 0xebd, 0xec2, 0xe9a, 0xeb0, 0xec5, 0xebc, 0xe9b,
    0xeb7, 0xe98, 0xe99, 0xe97, 0xe9d, 0xeb3, 0xe9e, 0xeaa, 0xea9, 0xe9c, 0xe96, 0xea1};

// this function loops forever if the dongle doesn't match, blocking the EE on loading
// this module.
int start(int argc, char** argv) {
  int i;
  int found;
  wibuInit();
  for (;;) {
    if (wibu_mask == 7) {
      found = 0;
      for (i = 0; i < 51; i++) {
        if (wibu_id == dongle_table[i] + WIBU_SERIAL_BASE)
          found = 1;
      }
#if IOPSETUP_REVISION >= 20030405
      if (wibu_id >= 5009148 && wibu_id < 5009898)
        found = 1;
      if (wibu_id >= 5005855 && wibu_id < 5006855)
        found = 1;
#if IOPSETUP_REVISION >= 20030713
      if (wibu_id >= 5039335 && wibu_id < 5040335)
        found = 1;
#endif
#else
      if (wibu_id > 0x4c6efb && wibu_id <= 0x4c721b)
        found = 1;
#endif
      if (found)
        break;
    }
    DelayThread(1000);
  }
  return 0;
}
