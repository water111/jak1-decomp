#include "sce/iop/mcman/raw_protocol.h"
#include "sce/iop/secrman/secrman.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"

void mcman_raw_init(void) {
  int port;
  memset(&mcman_raw_packet, 0, sizeof(mcman_raw_packet));
  for (port = 2; port < 4; port++) {
    struct PortControl {
      u_char a, b, c, d;
    }* control = (void*)&mcman_raw_packet.send1[port];
    union Sio2PortControl2* timing = &mcman_raw_packet.send2[port];
    control->a = 5;
    control->b = 4;
    control->c = 2;
    control->d = 0xff;
    timing->timing.baud = 5;
    timing->timing.size = 0xffff;
    timing->value &= ~0x03000000;
  }
  mcman_raw_packet.input_dma.address = mcman_raw_write_dma;
  mcman_raw_packet.output_dma.address = mcman_raw_read_dma;
  mcman_raw_packet.output_dma.size = 36;
  mcman_raw_packet.input_dma.size = 36;
  SecrSetMcCommandHandler(mcman_raw_security_command);
  SecrSetMcDevIDHandler(mcman_raw_device_id);
}

void mcman_raw_shutdown(void) {
  SecrSetMcCommandHandler(0);
  SecrSetMcDevIDHandler(0);
}

int mcman_raw_authenticate(int port, int slot) {
  int device;
  mcman_raw_reset(port, slot);
  device = mcman_raw_device_id(port, slot);
  return SecrAuthCard(port + 2, slot, device);
}

void mcman_raw_queue(int port,
                     int slot,
                     const void* data,
                     int length,
                     int index,
                     unsigned int wire_length) {
  int* count;
  unsigned int* reg;
  count = &mcman_raw_packet.input_dma.count;
  reg = mcman_raw_packet.send3;
  port = (((port & 1) + 2) & 3) | 0x70;
  port |= ((wire_length & 0x1ff) << 18);
  port |= ((wire_length & 0x1ff) << 8);
  *count = index;
  reg[index] = port;
  memcpy(mcman_raw_write_dma + index * MCMAN_RAW_PACKET_SIZE, data, length);
}

void mcman_raw_end_queue(int count) {
  mcman_raw_packet.input_dma.count = count;
  mcman_raw_packet.output_dma.count = count;
  mcman_raw_packet.send3[count] = 0;
}

int mcman_raw_execute(void) {
  int retry;
  for (retry = 0; retry < 5; retry++) {
    sio2_mc_init_legacy();
    sio2_transfer_legacy(&mcman_raw_packet);
    sio2_transfer_reset_legacy();
    if ((mcman_raw_packet.recv1 & 0xf000) == 0x1000)
      return 1;
  }
  return -1;
}

void mcman_raw_debug(int index) {
  int row, column;
  u_char* data;
  printf("------------------------ debug view ----------------------------\n");
  printf("cmd = %x %x %x \n", mcman_raw_write_dma[index * MCMAN_RAW_PACKET_SIZE],
         (mcman_raw_write_dma + 1)[index * MCMAN_RAW_PACKET_SIZE],
         (mcman_raw_write_dma + 2)[index * MCMAN_RAW_PACKET_SIZE]);
  data = mcman_raw_read_dma + index * MCMAN_RAW_PACKET_SIZE;
  for (row = 0; row < 9; row++) {
    printf("[%02x] ", row * 16);
    for (column = 0; column < 16; column++)
      printf("%02x ", *data++);
    data -= 16;
    for (column = 0; column < 16; column++) {
      u_char c = *data > 30 ? *data : ' ';
      data++;
      printf("%c", c);
    }
    printf("\n");
  }
  printf("----------------------------------------------------------------\n");
}

int mcman_raw_reset(int port, int slot) {
  mcman_raw_queue_reset(port, slot, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_reset(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0xf3;
  mcman_raw_queue(port, slot, command, 2, index, 5);
  return 1;
}

int mcman_raw_probe(int port, int slot) {
  mcman_raw_queue_probe(port, slot, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_probe(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x11;
  mcman_raw_queue(port, slot, command, 2, index, 4);
  return 1;
}

int mcman_raw_status(int port, int slot, int value) {
  mcman_raw_queue_status(port, slot, value, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_status(int port, int slot, int value, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x12;
  command[2] = value;
  mcman_raw_queue(port, slot, command, 3, index, 4);
  return 1;
}

int mcman_raw_erase(int port, int slot, int page) {
  int retry, result;
  mcman_raw_queue_erase_address(port, slot, page, 0);
  mcman_raw_queue_erase(port, slot, 1);
  mcman_raw_end_queue(2);
  result = mcman_raw_execute();
  if (result == -1)
    return -1;
  {
    for (retry = 0; retry < 16; retry++) {
      mcman_raw_queue_poll(port, slot, 0);
      mcman_raw_end_queue(1);
      result = mcman_raw_execute();
      if (result == 1 && mcman_raw_read_dma[3] == MCMAN_RAW_TERMINATOR)
        return 1;
      DelayThread(2000);
    }
  }
  return -1;
}

int mcman_raw_queue_erase_address(int port, int slot, int page, int index) {
  u_char command[7];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x21;
  command[2] = page;
  command[3] = page >> 8;
  command[4] = page >> 16;
  command[5] = page >> 24;
  command[6] = mcman_raw_checksum(command + 2, 4);
  mcman_raw_queue(port, slot, command, 7, index, 9);
  return 1;
}

int mcman_raw_read(int port, int slot, int page, int length, void* destination) {
  int index = 1, count;
  u_char* output = destination;
  mcman_raw_queue_read_address(port, slot, page, 0);
  while (length > 0) {
    if (length >= 128)
      mcman_raw_queue_read(port, slot, 128, index++);
    else
      mcman_raw_queue_read(port, slot, length, index++);
    length -= 128;
  }
  mcman_raw_queue_end(port, slot, index++);
  count = index;
  mcman_raw_end_queue(count);
  if (mcman_raw_execute() != 1)
    return -1;
  {
    // BUG: The final command response is also copied as a full data block.
    {
      int index;
      for (index = 1; index < count; index++) {
        memcpy(output, mcman_raw_read_dma + index * MCMAN_RAW_PACKET_SIZE + 4, 128);
        output += 128;
      }
    }
    return 1;
  }
  return -1;
}

int mcman_raw_queue_read_address(int port, int slot, int page, int index) {
  u_char command[7];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x23;
  command[2] = page;
  command[3] = page >> 8;
  command[4] = page >> 16;
  command[5] = page >> 24;
  command[6] = mcman_raw_checksum(command + 2, 4);
  mcman_raw_queue(port, slot, command, 7, index, 9);
  return 1;
}

int mcman_raw_queue_read(int port, int slot, int length, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x43;
  command[2] = length;
  mcman_raw_queue(port, slot, command, 3, index, length + 6);
  return 1;
}

int mcman_raw_write(int port, int slot, int page, int length, const void* source) {
  int index = 1;
  const u_char* data = source;
  mcman_raw_queue_write_address(port, slot, page, 0);
  while (length > 0) {
    if (length > 127)
      mcman_raw_queue_write(port, slot, data, 128, index++);
    else
      mcman_raw_queue_write(port, slot, data, length, index++);
    length -= 128;
    data += 128;
  }
  mcman_raw_queue_end(port, slot, index++);
  mcman_raw_end_queue(index);
  return mcman_raw_execute();
}

int mcman_raw_queue_write_address(int port, int slot, int page, int index) {
  u_char command[7];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x22;
  command[2] = page;
  command[3] = page >> 8;
  command[4] = page >> 16;
  command[5] = page >> 24;
  command[6] = mcman_raw_checksum(command + 2, 4);
  mcman_raw_queue(port, slot, command, 7, index, 9);
  return 1;
}

int mcman_raw_queue_write(int port, int slot, const void* data, int length, int index) {
  u_char command[136];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x42;
  command[2] = length;
  memcpy(command + 3, data, length);
  // BUG: The checksum byte following the payload is left uninitialized.
  mcman_raw_queue(port, slot, command, length + 4, index, length + 6);
  return 1;
}

int mcman_raw_command24(int port, int slot, int value) {
  mcman_raw_queue_command24(port, slot, value, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_command24(int port, int slot, int value, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x24;
  command[2] = value;
  mcman_raw_queue(port, slot, command, 3, index, 134);
  return 1;
}

int mcman_raw_card_spec(int port, int slot) {
  mcman_raw_queue_card_spec(port, slot, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_card_spec(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x26;
  mcman_raw_queue(port, slot, command, 2, index, 13);
  return 1;
}

int mcman_raw_set_terminator(int port, int slot, int value) {
  mcman_raw_queue_set_terminator(port, slot, value, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_set_terminator(int port, int slot, int value, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x27;
  command[2] = value;
  mcman_raw_queue(port, slot, command, 3, index, 5);
  return 1;
}

int mcman_raw_command25(int port, int slot) {
  mcman_raw_queue_command25(port, slot, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_command25(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x25;
  mcman_raw_queue(port, slot, command, 2, index, 4);
  return 1;
}

int mcman_raw_get_terminator(int port, int slot) {
  int result;
  mcman_raw_queue_get_terminator(port, slot, 0);
  mcman_raw_end_queue(1);
  result = mcman_raw_execute();
  if (result == 1)
    return mcman_raw_read_dma[4];
  return result;
}

int mcman_raw_queue_get_terminator(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x28;
  mcman_raw_queue(port, slot, command, 2, index, 5);
  return 1;
}

int mcman_raw_queue_end(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x81;
  mcman_raw_queue(port, slot, command, 2, index, 4);
  return 1;
}

int mcman_raw_queue_poll(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x12;
  command[2] = 0;
  mcman_raw_queue(port, slot, command, 3, index, 4);
  return 1;
}

int mcman_raw_queue_erase(int port, int slot, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0x82;
  mcman_raw_queue(port, slot, command, 2, index, 4);
  return 1;
}

int mcman_raw_commandbf(int port, int slot, int value) {
  mcman_raw_queue_commandbf(port, slot, value, 0);
  mcman_raw_end_queue(1);
  return mcman_raw_execute();
}

int mcman_raw_queue_commandbf(int port, int slot, int value, int index) {
  u_char command[3];
  command[0] = MCMAN_RAW_CARD;
  command[1] = 0xbf;
  command[2] = value;
  mcman_raw_queue(port, slot, command, 3, index, 5);
  return 1;
}

u_char mcman_raw_checksum(const u_char* data, int length) {
  u_char checksum = 0;
  while (length--)
    checksum ^= *data++;
  return checksum;
}

int mcman_raw_security_command(int port, int slot, struct Sio2Transfer* packet) {
  int result;
  sio2_mc_init_legacy();
  result = sio2_transfer_legacy(packet);
  sio2_transfer_reset_legacy();
  return result;
}

int mcman_raw_device_id(int port, int slot) {
  return (port & 1) * 8 + slot;
}
