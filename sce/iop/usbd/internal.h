#ifndef SCE_IOP_USBD_INTERNAL_H
#define SCE_IOP_USBD_INTERNAL_H
#include "sce/iop/usbd/usbd.h"

struct UsbTimer {
  int active;
  struct UsbTimer* next;
  struct UsbTimer* previous;
  void (*callback)(void*);
  void* argument;
  int delay;
};

struct UsbEndpoint {
  u_int id;
  u_int scheduled;
  struct UsbEndpoint* next;
  struct UsbEndpoint* previous;
  struct UsbEndpoint* schedule_next;
  struct UsbEndpoint* schedule_previous;
  struct UsbDevice* device;
  struct UsbRequest* request_head;
  struct UsbRequest* request_tail;
  struct UsbEndpointDescriptor* descriptor;
  int schedule_index;
  u_int next_frame;
  struct UsbTimer cleanup_timer;
  u_int alignment;
};

struct UsbRequest {
  u_int reserved0;
  u_int busy;
  struct UsbRequest* next;
  struct UsbRequest* previous;
  struct UsbEndpoint* endpoint;
  UsbDeviceRequest setup;
  void* buffer;
  int length;
  void (*callback)(struct UsbRequest*);
  int result;
  int transferred;
  u_int wait_frames;
  void* callback_argument;
  UsbCallback user_callback;
  u_int callback_gp;
};

struct UsbDevice {
  u_int id;
  struct UsbDevice* next;
  struct UsbDevice* previous;
  struct UsbEndpoint* endpoint_head;
  struct UsbEndpoint* endpoint_tail;
  UsbDriver* driver;
  u_char status, address, low_speed, reset_pending;
  struct UsbDevice* child_head;
  struct UsbDevice* child_tail;
  struct UsbDevice* parent;
  u_int port;
  void* private_data;
  struct UsbTimer reset_timer;
  struct UsbRequest enumeration_request;
  int address_retry_delay;
  u_char* descriptors;
  u_char* descriptor_end;
  int descriptor_phase;
};

struct UsbTransferDescriptor {
  u_int control;
  void* buffer;
  struct UsbTransferDescriptor* next;
  void* buffer_end;
};

struct UsbIsoDescriptor {
  u_int control;
  void* buffer_page;
  struct UsbIsoDescriptor* next;
  void* buffer_end;
  u_short packet_status[8];
};

#define USB_OHCI_INTERRUPT_SOF 0x4

struct UsbHostRegisters {
  u_int revision;
  u_int control;
  u_int command_status;
  u_int interrupt_status;
  u_int interrupt_enable;
  u_int interrupt_disable;
  u_int communication;
  u_int periodic_current;
  u_int control_head;
  u_int control_current;
  u_int bulk_head;
  u_int bulk_current;
  u_int done_head;
  u_int frame_interval;
  u_int frame_remaining;
  u_int frame_number;
  u_int periodic_start;
  u_int low_speed_threshold;
  u_int root_descriptor_a;
  u_int root_descriptor_b;
  u_int root_status;
  u_int port_status[2];
};

struct UsbEndpointDescriptor {
  u_int control;
  void* tail;
  void* head;
  struct UsbEndpointDescriptor* next;
};

struct UsbHostCommunication {
  u_int interrupt_table[32];
  u_short frame_number;
  u_short reserved;
  u_int done_head;
};

struct UsbHostState {
  volatile struct UsbHostRegisters* registers;
  struct UsbHostCommunication* communication;
  struct UsbEndpointDescriptor* endpoint_descriptors;
  struct UsbTransferDescriptor* free_transfer_head;
  struct UsbTransferDescriptor* transfer_descriptors;
  struct UsbTransferDescriptor* transfer_end;
  struct UsbIsoDescriptor* free_iso_head;
  struct UsbIsoDescriptor* iso_descriptors;
  struct UsbIsoDescriptor* iso_end;
  struct UsbRequest** transfer_requests;
  struct UsbRequest** iso_requests;
  struct UsbRequest* requests;
  struct UsbRequest* free_request_head;
  struct UsbRequest* free_request_tail;
  struct UsbDevice* devices;
  struct UsbDevice* free_device_head;
  struct UsbDevice* free_device_tail;
  struct UsbEndpoint* endpoints;
  struct UsbEndpoint* free_endpoint_head;
  struct UsbEndpoint* free_endpoint_tail;
  struct UsbEndpoint* scheduled_head;
  struct UsbEndpoint* scheduled_tail;
  int reset_active;
  u_int interrupt_count;
  u_int interrupt_counts[8];
  struct UsbTimer* timer_head;
  struct UsbTimer* timer_tail;
  struct UsbDevice* root;
};

struct UsbCallbackQueue {
  struct UsbRequest* head;
  struct UsbRequest* tail;
};

struct UsbRuntime {
  int api_semaphore;
  int event_thread;
  int interrupt_event;
  int callback_thread;
  int callback_event;
};
extern struct UsbRuntime usb_runtime;
#define usb_api_semaphore usb_runtime.api_semaphore
#define usb_event_thread usb_runtime.event_thread
#define usb_interrupt_event usb_runtime.interrupt_event
#define usb_callback_thread usb_runtime.callback_thread
#define usb_callback_event usb_runtime.callback_event
extern struct UsbCallbackQueue usb_callback_queue;

struct UsbDriverList {
  UsbDriver* head;
  UsbDriver* tail;
};
extern struct UsbDriverList usb_drivers;
#define usb_driver_head usb_drivers.head
#define usb_driver_tail usb_drivers.tail

struct UsbAutoloader {
  UsbDriver* driver;
};
extern struct UsbAutoloader usb_autoloader_state;
#define usb_autoloader usb_autoloader_state.driver
extern struct UsbHostState* usb_host;

struct UsbConfiguration {
  int devices;
  int endpoints;
  int transfers;
  int iso_transfers;
  int requests;
  int descriptor_bytes;
  int hubs;
  int ports;
  int allocated_bytes;
  int thread_priorities[2];
  struct UsbHostState* host;
};
extern struct UsbConfiguration usb_configuration;
#define usb_device_limit usb_configuration.devices
#define usb_endpoint_limit usb_configuration.endpoints
#define usb_transfer_limit usb_configuration.transfers
#define usb_iso_limit usb_configuration.iso_transfers
#define usb_request_limit usb_configuration.requests
#define usb_descriptor_limit usb_configuration.descriptor_bytes
#define usb_thread_priorities usb_configuration.thread_priorities
struct UsbDevice* usb_get_device(int device);
struct UsbEndpoint* usb_get_endpoint(int endpoint);
struct UsbDevice* usb_get_root(void);
struct UsbDevice* usb_attach_child(struct UsbDevice* parent, u_int port);
int usb_release_device(struct UsbDevice* device);
struct UsbRequest* usb_alloc_request(void);
int usb_free_request(struct UsbRequest* request);
struct UsbEndpoint* usb_alloc_endpoint(struct UsbDevice* device, u_int alignment);
int usb_free_endpoint(struct UsbDevice* device, struct UsbEndpoint* endpoint);
struct UsbTransferDescriptor* usb_alloc_transfer_descriptor(void);
void usb_free_transfer_descriptor(struct UsbTransferDescriptor* descriptor);
struct UsbIsoDescriptor* usb_alloc_iso_descriptor(void);
void usb_free_iso_descriptor(struct UsbIsoDescriptor* descriptor);
int usb_add_timer(struct UsbTimer* timer, void (*callback)(void*), void* argument, int delay);
int usb_cancel_timer(struct UsbTimer* timer);
void usb_tick_timers(void);
void usb_link_endpoint(int index, struct UsbEndpointDescriptor* descriptor);
void usb_unlink_endpoint(int index, struct UsbEndpointDescriptor* descriptor);
struct UsbEndpoint* usb_open_endpoint(struct UsbDevice* device,
                                      UsbEndpointInfo* info,
                                      u_int alignment);
void usb_cleanup_endpoint(struct UsbEndpoint* endpoint);
int usb_close_endpoint(struct UsbDevice* device, struct UsbEndpoint* endpoint);
int usb_schedule_control(struct UsbEndpoint* endpoint);
int usb_schedule_iso(struct UsbEndpoint* endpoint);
int usb_schedule_data(struct UsbEndpoint* endpoint);
int usb_schedule_endpoint(struct UsbEndpoint* endpoint);
int usb_start_device_reset(struct UsbDevice* device);
void usb_reset_hub_port(struct UsbDevice* device);
int usb_reset_next_device(struct UsbDevice* parent);
int usb_finish_device_reset(struct UsbDevice* device);
void usb_device_reset_callback(void* argument);
int usb_check_root_ports(void);
int usb_disconnect_device(struct UsbDevice* device);
int usb_begin_enumeration(struct UsbDevice* device);
void usb_retry_transfer(void);
void usb_complete_transfer(struct UsbTransferDescriptor* transfer);
void usb_retry_iso(void);
void usb_complete_iso(struct UsbIsoDescriptor* transfer);
int usb_handle_interrupt(void);
void* usb_alloc_memory(int size);
int usb_free_memory(void* memory);
void usb_cycle_interrupts(void);
int usb_reset_controller(volatile struct UsbHostRegisters* registers);
int usb_register_driver(UsbDriver* driver, u_int gp);
int usb_register_autoloader(UsbDriver* driver, u_int gp);
int usb_unregister_driver(UsbDriver* driver);
int usb_unregister_autoloader(void);
void* usb_scan_descriptor(int device, void* previous, u_char type);
struct UsbEndpoint* usb_open_pipe(struct UsbDevice* device, UsbEndpointInfo* info, u_int alignment);
int usb_close_pipe(struct UsbEndpoint* endpoint);
void usb_queue_callback(struct UsbRequest* request);
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

struct UsbInterruptContext {
  u_int reserved[2];
  int event;
};

int usb_interrupt_callback(void* argument);
void usb_event_worker(void* argument);
void usb_callback_worker(void* argument);

struct UsbOption {
  const char* name;
  int* first;
  int* second;
};

void usb_parse_option(struct UsbOption* option, const char* text);
void usb_attach_driver(struct UsbDevice* parent, UsbDriver* driver);
void usb_detach_driver(struct UsbDevice* device, UsbDriver* driver);
int usb_call_driver(int (*callback)(int), int device, u_int gp);
void usb_bind_device(struct UsbDevice* device);
int usb_get_device_location(struct UsbDevice* device, u_char* path);
extern struct UsbOption usb_options[];
extern u_int usbd_stub[];
#define usb_exports usbd_stub
int usb_initialize(void);
int usb_lock(void);
int usb_unlock(void);
#endif
