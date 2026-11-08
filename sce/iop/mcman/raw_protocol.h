#ifndef SCE_IOP_MCMAN_RAW_PROTOCOL_H
#define SCE_IOP_MCMAN_RAW_PROTOCOL_H
#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/types.h"
#define MCMAN_RAW_PACKET_SIZE 144
#define MCMAN_RAW_CARD 0x81
#define MCMAN_RAW_TERMINATOR 0x55
extern struct Sio2Transfer mcman_raw_packet;
extern u_char mcman_raw_write_dma[1584], mcman_raw_read_dma[1584];
void mcman_raw_init(void);
void mcman_raw_shutdown(void);
int mcman_raw_authenticate(int port, int slot);
void mcman_raw_queue(int port,
                     int slot,
                     const void* data,
                     int length,
                     int index,
                     unsigned int wire_length);
void mcman_raw_end_queue(int count);
int mcman_raw_execute(void);
void mcman_raw_debug(int index);
int mcman_raw_reset(int port, int slot);
int mcman_raw_queue_reset(int port, int slot, int index);
int mcman_raw_probe(int port, int slot);
int mcman_raw_queue_probe(int port, int slot, int index);
int mcman_raw_status(int port, int slot, int value);
int mcman_raw_queue_status(int port, int slot, int value, int index);
int mcman_raw_erase(int port, int slot, int page);
int mcman_raw_queue_erase_address(int port, int slot, int page, int index);
int mcman_raw_read(int port, int slot, int page, int length, void* destination);
int mcman_raw_queue_read_address(int port, int slot, int page, int index);
int mcman_raw_queue_read(int port, int slot, int length, int index);
int mcman_raw_write(int port, int slot, int page, int length, const void* source);
int mcman_raw_queue_write_address(int port, int slot, int page, int index);
int mcman_raw_queue_write(int port, int slot, const void* data, int length, int index);
int mcman_raw_command24(int port, int slot, int value);
int mcman_raw_queue_command24(int port, int slot, int value, int index);
int mcman_raw_card_spec(int port, int slot);
int mcman_raw_queue_card_spec(int port, int slot, int index);
int mcman_raw_set_terminator(int port, int slot, int value);
int mcman_raw_queue_set_terminator(int port, int slot, int value, int index);
int mcman_raw_command25(int port, int slot);
int mcman_raw_queue_command25(int port, int slot, int index);
int mcman_raw_get_terminator(int port, int slot);
int mcman_raw_queue_get_terminator(int port, int slot, int index);
int mcman_raw_queue_end(int port, int slot, int index);
int mcman_raw_queue_poll(int port, int slot, int index);
int mcman_raw_queue_erase(int port, int slot, int index);
int mcman_raw_commandbf(int port, int slot, int value);
int mcman_raw_queue_commandbf(int port, int slot, int value, int index);
u_char mcman_raw_checksum(const u_char* data, int length);
int mcman_raw_security_command(int port, int slot, struct Sio2Transfer* packet);
int mcman_raw_device_id(int port, int slot);
#endif
