#ifndef NEXT_KERNEL_KDSNETM_H
#define NEXT_KERNEL_KDSNETM_H

// PS2 DECI2 packet header.
struct Deci2Header {
  unsigned short len;
  unsigned short reserved;
  unsigned short proto;
  char src;
  char dst;
};

// Types of messages sent from the target to the development host.
enum ListenerMessageKind {
  MSG_ACK = 0,  //
  MSG_OUTPUT = 1,
  MSG_PRINT = 2
};

// Commands sent by the development host to the GOAL listener.
enum ListenerToTargetMsgKind {
  LTT_MSG_POKE = 1,
  LTT_MSG_INSPECT = 5,
  LTT_MSG_PRINT = 6,
  LTT_MSG_PRINT_SYMBOLS = 7,
  LTT_MSG_RESET = 8,
  LTT_MSG_CODE = 9
};

struct ListenerMessageHeader {
  Deci2Header deci2_header;
  unsigned short msg_kind;
  unsigned short unknown;
  unsigned int msg_size;
  unsigned long long msg_id;
};

#define DECI2_PROTOCOL 0xe042

struct GoalProtoState {
  int socket;
  int receive_progress;
  volatile int send_remaining;
  unsigned int reserved[13];
  unsigned int most_recent_event;
  unsigned int most_recent_param;
  unsigned int msg_kind;
  unsigned long long msg_id;
  ListenerMessageHeader* send_buffer;
  ListenerMessageHeader* receive_buffer;
  char* send_ptr;
  int last_receive_size;
  volatile int send_status;
  unsigned int reserved_after_status;
  int* deci2count;
};

extern GoalProtoState GoalProtoBlock;

void InitGoalProto();
void ShutdownGoalProto();
void GoalProtoHandler(int event, int param, void* opt);
void GoalProtoStatus();
int SendFromBufferD(int kind, long long message_id, char* buffer, int size);

#endif
