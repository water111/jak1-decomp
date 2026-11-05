#ifndef SCE_IOP_USBD_SCHEDULING_H
#define SCE_IOP_USBD_SCHEDULING_H
#include "sce/iop/usbd/internal.h"

static __inline__ void unschedule(struct UsbEndpoint* endpoint) {
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
}

static __inline__ void schedule(struct UsbEndpoint* endpoint, int state) {
  if (!endpoint->scheduled) {
    endpoint->schedule_previous = usb_host->scheduled_tail;
    if (endpoint->schedule_previous)
      endpoint->schedule_previous->schedule_next = endpoint;
    else
      usb_host->scheduled_head = endpoint;
    endpoint->schedule_next = 0;
    usb_host->scheduled_tail = endpoint;
    endpoint->scheduled = state;
  }
}

#endif
