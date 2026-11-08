#include "sce/iop/usbd/hub.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"

#define USB_PORT_CONNECTED 1
#define USB_PORT_ENABLED 2
#define USB_PORT_CHANGE_CONNECTION 0x10000
#define USB_PORT_CHANGE_ENABLE 0x20000
#define USB_PORT_CHANGE_SUSPEND 0x40000
#define USB_PORT_CHANGE_OVERCURRENT 0x80000
#define USB_PORT_CHANGE_RESET 0x100000
#define USB_DESCRIPTOR_HUB 0x29
#define USB_HUB_DESCRIPTOR_SIZE 39
#define USB_CLASS_HUB 9

extern UsbDriver usb_hub_driver;
struct UsbHub* usb_alloc_hub(void);
void usb_free_hub(struct UsbHub* hub);
struct UsbDevice* usb_hub_child(struct UsbDevice* parent, int port);
void usb_poll_hub(struct UsbHub* hub);
void usb_hub_control(struct UsbHub* hub,
                     u_char type,
                     u_char command,
                     u_short value,
                     u_short index,
                     u_short length,
                     void* buffer,
                     void (*callback)(struct UsbRequest*),
                     const char* operation);
void usb_hub_device_reset(void* argument);
void usb_hub_port_status(struct UsbRequest* request);
void usb_hub_reset_complete(struct UsbRequest* request);
void usb_reset_hub_port(struct UsbDevice* device);
void usb_hub_status(struct UsbRequest* request);
void usb_scan_hub_changes(struct UsbRequest* request);
void usb_hub_power_ports(struct UsbRequest* request);
void usb_hub_create_ports(struct UsbRequest* request);
void usb_hub_get_status(struct UsbRequest* request);
void usb_hub_descriptor_complete(struct UsbRequest* request);
void usb_hub_configured(struct UsbRequest* request);
int usb_hub_attach(int id);
int usb_hub_detach(int id);
int usb_hub_probe(int id);
int usb_initialize_hubs(void);

struct UsbHub* usb_alloc_hub(void) {
  struct UsbHub* hub = usb_free_hubs;
  if (hub) {
    usb_free_hubs = hub->next;
    hub->descriptor[1] = 0;
    hub->interrupt_request.busy = 0;
    hub->control_request.busy = 0;
    hub->id = usb_next_hub_id++;
  }
  return hub;
}

void usb_free_hub(struct UsbHub* hub) {
  hub->next = usb_free_hubs;
  usb_free_hubs = hub;
}

struct UsbDevice* usb_hub_child(struct UsbDevice* parent, int port) {
  struct UsbDevice* child = parent->child_head;
  goto test;
next:
  child = child->next;
test:
  if (child && --port > 0)
    goto next;
  return child;
}

void usb_poll_hub(struct UsbHub* hub) {
  struct UsbEndpoint* endpoint = hub->interrupt;
  if (!hub->interrupt_request.busy)
    usb_submit_data(endpoint, &hub->interrupt_request, hub->bitmap, (hub->port_count + 7) >> 3,
                    usb_scan_hub_changes);
}

void usb_hub_control(struct UsbHub* hub,
                     u_char type,
                     u_char command,
                     u_short value,
                     u_short index,
                     u_short length,
                     void* buffer,
                     void (*callback)(struct UsbRequest*),
                     const char* operation) {
  if (hub->control_request.busy)
    printf("hub_control_transfer: busy - %s\n", operation);
  else
    usb_submit_control(hub->control, &hub->control_request, type, command, value, index, length,
                       buffer, callback);
}

void usb_hub_device_reset(void* argument) {
  usb_start_device_reset(argument);
}

void usb_hub_port_status(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  struct UsbDevice* parent = hub->control->device;
  u_int status = hub->port_status;
  struct UsbDevice* device;
  int feature = -1;
  if (request->result)
    return;
  if (status & USB_PORT_CHANGE_CONNECTION)
    feature = 16;
  else if (status & USB_PORT_CHANGE_ENABLE)
    feature = 17;
  else if (status & USB_PORT_CHANGE_SUSPEND)
    feature = 18;
  else if (status & USB_PORT_CHANGE_OVERCURRENT)
    feature = 19;
  else if (status & USB_PORT_CHANGE_RESET)
    feature = 20;
  if (feature >= 0) {
    hub->port_status &= ~(1 << feature);
    usb_hub_control(hub, 0x23, 1, feature, hub->current_port, 0, 0, usb_hub_port_status,
                    "clear_port_feature");
    return;
  }
  device = usb_hub_child(parent, hub->current_port);
  if (device) {
    if (!(status & USB_PORT_CONNECTED))
      usb_disconnect_device(device);
    else {
      switch (device->status) {
        case 1:
          device->status = 3;
          usb_add_timer(&device->reset_timer, usb_hub_device_reset, device, 501);
          return;
        case 5:
          if (status & USB_PORT_ENABLED) {
            device->status = 6;
            device->low_speed = (status >> 9) & 1;
            if (usb_open_endpoint(device, 0, 0))
              usb_begin_enumeration(device);
            hub->reset_port = 0;
          }
          break;
      }
    }
  }
  usb_scan_hub_changes(&hub->interrupt_request);
}

void usb_hub_reset_complete(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  if (!request->result)
    usb_poll_hub(hub);
}

void usb_reset_hub_port(struct UsbDevice* device) {
  struct UsbHub* hub = device->parent->private_data;
  hub->reset_port = device->port;
  usb_hub_control(hub, 0x23, 3, 4, device->port, 0, 0, usb_hub_reset_complete, "hub_port_reset");
}

#if USBD_DRIVER_VERSION >= 0x010000
void usb_update_hub_power(struct UsbHub* hub) {
  struct UsbDevice* device = hub->device;
  switch (device->power_state) {
    case 0:
      if (hub->self_powered) {
        if (hub->max_power > 0)
          device->power_state = 3;
        else
          device->power_state = 2;
      } else {
        if (hub->max_power > 0)
          device->power_state = 1;
        else
          device->power_state = 0;
      }
      break;
    case 3:
    case 4:
    case 5:
      if (hub->status & 1)
        device->power_state = 4;
      else
        device->power_state = 5;
      break;
  }
}
#endif

void usb_hub_status(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  if (request->result)
    return;
  if (hub->changes & 1) {
    hub->changes &= ~1;
    usb_hub_control(hub, 0x20, 1, 0, 0, 0, 0, usb_hub_status, "clear_hub_feature");
#if USBD_DRIVER_VERSION >= 0x010000
    usb_update_hub_power(hub);
#endif
  } else if (hub->changes & 2) {
    hub->changes &= ~2;
    usb_hub_control(hub, 0x20, 1, 1, 0, 0, 0, usb_hub_status, "clear_hub_feature");
  } else
    usb_scan_hub_changes(&hub->interrupt_request);
}

void usb_scan_hub_changes(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  int port;
  if (request->result)
    return;
  if (hub->bitmap[0] & 1) {
    hub->bitmap[0] &= ~1;
    usb_hub_control(hub, 0xa0, 0, 0, 0, 4, &hub->status, usb_hub_status, "scan_change_bitmap");
    return;
  }
  port = hub->reset_port;
  if (port > 0) {
    if ((hub->bitmap[port >> 3] >> (port & 7)) & 1)
      goto get_status;
  } else {
    for (port = 1; port <= hub->port_count; ++port) {
      if ((hub->bitmap[port >> 3] >> (port & 7)) & 1) {
      get_status:
        hub->bitmap[port >> 3] &= ~(1 << (port & 7));
        hub->current_port = port;
        usb_hub_control(hub, 0xa3, 0, 0, hub->current_port, 4, &hub->port_status,
                        usb_hub_port_status, "scan_change_bitmap");
        return;
      }
    }
  }
  usb_poll_hub(hub);
}

void usb_hub_power_ports(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  if (hub->current_port > 0 && request->result)
    return;
  if (++hub->current_port <= hub->port_count
#if USBD_DRIVER_VERSION >= 0x010000
      && hub->current_port <= usb_configuration.ports
#endif
  )
    usb_hub_control(hub, 0x23, 3, 8, hub->current_port, 0, 0, usb_hub_power_ports,
                    "set_port_power");
  else
    usb_poll_hub(hub);
}

void usb_hub_create_ports(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  struct UsbDevice* parent = hub->control->device;
  int i;
  if (request->result)
    return;
#if USBD_DRIVER_VERSION >= 0x010000
  usb_update_hub_power(hub);
#endif
  hub->reset_port = 0;
  hub->port_count = hub->descriptor[2];
  hub->current_port = 0;
  for (i = 0; i < hub->port_count; ++i) {
    if (i >= usb_configuration.ports)
      break;
    if (!usb_attach_child(parent, i + 1)) {
      hub->port_count = i;
      break;
    }
  }
  if (hub->port_count > 0)
    usb_hub_power_ports(request);
}

void usb_hub_get_status(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  usb_hub_control(hub, 0xa0, 0, 0, 0, 4, &hub->status, usb_hub_create_ports, "get_hub_status");
}

void usb_hub_descriptor_complete(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  u_char* descriptor = hub->descriptor;
  if (!request->result
#if USBD_DRIVER_VERSION < 0x010000
      && descriptor[2] <= usb_configuration.ports
#endif
  )
    usb_hub_get_status(request);
}

void usb_hub_configured(struct UsbRequest* request) {
  struct UsbHub* hub = request->callback_argument;
  u_char* descriptor = hub->descriptor;
  if (request->result)
    return;
  if (descriptor[1] != USB_DESCRIPTOR_HUB)
    usb_hub_control(hub, 0xa0, 6, USB_DESCRIPTOR_HUB << 8, 0, USB_HUB_DESCRIPTOR_SIZE, descriptor,
                    usb_hub_descriptor_complete, "set_configuration_done");
  else
#if USBD_DRIVER_VERSION < 0x010000
      if (descriptor[2] <= usb_configuration.ports)
#endif
    usb_hub_get_status(request);
}

int usb_hub_attach(int id) {
  struct UsbDevice* device = usb_get_device(id);
  UsbConfigDescriptor* config;
  UsbInterfaceDescriptor* interface;
  UsbEndpointInfo* endpoint;
  u_char* descriptor;
  struct UsbHub* hub;
  struct UsbEndpoint* pipe;
  if (!device)
    return -1;
  config = usb_scan_descriptor(id, 0, USB_DESCRIPTOR_CONFIGURATION);
  if (!config)
    return -1;
  if (config->interface_count != 1)
    goto fail;
  interface = usb_scan_descriptor(id, config, USB_DESCRIPTOR_INTERFACE);
  if (!interface || interface->endpoint_count != 1)
    return -1;
  endpoint = usb_scan_descriptor(id, interface, 5);
  if (!endpoint || !(endpoint->address & 0x80) || (endpoint->attributes & 3) != 3)
    return -1;
#if USBD_DRIVER_VERSION < 0x010000
  if ((usb_configuration.ports + 8) / 8 <
      endpoint->max_packet_low + (endpoint->max_packet_high << 8))
    return -1;
#endif
  descriptor = usb_scan_descriptor(id, endpoint, USB_DESCRIPTOR_HUB);
  hub = usb_alloc_hub();
  if (!hub)
    return -1;
  device->private_data = hub;
#if USBD_DRIVER_VERSION >= 0x010000
  hub->device = device;
#endif
  hub->control = device->endpoint_head;
  pipe = usb_open_pipe(device, endpoint, 0);
  if (!pipe) {
    usb_free_hub(hub);
  fail:
    // needed for matching :(
    asm("");
    return -1;
  }
  hub->interrupt = pipe;
  hub->interrupt_request.callback_argument = hub;
  hub->control_request.callback_argument = hub;
  hub->max_power = config->max_power;
  hub->self_powered = (config->attributes >> 6) & 1;
#if USBD_DRIVER_VERSION >= 0x010000
  usb_update_hub_power(hub);
#endif
  if (descriptor) {
    int length = USB_HUB_DESCRIPTOR_SIZE;
    if (descriptor[0] < length)
      length = descriptor[0];
    bcopy(descriptor, hub->descriptor, length);
  }
  usb_hub_control(hub, 0, 9, config->value, 0, 0, 0, usb_hub_configured, "hub_attach");
  return 0;
}

int usb_hub_detach(int id) {
  struct UsbDevice* device = usb_get_device(id);
  if (!device)
    return -1;
  usb_free_hub(device->private_data);
  return 0;
}

int usb_hub_probe(int id) {
  UsbDeviceDescriptor* descriptor = usb_scan_descriptor(id, 0, USB_DESCRIPTOR_DEVICE);
  if (descriptor && descriptor->device_class == USB_CLASS_HUB)
    return descriptor->configuration_count == 1;
  return 0;
}

int usb_initialize_hubs(void) {
  int size = sizeof(struct UsbHub) * usb_configuration.hubs;
  struct UsbHub* hub = usb_alloc_memory(size);
  int i;
  u_int gp;
  usb_free_hubs = hub;
  if (!hub)
    return -1;
#if USBD_DRIVER_VERSION >= 0x010000
  usb_hub_memory = hub;
#endif
  bzero(hub, size);
  usb_configuration.allocated_bytes += size;
  for (i = 0; i < usb_configuration.hubs; ++i, ++hub)
    hub->next = i < usb_configuration.hubs - 1 ? hub + 1 : 0;
  __asm__ volatile("move %0,$gp" : "=r"(gp));
  usb_register_driver(&usb_hub_driver, gp);
  return 0;
}

#if USBD_DRIVER_VERSION >= 0x010000
void usb_shutdown_hubs(void) {
  usb_unregister_driver(&usb_hub_driver);
  usb_free_memory(usb_hub_memory);
}
#endif

struct UsbHub* usb_free_hubs = 0;
#if USBD_DRIVER_VERSION >= 0x010000
struct UsbHub* usb_hub_memory = 0;
#endif
u_int usb_next_hub_id = 0;
UsbDriver usb_hub_driver = {
    0, 0, "hub", usb_hub_probe, usb_hub_attach, usb_hub_detach, {0, 0, 0, 0, 0}, 0};
