#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/usbd/internal.h"
#include "sce/iop/usbd/scheduling.h"

#if USBD_DRIVER_VERSION >= 0x010000
static void* usb_controller_memory;
#endif

#define USB_ENDPOINT_CONTROL 0
#define USB_ENDPOINT_ISOCHRONOUS 1
#define USB_ENDPOINT_BULK 2
#define USB_ENDPOINT_INTERRUPT 3
#define USB_ENDPOINT_IN 0x80
#define USB_OHCI_ENDPOINT_LOW_SPEED 0x2000
#define USB_OHCI_ENDPOINT_ISOCHRONOUS 0x8000
#define USB_OHCI_ENDPOINT_IN 0x1000
#define USB_OHCI_ENDPOINT_OUT 0x800
#define USB_SCHEDULE_CONTROL 63
#define USB_SCHEDULE_BULK 64
#define USB_SCHEDULE_ISOCHRONOUS 65
#define USB_OHCI_ENDPOINT_SKIP 0x4000
#define USB_ENDPOINT_CLEANUP_DELAY 200
#define USB_OHCI_ENDPOINT_HALTED 1
#define USB_OHCI_CONTROL_FILLED 2
#define USB_TD_SETUP 0xe2e00000
#define USB_TD_DATA_IN 0xe3f40000
#define USB_TD_DATA_OUT 0xe3ec0000
#define USB_TD_STATUS_OUT 0xe3080000
#define USB_TD_STATUS_IN 0xe3100000
#define USB_ISO_NOT_ACCESSED 0xe0000000
#define USB_ISO_PACKET_NOT_ACCESSED 0xe000
#define USB_TD_DATA 0xe01c0000
#define USB_OHCI_BULK_FILLED 4
#define USB_DEVICE_WAIT_RESET 4
#define USB_DEVICE_RESETTING 5
#define USB_OHCI_PORT_RESET 0x10
#define USB_DEVICE_READY 7
#define USB_PORT_CONNECTED 1
#define USB_PORT_RESET 0x10
#define USB_PORT_CONNECT_CHANGE 0x10000
#define USB_PORT_CHANGE_MASK 0x1f0000
#define USB_DEVICE_ATTACHED 1
#define USB_DEVICE_RESET_DELAY 3
#define USB_DEVICE_ENUMERATING 6
#define USB_RESET_DELAY_TICKS 501
#define USB_TD_DIRECTION_MASK 0x180000
#define USB_TD_DELAY_MASK 0xe00000
#define USB_REQUEST_ABORTED 290
#define USB_INTERRUPT_OVERRUN 1
#define USB_INTERRUPT_DONE 2
#define USB_INTERRUPT_FRAME 4
#define USB_INTERRUPT_RESUME 8
#define USB_INTERRUPT_ERROR 16
#define USB_INTERRUPT_OVERFLOW 32
#define USB_INTERRUPT_ROOT 64
#define USB_INTERRUPT_OWNERSHIP 0x40000000
#define USB_INTERRUPT_MASTER 0x80000000
#define IOP_BOOT_ROM_BASE 0xbfc00000
#define USB_COMMAND_RESET 1
#define USB_OHCI_ENABLE_LISTS 0x3c
#define USB_RESET_SETTLE_MICROSECONDS 2000
#define USB_RESET_POLL_LIMIT 1000
#define USB_OHCI_BASE 0xbf801600
#define USB_DMA_PRIORITY (*(volatile u_int*)0xbf801570)
#define USB_DMA_ENABLE 0x08000000
#define USB_CONTROLLER_ENABLE (*(volatile u_int*)0xbf801680)
#define USB_UNCACHED_ADDRESS(address) (((u_int)(address) & 0x1fffffff) | 0xa0000000)
#define USB_ENDPOINT_SKIP 0x4000
#define USB_SCHEDULE_ENDPOINTS 66
#define USB_PERIODIC_ENDPOINTS 63
#define USB_FRAME_INTERVAL 0x27782edf
#define USB_PERIODIC_START 0x2a2f
#define USB_ENABLED_INTERRUPTS 0x80000073
#define USB_OPERATIONAL_CONTROL 0xbf

struct UsbDevice* usb_get_device(int device) {
  struct UsbDevice* result;
  if (device <= 0)
    return 0;
  if (device >= usb_device_limit)
    return 0;
  result = &usb_host->devices[device];
  if (!result->parent)
    return 0;
  return result;
}

struct UsbEndpoint* usb_get_endpoint(int endpoint) {
  struct UsbEndpoint* result;
  if (endpoint < 0)
    return 0;
  if (endpoint >= usb_endpoint_limit)
    return 0;
  result = &usb_host->endpoints[endpoint];
  if (!result->device)
    return 0;
  return result;
}

struct UsbDevice* usb_get_root(void) {
  return usb_host->root;
}

struct UsbDevice* usb_attach_child(struct UsbDevice* parent, u_int port) {
  struct UsbDevice* device = usb_host->free_device_head;
  if (device) {
    struct UsbDevice* next = device->next;
    struct UsbDevice* previous = device->previous;
    if (next)
      next->previous = previous;
    else
      usb_host->free_device_tail = previous;
    previous = device->previous;
    next = device->next;
    if (previous)
      previous->next = next;
    else
      usb_host->free_device_head = next;
    device->endpoint_tail = 0;
    device->endpoint_head = 0;
    device->driver = 0;
    device->status = 1;
    device->reset_pending = 0;
#if USBD_DRIVER_VERSION >= 0x010000
    device->power_state = 0;
#endif
    device->child_tail = 0;
    device->child_head = 0;
    device->parent = parent;
    device->port = port;
    device->private_data = 0;
    if (parent) {
      device->previous = parent->child_tail;
      if (device->previous)
        device->previous->next = device;
      else
        parent->child_head = device;
      device->next = 0;
      parent->child_tail = device;
    }
  }
  return device;
}

int usb_release_device(struct UsbDevice* device) {
  int index = device - usb_host->devices;
  if (device && index > 0 && index < usb_device_limit) {
    device->previous = usb_host->free_device_tail;
    if (device->previous)
      device->previous->next = device;
    else
      usb_host->free_device_head = device;
    device->next = 0;
    usb_host->free_device_tail = device;
    device->parent = 0;
  }
  return 0;
}

struct UsbRequest* usb_alloc_request(void) {
  struct UsbRequest* next;
  struct UsbRequest* previous;
  struct UsbRequest* request = usb_host->free_request_head;
  next = request;
  if (next) {
    previous = request->previous;
    if (request->next)
      request->next->previous = previous;
    else
      usb_host->free_request_tail = previous;
    previous = request->previous;
    next = request->next;
    if (previous)
      previous->next = next;
    else
      usb_host->free_request_head = next;
  }
  return request;
}

int usb_free_request(struct UsbRequest* request) {
  int index = request - usb_host->requests;
  struct UsbRequest* walk;
  if (!request)
    return 0;
  if (index >= 0 && index < usb_request_limit) {
    for (walk = usb_host->free_request_head; walk; walk = walk->next) {
      if (walk == request)
        break;
    }
    if (!walk) {
      request->busy = 0;
      request->previous = usb_host->free_request_tail;
      if (request->previous)
        request->previous->next = request;
      else
        usb_host->free_request_head = request;
      request->next = 0;
      usb_host->free_request_tail = request;
    }
  } else {
    request->busy = 0;
  }
  return 0;
}

struct UsbEndpoint* usb_alloc_endpoint(struct UsbDevice* device, u_int alignment) {
  struct UsbEndpoint* endpoint = usb_host->free_endpoint_head;
  if (endpoint) {
    struct UsbEndpoint* next = endpoint->next;
    struct UsbEndpoint* previous = endpoint->previous;
    if (next)
      next->previous = previous;
    else
      usb_host->free_endpoint_tail = previous;
    previous = endpoint->previous;
    next = endpoint->next;
    if (previous)
      previous->next = next;
    else
      usb_host->free_endpoint_head = next;
    endpoint->device = device;
    endpoint->request_tail = 0;
    endpoint->request_head = 0;
    endpoint->schedule_previous = 0;
    endpoint->schedule_next = 0;
    endpoint->scheduled = 0;
    endpoint->alignment = alignment;
    endpoint->previous = device->endpoint_tail;
    if (endpoint->previous)
      endpoint->previous->next = endpoint;
    else
      device->endpoint_head = endpoint;
    endpoint->next = 0;
    device->endpoint_tail = endpoint;
  }
  return endpoint;
}

int usb_free_endpoint(struct UsbDevice* device, struct UsbEndpoint* endpoint) {
  int index = endpoint - usb_host->endpoints;
  if (endpoint && index >= 0 && index < usb_endpoint_limit) {
    struct UsbEndpoint* link;
    struct UsbEndpoint* neighbor;
    if (endpoint->scheduled) {
      link = endpoint->schedule_next;
      neighbor = endpoint->schedule_previous;
      if (link)
        link->schedule_previous = neighbor;
      else
        usb_host->scheduled_tail = neighbor;
      neighbor = endpoint->schedule_previous;
      link = endpoint->schedule_next;
      if (neighbor)
        neighbor->schedule_next = link;
      else
        usb_host->scheduled_head = link;
      endpoint->scheduled = 0;
    }
    link = endpoint->next;
    neighbor = endpoint->previous;
    if (link)
      link->previous = neighbor;
    else
      device->endpoint_tail = neighbor;
    link = endpoint->previous;
    neighbor = endpoint->next;
    if (link)
      link->next = neighbor;
    else
      device->endpoint_head = neighbor;
    endpoint->previous = usb_host->free_endpoint_tail;
    if (endpoint->previous)
      endpoint->previous->next = endpoint;
    else
      usb_host->free_endpoint_head = endpoint;
    endpoint->next = 0;
    usb_host->free_endpoint_tail = endpoint;
    endpoint->device = 0;
  }
  return 0;
}

struct UsbTransferDescriptor* usb_alloc_transfer_descriptor(void) {
  struct UsbTransferDescriptor* descriptor = usb_host->free_transfer_head;
  if (descriptor) {
    usb_host->free_transfer_head = descriptor->next;
    descriptor->next = 0;
  }
  return descriptor;
}

void usb_free_transfer_descriptor(struct UsbTransferDescriptor* descriptor) {
  struct UsbTransferDescriptor* walk;
  if (!descriptor)
    return;
  for (walk = usb_host->free_transfer_head; walk; walk = walk->next) {
    if (descriptor == walk)
      return;
  }
  descriptor->next = usb_host->free_transfer_head;
  usb_host->free_transfer_head = descriptor;
}

struct UsbIsoDescriptor* usb_alloc_iso_descriptor(void) {
  struct UsbIsoDescriptor* descriptor = usb_host->free_iso_head;
  if (descriptor) {
    usb_host->free_iso_head = descriptor->next;
    descriptor->next = 0;
  }
  return descriptor;
}

void usb_free_iso_descriptor(struct UsbIsoDescriptor* descriptor) {
  struct UsbIsoDescriptor* walk;
  if (!descriptor)
    return;
  for (walk = usb_host->free_iso_head; walk; walk = walk->next) {
    if (descriptor == walk)
      return;
  }
  descriptor->next = usb_host->free_iso_head;
  usb_host->free_iso_head = descriptor;
}

int usb_add_timer(struct UsbTimer* timer, void (*callback)(void*), void* argument, int delay) {
  struct UsbTimer* walk;
  if (timer->active)
    return -1;
  timer->active = 1;
  if (delay > 0)
    --delay;
  timer->callback = callback;
  timer->argument = argument;
  for (walk = usb_host->timer_head; walk; walk = walk->next) {
    if (delay < walk->delay)
      break;
    delay -= walk->delay;
  }
  if (walk) {
    timer->previous = walk->previous;
    if (timer->previous)
      timer->previous->next = timer;
    else
      usb_host->timer_head = timer;
    timer->next = walk;
    walk->previous = timer;
    walk->delay -= delay;
  } else {
    timer->previous = usb_host->timer_tail;
    if (timer->previous)
      timer->previous->next = timer;
    else
      usb_host->timer_head = timer;
    usb_host->timer_tail = timer;
    timer->next = 0;
  }
  timer->delay = delay;
  usb_host->registers->interrupt_enable = USB_OHCI_INTERRUPT_SOF;
  return 0;
}

int usb_cancel_timer(struct UsbTimer* timer) {
  struct UsbTimer* link;
  struct UsbTimer* neighbor;
  if (!timer->active)
    return -1;
  link = timer->next;
  neighbor = timer->previous;
  if (link)
    link->previous = neighbor;
  else
    usb_host->timer_tail = neighbor;
  link = timer->previous;
  neighbor = timer->next;
  if (link)
    link->next = neighbor;
  else
    usb_host->timer_head = neighbor;
  timer->active = 0;
  timer->previous = 0;
  timer->next = 0;
  return 0;
}

void usb_tick_timers(void) {
  struct UsbTimer* timer = usb_host->timer_head;
  if (timer) {
    if (timer->delay > 0)
      --timer->delay;
    while ((timer = usb_host->timer_head)) {
      if (timer->delay > 0)
        break;
      if ((usb_host->timer_head = timer->next))
        timer->next->previous = 0;
      else
        usb_host->timer_tail = 0;
      timer->previous = 0;
      timer->next = 0;
      timer->active = 0;
      timer->callback(timer->argument);
    }
  }
  if (!usb_host->timer_head)
    usb_host->registers->interrupt_disable = USB_OHCI_INTERRUPT_SOF;
}

void usb_link_endpoint(int index, struct UsbEndpointDescriptor* descriptor) {
  descriptor->next = usb_host->endpoint_descriptors[index].next;
  usb_host->endpoint_descriptors[index].next = descriptor;
}

void usb_unlink_endpoint(int index, struct UsbEndpointDescriptor* descriptor) {
  struct UsbEndpointDescriptor* previous = &usb_host->endpoint_descriptors[index];
  while (previous->next) {
    struct UsbEndpointDescriptor* current = previous->next;
    if (current == descriptor) {
      previous->next = current->next;
      return;
    }
    previous = current;
  }
}

#if USBD_DRIVER_VERSION >= 0x010000
#define USB_PERIODIC_FRAMES 32
#define USB_PACKET_OVERHEAD 13
#define USB_FULL_SPEED_PACKET_BYTES 64

int usb_reserve_bandwidth(struct UsbEndpoint* endpoint, UsbEndpointInfo* info, int low_speed) {
  int packet_size = info->max_packet_low + (info->max_packet_high << 8);
  int* loads = usb_host->periodic_load;
  int polling_interval = info->interval;
  int slot;
  int interval, frames, schedule, phase, best_load, i, j, load;
  if (polling_interval >= 32) {
    schedule = 31;
    frames = 1;
    interval = 32;
  } else if (polling_interval >= 16) {
    schedule = 15;
    frames = 2;
    interval = 16;
  } else if (polling_interval >= 8) {
    schedule = 7;
    frames = 4;
    interval = 8;
  } else if (polling_interval >= 4) {
    schedule = 3;
    frames = 8;
    interval = 4;
  } else if (polling_interval >= 2) {
    schedule = 1;
    frames = 16;
    interval = 2;
  } else {
    schedule = 0;
    frames = 32;
    interval = 1;
  }
  phase = 0;
  if (interval > 1) {
    phase = -1;
    best_load = 0;
    for (i = 0; i < interval; ++i) {
      slot = i;
      load = 0;
      for (j = 0; j < frames; ++j, slot += interval)
        load += loads[slot];
      if (phase < 0 || load < best_load) {
        phase = i;
        best_load = load;
      }
    }
    schedule += phase;
  }
  if (packet_size > USB_FULL_SPEED_PACKET_BYTES)
    packet_size = USB_FULL_SPEED_PACKET_BYTES;
  load = packet_size + USB_PACKET_OVERHEAD;
  endpoint->schedule_phase = phase;
  endpoint->schedule_frames = frames;
  endpoint->schedule_interval = interval;
  endpoint->schedule_cost = load;
  if (low_speed)
    load <<= 3;
  slot = phase;
  for (j = 0; j < frames; ++j, slot += interval)
    loads[slot] += load;
  return schedule;
}

void usb_release_bandwidth(struct UsbEndpoint* endpoint, int low_speed) {
  int* loads = usb_host->periodic_load;
  int cost = endpoint->schedule_cost;
  int frames = endpoint->schedule_frames;
  int interval = endpoint->schedule_interval;
  int phase = endpoint->schedule_phase;
  int i;
  int* load;
  if (low_speed)
    cost <<= 3;
  for (i = 0; i < frames; ++i, phase += interval) {
    load = &loads[phase];
    *load -= cost;
    if (*load < 0)
      *load = 0;
  }
}
#endif

struct UsbEndpoint* usb_open_endpoint(struct UsbDevice* device,
                                      UsbEndpointInfo* info,
#if USBD_DRIVER_VERSION >= 0x010000
                                      u_int input_alignment) {
  register u_int alignment asm("$21") = input_alignment;
#else
                                      u_int alignment) {
#endif
  void* transfer = 0;
  int schedule = 0;
  struct UsbEndpoint* endpoint = usb_alloc_endpoint(device, alignment);
  struct UsbEndpointDescriptor* descriptor;
  int packet_size;
  u_int control;
  u_int flags;
  u_int address;
  u_int direction;
#if USBD_DRIVER_VERSION >= 0x010000
  // Keep the alignment mode in its original saved register across allocation.
  asm("" : "+r"(alignment));
#endif
  if (!endpoint)
    return transfer;
  descriptor = endpoint->descriptor;
  if (!info) {
    descriptor->control = 0x80000 | (device->low_speed ? USB_OHCI_ENDPOINT_LOW_SPEED : 0);
    schedule = USB_SCHEDULE_CONTROL;
  } else {
    packet_size = info->max_packet_low + (info->max_packet_high << 8);
    switch (info->attributes & 3) {
      case USB_ENDPOINT_CONTROL:
        schedule = USB_SCHEDULE_CONTROL;
        if (!alignment && packet_size >= 63)
          packet_size = 62;
        break;
      case USB_ENDPOINT_ISOCHRONOUS:
        schedule = USB_SCHEDULE_ISOCHRONOUS;
        transfer = usb_alloc_iso_descriptor();
        if (!transfer) {
          return (struct UsbEndpoint*)usb_free_endpoint(device, endpoint);
        }
        break;
      case USB_ENDPOINT_BULK:
        schedule = USB_SCHEDULE_BULK;
        if (!(info->address & USB_ENDPOINT_IN) && !alignment && packet_size >= 63)
          packet_size = 62;
        break;
      case USB_ENDPOINT_INTERRUPT:
#if USBD_DRIVER_VERSION >= 0x010000
        schedule = usb_reserve_bandwidth(endpoint, info, device->low_speed);
#else
        if (info->interval >= 32)
          schedule = 31;
        else if (info->interval >= 16)
          schedule = 15;
        else if (info->interval >= 8)
          schedule = 7;
        else if (info->interval >= 4)
          schedule = 3;
        else
          schedule = info->interval >= 2;
#endif
        if (!(info->address & USB_ENDPOINT_IN) && !alignment && packet_size >= 63)
          packet_size = 62;
        break;
    }
    control = (packet_size << 16) & 0x07ff0000;
    if (schedule == USB_SCHEDULE_ISOCHRONOUS)
      control |= USB_OHCI_ENDPOINT_ISOCHRONOUS;
    flags = control;
    if (device->low_speed)
      flags |= USB_OHCI_ENDPOINT_LOW_SPEED;
    control = (info->address & 31) << 7;
    address = device->address & 127;
    if (info->address & USB_ENDPOINT_IN)
      direction = control | USB_OHCI_ENDPOINT_IN;
    else
      direction = control | USB_OHCI_ENDPOINT_OUT;
    descriptor->control = flags | direction | address;
  }
  endpoint->schedule_index = schedule;
  if (!transfer) {
    transfer = usb_alloc_transfer_descriptor();
    if (!transfer) {
      return (struct UsbEndpoint*)usb_free_endpoint(device, endpoint);
    }
  }
  descriptor->head = transfer;
  descriptor->tail = transfer;
  usb_link_endpoint(schedule, descriptor);
  return endpoint;
}

void usb_cleanup_endpoint(struct UsbEndpoint* endpoint) {
  struct UsbEndpointDescriptor* descriptor = endpoint->descriptor;
  struct UsbRequest* request;
  int i;
  if (endpoint->schedule_index == USB_SCHEDULE_ISOCHRONOUS) {
    for (i = 0; i < usb_iso_limit; ++i) {
      request = usb_host->iso_requests[i];
      if (request && request->endpoint == endpoint) {
        usb_free_request(request);
        usb_host->iso_requests[i] = 0;
        usb_free_iso_descriptor(&usb_host->iso_descriptors[i]);
      }
    }
    usb_free_iso_descriptor(descriptor->tail);
    descriptor->tail = 0;
  } else {
    for (i = 0; i < usb_transfer_limit; ++i) {
      request = usb_host->transfer_requests[i];
      if (request && request->endpoint == endpoint) {
        usb_free_request(request);
        usb_host->transfer_requests[i] = 0;
        usb_free_transfer_descriptor(&usb_host->transfer_descriptors[i]);
      }
    }
    usb_free_transfer_descriptor(descriptor->tail);
    descriptor->tail = 0;
  }
  descriptor->head = 0;
  while ((request = endpoint->request_head)) {
    struct UsbRequest* neighbor;
    struct UsbRequest* link;
    link = request->next;
    neighbor = request->previous;
    if (link)
      link->previous = neighbor;
    else
      endpoint->request_tail = neighbor;
    link = request->previous;
    neighbor = request->next;
    if (link)
      link->next = neighbor;
    else
      endpoint->request_head = neighbor;
    usb_free_request(request);
  }
#if USBD_DRIVER_VERSION >= 0x010000
  usb_release_bandwidth(endpoint, endpoint->device->low_speed);
#endif
  if (endpoint->scheduled) {
    struct UsbEndpoint* link = endpoint->schedule_next;
    struct UsbEndpoint* neighbor = endpoint->schedule_previous;
    if (link)
      link->schedule_previous = neighbor;
    else
      usb_host->scheduled_tail = neighbor;
    link = endpoint->schedule_previous;
    neighbor = endpoint->schedule_next;
    if (link)
      link->schedule_next = neighbor;
    else
      usb_host->scheduled_head = neighbor;
    endpoint->scheduled = 0;
  }
  endpoint->previous = usb_host->free_endpoint_tail;
  if (endpoint->previous)
    endpoint->previous->next = endpoint;
  else
    usb_host->free_endpoint_head = endpoint;
  endpoint->next = 0;
  usb_host->free_endpoint_tail = endpoint;
}

int usb_close_endpoint(struct UsbDevice* device, struct UsbEndpoint* endpoint) {
  struct UsbEndpoint* neighbor;
  struct UsbEndpoint* link;
  endpoint->descriptor->control |= USB_OHCI_ENDPOINT_SKIP;
  usb_unlink_endpoint(endpoint->schedule_index, endpoint->descriptor);
  link = endpoint->next;
  neighbor = endpoint->previous;
  if (link)
    link->previous = neighbor;
  else
    device->endpoint_tail = neighbor;
  link = endpoint->previous;
  neighbor = endpoint->next;
  if (link)
    link->next = neighbor;
  else
    device->endpoint_head = neighbor;
  endpoint->device = 0;
  usb_add_timer(&endpoint->cleanup_timer, (void (*)(void*))usb_cleanup_endpoint, endpoint,
                USB_ENDPOINT_CLEANUP_DELAY);
  return 0;
}

int usb_schedule_control(struct UsbEndpoint* endpoint) {
  struct UsbEndpointDescriptor* descriptor = endpoint->descriptor;
  struct UsbRequest* request;
  struct UsbTransferDescriptor* setup;
  struct UsbTransferDescriptor* data;
  struct UsbTransferDescriptor* status;
  struct UsbTransferDescriptor* tail;
  struct UsbTransferDescriptor* next;
  UsbDeviceRequest* setup_packet;
  u_int control;
  if (!descriptor->tail || ((u_int)descriptor->head & USB_OHCI_ENDPOINT_HALTED) ||
      (descriptor->control & USB_OHCI_ENDPOINT_SKIP)) {
    unschedule(endpoint);
    return 0;
  }
  request = endpoint->request_head;
  data = 0;
  if (!request) {
    unschedule(endpoint);
    return 0;
  }
  setup = descriptor->tail;
  if (request->buffer && request->length > 0) {
    data = usb_alloc_transfer_descriptor();
    if (!data) {
      schedule(endpoint, 1);
      return 0;
    }
  }
  status = usb_alloc_transfer_descriptor();
  tail = usb_alloc_transfer_descriptor();
  if (!status || !tail) {
    usb_free_transfer_descriptor(data);
    usb_free_transfer_descriptor(status);
    usb_free_transfer_descriptor(tail);
    schedule(endpoint, 1);
    return 0;
  }
  {
    struct UsbRequest* neighbor;
    struct UsbRequest* link;
    link = request->next;
    neighbor = request->previous;
    if (link)
      link->previous = neighbor;
    else
      endpoint->request_tail = neighbor;
    link = request->previous;
    neighbor = request->next;
    if (link)
      link->next = neighbor;
    else
      endpoint->request_head = neighbor;
  }
  next = data;
  setup_packet = &request->setup;
  setup->control = USB_TD_SETUP;
  setup->buffer = setup_packet;
  if (!next)
    next = status;
  setup->next = next;
  setup->buffer_end = (u_char*)&request->setup + 7;
  usb_host->transfer_requests[setup - usb_host->transfer_descriptors] = request;
  if (data) {
    control = USB_TD_DATA_IN;
    if (!(request->setup.request_type & 0x80))
      control = USB_TD_DATA_OUT;
    data->control = control;
    data->buffer = request->buffer;
    data->next = status;
    data->buffer_end = (u_char*)request->buffer + request->length - 1;
    usb_host->transfer_requests[data - usb_host->transfer_descriptors] = request;
  }
  control = USB_TD_STATUS_OUT;
  if (!(setup_packet->request_type & 0x80))
    control = USB_TD_STATUS_IN;
  status->control = control;
  status->buffer = 0;
  status->next = tail;
  status->buffer_end = 0;
  usb_host->transfer_requests[status - usb_host->transfer_descriptors] = request;
  descriptor->tail = tail;
  usb_host->registers->command_status |= USB_OHCI_CONTROL_FILLED;
  if (!endpoint->request_head && endpoint->scheduled) {
    struct UsbEndpoint* next = endpoint->schedule_next;
    struct UsbEndpoint* previous = endpoint->schedule_previous;
    if (next)
      next->schedule_previous = previous;
    else
      usb_host->scheduled_tail = previous;
    previous = endpoint->schedule_previous;
    next = endpoint->schedule_next;
    if (previous)
      previous->schedule_next = next;
    else
      usb_host->scheduled_head = next;
    endpoint->scheduled = 0;
  }
  return 1;
}

int usb_schedule_iso(struct UsbEndpoint* endpoint) {
  struct UsbEndpointDescriptor* descriptor = endpoint->descriptor;
  struct UsbRequest* request;
  struct UsbIsoDescriptor* transfer;
  struct UsbIsoDescriptor* tail;
  u_short frame;
  if (!descriptor->tail || ((u_int)descriptor->head & USB_OHCI_ENDPOINT_HALTED) ||
      (descriptor->control & USB_OHCI_ENDPOINT_SKIP)) {
    unschedule(endpoint);
    return 0;
  }
  request = endpoint->request_head;
  if (!request) {
    unschedule(endpoint);
    return 0;
  }
  transfer = descriptor->tail;
  tail = usb_alloc_iso_descriptor();
  if (!tail) {
    schedule(endpoint, 2);
    return 0;
  }
  {
    struct UsbRequest* neighbor;
    struct UsbRequest* link;
    link = request->next;
    neighbor = request->previous;
    if (link)
      link->previous = neighbor;
    else
      endpoint->request_tail = neighbor;
    link = request->previous;
    neighbor = request->next;
    if (link)
      link->next = neighbor;
    else
      endpoint->request_head = neighbor;
  }
  if (((u_int)descriptor->head & ~15) == (u_int)descriptor->tail)
    frame = usb_host->communication->frame_number + 2;
  else
    frame = endpoint->next_frame;
#if USBD_DRIVER_VERSION >= 0x010000
  {
    UsbMultiIsoRequest* multi = &request->multi_iso;
    u_short is_multi = multi->packet_count != 0;
    int frames = 1;
    if (is_multi)
      frames = (u_short)multi->packet_count;
    frame += (u_short)request->wait_frames;
    endpoint->next_frame = frames + frame;
    if (is_multi) {
      u_int buffer = (u_int)request->buffer;
      u_int offset;
      int i;
      u_int control = frame | USB_ISO_NOT_ACCESSED;
      transfer->control = ((multi->packet_count - 1) << 24) | control;
      transfer->buffer_page = (void*)(buffer & ~4095);
      transfer->next = tail;
      transfer->buffer_end =
          buffer && request->length > 0 ? (u_char*)request->buffer + request->length - 1 : 0;
      offset = (buffer & 4095) | USB_ISO_PACKET_NOT_ACCESSED;
      for (i = 0; i < multi->packet_count; ++i) {
        transfer->packet_status[i] = offset;
        offset += multi->packets[i] & 0x7ff;
      }
    } else {
#else
  frame += (u_short)request->wait_frames;
  endpoint->next_frame = frame + 1;
#endif
      transfer->control = frame | USB_ISO_NOT_ACCESSED;
      transfer->buffer_page = (void*)((u_int)request->buffer & ~4095);
      transfer->next = tail;
      transfer->buffer_end = request->buffer && request->length > 0
                                 ? (u_char*)request->buffer + request->length - 1
                                 : 0;
      transfer->packet_status[0] = ((u_int)request->buffer & 4095) | USB_ISO_PACKET_NOT_ACCESSED;
#if USBD_DRIVER_VERSION >= 0x010000
    }
  }
#endif
  usb_host->iso_requests[transfer - usb_host->iso_descriptors] = request;
  descriptor->tail = tail;
  if (!endpoint->request_head && endpoint->scheduled) {
    struct UsbEndpoint* next = endpoint->schedule_next;
    struct UsbEndpoint* previous = endpoint->schedule_previous;
    if (next)
      next->schedule_previous = previous;
    else
      usb_host->scheduled_tail = previous;
    previous = endpoint->schedule_previous;
    next = endpoint->schedule_next;
    if (previous)
      previous->schedule_next = next;
    else
      usb_host->scheduled_head = next;
    endpoint->scheduled = 0;
  }
  return 1;
}

int usb_schedule_data(struct UsbEndpoint* endpoint) {
  struct UsbEndpointDescriptor* descriptor = endpoint->descriptor;
  struct UsbRequest* request;
  struct UsbTransferDescriptor* transfer;
  struct UsbTransferDescriptor* tail;
  if (!descriptor->tail || ((u_int)descriptor->head & USB_OHCI_ENDPOINT_HALTED) ||
      (descriptor->control & USB_OHCI_ENDPOINT_SKIP)) {
    unschedule(endpoint);
    return 0;
  }
  request = endpoint->request_head;
  if (!request) {
    unschedule(endpoint);
    return 0;
  }
  transfer = descriptor->tail;
  tail = usb_alloc_transfer_descriptor();
  if (!tail) {
    schedule(endpoint, 1);
    return 0;
  }
  {
    struct UsbRequest* neighbor;
    struct UsbRequest* link;
    link = request->next;
    neighbor = request->previous;
    if (link)
      link->previous = neighbor;
    else
      endpoint->request_tail = neighbor;
    link = request->previous;
    neighbor = request->next;
    if (link)
      link->next = neighbor;
    else
      endpoint->request_head = neighbor;
  }
  transfer->control = USB_TD_DATA;
  transfer->buffer = request->buffer;
  transfer->next = tail;
  transfer->buffer_end =
      request->buffer && request->length > 0 ? (u_char*)request->buffer + request->length - 1 : 0;
  usb_host->transfer_requests[transfer - usb_host->transfer_descriptors] = request;
  descriptor->tail = tail;
  if (endpoint->schedule_index == USB_SCHEDULE_BULK)
    usb_host->registers->command_status |= USB_OHCI_BULK_FILLED;
  if (!endpoint->request_head)
    unschedule(endpoint);
  return 1;
}

int usb_schedule_endpoint(struct UsbEndpoint* endpoint) {
  if (endpoint->schedule_index == USB_SCHEDULE_CONTROL)
    return usb_schedule_control(endpoint);
  else if (endpoint->schedule_index == USB_SCHEDULE_ISOCHRONOUS)
    return usb_schedule_iso(endpoint);
  else
    return usb_schedule_data(endpoint);
}

#if USBD_DRIVER_VERSION >= 0x010000
void usb_reset_root_ports(void) {
  usb_host->registers->port_status[0] = USB_OHCI_PORT_RESET;
  usb_host->registers->port_status[1] = USB_OHCI_PORT_RESET;
}
#endif

int usb_start_device_reset(struct UsbDevice* device) {
  if (usb_host->reset_active) {
    device->status = USB_DEVICE_WAIT_RESET;
    return 0;
  }
  usb_host->reset_active = 1;
  device->status = USB_DEVICE_RESETTING;
  device->reset_pending = 1;
  if (device->parent == usb_host->root)
    usb_host->registers->port_status[device->port - 1] = USB_OHCI_PORT_RESET;
  else
    usb_reset_hub_port(device);
  return 1;
}

int usb_reset_next_device(struct UsbDevice* parent) {
  struct UsbDevice* child;
  for (child = parent->child_head; child; child = child->next) {
    if (child->status == USB_DEVICE_WAIT_RESET) {
      usb_start_device_reset(child);
      return 1;
    }
    if (child->child_head &&
#if USBD_DRIVER_VERSION >= 0x010000
        (u_char)(child->status - USB_DEVICE_READY) < 2 &&
#else
        child->status == USB_DEVICE_READY &&
#endif
        usb_reset_next_device(child))
      return 1;
  }
  return 0;
}

int usb_finish_device_reset(struct UsbDevice* device) {
  usb_host->reset_active = 0;
  device->reset_pending = 0;
  usb_reset_next_device(usb_host->root);
  return 0;
}

void usb_device_reset_callback(void* argument) {
  usb_start_device_reset(argument);
}

int usb_check_root_ports(void) {
  struct UsbDevice* device = usb_host->root->child_head;
  volatile u_int* port = usb_host->registers->port_status;
  struct UsbDevice* next;
  u_int status;
  while (device) {
    next = device->next;
    status = *port;
    *port = USB_PORT_CHANGE_MASK;
    if (!(status & USB_PORT_CONNECTED)) {
      usb_disconnect_device(device);
    } else {
      if (device->status >= USB_DEVICE_RESET_DELAY && (status & USB_PORT_CONNECT_CHANGE))
        usb_disconnect_device(device);
      switch (device->status) {
        case USB_DEVICE_ATTACHED:
          if (status & USB_PORT_CONNECTED) {
            device->status = USB_DEVICE_RESET_DELAY;
            usb_add_timer(&device->reset_timer, usb_device_reset_callback, device,
                          USB_RESET_DELAY_TICKS);
          }
          break;
        case USB_DEVICE_RESETTING:
          if (!(status & USB_PORT_RESET)) {
            device->status = USB_DEVICE_ENUMERATING;
            device->low_speed = (status >> 9) & 1;
            if (usb_open_endpoint(device, 0, 0))
              usb_begin_enumeration(device);
          }
          break;
      }
    }
    device = next;
    ++port;
  }
  return 0;
}

void usb_retry_transfer(void) {
  struct UsbEndpoint* endpoint;
  if (usb_host->free_transfer_head) {
    for (endpoint = usb_host->scheduled_head; endpoint; endpoint = endpoint->schedule_next) {
      if (endpoint->scheduled == 1) {
        usb_schedule_endpoint(endpoint);
        break;
      }
    }
  }
}

struct RequestList {
  struct UsbRequest* head;
  struct UsbRequest* tail;
};

static __inline__ struct UsbRequest* take_request(struct UsbTransferDescriptor* transfer) {
  struct UsbRequest** slot =
      &usb_host->transfer_requests[transfer - usb_host->transfer_descriptors];
  struct UsbRequest* request = *slot;
  *slot = 0;
  return request;
}

void usb_complete_transfer(struct UsbTransferDescriptor* transfer) {
  struct RequestList completed;
  u_int control;
  void* buffer;
  void* end;
  struct UsbRequest* request;
  int condition;
  struct UsbEndpointDescriptor* descriptor;
  struct UsbTransferDescriptor* next;
  struct UsbRequest* walk;
  completed.tail = 0;
  completed.head = 0;
  control = transfer->control;
  buffer = transfer->buffer;
  end = transfer->buffer_end;
  condition = control >> 28;
  request = take_request(transfer);
  if (!request)
    return;
  usb_free_transfer_descriptor(transfer);
  if (end && (control & USB_TD_DIRECTION_MASK)) {
    request->transferred = buffer ? (u_char*)buffer - (u_char*)request->buffer : request->length;
  }
  if (!request->result)
    request->result = condition;
  if (condition || (transfer->control & USB_TD_DELAY_MASK) != USB_TD_DELAY_MASK) {
    request->previous = completed.tail;
    if (request->previous)
      request->previous->next = request;
    else
      completed.head = request;
    request->next = 0;
    completed.tail = request;
  }
  if (condition) {
    descriptor = request->endpoint->descriptor;
    if ((u_int)descriptor->head & 1) {
      transfer = (struct UsbTransferDescriptor*)((u_int)descriptor->head & ~15);
      while (transfer && transfer != descriptor->tail) {
        next = transfer->next;
        usb_free_transfer_descriptor(transfer);
        request = take_request(transfer);
        if (request) {
          for (walk = completed.head; walk; walk = walk->next) {
            if (walk == request)
              break;
          }
          if (!walk) {
            request->result = USB_REQUEST_ABORTED;
            {
              request->previous = completed.tail;
              if (request->previous)
                request->previous->next = request;
              else
                completed.head = request;
              request->next = 0;
              completed.tail = request;
            }
          }
        }
        transfer = next;
      }
      descriptor->head = descriptor->tail;
    }
  }
  for (request = completed.head; request; request = walk) {
    request->busy = 0;
    walk = request->next;
    if (!request->endpoint->device)
      usb_free_request(request);
    else if (request->callback)
      request->callback(request);
  }
  usb_retry_transfer();
}

void usb_retry_iso(void) {
  struct UsbEndpoint* endpoint;
  if (usb_host->free_iso_head) {
    for (endpoint = usb_host->scheduled_head; endpoint; endpoint = endpoint->schedule_next) {
      if (endpoint->scheduled == 2) {
        usb_schedule_endpoint(endpoint);
        break;
      }
    }
  }
}

struct IsoRequestList {
  struct UsbRequest* head;
  struct UsbRequest* tail;
};

static __inline__ struct UsbRequest* take_iso_request(struct UsbIsoDescriptor* transfer) {
  struct UsbRequest** slot = &usb_host->iso_requests[transfer - usb_host->iso_descriptors];
  struct UsbRequest* request = *slot;
  *slot = 0;
  return request;
}

void usb_complete_iso(struct UsbIsoDescriptor* transfer) {
  struct IsoRequestList completed;
  u_int control;
  u_int packet;
  int packet_condition;
  int bytes;
  struct UsbRequest* request;
  int condition;
  struct UsbEndpointDescriptor* descriptor;
  struct UsbIsoDescriptor* next;
  struct UsbRequest* walk;
  completed.tail = 0;
  completed.head = 0;
  control = transfer->control;
  packet = transfer->packet_status[0];
  condition = control >> 28;
  packet_condition = packet >> 12;
  bytes = packet & 0x7ff;
  request = take_iso_request(transfer);
  if (!request)
    return;
#if USBD_DRIVER_VERSION >= 0x010000
  if (request->multi_iso.packet_count)
    bcopy(transfer->packet_status, request->multi_iso.packets, sizeof(request->multi_iso.packets));
#endif
  usb_free_iso_descriptor(transfer);
  request->transferred = 0;
#if USBD_DRIVER_VERSION >= 0x010000
  if (request->multi_iso.packet_count)
    request->result = condition;
  else {
#endif
    request->result = condition | (packet_condition << 4);
    if (!condition && (!packet_condition || packet_condition == 9)) {
      if ((request->endpoint->descriptor->control & 0x1800) == 0x1000)
        request->transferred = bytes;
      else
        request->transferred = request->length;
    }
#if USBD_DRIVER_VERSION >= 0x010000
  }
#endif
  {
    request->previous = completed.tail;
    if (request->previous)
      request->previous->next = request;
    else
      completed.head = request;
    request->next = 0;
    completed.tail = request;
  }
  {
    descriptor = request->endpoint->descriptor;
    if ((u_int)descriptor->head & 1) {
      transfer = (struct UsbIsoDescriptor*)((u_int)descriptor->head & ~15);
      while (transfer && transfer != descriptor->tail) {
        next = transfer->next;
        usb_free_iso_descriptor(transfer);
        request = take_iso_request(transfer);
        if (request) {
          for (walk = completed.head; walk; walk = walk->next) {
            if (walk == request)
              break;
          }
          if (!walk) {
            request->result = USB_REQUEST_ABORTED;
            {
              request->previous = completed.tail;
              if (request->previous)
                request->previous->next = request;
              else
                completed.head = request;
              request->next = 0;
              completed.tail = request;
            }
          }
        }
        transfer = next;
      }
      descriptor->head = descriptor->tail;
    }
  }
  for (request = completed.head; request; request = walk) {
    request->busy = 0;
    walk = request->next;
    if (!request->endpoint->device)
      usb_free_request(request);
    else if (request->callback)
      request->callback(request);
  }
  usb_retry_iso();
}

int usb_handle_interrupt(void) {
  volatile struct UsbHostRegisters* registers = usb_host->registers;
  struct UsbHostCommunication* communication;
  u_int pending;
  struct UsbTransferDescriptor* current;
  struct UsbTransferDescriptor* next;
  struct UsbTransferDescriptor* remaining;
  ++usb_host->interrupt_count;
  pending = registers->interrupt_status & registers->interrupt_enable;
  communication = usb_host->communication;
  if (pending & USB_INTERRUPT_OVERRUN) {
    registers->interrupt_status |= USB_INTERRUPT_OVERRUN;
    ++usb_host->interrupt_counts[0];
    pending &= ~USB_INTERRUPT_OVERRUN;
  }
  remaining = (struct UsbTransferDescriptor*)(communication->done_head & ~15);
  if (remaining) {
    communication->done_head = 0;
    registers->interrupt_status |= USB_INTERRUPT_DONE;
    current = 0;
    do {
      next = remaining;
      remaining = next->next;
      next->next = current;
      current = next;
    } while (remaining);
    for (; current; current = next) {
      next = current->next;
      if (current >= usb_host->transfer_descriptors && current < usb_host->transfer_end)
        usb_complete_transfer(current);
      else if ((struct UsbIsoDescriptor*)current >= usb_host->iso_descriptors &&
               (struct UsbIsoDescriptor*)current < usb_host->iso_end)
        usb_complete_iso((struct UsbIsoDescriptor*)current);
    }
    ++usb_host->interrupt_counts[1];
    pending &= ~USB_INTERRUPT_DONE;
  }
  if (pending & USB_INTERRUPT_FRAME) {
    registers->interrupt_status |= USB_INTERRUPT_FRAME;
    usb_tick_timers();
    ++usb_host->interrupt_counts[2];
    pending &= ~USB_INTERRUPT_FRAME;
  }
  if (pending & USB_INTERRUPT_RESUME) {
    registers->interrupt_status |= USB_INTERRUPT_RESUME;

    ++usb_host->interrupt_counts[3];
    pending &= ~USB_INTERRUPT_RESUME;
  }
  if (pending & USB_INTERRUPT_ERROR) {
    registers->interrupt_status |= USB_INTERRUPT_ERROR;

    ++usb_host->interrupt_counts[4];
    pending &= ~USB_INTERRUPT_ERROR;
  }
  if (pending & USB_INTERRUPT_OVERFLOW) {
    registers->interrupt_status |= USB_INTERRUPT_OVERFLOW;

    ++usb_host->interrupt_counts[5];
    pending &= ~USB_INTERRUPT_OVERFLOW;
  }
  if (pending & USB_INTERRUPT_ROOT) {
    registers->interrupt_status |= USB_INTERRUPT_ROOT;
    usb_check_root_ports();
    ++usb_host->interrupt_counts[6];
    pending &= ~USB_INTERRUPT_ROOT;
  }
  if (pending & USB_INTERRUPT_OWNERSHIP) {
    registers->interrupt_status |= USB_INTERRUPT_OWNERSHIP;

    ++usb_host->interrupt_counts[7];
    pending &= ~USB_INTERRUPT_OWNERSHIP;
  }
  if (pending & ~USB_INTERRUPT_MASTER)
    registers->interrupt_disable = pending;
  return 0;
}

static __inline__ void bus_delay(void) {
  // Read uncached boot ROM and discard the result for the bus delay.
  int count = 0;
  do {
    __asm__ volatile("lw $0,%0" : : "i"(IOP_BOOT_ROM_BASE));
  } while (count-- > 0);
}

void usb_cycle_interrupts(void) {
  volatile struct UsbHostRegisters* registers = usb_host->registers;
  registers->interrupt_disable = USB_INTERRUPT_MASTER;
  bus_delay();
  registers->interrupt_enable = USB_INTERRUPT_MASTER;
}

int usb_reset_controller(volatile struct UsbHostRegisters* registers) {
  int attempts;
  registers->interrupt_disable = -1;
#if USBD_DRIVER_VERSION >= 0x010100
  registers->control &= ~USB_OHCI_ENABLE_LISTS;
  DelayThread(USB_RESET_SETTLE_MICROSECONDS);
#endif
  registers->command_status = USB_COMMAND_RESET;
  registers->control = 0;
  attempts = 0;
  while (registers->command_status & USB_COMMAND_RESET) {
    if (++attempts >= USB_RESET_POLL_LIMIT)
      return -1;
    bus_delay();
  }
  return 0;
}

int usb_initialize_controller(void) {
  volatile struct UsbHostRegisters* registers = (void*)USB_OHCI_BASE;
  struct UsbHostCommunication* communication;
  struct UsbIsoDescriptor* iso;
  struct UsbTransferDescriptor* transfer;
  struct UsbEndpointDescriptor* descriptors;
  struct UsbEndpointDescriptor* descriptor;
  struct UsbHostState* host;
  struct UsbEndpoint* endpoint;
  struct UsbDevice* device;
  struct UsbRequest* request;
  struct UsbRequest** mappings;
  struct UsbRequest** iso_mappings;
  u_char* buffer;
  int endpoint_bytes;
  int size, i;
  int transfer_count;
  if (usb_reset_controller(registers) < 0)
    return -1;
  USB_DMA_PRIORITY |= USB_DMA_ENABLE;
  USB_CONTROLLER_ENABLE = 1;
  size = 284 + (int)sizeof(struct UsbIsoDescriptor) * usb_iso_limit +
         (int)sizeof(struct UsbTransferDescriptor) * usb_transfer_limit +
         (int)sizeof(struct UsbEndpointDescriptor) * usb_endpoint_limit +
         (endpoint_bytes = (int)sizeof(struct UsbEndpoint) * usb_endpoint_limit +
                           USB_SCHEDULE_ENDPOINTS * (int)sizeof(struct UsbEndpointDescriptor) +
                           (int)sizeof(struct UsbHostState)) +
         (int)sizeof(struct UsbDevice) * usb_device_limit +
         (int)sizeof(struct UsbRequest) * usb_request_limit +
         (int)sizeof(struct UsbRequest*) * usb_iso_limit +
         (int)sizeof(struct UsbRequest*) * usb_transfer_limit +
         usb_descriptor_limit * usb_device_limit;
  mappings = usb_alloc_memory(size);
  if (!mappings)
    return -1;
  if ((u_int)mappings & 255) {
    usb_free_memory(mappings);
    return -1;
  }
#if USBD_DRIVER_VERSION >= 0x010000
  usb_controller_memory = mappings;
#endif
  bzero(mappings, size);
  communication = (void*)mappings;
  transfer_count = usb_transfer_limit;
  iso = (void*)(((u_int)communication + 283) & ~31);
  transfer = (void*)(iso + usb_iso_limit);
  descriptor = (void*)(transfer + transfer_count);
  descriptors = descriptor + usb_endpoint_limit;
  host = (void*)(descriptors + USB_SCHEDULE_ENDPOINTS);
  device = (void*)(host + 1);
  endpoint = (void*)device;
  device = 0;
  device = (void*)(endpoint + usb_endpoint_limit);
  request = (void*)(device + usb_device_limit);
  usb_host = host;
  usb_configuration.host = host;
  mappings = (void*)(request + usb_request_limit);
  iso_mappings = mappings;
  mappings += usb_iso_limit;
  usb_configuration.allocated_bytes += size;
  ((struct UsbConfiguration**)iso)[-1] = &usb_configuration;
  host->registers = registers;
  host->communication = (void*)USB_UNCACHED_ADDRESS(communication);
  host->endpoint_descriptors = descriptors;
  host->iso_requests = iso_mappings;
  host->transfer_requests = mappings;
  host->endpoints = endpoint;
  buffer = (void*)(mappings + transfer_count);
  for (i = 0; i < usb_endpoint_limit; ++i) {
    endpoint->id = i;
    endpoint->descriptor = descriptor++;
    endpoint->previous = host->free_endpoint_tail;
    if (endpoint->previous)
      endpoint->previous->next = endpoint;
    else
      host->free_endpoint_head = endpoint;
    endpoint->next = 0;
    host->free_endpoint_tail = endpoint;
    ++endpoint;
  }
  usb_host->scheduled_tail = 0;
  usb_host->scheduled_head = 0;
  usb_host->devices = device;
  for (i = 0; i < usb_device_limit; ++i) {
    device->id = device->address = i;
    device->descriptors = buffer;
    buffer += usb_descriptor_limit;
    device->previous = usb_host->free_device_tail;
    if (device->previous)
      device->previous->next = device;
    else
      usb_host->free_device_head = device;
    device->next = 0;
    usb_host->free_device_tail = device;
    ++device;
  }
  usb_host->root = usb_attach_child(0, 0);
#if USBD_DRIVER_VERSION >= 0x010000
  usb_host->root->power_state = 2;
#endif
  usb_attach_child(usb_host->root, 1);
  usb_attach_child(usb_host->root, 2);
  usb_host->requests = request;
  for (i = 0; i < usb_request_limit; ++i) {
    request->reserved0 = i;
    request->previous = usb_host->free_request_tail;
    if (request->previous)
      request->previous->next = request;
    else
      usb_host->free_request_head = request;
    request->next = 0;
    usb_host->free_request_tail = request;
    ++request;
  }
  usb_host->free_transfer_head = transfer;
  usb_host->transfer_descriptors = transfer;
  usb_host->transfer_end = transfer + usb_transfer_limit;
  for (i = 0; i < usb_transfer_limit - 1; ++i) {
    transfer->next = transfer + 1;
    ++transfer;
  }
  usb_host->free_iso_head = iso;
  usb_host->iso_descriptors = iso;
  usb_host->iso_end = iso + usb_iso_limit;
  for (i = 0; i < usb_iso_limit - 1; ++i) {
    iso->next = iso + 1;
    ++iso;
  }
  descriptor = descriptors;
  descriptors = 0;
  descriptors = descriptor;
  for (i = 0; i < USB_PERIODIC_ENDPOINTS; ++descriptor, ++i) {
    int slot;
    descriptor->control = USB_ENDPOINT_SKIP;
    if (i > 0)
      descriptor->next = &descriptors[(i - 1) >> 1];
    else
      descriptor->next = 0;
    slot = i - 31;
    if (slot >= 0) {
      int reversed = ((slot & 1) << 4) + ((slot & 2) << 2) + (slot & 4) + ((slot & 8) >> 2) +
                     ((slot & 16) >> 4);
      communication->interrupt_table[reversed] = (u_int)descriptor;
    }
  }
  descriptor->control = USB_ENDPOINT_SKIP;
  registers->control_head = (u_int)descriptor++;
  descriptor->control = USB_ENDPOINT_SKIP;
  registers->bulk_head = (u_int)descriptor++;
  descriptor->control = USB_ENDPOINT_SKIP;
  descriptors->next = descriptor;
  registers->communication = (u_int)communication;
  registers->frame_interval = USB_FRAME_INTERVAL;
  registers->periodic_start = USB_PERIODIC_START;
  registers->interrupt_enable = USB_ENABLED_INTERRUPTS;
  registers->control |= USB_OPERATIONAL_CONTROL;
  return 0;
}

struct UsbHostState* usb_host = 0;

#if USBD_DRIVER_VERSION >= 0x010000
void usb_shutdown_controller(void) {
  struct UsbDevice* device;
  struct UsbReportDescriptor* report;
  struct UsbReportDescriptor* next;
#if USBD_DRIVER_VERSION >= 0x010100
  ((volatile struct UsbHostRegisters*)USB_OHCI_BASE)->control &= ~USB_OHCI_ENABLE_LISTS;
#endif
  for (device = usb_host->free_device_head; device; device = device->next) {
    for (report = device->reports; report; report = next) {
      next = report->next;
      usb_free_memory(report);
    }
  }
  usb_free_memory(usb_controller_memory);
}
#endif
