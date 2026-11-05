#ifndef SCE_IOP_PADMAN_TRANSFER_H
#define SCE_IOP_PADMAN_TRANSFER_H
#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/types.h"
#define PAD_PORT_COUNT 2
#define PAD_SLOT_COUNT 4
#define PAD_PACKET_SIZE 32
#define PAD_TRANSFER_BUFFER_SIZE 256
int pad_transfer_packet_error(int packet, struct Sio2Transfer* transfer);
void pad_transfer_init(void);
int pad_transfer_set_active(int port, int slot, int active);
int pad_transfer_get_active(int port, int slot);
int pad_transfer_set_connected(int port, int slot, int connected);
int pad_transfer_get_connected(int port, int slot);
int pad_transfer_append(int packet, int port, int slot);
int pad_transfer_receive(int packet, int port, int slot);
int pad_transfer_slot(int slot);
int pad_transfer_select_slot(int slot);
int pad_transfer_all(void);
int pad_transfer_set_control1(u_int port, int slot, u_int value);
int pad_transfer_set_control2(u_int port, int slot, u_int value);
int pad_transfer_set_control3(u_int port, int slot, u_int value);
int pad_transfer_get_error(u_int port, int slot);
int pad_transfer_set_input_size(u_int port, int slot, int size);
int pad_transfer_set_input(u_int port, int slot, int size, const u_char* buffer);
int pad_transfer_set_output_size(u_int port, int slot, int size);
int pad_transfer_get_output(u_int port, int slot, int size, u_char* buffer);
int pad_transfer_setup_probe(int port, int slot, int connected);
int pad_transfer_probe_port(int port, int slot);
int pad_transfer_probe(int port, int slot);
#endif
