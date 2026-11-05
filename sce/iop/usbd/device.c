#include "sce/iop/usbd/api_gp.h"
#include "sce/iop/usbd/internal.h"
#define USB_DEVICE_READY 7
#define USB_ERROR_DRIVER 260
#define USB_ERROR_DRIVER_BUSY 289
#define USB_LOCATION_DEPTH 6
#define USB_LOCATION_SIZE 7
#define USB_ERROR_LOCATION 263
#define USB_ERROR_REQUEST_BUSY 289
#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_SET_ADDRESS 5
#define USB_RESULT_NO_RESPONSE 5
#define USB_DEVICE_DESCRIPTOR_SIZE 18
#define USB_DEVICE_ATTACHED 1

int usb_call_driver(int (*callback)(int), int device, u_int gp);
void usb_attach_driver(struct UsbDevice* parent, UsbDriver* driver);
int usb_register_driver(UsbDriver* driver, u_int gp);
int usb_register_autoloader(UsbDriver* driver, u_int gp);
void usb_detach_driver(struct UsbDevice* device, UsbDriver* driver);
int usb_unregister_driver(UsbDriver* driver);
int usb_unregister_autoloader(void);
void* usb_scan_descriptor(int id, void* previous, u_char type);
int usb_get_device_location(struct UsbDevice* device, u_char* path);
struct UsbEndpoint* usb_open_pipe(struct UsbDevice* device, UsbEndpointInfo* info, u_int alignment);
int usb_close_pipe(struct UsbEndpoint* endpoint);
int usb_submit_data(struct UsbEndpoint* endpoint,
                    struct UsbRequest* request,
                    void* buffer,
                    int length,
                    void (*callback)(struct UsbRequest*));
int usb_submit_control(struct UsbEndpoint* endpoint,
                       struct UsbRequest* request,
                       u_char type,
                       u_char command,
                       u_short value,
                       u_short index,
                       u_short length,
                       void* buffer,
                       void (*callback)(struct UsbRequest*));
void usb_bind_device(struct UsbDevice* device);
void usb_restart_enumeration(struct UsbDevice* device, struct UsbEndpoint* endpoint);
void usb_configuration_complete(struct UsbRequest* request);
void usb_device_descriptor_complete(struct UsbRequest* request);
void usb_read_device_descriptor(struct UsbRequest* request);
void usb_address_settled(void* argument);
void usb_address_complete(struct UsbRequest* request);
void usb_set_device_address(void* argument);
int usb_begin_enumeration(struct UsbDevice* device);
int usb_disconnect_device(struct UsbDevice* device);

int usb_call_driver(int (*callback)(int), int device, u_int gp) {
  int result;
  if (!callback)
    return 0;
  usb_unlock();
  USB_RESTORE_GP(gp);
  result = callback(device);
  __asm__ volatile("la $gp,_gp");
  usb_lock();
  return result;
}

void usb_attach_driver(struct UsbDevice* parent, UsbDriver* driver) {
  struct UsbDevice* device;
  for (device = parent->child_head; device; device = device->next) {
    if (device->status == USB_DEVICE_READY) {
      if (device->driver) {
        if (device->child_head)
          usb_attach_driver(device, driver);
      } else {
        if (usb_call_driver(driver->probe, device->id, driver->gp)) {
          device->driver = driver;
          usb_call_driver(driver->attach, device->id, driver->gp);
        }
      }
    }
  }
}

int usb_register_driver(UsbDriver* driver, u_int gp) {
  if (driver->next || driver->previous || !driver->name || driver->reserved[0] ||
      driver->reserved[1])
    return USB_ERROR_DRIVER;
  if (usb_driver_head == driver)
    return USB_ERROR_DRIVER_BUSY;
  driver->gp = gp;
  driver->previous = usb_driver_tail;
  if (driver->previous)
    driver->previous->next = driver;
  else
    usb_driver_head = driver;
  driver->next = 0;
  usb_driver_tail = driver;
  if (driver->probe)
    usb_attach_driver(usb_get_root(), driver);
  return 0;
}

int usb_register_autoloader(UsbDriver* driver, u_int gp) {
  if (driver->next || driver->previous || !driver->name || driver->reserved[0] ||
      driver->reserved[1])
    return USB_ERROR_DRIVER;
  if (usb_autoloader)
    return USB_ERROR_DRIVER_BUSY;
  driver->gp = gp;
  usb_autoloader = driver;
  if (driver->probe)
    usb_attach_driver(usb_get_root(), driver);
  return 0;
}

void usb_detach_driver(struct UsbDevice* device, UsbDriver* driver) {
  struct UsbDevice* child;
  if (driver == device->driver) {
    struct UsbEndpoint* endpoint = device->endpoint_head;
    struct UsbEndpoint* next;
    if (endpoint) {
      for (endpoint = endpoint->next; endpoint; endpoint = next) {
        next = endpoint->next;
        usb_close_endpoint(device, endpoint);
      }
    }
    device->driver = 0;
    device->private_data = 0;
  }
  for (child = device->child_head; child; child = child->next)
    usb_detach_driver(child, driver);
}

int usb_unregister_driver(UsbDriver* driver) {
  UsbDriver* walk;
  for (walk = usb_driver_head; walk; walk = walk->next) {
    if (walk == driver)
      break;
  }
  if (!walk)
    return USB_ERROR_DRIVER;
  {
    UsbDriver* neighbor;
    UsbDriver* link;
    link = driver->next;
    neighbor = driver->previous;
    if (link)
      link->previous = neighbor;
    else
      usb_driver_tail = neighbor;
    link = driver->previous;
    neighbor = driver->next;
    if (link)
      link->next = neighbor;
    else
      usb_driver_head = neighbor;
  }
  usb_detach_driver(usb_get_root(), driver);
  return 0;
}

int usb_unregister_autoloader(void) {
  usb_autoloader = 0;
  return 0;
}

void* usb_scan_descriptor(int id, void* previous, u_char type) {
  struct UsbDevice* device = usb_get_device(id);
  u_char* current;
  u_char* end;
  if (!device)
    return 0;
  if (previous)
    current = (u_char*)previous + *(u_char*)previous;
  else
    current = device->descriptors;
  end = device->descriptor_end;
  while (current[0] >= 2 && current < end) {
    if (!type || current[1] == type)
      return current;
    current += current[0];
  }
  return 0;
}

int usb_get_device_location(struct UsbDevice* device, u_char* path) {
  u_char reverse[16];
  u_char* output;
  int depth;
  struct UsbDevice* root = usb_get_root();
  int i;
  for (depth = 0; device != root && depth < USB_LOCATION_DEPTH; ++depth) {
    reverse[depth] = device->port;
    device = device->parent;
  }
  if (depth >= USB_LOCATION_DEPTH)
    return USB_ERROR_LOCATION;
  for (i = 0; i < USB_LOCATION_SIZE; ++i) {
    output = &path[i];
    if (i < depth)
      *output = reverse[depth - i - 1];
    else
      *output = 0;
  }
  return 0;
}

struct UsbEndpoint* usb_open_pipe(struct UsbDevice* device,
                                  UsbEndpointInfo* info,
                                  u_int alignment) {
  struct UsbEndpoint* result;
  if (!device->parent)
    return 0;
  if (!info)
    result = device->endpoint_head;
  else
    result = usb_open_endpoint(device, info, alignment);
  return result;
}

int usb_close_pipe(struct UsbEndpoint* endpoint) {
  struct UsbDevice* device = endpoint->device;
  if (device->endpoint_head == endpoint)
    return 0;
  return usb_close_endpoint(device, endpoint);
}

int usb_submit_data(struct UsbEndpoint* endpoint,
                    struct UsbRequest* request,
                    void* buffer,
                    int length,
                    void (*callback)(struct UsbRequest*)) {
  if (!endpoint->device || request->busy)
    return USB_ERROR_REQUEST_BUSY;
  request->busy = 1;
  request->endpoint = endpoint;
  request->buffer = buffer;
  request->length = length;
  request->callback = callback;
  request->result = 0;
  request->previous = endpoint->request_tail;
  if (request->previous)
    request->previous->next = request;
  else
    endpoint->request_head = request;
  request->next = 0;
  endpoint->request_tail = request;
  usb_schedule_endpoint(endpoint);
  return 0;
}

int usb_submit_control(struct UsbEndpoint* endpoint,
                       struct UsbRequest* request,
                       u_char type,
                       u_char command,
                       u_short value,
                       u_short index,
                       u_short length,
                       void* buffer,
                       void (*callback)(struct UsbRequest*)) {
  UsbDeviceRequest* setup = &request->setup;
  int result;
  if (!request->busy) {
    setup->request_type = type;
    setup->request = command;
    setup->value = value;
    setup->index = index;
    setup->length = length;
    result = usb_submit_data(endpoint, request, buffer, length, callback);
  } else {
    result = USB_ERROR_REQUEST_BUSY;
  }
  return result;
}

void usb_bind_device(struct UsbDevice* device) {
  UsbDriver* driver;
  for (driver = usb_driver_head; driver; driver = driver->next) {
    if (usb_call_driver(driver->probe, device->id, driver->gp))
      goto found;
  }
  driver = usb_autoloader;
  if (driver && usb_call_driver(driver->probe, device->id, driver->gp)) {
  found:
    device->driver = driver;
    usb_call_driver(driver->attach, device->id, driver->gp);
  }
}

void usb_restart_enumeration(struct UsbDevice* device, struct UsbEndpoint* endpoint) {
  usb_close_endpoint(device, endpoint);
  usb_finish_device_reset(device);
  usb_start_device_reset(device);
}

void usb_configuration_complete(struct UsbRequest* request) {
  struct UsbEndpoint* endpoint = request->endpoint;
  struct UsbDevice* device = endpoint->device;
  u_char* descriptor = device->descriptors;
  int full, index, length;
  u_char* output;
  if (device->descriptor_phase > 0 && request->result) {
    usb_restart_enumeration(device, endpoint);
    return;
  }
  index = device->descriptor_phase;
  device->descriptor_phase = index + 1;
  full = index & 1;
  index >>= 1;
  if (index > 0 && !full) {
    output = device->descriptor_end;
    length = output[2] + (output[3] << 8);
    device->descriptor_end = output + length;
  }
  output = device->descriptor_end;
  length = 4;
  if (full)
    length = output[2] + (output[3] << 8);
  if (device->descriptors + usb_descriptor_limit < output + length)
    return;
  if (index < descriptor[17])
    usb_submit_control(endpoint, &device->enumeration_request, 0x80, USB_REQUEST_GET_DESCRIPTOR,
                       (USB_DESCRIPTOR_CONFIGURATION << 8) | index, 0, length, output,
                       usb_configuration_complete);
  else
    usb_bind_device(device);
}

void usb_device_descriptor_complete(struct UsbRequest* request) {
  struct UsbEndpoint* endpoint = request->endpoint;
  struct UsbDevice* device = endpoint->device;
  struct UsbEndpointDescriptor* hardware = endpoint->descriptor;
  u_char* descriptor = device->descriptors;
  if (request->result)
    usb_restart_enumeration(device, endpoint);
  else if ((u_int)request->transferred < USB_DEVICE_DESCRIPTOR_SIZE) {
    hardware->control = (hardware->control & 0xf800ffff) | (descriptor[7] << 16);
    request->length = USB_DEVICE_DESCRIPTOR_SIZE;
    usb_read_device_descriptor(request);
  } else {
    device->descriptor_phase = 0;
    device->descriptor_end += USB_DEVICE_DESCRIPTOR_SIZE;
    usb_configuration_complete(request);
  }
}

void usb_read_device_descriptor(struct UsbRequest* request) {
  struct UsbEndpoint* endpoint = request->endpoint;
  struct UsbDevice* device = endpoint->device;
  u_char* descriptor = device->descriptors;
  device->descriptor_end = descriptor;
  usb_submit_control(endpoint, &device->enumeration_request, 0x80, USB_REQUEST_GET_DESCRIPTOR,
                     (USB_DESCRIPTOR_DEVICE << 8), 0, request->length, descriptor,
                     usb_device_descriptor_complete);
}

void usb_address_settled(void* argument) {
  struct UsbRequest* request = argument;
  request->length = 8;
  usb_read_device_descriptor(request);
  usb_finish_device_reset(request->endpoint->device);
}

void usb_address_complete(struct UsbRequest* request) {
  struct UsbEndpoint* endpoint = request->endpoint;
  struct UsbDevice* device = endpoint->device;
  if (request->result == USB_RESULT_NO_RESPONSE) {
    int delay = device->address_retry_delay * 2;
    device->address_retry_delay = delay;
    if (delay <= 1280)
      usb_add_timer(&device->reset_timer, usb_set_device_address, endpoint, delay | 1);
    else
      usb_restart_enumeration(device, endpoint);
  } else {
    struct UsbEndpointDescriptor* hardware = endpoint->descriptor;
    hardware->control |= device->address & 0x7f;
    device->status = USB_DEVICE_READY;
    usb_add_timer(&device->reset_timer, usb_address_settled, request, 5);
  }
}

void usb_set_device_address(void* argument) {
  struct UsbEndpoint* endpoint = argument;
  struct UsbDevice* device = endpoint->device;
  usb_submit_control(endpoint, &device->enumeration_request, 0, USB_REQUEST_SET_ADDRESS,
                     device->address, 0, 0, 0, usb_address_complete);
}

int usb_begin_enumeration(struct UsbDevice* device) {
  struct UsbEndpoint* endpoint = device->endpoint_head;
  device->address_retry_delay = 20;
  usb_add_timer(&device->reset_timer, usb_set_device_address, endpoint,
                device->address_retry_delay | 1);
  return 0;
}

int usb_disconnect_device(struct UsbDevice* device) {
  struct UsbDevice* child;
  struct UsbEndpoint* endpoint;
  if (device->status != USB_DEVICE_ATTACHED) {
    device->status = USB_DEVICE_ATTACHED;
    if (device->driver)
      usb_call_driver(device->driver->detach, device->id, device->driver->gp);
    device->driver = 0;
    if (device->reset_timer.active)
      usb_cancel_timer(&device->reset_timer);
    while ((endpoint = device->endpoint_head))
      usb_close_endpoint(device, endpoint);
    while ((child = device->child_head)) {
      struct UsbDevice* neighbor;
      struct UsbDevice* link;
      link = child->next;
      neighbor = child->previous;
      if (link)
        link->previous = neighbor;
      else
        device->child_tail = neighbor;
      link = child->previous;
      neighbor = child->next;
      if (link)
        link->next = neighbor;
      else
        device->child_head = neighbor;
      usb_disconnect_device(child);
      usb_release_device(child);
    }
    device->enumeration_request.busy = 0;
  }
  if (device->reset_pending)
    usb_finish_device_reset(device);
  return 0;
}
