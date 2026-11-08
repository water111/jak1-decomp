#include "sce/iop/mcman/filesystem.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/transport.h"
#include "sce/iop/secrman/secrman.h"
#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"

extern struct Sio2Transfer mcman_sio2_packet;
extern u_char mcman_write_dma[1584], mcman_read_dma[1584];

#if MCMAN_VERSION >= 0x0220
extern int mcman_probe_status;

int mcman_get_probe_status(void) {
  return mcman_probe_status;
}
#endif

void mcman_begin_transfer(int port, int slot) {
  int slots[8];
  slots[0] = -1;
  slots[1] = -1;
  slots[2] = -1;
  slots[3] = -1;
  slots[(port & 1) + 2] = slot;
  sio2_mc_init_legacy();
  sio2_change_slot(slots);
}

void mcman_end_transfer(void) {
  sio2_transfer_reset_legacy();
}

void mcman_initPS2com(void) {
  int port;
  mcman_wmemset(&mcman_sio2_packet, sizeof(mcman_sio2_packet), 0);
  for (port = 2; port < 4; port++) {
    struct McSend1 {
      u_char a, b, c, d;
    }* send1 = (void*)&mcman_sio2_packet.send1[port];

    union McSend2 {
      u_int word;

      struct {
        u_short size;
        u_char baud, flags;
      } fields;
    }* send2 = (void*)&mcman_sio2_packet.send2[port];

    send1->a = 5;
    send1->b = 4;
    send1->c = 2;
    send1->d = 0xff;
    send2->fields.baud = 5;
    send2->fields.size = 0xffff;
    send2->word &= ~0x03000000;
  }
  mcman_sio2_packet.input_dma.address = mcman_write_dma;
  mcman_sio2_packet.output_dma.address = mcman_read_dma;
  mcman_sio2_packet.output_dma.size = 0x24;
  mcman_sio2_packet.input_dma.size = 0x24;
  SecrSetMcCommandHandler(mcman_security_command);
  SecrSetMcDevIDHandler(mcman_getcnum);
}

#if MCMAN_VERSION >= 0x0220
void mcman_shutdownPS2com(void) {
  SecrSetMcCommandHandler(0);
  SecrSetMcDevIDHandler(0);
}
#endif

extern struct Sio2Transfer mcman_sio2_packet;
extern u_char mcman_write_dma[1584], mcman_read_dma[1584];
u_char mcman_command_table[][2] = {{0x11, 4},   {0x12, 4},   {0x21, 9},  {0x22, 9}, {0x23, 9},
                                   {0x24, 134}, {0x25, 4},   {0x26, 13}, {0x27, 5}, {0x28, 5},
                                   {0x42, 134}, {0x43, 134}, {0x81, 4},  {0x82, 4}, {0x42, 6},
                                   {0x43, 6},   {0xbf, 5},   {0xf3, 5}};

#if MCMAN_VERSION >= 0x0220
int mcman_probe_status = 1;
#endif

struct McSio2Command {
  u_int port : 2;
  u_int control : 6;
  u_int input_length : 9;
  u_int reserved0 : 1;
  u_int output_length : 9;
  u_int reserved1 : 5;
};

void mcman_build_command(int port, int slot, int command, void* argument) {
  int index, size;
  u_int* reg;
  u_char* tx;
  if (command == -1) {
    mcman_sio2_packet.input_dma.count = 0;
    return;
  }
  if (mcman_sio2_packet.input_dma.count > 10)
    return;
  if (command == -2) {
    mcman_sio2_packet.send3[mcman_sio2_packet.input_dma.count] = 0;
    mcman_sio2_packet.output_dma.count = mcman_sio2_packet.input_dma.count;
    return;
  }
  index = mcman_sio2_packet.input_dma.count++;
  reg = &mcman_sio2_packet.send3[index];
  *reg = (((port & 1) + 2) & 3) | 0x70;
  *reg = *reg | (mcman_command_table[command][1] << 18) | (mcman_command_table[command][1] << 8);
  tx = &mcman_write_dma[index * 144];
  tx[0] = 0x81;
  tx[1] = mcman_command_table[command][0];
  switch (command) {
    case 2:
    case 3:
    case 4:
      tx[2] = (u_int)argument;
      tx[3] = (u_int)argument >> 8;
      tx[4] = (u_int)argument >> 16;
      tx[5] = (u_int)argument >> 24;
      tx[6] = mcman_calcEDC(tx + 2, 4);
      break;
    case 11:
      tx[2] = 128;
      break;
    case 10:
      tx[2] = 128;
      memcpy(tx + 3, argument, 128);
      tx[131] = mcman_calcEDC(argument, 128);
      break;
    case 14:
      tx[2] = mcman_spare_size(port, slot);
      memcpy(tx + 3, argument, tx[2]);
      tx[3 + tx[2]] = mcman_calcEDC(argument, tx[2]);
      size = mcman_command_table[command][1] + tx[2];
      ((struct McSio2Command*)reg)->output_length = size;
      ((struct McSio2Command*)reg)->input_length = size;
      break;
    case 15:
      tx[2] = mcman_spare_size(port, slot);
      size = mcman_command_table[command][1] + tx[2];
      ((struct McSio2Command*)reg)->output_length = size;
      ((struct McSio2Command*)reg)->input_length = size;
      break;
    case 8:
      tx[2] = 0x55;
      break;
    case 1:
      tx[2] = 0;
      break;
    case 5:
    case 16:
      tx[2] = (u_int)argument;
      break;
  }
}

int mcman_cardchanged(int port, int slot) {
  int retry;
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 0, 0);
  mcman_build_command(port, slot, -2, 0);
  for (retry = 0; retry < 5; retry++) {
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[3]
#if MCMAN_VERSION >= 0x0220
                                                            == 0x55)
#else
                                                            != 0x66)
#endif
      return 0;
  }
  return -1;
}

int mcman_eraseblock(int port, int slot, int block, void** pages, void* ecc) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int retry, page;
  u_char* checksum;
  void** page_data;
  void** current_page;
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 2, (void*)(block * device->pages_per_block));
  retry = 0;
  mcman_build_command(port, slot, 13, 0);
  mcman_build_command(port, slot, -2, 0);
  for (; retry < 5; retry++) {
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[8] == 0x55 &&
        mcman_read_dma[147] == 0x55)
      break;
  }
  if (retry >= 5)
    return -1;
  if (pages && ecc) {
    mcman_wmemset(ecc, 32, 0);
    page = 0;
    if (page < device->pages_per_block) {
      current_page = pages;
      do {
        checksum = (u_char*)ecc + (page * device->page_size) / 32;
        retry = 0;
        if (retry < device->page_size) {
          page_data = current_page;
          do {
            if (*page_data)
              McDataChecksum((u_char*)*page_data + retry, checksum);
            retry += 128;
            checksum += 3;
          } while (retry < device->page_size);
        }
        current_page++;
      } while (++page < device->pages_per_block);
    }
  }
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 1, 0);
  mcman_build_command(port, slot, -2, 0);
  for (retry = 0; retry < 100; retry++) {
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[3] == 0x55)
      return 0;
  }
  if (mcman_read_dma[3] == 0x66)
    return -8;
  return -2;
}

int McWritePage(int port, int slot, int page, const void* buffer, const void* ecc) {
  int page_size = mcman_devices[port][slot].page_size;
  int retry, index, offset;
  u_int spare;
  for (retry = 0; retry < 5; retry++) {
    if (retry != 0)
      mcman_cardchanged(port, slot);
    mcman_build_command(port, slot, -1, 0);
    mcman_build_command(port, slot, 3, (void*)page);
    for (index = 0; index < page_size; index += 128)
      mcman_build_command(port, slot, 10, (u_char*)buffer + index);
    if (mcman_devices[port][slot].card_flags & 1)
      mcman_build_command(port, slot, 14, (void*)ecc);
    mcman_build_command(port, slot, -2, 0);
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) != 0x1000 || mcman_read_dma[8] != 0x55)
      continue;
    for (index = 0; index < page_size / 128; index++) {
      if (mcman_read_dma[(index + 1) * 144 + 133] != 0x55)
        break;
    }
    if (index < page_size / 128)
      continue;
    if (mcman_devices[port][slot].card_flags & 1) {
      spare = mcman_spare_size(port, slot);
      offset = (index + 1) * 144 + 5;
      if (mcman_read_dma[spare + offset] != 0x55)
        continue;
    }
    mcman_build_command(port, slot, -1, 0);
    mcman_build_command(port, slot, 12, 0);
    mcman_build_command(port, slot, -2, 0);
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[3] == 0x55)
      return 0;
  }
  if (mcman_read_dma[3] == 0x66)
    return -8;
  return -2;
}

struct McSendTiming {
  u_short size, flags;
};
extern struct Sio2Transfer mcman_sio2_packet;
extern u_char mcman_read_dma[1584];

int mcman_readpage(int port, int slot, int page, void* buffer, void* ecc) {
  int chunks = mcman_devices[port][slot].page_size / 128;
  int retry, index;
  int device_offset;
  u_short timing_index;
  u_char* data;
  struct Sio2Transfer* packet;
  retry = 0;
  for (; retry < 5; retry++) {
    device_offset = slot * (int)sizeof(McDeviceInfo) + port * (int)sizeof(mcman_devices[0]);
    packet = &mcman_sio2_packet;
    data = mcman_read_dma + 148;

    if (mcman_cardchanged(port, slot) != 0)
      continue;
    mcman_build_command(port, slot, -1, 0);
    mcman_build_command(port, slot, 4, (void*)page);
    for (index = 0; index < chunks; index++)
      mcman_build_command(port, slot, 11, 0);
    if (((McDeviceInfo*)((u_char*)mcman_devices + device_offset))->card_flags & 1)
      mcman_build_command(port, slot, 15, 0);
    mcman_build_command(port, slot, 12, 0);
    mcman_build_command(port, slot, -2, 0);
    timing_index = ((port & 1) + 2) * 4;
    *(u_short*)((u_int)mcman_sio2_packet.send2 + timing_index) = 0x7fff;
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(packet);
    mcman_end_transfer();
    *(u_short*)((u_int)mcman_sio2_packet.send2 + timing_index) = 0xffff;
    if ((packet->recv1 & 0xf000) != 0x1000 || mcman_read_dma[8] != 0x55 ||
        mcman_read_dma[867] != 0x55)
      continue;
    for (index = 0; index < chunks; index++) {
      if ((u_char)mcman_calcEDC((void*)(index * 144 + (u_int)data), 128) !=
          mcman_read_dma[(index + 1) * 144 + 132])
        break;
    }
    if (index < chunks)
      continue;
    index = 0;
    if (index < chunks) {
      for (; index < chunks; index++)
        memcpy((u_char*)buffer + index * 128, data + index * 144, 128);
    }
    memcpy(ecc, data + chunks * 144, mcman_spare_size(port, slot));
    break;
  }
  if (retry >= 5)
    return -1;
  return 0;
}

int McReadPage(int port, int slot, int page, void* buffer) {
  u_char ecc[32];
  int chunks = mcman_devices[port][slot].page_size / 128;
  int errors = 0;
  u_int erased = 0;
  int retry, index, result;
  u_char* data;
  u_char* checksum;
  if (!(mcman_devices[port][slot].card_flags & 16))
    erased = 255;
  for (retry = 0; retry < 5; retry++) {
    if (mcman_readpage(port, slot, page, buffer, ecc) == 0) {
      if (!(mcman_devices[port][slot].card_flags & 1))
        break;
      if (ecc[mcman_spare_size(port, slot) - 1] == erased)
        break;
      errors = 0;
      index = 0;
      if (index < chunks) {
        checksum = ecc;
        data = buffer;
        do {
          result = mcman_correctECC(data, checksum);
          if (result < errors)
            errors = result;
          checksum += 3;
          data += 128;
        } while (++index < chunks);
      }
      if (errors == 0 || (retry == 4 && errors > -3))
        break;
    }
  }
  if (retry >= 5) {
    if (errors != 0)
      return -2;
    return -1;
  }
  return 0;
}

extern struct Sio2Transfer mcman_sio2_packet;
extern u_char mcman_write_dma[1584], mcman_read_dma[1584];

int McGetCardSpec(int port,
                  int slot,
                  short* page_size,
                  short* block_size,
                  int* card_size,
                  u_char* flags) {
  int retry;
  u_char* rx;
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 7, 0);
  mcman_build_command(port, slot, -2, 0);
  rx = mcman_read_dma;
  for (retry = 0; retry < 5; retry++) {
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && rx[12] == 0x55 &&
        rx[11] == (u_char)mcman_calcEDC(rx + 3, 8))
      break;
  }
  if (retry == 5)
    return -1;
  *page_size = rx[3] + (rx[4] << 8);
  *block_size = rx[5] + (rx[6] << 8);
  *card_size = rx[7] + (rx[8] << 8) + (rx[9] << 16) + (rx[10] << 24);
  *flags = rx[2];
  return 0;
}

int mcman_resetauth(int port, int slot) {
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 17, 0);
  mcman_build_command(port, slot, -2, 0);
  mcman_begin_transfer(port, slot);
  sio2_transfer_legacy(&mcman_sio2_packet);
  mcman_end_transfer();
  if ((mcman_sio2_packet.recv1 & 0xf000) != 0x1000)
    return -1;
  return 0;
}

int mcman_probePS2Card2(int port, int slot) {
  int retry;
  for (retry = 0; retry < 5; retry++) {
    mcman_build_command(port, slot, -1, 0);
    mcman_build_command(port, slot, 9, 0);
    mcman_build_command(port, slot, -2, 0);
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[4] != 0x66)
      break;
  }
  if (retry == 5)
    return -12;
  if (mcman_read_dma[3] == 0x55) {
    if (McGetFormat(port, slot) > 0)
      return 0;
    if (McGetFormat(port, slot) < 0)
      return -2;
  }
  return -13;
}

int mcman_probePS2Card(int port, int slot) {
  int retry, result;
#if MCMAN_VERSION >= 0x0220
  mcman_probe_status = 255;
#endif
  if (mcman_cardchanged(port, slot) == 0 && McGetFormat(port, slot) != 0)
    return 0;
  if (mcman_resetauth(port, slot) != 0)
    return -11;
  if (SecrAuthCard(port + 2, slot, mcman_getcnum(port, slot)) == 0)
    return -90;
  for (retry = 0; retry < 5; retry++) {
    mcman_build_command(port, slot, -1, 0);
    mcman_build_command(port, slot, 9, 0);
    mcman_build_command(port, slot, -2, 0);
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[4] != 0x66)
      break;
  }
  if (retry == 5)
    return -12;
  mcman_clearcache(port, slot);
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 8, 0);
  mcman_build_command(port, slot, -2, 0);
  for (retry = 0; retry < 5; retry++) {
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[4] == 0x55)
      break;
  }
  if (retry == 5)
    return -13;
#if MCMAN_VERSION >= 0x0220
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 16, (void*)0x55);
  mcman_build_command(port, slot, -2, 0);
  for (retry = 0; retry < 5; retry++) {
    mcman_begin_transfer(port, slot);
    sio2_transfer_legacy(&mcman_sio2_packet);
    mcman_end_transfer();
    if ((mcman_sio2_packet.recv1 & 0xf000) == 0x1000 && mcman_read_dma[4] == 0x55)
      break;
  }
  if (retry == 5)
    return -14;
#endif
  result = mcman_setPS2devinfos(port, slot);
  if (result == 0)
    return -1;
  if (result == -2)
    mcman_devices[port][slot].formatted = -2;
  return result;
}

extern struct Sio2Transfer mcman_sio2_packet;
extern u_char mcman_write_dma[1584], mcman_read_dma[1584];

#if MCMAN_VERSION >= 0x0220
int mcman_read_page_fast(int port, int slot, int page) {
  int i;
  int channel;
  mcman_cardchanged(port, slot);
  mcman_build_command(port, slot, -1, 0);
  mcman_build_command(port, slot, 4, (void*)page);
  for (i = 0; i < 4; i++)
    mcman_build_command(port, slot, 11, 0);
  mcman_build_command(port, slot, 12, 0);
  mcman_build_command(port, slot, -2, 0);
  channel = (port & 1) + 2;
  mcman_sio2_packet.send2[channel].timing.size = 0x7fff;
  mcman_sio2_packet.send2[channel].timing.baud = 3;
  sio2_mc_init_legacy();
  sio2_transfer_legacy(&mcman_sio2_packet);
  sio2_transfer_reset_legacy();
  mcman_sio2_packet.send2[channel].timing.size = 0xffff;
  mcman_sio2_packet.send2[channel].timing.baud = 5;
  if ((mcman_sio2_packet.recv1 & 0xf000) != 0x1000 || mcman_read_dma[8] != 0x55 ||
      mcman_read_dma[723] != 0x55) {
    printf("r(%02x,%02x,%02x)", mcman_sio2_packet.recv1 >> 8, mcman_read_dma[8],
           mcman_read_dma[723]);
    return -1;
  }
  return 0;
}
#endif

int mcman_security_command(int port, int slot, struct Sio2Transfer* packet) {
  int result;
  mcman_begin_transfer(port, slot);
  result = sio2_transfer_legacy(packet);
  mcman_end_transfer();
  return result;
}

int mcman_getcnum(int port, int slot) {
  return (port & 1) * 8 + slot;
}

static u_char mcman_ecc_table[256] = {
    0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00,
    0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0, 0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3,
    0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2,
    0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
    0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1,
    0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22,
    0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87, 0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33,
    0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0,
    0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0,
    0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87, 0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33,
    0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22,
    0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1,
    0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
    0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2,
    0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0, 0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3,
    0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00,
};

void McDataChecksum(const void* data, u_char* ecc) {
  int row_odd = 0, row_even = 0, column = 0;
  int i = 0;
  const u_char* p = data;
  do {
    int parity = mcman_ecc_table[*p++];
    column ^= parity;
    if (parity & 0x80) {
      row_even ^= ~i;
      row_odd ^= i;
    }
  } while (++i < 128);
  ecc[0] = ~column & 0x77;
  ecc[1] = ~row_even & 0x7f;
  ecc[2] = ~row_odd & 0x7f;
}

int mcman_correctECC(void* data, const u_char* ecc) {
  u_char calculated[3];
  int row_even, row_odd, column, row_syndrome, column_syndrome;
  int errors, i;
  McDataChecksum(data, calculated);
  row_even = ecc[1] ^ calculated[1];
  row_odd = ecc[2] ^ calculated[2];
  row_syndrome = row_odd ^ row_even;
  column = ecc[0] ^ calculated[0];
  column_syndrome = (column & 15) ^ (column >> 4);
  if (!row_odd && !row_even && !column)
    return 0;
  if (row_syndrome == 0x7f && column_syndrome == 7) {
    ((u_char*)data)[row_odd] ^= 1 << (column >> 4);
    return -1;
  }
  errors = 0;
  for (i = 7; i >= 0; i--) {
    if (row_syndrome & 1)
      errors++;
    row_syndrome >>= 1;
  }
  for (i = 3; i >= 0; i--) {
    if (column_syndrome & 1)
      errors++;
    column_syndrome >>= 1;
  }
  if (errors == 1)
    return -2;
  return -3;
}

#if MCMAN_VERSION >= 0x0222
int mcman_reserved_export_57(void) {
  return 0;
}

int mcman_reserved_export_58(void) {
  return 0;
}

int mcman_reserved_export_56(void) {
  return 0;
}

int mcman_reserved_export_59(void) {
  return 0;
}
#endif
