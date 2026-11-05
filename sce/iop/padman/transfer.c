#include "sce/iop/padman/transfer.h"
#include "sce/iop/padman/commands.h"

typedef struct PadTransferSlot {
  int active;
  int reserved[2];
  int connected;
  int input_size;
  int output_size;
  u_char input[PAD_PACKET_SIZE];
  u_char output[PAD_PACKET_SIZE];
  u_int control1, control2, control3;
  int error;
} PadTransferSlot;

static PadTransferSlot transfer_slots[PAD_PORT_COUNT][PAD_SLOT_COUNT];
static struct Sio2Transfer transfer;

static struct {
  int slot[4];
  int status[4];
} slot_selection;

static u_char input_buffer[PAD_TRANSFER_BUFFER_SIZE];
static u_char output_buffer[PAD_TRANSFER_BUFFER_SIZE];
static u_int transfer_count = 0;

int pad_transfer_packet_error(int packet, struct Sio2Transfer* transfer) {
  return (transfer->recv1 & (0x10000 << packet)) != 0;
}

void pad_transfer_init(void) {
  int port, slot;
  for (port = 0; port < PAD_PORT_COUNT; ++port) {
    for (slot = 0; slot < PAD_SLOT_COUNT; ++slot)
      transfer_slots[port][slot].active = 0;
    // BUG: probably should have been in the loop!
    transfer_slots[port][slot].connected = 0;
  }
  transfer.input = input_buffer;
  transfer.output = output_buffer;
}

int pad_transfer_set_active(int port, int slot, int active) {
  int old = transfer_slots[port][slot].active;
  transfer_slots[port][slot].active = active;
  return old;
}

int pad_transfer_get_active(int port, int slot) {
  return transfer_slots[port][slot].active;
}

int pad_transfer_set_connected(int port, int slot, int connected) {
  int old = transfer_slots[port][slot].connected;
  transfer_slots[port][slot].connected = connected;
  return old;
}

int pad_transfer_get_connected(int port, int slot) {
  return transfer_slots[port][slot].connected;
}

int pad_transfer_append(int packet, int port, int slot) {
  int i;
  for (i = 0; i < transfer_slots[port][slot].input_size; ++i)
    transfer.input[transfer.input_size++] = transfer_slots[port][slot].input[i];
  transfer.output_size += transfer_slots[port][slot].output_size;
  transfer.send1[port] = transfer_slots[port][slot].control1;
  transfer.send2[port] = transfer_slots[port][slot].control2;
  transfer.send3[packet++] = (transfer_slots[port][slot].control3 & ~3) | (port & 3);
  transfer.send3[packet] = 0;
  return packet;
}

int pad_transfer_receive(int packet, int port, int slot) {
  int i;

  union {
    u_int value;

    struct {
      u_int reserved : 13, error : 1, rest : 18;
    } fields;
  } status;

  status.value = transfer.recv1;
  if (status.fields.error == 1) {
    transfer_slots[port][slot].error = 1;
    return 0;
  }
  if (pad_transfer_packet_error(packet, &transfer) == 1) {
    transfer_slots[port][slot].error = 1;
    return 0;
  }
  transfer_slots[port][slot].error = 0;
  for (i = 0; i < transfer_slots[port][slot].output_size; ++i)
    transfer_slots[port][slot].output[i] = transfer.output[transfer.output_size++];
  return 1;
}

int pad_transfer_slot(int slot) {
  int active1 = 0, active0 = 0;
  int packet1 = 0, packet0 = 0, next = 0;
  u_int connected;
  transfer.input_size = 0;
  transfer.output_size = 0;
  transfer.output_dma.address = 0;
  transfer.input_dma.address = 0;
  connected = sio2_recv2_get_legacy();
  pad_transfer_set_connected(0, slot, (connected >> 4) & 1);
  pad_transfer_set_connected(1, slot, (connected >> 5) & 1);
  if (transfer_slots[0][slot].active == 1 && slot_selection.status[0] == 1) {
    active0 = 1;
    packet0 = next;
    next = pad_transfer_append(next, 0, slot);
  }
  if (transfer_slots[1][slot].active == 1 && slot_selection.status[1] == 1) {
    active1 = 1;
    packet1 = next;
    next = pad_transfer_append(next, 1, slot);
  }
  if (transfer_slots[0][slot].active == 1 && slot_selection.status[0] != 1)
    transfer_slots[0][slot].error = 10;
  if (transfer_slots[1][slot].active == 1 && slot_selection.status[1] != 1)
    transfer_slots[1][slot].error = 10;
  if (!next)
    return 0;
  sio2_transfer_legacy(&transfer);
  transfer.output_size = 0;
  if (active0 == 1) {
    pad_transfer_receive(packet0, 0, slot);
    transfer.output_size = transfer_slots[0][slot].output_size;
  }
  if (active1 == 1) {
    pad_transfer_receive(packet1, 1, slot);
    transfer.output_size = transfer_slots[1][slot].output_size;
  }
  return 1;
}

int pad_transfer_select_slot(int slot) {
  slot_selection.slot[0] = -1;
  slot_selection.slot[1] = -1;
  slot_selection.slot[2] = -1;
  slot_selection.slot[3] = -1;
  if (!transfer_slots[0][slot].active && !transfer_slots[1][slot].active)
    return 0;
  if (transfer_slots[0][slot].active == 1)
    slot_selection.slot[0] = slot;
  if (transfer_slots[1][slot].active == 1)
    slot_selection.slot[1] = slot;
  sio2_change_slot(slot_selection.slot);
  return 1;
}

int pad_transfer_all(void) {
  int slot;
  sio2_pad_init_legacy();
  ++transfer_count;
  for (slot = 0; slot < PAD_SLOT_COUNT; ++slot) {
    if (transfer_slots[0][slot].active == 1 || transfer_slots[1][slot].active == 1) {
      pad_transfer_select_slot(slot);
      pad_transfer_slot(slot);
    }
  }
  pad_transfer_select_slot(0);
  sio2_transfer_reset();
  return 1;
}

int pad_transfer_set_control1(u_int port, int slot, u_int value) {
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    transfer_slots[port][slot].control1 = value;
    return 1;
  }
  return 0;
}

int pad_transfer_set_control2(u_int port, int slot, u_int value) {
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    transfer_slots[port][slot].control2 = value;
    return 1;
  }
  return 0;
}

int pad_transfer_set_control3(u_int port, int slot, u_int value) {
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    transfer_slots[port][slot].control3 = value;
    return 1;
  }
  return 0;
}

int pad_transfer_get_error(u_int port, int slot) {
  if (port >= PAD_PORT_COUNT)
    return -1;
  return transfer_slots[port][slot].error;
}

int pad_transfer_set_input_size(u_int port, int slot, int size) {
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    transfer_slots[port][slot].input_size = size;
    return size;
  }
  return 0;
}

int pad_transfer_set_input(u_int port, int slot, int size, const u_char* buffer) {
  int i;
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    if (size == 0)
      size = transfer_slots[port][slot].input_size;
    for (i = 0; i < size; ++i)
      transfer_slots[port][slot].input[i] = buffer[i];
    return size;
  }
  return 0;
}

int pad_transfer_set_output_size(u_int port, int slot, int size) {
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    transfer_slots[port][slot].output_size = size;
    return size;
  }
  return 0;
}

int pad_transfer_get_output(u_int port, int slot, int size, u_char* buffer) {
  int i;
  if (port >= PAD_PORT_COUNT)
    return 0;
  {
    if (size == 0)
      size = transfer_slots[port][slot].output_size;
    for (i = 0; i < size; ++i)
      buffer[i] = transfer_slots[port][slot].output[i];
    return size;
  }
  return 0;
}

int pad_transfer_setup_probe(int port, int slot, int connected) {
  u_char buffer[PAD_PACKET_SIZE];
  pad_transfer_set_control1(port, slot, pad_command_port_control1(0, connected, 0));
  pad_transfer_set_control2(port, slot, pad_command_port_control2(0, connected));
  pad_transfer_set_control3(port, slot, pad_command_transfer_control(0));
  pad_transfer_set_input_size(port, slot, pad_command_input_size(0));
  pad_transfer_set_output_size(port, slot, pad_command_output_size(0));
  pad_command_read(0, buffer);
  pad_transfer_set_input(port, slot, 0, buffer);
  return 1;
}

int pad_transfer_probe_port(int port, int slot) {
  u_int connected;
  transfer.input_size = 0;
  transfer.output_size = 0;
  transfer.output_dma.address = 0;
  transfer.input_dma.address = 0;
  if (slot_selection.status[port] != 1)
    return 0;
  connected = sio2_recv2_get_legacy();
  if (port == 0)
    pad_transfer_set_connected(0, slot, connected >> 4 & 1);
  if (port == 1)
    pad_transfer_set_connected(1, slot, connected >> 5 & 1);
  pad_transfer_append(0, port, slot);
  sio2_transfer_legacy(&transfer);
  transfer.output_size = 0;
  pad_transfer_receive(0, port, slot);
  transfer.output_size = transfer_slots[port][slot].output_size;
  if (slot_selection.status[port] != 1)
    transfer_slots[port][slot].error = 10;
  return 1;
}

int pad_transfer_probe(int port, int slot) {
  int result = 0;
  sio2_pad_init_legacy();
  slot_selection.slot[0] = -1;
  slot_selection.slot[1] = -1;
  slot_selection.slot[2] = -1;
  slot_selection.slot[3] = -1;
  slot_selection.slot[0] = slot;
  slot_selection.slot[1] = slot;
  sio2_change_slot(slot_selection.slot);
  pad_transfer_setup_probe(0, slot, pad_transfer_get_connected(0, slot));
  pad_transfer_setup_probe(1, slot, pad_transfer_get_connected(1, slot));
  if (pad_transfer_probe_port(port, slot) == 1)
    result = transfer_slots[port][slot].error == 0;
  slot_selection.slot[0] = -1;
  slot_selection.slot[1] = -1;
  slot_selection.slot[2] = -1;
  slot_selection.slot[3] = -1;
  slot_selection.slot[0] = 0;
  slot_selection.slot[1] = 0;
  sio2_change_slot(slot_selection.slot);
  sio2_transfer_reset();
  return result;
}
