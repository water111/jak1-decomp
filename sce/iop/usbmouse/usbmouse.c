#include "sce/iop/usbmouse/usbmouse.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sifcmd/sifrpc.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/sysmem/sysmem.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/usbd/usbd.h"

#define USBMOUSE_THREAD_PRIORITY 50
#define USBMOUSE_THREAD_STACK_SIZE 2048
#define USBMOUSE_RPC_BUFFER_SIZE 128
#define USBMOUSE_LOAD_NORMAL 0
#define USBMOUSE_LOAD_AUTO 1
#define USB_CLASS_HID 3
#define USB_HID_BOOT_SUBCLASS 1
#define USB_HID_MOUSE_PROTOCOL 2
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_TYPE_MASK 3
#define USB_ENDPOINT_INTERRUPT 3
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_SET_INTERFACE 11
#define USB_REQUEST_INTERFACE 1

struct MouseUnit {
  int device;
  int index;
  int control_pipe;
  int interrupt_pipe;
  int report_size;
  int interface_number;
  int alternate;
  int reserved;
  int read_index;
  int write_index;
  int report_count;
  struct UsbMouseReport reports[USBMOUSE_REPORT_COUNT];
  unsigned char data[0];
};

struct MouseState {
  int next_index;
  int count;
  struct MouseUnit* units[USBMOUSE_MAX_DEVICES];
  int thread;
  sceSifQueueData queue;
  sceSifServeData server;
};

static void sifrpc_thread(void);
static void* sifrpc_server(unsigned int command, void* data, int size);
static void* get_connect_info(unsigned int command, void* data, int size);
static void* read_mouse(unsigned int command, void* data, int size);
static void* get_device_location(unsigned int command, void* data, int size);
static int usbmouse_probe(int device);
static int usbmouse_attach(int device);
static struct MouseUnit* unit_alloc(int device, int size, int interface_number, int alternate);
static void unit_free(struct MouseUnit* unit);
static void set_config_done(int result, int bytes, void* argument);
static void set_interface_done(int result, int bytes, void* argument);
static void data_transfer(struct MouseUnit* unit);
static void data_transfer_done(int result, int bytes, void* argument);
static int usbmouse_detach(int device);
static int stop_usbmouse(void);

struct ModuleInfo Module = {"SCE_USB_MOUSE_SAMPLE", 0x0202};
static int load_mode = USBMOUSE_LOAD_NORMAL;
static UsbDriver usbmouse_ops = {
    0, 0, "usbmouse", usbmouse_probe, usbmouse_attach, usbmouse_detach};
static struct MouseState USBMOUSE;
static unsigned char buffer[USBMOUSE_RPC_BUFFER_SIZE];
static int resident_flag;

int start(int count, char** argv) {
  struct ThreadParam param;
  int i, j, result;
  int argc = count;
  UsbDriver* driver;
  if (argc < 0)
    return stop_usbmouse();
  else {
    USBMOUSE.next_index = 0;
    USBMOUSE.count = 0;
    for (i = 0; i < USBMOUSE_MAX_DEVICES; i++)
      USBMOUSE.units[i] = 0;
    load_mode = USBMOUSE_LOAD_NORMAL;
    for (i = 0; i < argc; i++) {
      for (j = 0; argv[i][j]; j++) {
        if (argv[i][j] == '=') {
          argv[i][j] = 0;
          j++;
          break;
        }
      }
      if (!strcmp(argv[i], "lmode") && !strcmp(argv[i] + j, "AUTOLOAD")) {
        load_mode = USBMOUSE_LOAD_AUTO;
        break;
      }
    }
    switch (load_mode) {
      case USBMOUSE_LOAD_NORMAL:
        resident_flag = 1;
        break;
      case USBMOUSE_LOAD_AUTO:
        printf("usbmouse : AUTOLOAD MODE\n");
        resident_flag = 0;
        break;
    }
    driver = &usbmouse_ops;
    result = sceUsbdRegisterLdd(driver);
    if (result)
      printf("usbmouse: sceUsbdRegisterLdd -> 0x%x\n", result);
    if (resident_flag) {
      param.attr = TH_C;
      param.entry = sifrpc_thread;
      param.initPriority = USBMOUSE_THREAD_PRIORITY;
      param.stackSize = USBMOUSE_THREAD_STACK_SIZE;
      param.option = 0;
      USBMOUSE.thread = CreateThread(&param);
      if (USBMOUSE.thread > 0) {
        StartThread(USBMOUSE.thread, 0);
        return 2;
      }
    }
    sceUsbdUnregisterLdd(driver);
    return 1;
  }
}

static void sifrpc_thread(void) {
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&USBMOUSE.queue, GetThreadId());
#ifdef USBMOUSE_ORDER_RPC_SYMBOLS
  // Declare the buffer before the RPC callback to preserve the later builds' symbol-table order.
  // This assembler directive emits no instructions or data.
  asm(".local buffer");
#endif
  sceSifRegisterRpc(&USBMOUSE.server, USBMOUSE_RPC_ID, sifrpc_server, buffer, 0, 0,
                    &USBMOUSE.queue);
  sceSifRpcLoop(&USBMOUSE.queue);
}

static void* sifrpc_server(unsigned int command, void* data, int size) {
  switch (command) {
    case USBMOUSE_GET_CONNECT_INFO:
      get_connect_info(command, data, size);
      break;
    case USBMOUSE_READ:
      read_mouse(command, data, size);
      break;
    case USBMOUSE_GET_DEVICE_LOCATION:
      get_device_location(command, data, size);
      break;
    default:
      printf("Illegal function No.%d from EE\n", command);
      break;
  }
  return data;
}

static void* get_connect_info(unsigned int command, void* data, int size) {
  unsigned char* out = data;
  int i;
  *out++ = USBMOUSE_MAX_DEVICES;
  *out++ = USBMOUSE.count;
  for (i = 0; i < USBMOUSE_MAX_DEVICES; i++) {
    if (USBMOUSE.units[i])
      *out++ = 1;
    else
      *out++ = 0;
  }
  return data;
}

static void* read_mouse(unsigned int command, void* data, int size) {
  unsigned char* out = data;
  struct MouseUnit* unit;
  struct UsbMouseReport* report;
  int i, intr_state;
  // BUG: Index 8 is accepted even though the unit array has only eight entries.
  if (*out > USBMOUSE_MAX_DEVICES) {
    printf("Illegal index_no : %d\n", *out);
    *out = 0;
    return data;
  }
  unit = USBMOUSE.units[*out];
  if (!unit) {
    printf("USB mouse %d is not connected!\n", *out);
    *out = 0;
    return data;
  }
  CpuSuspendIntr(&intr_state);
  if (unit->report_count > 0) {
    report = &unit->reports[unit->read_index];
    *out++ = report->size;
    for (i = 0; i < unit->report_size; i++)
      *out++ = *(i + report->data);
    if (++unit->read_index >= USBMOUSE_REPORT_COUNT)
      unit->read_index = 0;
    unit->report_count--;
  } else {
    *out++ = 0;
    for (i = 0; i < unit->report_size; i++)
      *out++ = 0;
  }
  CpuResumeIntr(intr_state);
  return data;
}

static void* get_device_location(unsigned int command, void* data, int size) {
  unsigned char* out = data;
  unsigned char location[USBMOUSE_LOCATION_BYTES];
  int i, result;
  if (*out >= USBMOUSE_MAX_DEVICES) {
    printf("Illegal USB mouse number! : %d\n", *out);
    for (i = 0; i < USBMOUSE_LOCATION_REPLY_BYTES; i++)
      *out++ = 0;
  } else if (!USBMOUSE.units[*out]) {
    printf("USB mouse %d is not connected!\n", *out);
    for (i = 0; i < USBMOUSE_LOCATION_REPLY_BYTES; i++)
      *out++ = 0;
  } else {
    result = sceUsbdGetDeviceLocation(USBMOUSE.units[*out]->device, location);
    if (result) {
      printf("usbmouse%d: %s -> 0x%x\n", USBMOUSE.units[*(unsigned char*)data]->index,
             "sceUsbdGetDeviceLocation", result);
      for (i = 0; i < USBMOUSE_LOCATION_REPLY_BYTES; i++)
        *out++ = 0;
    } else {
      for (i = 0; i < USBMOUSE_LOCATION_BYTES; i++)
        *out++ = location[i];
    }
  }
  return data;
}

static int usbmouse_probe(int device) {
  UsbDeviceDescriptor* desc;
  UsbInterfaceDescriptor* interface;
  printf("usbmouse : probe\n");
  if (USBMOUSE.count >= USBMOUSE_MAX_DEVICES)
    return 0;
  desc = sceUsbdScanStaticDescriptor(device, 0, USB_DESCRIPTOR_DEVICE);
  if (!desc)
    return 0;
  if (desc->device_class != 0 || desc->configuration_count != 1)
    return 0;
  interface = sceUsbdScanStaticDescriptor(device, desc, USB_DESCRIPTOR_INTERFACE);
  if (!interface)
    return 0;
  if (interface->interface_class != USB_CLASS_HID || interface->subclass != USB_HID_BOOT_SUBCLASS ||
      interface->protocol != USB_HID_MOUSE_PROTOCOL)
    return 0;
  printf("usbmouse : resident_flag is ON\n");
  resident_flag = 1;
  return 1;
}

static int usbmouse_attach(int device) {
  UsbConfigDescriptor* config;
  UsbInterfaceDescriptor* interface;
  UsbEndpointInfo* endpoint;
  struct MouseUnit* unit;
  unsigned char location[USBMOUSE_LOCATION_BYTES];
  UsbDeviceRequest request;
  int result;
  config = sceUsbdScanStaticDescriptor(device, 0, USB_DESCRIPTOR_CONFIGURATION);
  if (!config)
    return -1;
  if (config->interface_count != 1)
    return -1;
  interface = sceUsbdScanStaticDescriptor(device, config, USB_DESCRIPTOR_INTERFACE);
  if (!interface)
    return -1;
  if (interface->interface_class != USB_CLASS_HID || interface->subclass != USB_HID_BOOT_SUBCLASS ||
      interface->protocol != USB_HID_MOUSE_PROTOCOL || interface->endpoint_count != 1)
    return -1;
  endpoint = sceUsbdScanStaticDescriptor(device, interface, USB_DESCRIPTOR_ENDPOINT);
  if (!endpoint)
    return -1;
  if (!(endpoint->address & USB_ENDPOINT_IN) ||
      (endpoint->attributes & USB_ENDPOINT_TYPE_MASK) != USB_ENDPOINT_INTERRUPT)
    return -1;
  unit = unit_alloc(device, endpoint->max_packet_low | (endpoint->max_packet_high << 8),
                    interface->number, interface->alternate);
  if (!unit)
    return -1;
  unit->control_pipe = sceUsbdOpenPipe(device, 0);
  if (unit->control_pipe < 0)
    return -1;
  unit->interrupt_pipe = sceUsbdOpenPipe(device, endpoint);
  if (unit->interrupt_pipe < 0) {
    unit_free(unit);
    return -1;
  }
  sceUsbdSetPrivateData(device, unit);
  request.request_type = 0;
  request.request = USB_REQUEST_SET_CONFIGURATION;
  request.value = config->value;
  request.index = 0;
  request.length = 0;
  result = sceUsbdTransferPipe(unit->control_pipe, 0, 0, &request, set_config_done, unit);
  if (result) {
    printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdSetConfiguration", result);
    return -1;
  }
  result = sceUsbdGetDeviceLocation(device, location);
  if (result) {
    printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdGetDeviceLocation", result);
    return -1;
  }
  printf("usbmouse%d: attached (port=", unit->index);
  for (result = 0; result < USBMOUSE_LOCATION_BYTES; result++) {
    if (!location[result])
      break;
    printf("%s%d", result ? "," : "", location[result]);
  }
  printf(")\n");
  USBMOUSE.count++;
  USBMOUSE.units[unit->index] = unit;
  return 0;
}

static struct MouseUnit* unit_alloc(int device, int size, int interface_number, int alternate) {
  struct MouseUnit* unit;
  int intr_state;
  if (USBMOUSE.count >= USBMOUSE_MAX_DEVICES)
    return 0;
  CpuSuspendIntr(&intr_state);
  unit = AllocSysMemory(SMEM_Low, sizeof(*unit) + size, 0);
  CpuResumeIntr(intr_state);
  if (unit) {
    unit->device = device;
    unit->index = USBMOUSE.next_index;
    do {
      if (++USBMOUSE.next_index >= USBMOUSE_MAX_DEVICES)
        USBMOUSE.next_index = 0;
    } while (USBMOUSE.units[USBMOUSE.next_index]);
    unit->report_size = size;
    unit->interface_number = interface_number;
    unit->alternate = alternate;
    unit->reserved = 0;
    unit->read_index = 0;
    unit->write_index = 0;
    unit->report_count = 0;
  }
  return unit;
}

static void unit_free(struct MouseUnit* unit) {
  int intr_state;
  CpuSuspendIntr(&intr_state);
  FreeSysMemory(unit);
  CpuResumeIntr(intr_state);
}

static void set_config_done(int result, int bytes, void* argument) {
  struct MouseUnit* unit = argument;
  UsbDeviceRequest request;
  int transfer_result;
  if (result)
    printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdSetConfiguration", result);
  if (unit->alternate > 0) {
    request.request_type = USB_REQUEST_INTERFACE;
    request.request = USB_REQUEST_SET_INTERFACE;
    request.value = unit->alternate;
    request.index = unit->interface_number;
    request.length = 0;
    transfer_result =
        sceUsbdTransferPipe(unit->control_pipe, 0, 0, &request, set_interface_done, unit);
    if (transfer_result)
      printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdSetInterface", transfer_result);
  } else
    data_transfer(unit);
}

static void set_interface_done(int result, int bytes, void* argument) {
  struct MouseUnit* unit = argument;
  if (result)
    printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdSetInterface", result);
  data_transfer(unit);
}

static void data_transfer(struct MouseUnit* unit) {
  int result = sceUsbdTransferPipe(unit->interrupt_pipe, unit->data, unit->report_size, 0,
                                   data_transfer_done, unit);
  if (result)
    printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdInterruptTransfer", result);
}

static void data_transfer_done(int result, int bytes, void* argument) {
  struct MouseUnit* unit = argument;
  struct UsbMouseReport* report;
  int i, intr_state;
  if (result)
    printf("usbmouse%d: %s -> 0x%x\n", unit->index, "sceUsbdInterruptTransfer", result);
  CpuSuspendIntr(&intr_state);
  if (unit->report_count < USBMOUSE_REPORT_COUNT) {
    report = &unit->reports[unit->write_index];
    report->size = bytes;
    for (i = 0; i < bytes; i++)
      *(i + report->data) = unit->data[i];
    if (++unit->write_index >= USBMOUSE_REPORT_COUNT)
      unit->write_index = 0;
    unit->report_count++;
  }
  CpuResumeIntr(intr_state);
  data_transfer(unit);
}

static int usbmouse_detach(int device) {
  struct MouseUnit* unit = sceUsbdGetPrivateData(device);
  if (!unit)
    return -1;
  USBMOUSE.count--;
  USBMOUSE.units[unit->index] = 0;
  printf("usbmouse%d: detached\n", unit->index);
  unit_free(unit);
  return 0;
}

static int stop_usbmouse(void) {
  struct MouseUnit* unit;
  int i, intr_state;
  for (i = 0; i < USBMOUSE_MAX_DEVICES; i++) {
    unit = USBMOUSE.units[i];
    if (unit) {
      sceUsbdClosePipe(unit->control_pipe);
      sceUsbdClosePipe(unit->interrupt_pipe);
      CpuSuspendIntr(&intr_state);
      FreeSysMemory(unit);
      CpuResumeIntr(intr_state);
    }
  }
  sceUsbdUnregisterLdd(&usbmouse_ops);
  TerminateThread(USBMOUSE.thread);
  DeleteThread(USBMOUSE.thread);
  sceSifRemoveRpc(&USBMOUSE.server, &USBMOUSE.queue);
  sceSifRemoveRpcQueue(&USBMOUSE.queue);
  return 1;
}
