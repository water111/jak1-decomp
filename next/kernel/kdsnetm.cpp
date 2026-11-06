// DECI2 protocol implementation for communication with the development listener.
#include "next/kernel/kdsnetm.h"

#include "next/kernel/kprint.h"
#include "sce/ee/libkernl/deci2.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/stdlib.h"
// These SDK inlines affect GCC 2.9's function-end code, required for matching.
// (other headers with inline functions may also work)
#include "sce/ee/libpc/libpc.h"

// Open the DECI2 connection and set up GoalProtoBlock to receive to MessBufArea.
void InitGoalProto() {
  GoalProtoBlock.socket = sceDeci2Open(DECI2_PROTOCOL, &GoalProtoBlock, GoalProtoHandler);
  if (GoalProtoBlock.socket < 0) {
    MsgErr("gproto: open proto error\n");
  } else {
    GoalProtoBlock.send_buffer = 0;
    GoalProtoBlock.receive_buffer = (ListenerMessageHeader*)MessBufArea;
    GoalProtoBlock.send_status = -1;
    GoalProtoBlock.last_receive_size = -1;
    GoalProtoBlock.receive_progress = 0;
    GoalProtoBlock.deci2count = 0;
    Msg(6, "gproto: proto open at socket %d\n", GoalProtoBlock.socket);
  }
}

// Close the GOAL DECI2 connection.
void ShutdownGoalProto() {
  if (GoalProtoBlock.socket > 0) {
    sceDeci2Close(GoalProtoBlock.socket);
  }
}

// Process a DECI2 event and re-enable interrupts before returning.
// This callback is called from the PS2 kernel to read/write a chunk
// over the DECI2 protocol.
void GoalProtoHandler(int event, int param, void* opt) {
  int transferred = 0;
  GoalProtoState* pb = (GoalProtoState*)opt;
  if (pb != &GoalProtoBlock) {
    Msg(6, "gproto: BAD OPT POINTER PASSED IN!!!!\n");
    pb = &GoalProtoBlock;
  }
  if (pb->deci2count) {
    (*pb->deci2count)++;
  }
  pb->most_recent_event = event;
  pb->most_recent_param = param;

  switch (event) {
    case DECI2_READ:
      if (pb->receive_progress + param > DEBUG_MESSAGE_BUFFER_SIZE) {
        pb->last_receive_size = -1;
        GoalProtoBlock.receive_progress = 0;
        break;
      }
      transferred =
          sceDeci2ExRecv(pb->socket, (char*)pb->receive_buffer + pb->receive_progress, param);
      if (transferred < 0) {
        pb->last_receive_size = -1;
        GoalProtoBlock.receive_progress = 0;
        break;
      }
      pb->receive_progress += transferred;
      break;
    case DECI2_READDONE:
      pb->last_receive_size = pb->receive_progress;
      pb->receive_progress = 0;
      break;
    case DECI2_WRITE:
      transferred = sceDeci2ExSend(pb->socket, pb->send_ptr, pb->send_remaining);
      if (transferred < 0) {
        pb->send_status = transferred;
        break;
      }
      pb->send_ptr += transferred;
      pb->send_remaining -= transferred;
      break;
    case DECI2_WRITEDONE:
      if (pb->send_remaining > 0) {
        pb->send_status = -abs(pb->send_remaining);
      } else {
        pb->send_status = 0;
      }
      break;
    case DECI2_CHSTATUS:
      break;
    case DECI2_ERROR:
    default:
      pb->last_receive_size = -1;
      break;
  }
  asm volatile("sync\n\tei" : : : "memory");
}

// Send a packet, retrying failed transfers up to ten times.
int SendFromBufferD(int kind, long long message_id, char* buffer, int size) {
  int result;
  int packet_size = size + sizeof(ListenerMessageHeader);
  int retries = 0;
  while (GoalProtoBlock.send_status > 0) {
  }
retry: {
  // Select the EE uncached alias of the packet buffer.
  GoalProtoBlock.send_buffer =
      (ListenerMessageHeader*)((unsigned int)(buffer - sizeof(ListenerMessageHeader)) |
                               EE_UNCACHED_MASK);
  ListenerMessageHeader* header = GoalProtoBlock.send_buffer;
  GoalProtoBlock.send_remaining = packet_size;
  GoalProtoBlock.send_ptr = (char*)header;
  GoalProtoBlock.send_status = GoalProtoBlock.send_remaining;
  FlushCache(WRITEBACK_DCACHE);

  header->deci2_header.len = GoalProtoBlock.send_remaining;
  header->deci2_header.reserved = 0;
  header->deci2_header.proto = DECI2_PROTOCOL;
  header->deci2_header.src = 'E';
  header->deci2_header.dst = 'H';
  header->msg_kind = kind;
  header->unknown = 0;
  header->msg_size = size;
  header->msg_id = message_id;

  result = sceDeci2ReqSend(GoalProtoBlock.socket, header->deci2_header.dst);
  if (result < 0) {
    printf("1sceDeci2ReqSend fail, reason code = %08x\n", result);
    return -6;
  }
  while (GoalProtoBlock.send_status > 0) {
  }
}
  if (GoalProtoBlock.send_status < 0 && retries < 10) {
    retries++;
    goto retry;
  }
  return 0;
}

// Print the last event and current transfer sizes.
void GoalProtoStatus() {
  Msg(6, "gproto: got %d %d\n", GoalProtoBlock.most_recent_event, GoalProtoBlock.most_recent_param);
  Msg(6, "gproto: %d %d\n", GoalProtoBlock.last_receive_size, GoalProtoBlock.send_remaining);
}

GoalProtoState GoalProtoBlock = {0};
