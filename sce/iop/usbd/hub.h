#ifndef SCE_IOP_USBD_HUB_H
#define SCE_IOP_USBD_HUB_H
#include "sce/iop/usbd/internal.h"

struct UsbHub {
  struct UsbHub* next;
  u_int reserved;
  u_int id;
  struct UsbEndpoint* control;
  struct UsbEndpoint* interrupt;
  struct UsbRequest control_request;
  struct UsbRequest interrupt_request;
  int max_power;
  int self_powered;
  u_char descriptor[40];
  int port_count;
  int current_port;
  int reset_port;
  u_short status;
  u_short changes;
  u_int port_status;
  u_char bitmap[32];
};
extern struct UsbHub* usb_free_hubs;
extern u_int usb_next_hub_id;
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
void usb_scan_hub_changes(struct UsbRequest* request);
void usb_hub_reset_complete(struct UsbRequest* request);
void usb_hub_device_reset(void* argument);
#endif
