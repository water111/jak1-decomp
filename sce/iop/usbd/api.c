#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/sysmem/sysmem.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thevent/thevent.h"
#include "sce/iop/thsemap/thsemap.h"
#include "sce/iop/usbd/api_gp.h"
#include "sce/iop/usbd/internal.h"

#define USB_ERROR_DEVICE 257
#define USB_ERROR_PIPE 258
#define USB_CALLBACK_READY 1
#define USB_ERROR_LENGTH 259
#define USB_ERROR_ALIGNMENT 262
#define USB_ERROR_NO_REQUEST 274
#define USB_ERROR_SETUP 275
#define USB_SCHEDULE_CONTROL 63
#define USB_SCHEDULE_BULK 64
#define USB_SCHEDULE_ISOCHRONOUS 65
#define USB_IOP_INTERRUPT 22
#define USB_EVENT_READY 1
#define USB_WORKER_STACK_SIZE 0x4000

int usb_lock(void);
int usb_unlock(void);
int sceUsbdRegisterLdd(UsbDriver* driver);
int sceUsbdRegisterAutoloader(UsbDriver* driver);
int sceUsbdUnregisterLdd(UsbDriver* driver);
int sceUsbdUnregisterAutoloader(void);
void* sceUsbdScanStaticDescriptor(int device, void* previous, u_char type);
int sceUsbdGetDeviceLocation(int id, u_char* path);
int sceUsbdSetPrivateData(int id, void* data);
void* sceUsbdGetPrivateData(int id);
int sceUsbdOpenPipe(int id, void* info);
int sceUsbdOpenPipeAligned(int id, void* info);
int sceUsbdClosePipe(int id);
void usb_queue_callback(struct UsbRequest* request);
int sceUsbdTransferPipe(int pipe,
                        void* buffer,
                        int length,
                        void* setup,
                        UsbCallback callback,
                        void* argument);
int sceUsbdChangeThreadPriority(int event_priority, int callback_priority);
int usb_interrupt_callback(void* argument);
void usb_event_worker(void* argument);
void usb_callback_worker(void* argument);
void usb_parse_option(struct UsbOption* option, const char* text);
int start(int argc, char** argv);

void* usb_alloc_memory(int size) {
  int state;
  void* memory;
  CpuSuspendIntr(&state);
  memory = AllocSysMemory(0, size, 0);
  CpuResumeIntr(state);
  return memory;
}

int usb_free_memory(void* memory) {
  int state;
  int result;
  CpuSuspendIntr(&state);
  result = FreeSysMemory(memory);
  CpuResumeIntr(state);
  return result;
}

int usb_lock(void) {
  return WaitSema(usb_api_semaphore);
}

int usb_unlock(void) {
  return SignalSema(usb_api_semaphore);
}

int sceUsbdRegisterLdd(UsbDriver* driver) {
  u_int saved_gp;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    result = usb_register_driver(driver, saved_gp);
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdRegisterAutoloader(UsbDriver* driver) {
  u_int saved_gp;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    result = usb_register_autoloader(driver, saved_gp);
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdUnregisterLdd(UsbDriver* driver) {
  u_int saved_gp;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    result = usb_unregister_driver(driver);
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdUnregisterAutoloader(void) {
  u_int saved_gp;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    result = usb_unregister_autoloader();
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

void* sceUsbdScanStaticDescriptor(int device, void* previous, u_char type) {
  u_int saved_gp;
  void* result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    result = usb_scan_descriptor(device, previous, type);
    usb_unlock();
  } else {
    result = 0;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdGetDeviceLocation(int id, u_char* path) {
  u_int saved_gp;
  struct UsbDevice* device;
  int result;
  USB_SAVE_GP(saved_gp);
  result = USB_ERROR_DEVICE;
  if (!usb_lock()) {
    device = usb_get_device(id);
    if (device)
      result = usb_get_device_location(device, path);
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdSetPrivateData(int id, void* data) {
  u_int saved_gp;
  struct UsbDevice* device;
  int result;
  USB_SAVE_GP(saved_gp);
  result = USB_ERROR_DEVICE;
  if (!usb_lock()) {
    device = usb_get_device(id);
    if (device) {
      device->private_data = data;
      result = 0;
    }
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

void* sceUsbdGetPrivateData(int id) {
  u_int saved_gp;
  struct UsbDevice* device;
  void* result;
  USB_SAVE_GP(saved_gp);
  result = 0;
  if (!usb_lock()) {
    device = usb_get_device(id);
    if (device)
      result = device->private_data;
    usb_unlock();
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdOpenPipe(int id, void* info) {
  u_int saved_gp;
  struct UsbDevice* device;
  struct UsbEndpoint* endpoint;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    device = usb_get_device(id);
    if (!device) {
      result = -1;
    } else {
      endpoint = usb_open_pipe(device, info, 0);
      result = endpoint ? endpoint->id : -1;
    }
    usb_unlock();
  } else {
    result = -1;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdOpenPipeAligned(int id, void* info) {
  u_int saved_gp;
  struct UsbDevice* device;
  struct UsbEndpoint* endpoint;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    device = usb_get_device(id);
    if (!device) {
      result = -1;
    } else {
      endpoint = usb_open_pipe(device, info, 1);
      result = endpoint ? endpoint->id : -1;
    }
    usb_unlock();
  } else {
    result = -1;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdClosePipe(int id) {
  u_int saved_gp;
  struct UsbEndpoint* endpoint;
  int result;
  USB_SAVE_GP(saved_gp);
  if (!usb_lock()) {
    endpoint = usb_get_endpoint(id);
    result = USB_ERROR_PIPE;
    if (endpoint)
      result = usb_close_pipe(endpoint);
    usb_unlock();
  } else {
    result = USB_ERROR_LOCK;
  }
  USB_RESTORE_GP(saved_gp);
  return result;
}

void usb_queue_callback(struct UsbRequest* request) {
  int state;
  CpuSuspendIntr(&state);
  request->previous = usb_callback_queue.tail;
  if (request->previous)
    request->previous->next = request;
  else
    usb_callback_queue.head = request;
  request->next = 0;
  usb_callback_queue.tail = request;
  CpuResumeIntr(state);
  SetEventFlag(usb_callback_event, USB_CALLBACK_READY);
}

int sceUsbdTransferPipe(int pipe,
                        void* buffer,
                        int length,
                        void* setup,
                        UsbCallback callback,
                        void* argument) {
  u_int saved_gp;
  struct UsbEndpoint* endpoint;
  struct UsbRequest* request;
  UsbDeviceRequest* packet = setup;
  int result;
  USB_SAVE_GP(saved_gp);
  if (usb_lock()) {
    USB_RESTORE_GP(saved_gp);
    return USB_ERROR_LOCK;
  }
  endpoint = usb_get_endpoint(pipe);
  if (!endpoint) {
    result = USB_ERROR_PIPE;
    goto done;
  }
  if (buffer && length > 0) {
    u_int start_page = (u_int)buffer >> 12;
    if ((((u_int)buffer + length - 1) >> 12) - start_page >= 2)
      goto bad_length;
    if (endpoint->alignment && ((u_int)buffer & 3))
      goto bad_alignment;
    if (endpoint->schedule_index == USB_SCHEDULE_CONTROL && ((u_int)buffer & 3) &&
        !(packet->request_type & 0x80)) {
      if ((((u_short*)&endpoint->descriptor->control)[1] & 0x7ff) == 64 && length >= 63) {
      bad_alignment:
        result = USB_ERROR_ALIGNMENT;
        goto done;
      }
    }
    if (endpoint->schedule_index == USB_SCHEDULE_ISOCHRONOUS &&
        (u_int)length > (((u_short*)&endpoint->descriptor->control)[1] & 0x7ff))
      goto bad_length;
  }
  request = usb_alloc_request();
  if (!request) {
    result = USB_ERROR_NO_REQUEST;
    goto done;
  }
  request->callback_argument = argument;
  request->user_callback = callback;
  request->callback_gp = saved_gp;
  switch (endpoint->schedule_index) {
    case USB_SCHEDULE_CONTROL:
      if (!packet) {
        usb_free_request(request);
        result = USB_ERROR_SETUP;
        goto done;
      }
      if (packet->length != length) {
        usb_free_request(request);
      bad_length:
        result = USB_ERROR_LENGTH;
        goto done;
      }
      result = usb_submit_control(endpoint, request, packet->request_type, packet->request,
                                  packet->value, packet->index, packet->length, buffer,
                                  usb_queue_callback);
      break;
    case USB_SCHEDULE_ISOCHRONOUS:
      request->wait_frames = (u_int)packet;
    case USB_SCHEDULE_BULK:
    default:
      result = usb_submit_data(endpoint, request, buffer, length, usb_queue_callback);
      break;
  }
done:
  usb_unlock();
  USB_RESTORE_GP(saved_gp);
  return result;
}

int sceUsbdChangeThreadPriority(int event_priority, int callback_priority) {
  u_int saved_gp;
  int result;
  USB_SAVE_GP(saved_gp);
  result = 0;
  if (usb_thread_priorities[0] != event_priority) {
    usb_thread_priorities[0] = event_priority;
    result = ChangeThreadPriority(usb_event_thread, event_priority);
    if (result)
      goto done;
  }
  if (usb_thread_priorities[1] != callback_priority) {
    usb_thread_priorities[1] = callback_priority;
    result = ChangeThreadPriority(usb_callback_thread, callback_priority);
  }
done:
  USB_RESTORE_GP(saved_gp);
  return result;
}

int usb_interrupt_callback(void* argument) {
  struct UsbInterruptContext* context = argument;
  iSetEventFlag(context->event, USB_EVENT_READY);
  return 0;
}

void usb_event_worker(void* argument) {
  u_int bits;
  for (;;) {
    WaitEventFlag(usb_interrupt_event, USB_EVENT_READY, EW_OR | EW_CLEAR, &bits);
    usb_lock();
    usb_handle_interrupt();
    EnableIntr(USB_IOP_INTERRUPT);
    usb_cycle_interrupts();
    usb_unlock();
  }
}

void usb_callback_worker(void* argument) {
  struct UsbRequest copy;
  u_int bits;
  int state;
  struct UsbRequest* request;
  for (;;) {
    WaitEventFlag(usb_callback_event, 1, EW_OR | EW_CLEAR, &bits);
    for (;;) {
      CpuSuspendIntr(&state);
      request = usb_callback_queue.head;
      if (request) {
        struct UsbRequest* neighbor;
        struct UsbRequest* link;
        link = request->next;
        neighbor = request->previous;
        if (link)
          link->previous = neighbor;
        else
          usb_callback_queue.tail = neighbor;
        link = request->previous;
        neighbor = request->next;
        if (link)
          link->next = neighbor;
        else
          usb_callback_queue.head = neighbor;
      }
      CpuResumeIntr(state);
      if (!request)
        break;
      bcopy(request, &copy, sizeof(copy));
      usb_lock();
      usb_free_request(request);
      usb_unlock();
      if (copy.user_callback) {
        USB_RESTORE_GP(copy.callback_gp);
        copy.user_callback(copy.result, copy.transferred, copy.callback_argument);
        __asm__ volatile("la $gp,_gp");
      }
    }
  }
}

void usb_parse_option(struct UsbOption* option, const char* text) {
  int value = 0;
  const char* start = text;
  const char* second_start;
  while (*text) {
    char ch = *text;
    if (ch == ',')
      break;
    // Preserve the original parser's upper bound: '9' is rejected.
    if (ch < '0' || ch >= '9')
      return;
    value = value * 10 + ch - '0';
    ++text;
  }
  if (option->second) {
    if (*text != ',')
      return;
  } else {
    if (*text == ',')
      return;
  }
  if (start < text++)
    *option->first = value;
  value = 0;
  if (option->second) {
    second_start = text;
    while (*text) {
      if (*text < '0' || *text >= '9')
        return;
      value = value * 10 + *text - '0';
      ++text;
    }
    if (second_start < text)
      *option->second = value;
  }
}

int start(int argc, char** argv) {
  struct EventFlagParam event;
  struct ThreadParam thread;
  struct SemaParam semaphore;
  int old_interrupt;
  --argc;
  ++argv;
  while (argc > 0) {
    struct UsbOption* option = usb_options;
    while (option->name) {
      const char* name = option->name;
      const char* text = *argv;
      while (*name && *name == *text) {
        ++name;
        ++text;
      }
      if (!*name) {
        usb_parse_option(option, text);
        break;
      }
      ++option;
    }
    --argc;
    ++argv;
  }
  DisableIntr(USB_IOP_INTERRUPT, &old_interrupt);
  if (RegisterIntrHandler(USB_IOP_INTERRUPT, 1, usb_interrupt_callback, &usb_runtime)) {
    if (old_interrupt == USB_IOP_INTERRUPT)
      EnableIntr(USB_IOP_INTERRUPT);
    return 1;
  }
  if (RegisterLibraryEntries(usb_exports))
    goto fail;
  semaphore.attr = SA_THPRI;
  semaphore.initCount = 1;
  semaphore.maxCount = 1;
  semaphore.option = 0;
  if ((usb_api_semaphore = CreateSema(&semaphore)) < 0)
    goto fail;
  event.attr = 0;
  event.initPattern = 0;
  event.option = 0;
  if ((usb_interrupt_event = CreateEventFlag(&event)) < 0)
    goto fail;
  thread.attr = TH_C;
  thread.entry = usb_event_worker;
  thread.stackSize = USB_WORKER_STACK_SIZE;
  thread.option = 0;
  thread.initPriority = usb_thread_priorities[0];
  if ((usb_event_thread = CreateThread(&thread)) < 0)
    goto fail;
  if (StartThread(usb_event_thread, 0))
    goto fail;
  event.attr = 0;
  event.initPattern = 0;
  event.option = 0;
  if ((usb_callback_event = CreateEventFlag(&event)) < 0)
    goto fail;
  thread.attr = TH_C;
  thread.entry = usb_callback_worker;
  thread.stackSize = USB_WORKER_STACK_SIZE;
  thread.option = 0;
  thread.initPriority = usb_thread_priorities[1];
  if ((usb_callback_thread = CreateThread(&thread)) < 0)
    goto fail;
  if (StartThread(usb_callback_thread, 0))
    goto fail;
  if (usb_initialize() < 0)
    goto fail;
  EnableIntr(USB_IOP_INTERRUPT);
  return 0;
fail:
  if (usb_callback_thread > 0)
    DeleteThread(usb_callback_thread);
  if (usb_callback_event > 0)
    DeleteEventFlag(usb_callback_event);
  if (usb_event_thread > 0)
    DeleteThread(usb_event_thread);
  if (usb_interrupt_event > 0)
    DeleteEventFlag(usb_interrupt_event);
  if (usb_api_semaphore > 0)
    DeleteSema(usb_api_semaphore);
  ReleaseIntrHandler(USB_IOP_INTERRUPT);
  return 1;
}

#include "sce/iop/usbd/state.c"
