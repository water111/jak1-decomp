// Loose abstraction over DECI2 protocol for socket-like communications
// to the development listener. It's possible there were once other protocols
// but they were compiled out or removed. (see unused KERNELTOLISTENER files)
#include "next/kernel/ksocket.h"

#include "next/kernel/fileio.h"
#include "next/kernel/kboot.h"
#include "next/kernel/kdsnetm.h"
#include "next/kernel/kprint.h"
#include "sce/ee/libkernl/eekernel.h"

// Return the size of the message received.
// The message is received in the background in DECI2 handlers,
// this just verifies it and returns the size.
int ReceiveToBuffer(char* buffer) {
  int message_size;

  // check size is at least size of header
  int received_size = GoalProtoBlock.last_receive_size;
  if (received_size < (int)sizeof(ListenerMessageHeader)) {
    return -1;
  }

  // read size out of header
  FlushCache(WRITEBACK_DCACHE);
  ListenerMessageHeader* header = GoalProtoBlock.receive_buffer;
  message_size = header->msg_size;

  // check message protocol
  if (header->deci2_header.proto != DECI2_PROTOCOL) {
    MsgErr("dkernel: got a bad packet to goal proto (goal #x%x bytes %d %d %d %d %d)\n", header,
           GoalProtoBlock.last_receive_size, header->msg_kind, header->unknown, header->msg_id,
           message_size);
    GoalProtoBlock.last_receive_size = -1;
    return -1;
  }

  // null-terminate buffer and set metadata.
  buffer = (char*)(header + 1);
  buffer[message_size] = '\0';
  GoalProtoBlock.msg_kind = header->msg_kind;
  GoalProtoBlock.msg_id = header->msg_id;
  GoalProtoBlock.last_receive_size = -1;
  return message_size;
}

// Send an output message.
int SendFromBuffer(char* buffer, int size) {
  return SendFromBufferD(MSG_OUTPUT, 0, buffer, size);
}

// Initialize Listener by sending a single ack.
void InitListenerConnect() {
  if (MasterDebug) {
    kstrcpy(AckBufArea + sizeof(ListenerMessageHeader), "ack\n");
  }
}

// Does nothing.
void InitCheckListener() {
}

// Poll for a message and return pointer to null-terminated message or NULL.
char* WaitForMessageAndAck() {
  if (MasterDebug) {
    MessCount = ReceiveToBuffer(MessBufArea + sizeof(ListenerMessageHeader));
  } else {
    MessCount = -1;
  }
  return MessCount >= 0 ? MessBufArea + sizeof(ListenerMessageHeader) : 0;
}

// Just print a message.
void CloseListener() {
  Msg(6, "dconnect: closed socket at kernel side\n");
}
