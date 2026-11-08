#ifndef SCE_IOP_USBD_H
#define SCE_IOP_USBD_H

#include "sce/iop/types.h"

#define USB_DESCRIPTOR_DEVICE 1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_STRING 3
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_ISO_MAX_PACKETS 8
#define USB_ISO_PACKET_LENGTH_MASK 0x7ff
#define USB_CLASS_VENDOR 0xff

typedef struct {
  u_char length, type;
  u_short usb_version;
  u_char device_class, subclass, protocol, max_packet_size;
  u_short vendor, product, device_version;
  u_char manufacturer, product_string, serial_string, configuration_count;
} UsbDeviceDescriptor;

typedef struct {
  u_char length, type;
  u_char total_length_low, total_length_high;
  u_char interface_count, value, description, attributes, max_power;
} UsbConfigDescriptor;

typedef struct {
  u_char length, type, number, alternate, endpoint_count, interface_class, subclass, protocol;
  u_char description;
} UsbInterfaceDescriptor;

typedef struct {
  u_char request_type, request;
  u_short value, index, length;
} UsbDeviceRequest;

typedef struct {
  u_char length, type, address, attributes;
  u_char max_packet_low, max_packet_high, interval;
} UsbEndpointInfo;

typedef struct UsbDriver {
  struct UsbDriver *next, *previous;
  char* name;
  int (*probe)(int device);
  int (*attach)(int device);
  int (*detach)(int device);
  void* reserved[5];
  u_int gp;
} UsbDriver;

typedef struct {
  void* buffer;
  u_int wait_frames;
  int packet_count;
  u_short packets[USB_ISO_MAX_PACKETS];
} UsbMultiIsoRequest;

typedef void (*UsbCallback)(int result, int bytes, void* argument);
typedef void (*UsbMultiIsoCallback)(int result, UsbMultiIsoRequest* request, void* argument);
int sceUsbdRegisterLdd(UsbDriver* driver);
int sceUsbdUnregisterLdd(UsbDriver* driver);
int sceUsbdRegisterAutoloader(UsbDriver* driver);
int sceUsbdUnregisterAutoloader(void);
void* sceUsbdScanStaticDescriptor(int device, void* previous, u_char type);
int sceUsbdGetDeviceLocation(int device, u_char* path);
int sceUsbdSetPrivateData(int device, void* data);
void* sceUsbdGetPrivateData(int device);
int sceUsbdOpenPipe(int device, void* endpoint);
int sceUsbdOpenPipeAligned(int device, void* endpoint);
int sceUsbdClosePipe(int pipe);
int sceUsbdTransferPipe(int pipe,
                        void* data,
                        int length,
                        void* request,
                        UsbCallback callback,
                        void* argument);
int sceUsbdChangeThreadPriority(int event_priority, int callback_priority);

int sceUsbdMultiIsochronousTransfer(int pipe,
                                    UsbMultiIsoRequest* request,
                                    UsbMultiIsoCallback callback,
                                    void* argument);
int sceUsbdGetReportDescriptor(int device,
                               int configuration,
                               int interface,
                               void** data,
                               int* size);

#endif
