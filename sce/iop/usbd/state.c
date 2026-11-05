#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/usbd/internal.h"

char usb_build_id[16] = "PsIIusbd    2200";
struct ModuleInfo Module = {"USB_driver", 0x0204};
struct UsbRuntime usb_runtime = {-1, -1, -1, -1, -1};
struct UsbCallbackQueue usb_callback_queue = {0, 0};
struct UsbOption usb_options[] = {
    {"dev=", &usb_configuration.devices, 0},
    {"ed=", &usb_configuration.endpoints, 0},
    {"gtd=", &usb_configuration.transfers, 0},
    {"itd=", &usb_configuration.iso_transfers, 0},
    {"ioreq=", &usb_configuration.requests, 0},
    {"conf=", &usb_configuration.descriptor_bytes, 0},
    {"hub=", &usb_configuration.hubs, 0},
    {"port=", &usb_configuration.ports, 0},
    {"thpri=", &usb_configuration.thread_priorities[0], &usb_configuration.thread_priorities[1]},
    {0, 0, 0}};
struct UsbConfiguration usb_configuration
    __attribute__((aligned(16))) = {32, 64, 128, 128, 256, 512, 8, 8, 0, {30, 36}, 0};
struct UsbDriverList usb_drivers = {0, 0};
struct UsbAutoloader usb_autoloader_state = {0};
