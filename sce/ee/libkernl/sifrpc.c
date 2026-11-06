#include "sce/ee/libkernl/sif.h"

extern int DIntr(void), EIntr(void), SleepThread(void);
extern int iWakeupThread(int), iSignalSema(int), WaitSema(int), DeleteSema(int);
extern void sceSifInitCmd(void), sceSifExitCmd(void);
extern unsigned sceSifGetSreg(int);
extern unsigned sceSifSendCmd(unsigned, void*, int, void*, void*, int);
extern unsigned isceSifSendCmd(unsigned, void*, int, void*, void*, int);

typedef struct {
  int current, maximum, initial, waiting;
  unsigned attributes, option;
} RpcSemaphore;

extern int CreateSema(RpcSemaphore*);

typedef struct RpcQueue RpcQueue;
typedef struct RpcServer RpcServer;
typedef void* (*RpcFunction)(unsigned, void*, int);

struct RpcServer {
  unsigned command;
  RpcFunction function;
  void* buffer;
  int size;
  RpcFunction callback;
  void* callback_buffer;
  int callback_size;
  SifRpcClient* client;
  void* packet;
  unsigned function_number;
  void* receive;
  int receive_size, reply;
  unsigned request_id;
  RpcServer *link, *next;
  RpcQueue* queue;
};

struct RpcQueue {
  int thread, active;
  RpcServer *servers, *start, *end;
  RpcQueue* next;
};

typedef struct {
  SifRpcRequest request;
  void *source, *destination;
  int size;
} RpcReceive;

typedef struct RpcPacket {
  SifCommandHeader header;
  unsigned flags;
  void* packet;
  unsigned id;
  SifRpcClient* client;

  union {
    struct {
      unsigned command;
      RpcServer* server;
      void *buffer, *callback_buffer;
    } end;

    struct {
      unsigned command;
    } bind;

    struct {
      void *source, *destination;
      int size;
    } data;

    struct {
      unsigned function;
      int size;
      void* receive;
      int receive_size, reply;
      RpcServer* server;
    } call;

    unsigned words[8];
  } body;
} RpcPacket;

typedef struct {
  unsigned next_id;
  void* packets;
  int packet_count;
  void* unused[2];
  void* replies;
  int reply_count;
  void* indexed_replies;
  int indexed_count, next_reply;
  void* queues;
} RpcState;

static int _sceSifInitCheck = 0;
static unsigned char _packet_buffer[2048] __attribute__((aligned(64)));
static RpcPacket _free_buffer[32] __attribute__((aligned(64)));
static RpcPacket _free_buffer2[32] __attribute__((aligned(64)));
static RpcState _data_table;

static void _request_end(void*, void*);
static void _request_bind(void*, void*);
static void _request_call(void*, void*);
static void _request_rdata(void*, void*);

void sceSifInitRpc(unsigned mode) {
  DIntr();
  if (_sceSifInitCheck) {
    EIntr();
    return;
  }
  _sceSifInitCheck = 1;
  EIntr();
  sceSifInitCmd();
  DIntr();
  _data_table.packets = (void*)((unsigned)_packet_buffer | 0x20000000);
  _data_table.packet_count = 32;
  _data_table.unused[0] = 0;
  _data_table.unused[1] = 0;
  _data_table.replies = (void*)((unsigned)_free_buffer | 0x20000000);
  _data_table.reply_count = 32;
  _data_table.indexed_replies = (void*)((unsigned)_free_buffer2 | 0x20000000);
  _data_table.indexed_count = 32;
  _data_table.next_id = 1;
  _data_table.next_reply = 0;
  sceSifAddCmdHandler(0x80000008, _request_end, &_data_table);
  sceSifAddCmdHandler(0x80000009, _request_bind, &_data_table);
  sceSifAddCmdHandler(0x8000000a, _request_call, &_data_table);
  sceSifAddCmdHandler(0x8000000c, _request_rdata, &_data_table);
  EIntr();
  if (sceSifGetReg(0x80000002))
    return;
  {
    ((SifCommandHeader*)(_packet_buffer + 64))->option = 1;
    sceSifSendCmd(0x80000002, _packet_buffer + 64, 16, 0, 0, 0);
    while (!sceSifGetSreg(0)) {
    }
    sceSifSetReg(0x80000002, 1);
    return;
  }
}

void sceSifExitRpc(void) {
  sceSifExitCmd();
  _sceSifInitCheck = 0;
}

static RpcPacket* _sceRpcGetPacket(RpcState* state) {
  RpcPacket* packet;
  int i;
  DIntr();
  packet = state->packets;
  for (i = 0; i < state->packet_count; i++, packet++) {
    if (!(packet->flags & 1)) {
      unsigned old;
      register unsigned id __asm__("$3");
      packet->flags = (i << 16) | 5;
      old = state->next_id;
      id = old + 1;
      state->next_id = id;
      if (id == 1) {
        state->next_id = old + 2;
        id = 1;
      }
      packet->packet = packet;
      packet->id = id;
      EIntr();
      return packet;
    }
  }
  EIntr();
  return 0;
}

static void _sceRpcFreePacket(RpcPacket* packet) {
  packet->flags &= ~1u;
  packet->id = 0;
}

static RpcPacket* _sceRpcGetFPacket(RpcState* state) {
  state->next_reply %= state->reply_count;
  return (RpcPacket*)state->replies + state->next_reply++;
}

static RpcPacket* _sceRpcGetFPacket2(RpcState* state, int index) {
  if (index < 0 || index >= state->indexed_count)
    return _sceRpcGetFPacket(state);
  return (RpcPacket*)state->indexed_replies + index;
}

static void _request_end(void* data, void* argument) {
  RpcPacket* packet = data;
  SifRpcClient* client;
  switch (packet->body.end.command) {
    case 0x8000000c:
      break;
    case 0x8000000a:
      client = packet->client;
      if (client->callback)
        client->callback(client->callback_argument);
      break;
    case 0x80000009:
      client = packet->client;
      client->server = packet->body.end.server;
      client->buffer = packet->body.end.buffer;
      client->callback_buffer = packet->body.end.callback_buffer;
      break;
  }
  client = packet->client;
  if (client->request.thread_id >= 0)
    iSignalSema(client->request.thread_id);
  _sceRpcFreePacket(client->request.packet);
  client->request.packet = 0;
}

static void _request_rdata(void* data, void* argument) {
  RpcPacket* request = data;
  RpcPacket* reply = _sceRpcGetFPacket(argument);
  reply->packet = request->packet;
  reply->client = request->client;
  reply->body.end.command = 0x8000000c;
  isceSifSendCmd(0x80000008, reply, 64, request->body.data.source, request->body.data.destination,
                 request->body.data.size);
}

int sceSifGetOtherData(RpcReceive* receive,
                       void* source,
                       void* destination,
                       int size,
                       unsigned mode) {
  RpcSemaphore sema;
  RpcPacket* packet = _sceRpcGetPacket(&_data_table);
  if (!packet)
    return -1;
  receive->request.packet = packet;
  receive->request.packet_id = packet->id;
  packet->body.data.source = source;
  packet->body.data.destination = destination;
  packet->body.data.size = size;
  packet->packet = packet;
  packet->client = (SifRpcClient*)receive;
  if (!(mode & 1)) {
    sema.initial = 0;
    sema.maximum = 1;
    receive->request.thread_id = CreateSema(&sema);
    if (receive->request.thread_id < 0) {
      _sceRpcFreePacket(packet);
      return -3;
    }
    if (!sceSifSendCmd(0x8000000c, packet, 64, 0, 0, 0)) {
      _sceRpcFreePacket(packet);
      DeleteSema(receive->request.thread_id);
      return -2;
    }
    WaitSema(receive->request.thread_id);
    DeleteSema(receive->request.thread_id);
    return 0;
  }
  receive->request.thread_id = -1;
  if (!sceSifSendCmd(0x8000000c, packet, 64, 0, 0, 0)) {
    _sceRpcFreePacket(packet);
    return -2;
  }
  return 0;
}

static RpcServer* _search_svdata(unsigned command, RpcState* state) {
  RpcQueue* queue;
  RpcServer* server;
  for (queue = state->queues; queue; queue = queue->next)
    for (server = queue->servers; server; server = server->link)
      if (server->command == command)
        return server;
  return 0;
}

static void _request_bind(void* data, void* argument) {
  RpcPacket* request = data;
  RpcPacket* reply = _sceRpcGetFPacket(argument);
  RpcServer* server;
  reply->packet = request->packet;
  reply->client = request->client;
  reply->body.end.command = 0x80000009;
  server = _search_svdata(request->body.bind.command, argument);
  if (!server) {
    reply->body.end.server = 0;
    reply->body.end.buffer = 0;
    reply->body.end.callback_buffer = 0;
  } else {
    reply->body.end.server = server;
    reply->body.end.buffer = server->buffer;
    reply->body.end.callback_buffer = server->callback_buffer;
  }
  isceSifSendCmd(0x80000008, reply, 64, 0, 0, 0);
}

int sceSifBindRpc(SifRpcClient* client, unsigned command, unsigned mode) {
  RpcSemaphore sema;
  RpcPacket* packet;
  client->command = 0;
  client->server = 0;
  packet = _sceRpcGetPacket(&_data_table);
  if (!packet)
    return -1;
  client->request.packet = packet;
  client->request.packet_id = packet->id;
  packet->body.bind.command = command;
  packet->packet = packet;
  packet->client = client;
  if (!(mode & 1)) {
    sema.initial = 0;
    sema.maximum = 1;
    client->request.thread_id = CreateSema(&sema);
    if (client->request.thread_id < 0) {
      _sceRpcFreePacket(packet);
      return -3;
    }
    if (!sceSifSendCmd(0x80000009, packet, 64, 0, 0, 0)) {
      _sceRpcFreePacket(packet);
      DeleteSema(client->request.thread_id);
      return -2;
    }
    WaitSema(client->request.thread_id);
    DeleteSema(client->request.thread_id);
    return 0;
  }
  client->request.thread_id = -1;
  if (!sceSifSendCmd(0x80000009, packet, 64, 0, 0, 0)) {
    _sceRpcFreePacket(packet);
    return -2;
  }
  return 0;
}

static void _request_call(void* data, void* argument) {
  volatile RpcPacket* packet = data;
  RpcServer* server = packet->body.call.server;
  RpcQueue* queue = server->queue;
  if (!queue->start)
    queue->start = server;
  else
    queue->end->next = server;
  ((volatile RpcQueue*)queue)->end = server;
  server->packet = packet->packet;
  server->client = packet->client;
  server->function_number = packet->body.call.function;
  server->size = packet->body.call.size;
  server->receive = packet->body.call.receive;
  server->receive_size = packet->body.call.receive_size;
  server->reply = packet->body.call.reply;
  server->request_id = packet->flags;
  if (queue->thread >= 0 && !queue->active)
    iWakeupThread(queue->thread);
}

int sceSifCallRpc(SifRpcClient* client,
                  unsigned function,
                  unsigned mode,
                  void* send,
                  int send_size,
                  void* receive,
                  int receive_size,
                  void (*callback)(void*),
                  void* argument) {
  RpcSemaphore sema;
  RpcPacket* packet = _sceRpcGetPacket(&_data_table);
  if (!packet)
    return -1;
  client->request.packet = packet;
  client->request.packet_id = packet->id;
  client->callback = callback;
  client->callback_argument = argument;
  packet->body.call.function = function;
  packet->body.call.size = send_size;
  packet->body.call.receive = receive;
  packet->body.call.receive_size = receive_size;
  packet->packet = packet;
  packet->body.call.server = client->server;
  packet->client = client;
  if (!(mode & 2)) {
    if (send == receive)
      sceSifWriteBackDCache(send, send_size < receive_size ? receive_size : send_size);
    else {
      if (send_size > 0)
        sceSifWriteBackDCache(send, send_size);
      if (receive_size > 0)
        sceSifWriteBackDCache(receive, receive_size);
    }
  }
  if (mode & 1) {
    if (!callback)
      packet->body.call.reply = 0;
    else
      packet->body.call.reply = 1;
    client->request.thread_id = -1;
    if (!sceSifSendCmd(0x8000000a, packet, 64, send, client->buffer, send_size)) {
      _sceRpcFreePacket(packet);
      return -2;
    }
  } else {
    sema.initial = 0;
    sema.maximum = 1;
    client->request.thread_id = CreateSema(&sema);
    if (client->request.thread_id < 0) {
      _sceRpcFreePacket(packet);
      return -3;
    }
    packet->body.call.reply = 1;
    if (!sceSifSendCmd(0x8000000a, packet, 64, send, client->buffer, send_size)) {
      DeleteSema(client->request.thread_id);
      _sceRpcFreePacket(packet);
      return -2;
    }
    WaitSema(client->request.thread_id);
    DeleteSema(client->request.thread_id);
  }
  return 0;
}

int sceSifCheckStatRpc(SifRpcRequest* request) {
  RpcPacket* packet = request->packet;
  if (!packet || request->packet_id != packet->id || !(packet->flags & 1))
    return 0;
  return 1;
}

void sceSifSetRpcQueue(RpcQueue* queue, int thread) {
  RpcQueue* last;
  DIntr();
  queue->thread = thread;
  queue->active = 0;
  queue->servers = 0;
  queue->start = 0;
  queue->end = 0;
  queue->next = 0;
  if (!_data_table.queues)
    _data_table.queues = queue;
  else {
    last = _data_table.queues;
    while (last->next)
      last = last->next;
    last->next = queue;
  }
  EIntr();
}

void sceSifRegisterRpc(RpcServer* server,
                       unsigned command,
                       RpcFunction function,
                       void* buffer,
                       RpcFunction callback,
                       void* callback_buffer,
                       RpcQueue* queue) {
  RpcServer* last;
  DIntr();
  server->next = 0;
  server->link = 0;
  server->command = command;
  server->function = function;
  server->buffer = buffer;
  server->callback = callback;
  server->callback_buffer = callback_buffer;
  server->queue = queue;
  if (!queue->servers)
    queue->servers = server;
  else {
    last = queue->servers;
    while (last->link)
      last = last->link;
    last->link = server;
  }
  EIntr();
}

RpcServer* sceSifRemoveRpc(RpcServer* server, RpcQueue* queue) {
  RpcServer* previous;
  DIntr();
  previous = queue->servers;
  if (previous == server)
    queue->servers = previous->link;
  else {
    for (; previous; previous = previous->link) {
      if (previous->link == server) {
        previous->link = server->link;
        break;
      }
    }
  }
  EIntr();
  return previous;
}

RpcQueue* sceSifRemoveRpcQueue(RpcQueue* queue) {
  RpcQueue* previous;
  DIntr();
  previous = _data_table.queues;
  if (previous == queue)
    _data_table.queues = previous->next;
  else {
    for (; previous; previous = previous->next) {
      if (previous->next == queue) {
        previous->next = queue->next;
        break;
      }
    }
  }
  EIntr();
  return previous;
}

RpcServer* sceSifGetNextRequest(RpcQueue* queue) {
  RpcServer* server;
  DIntr();
  server = queue->start;
  if (!server)
    queue->active = 0;
  else {
    queue->active = 1;
    queue->start = server->next;
  }
  EIntr();
  return server;
}

void sceSifExecRequest(RpcServer* server) {
  SifDmaTransfer transfer[2];
  RpcPacket* packet;
  void* result;
  int size = 0, count, delay;
  unsigned sent;
  result = server->function(server->function_number, server->buffer, server->size);
  if (result)
    size = server->receive_size;
  if (server->size > 0)
    sceSifWriteBackDCache(server->buffer, server->size);
  if (size > 0)
    sceSifWriteBackDCache(result, size);
  DIntr();
  if (server->request_id & 4)
    packet = _sceRpcGetFPacket2(&_data_table, server->request_id >> 16);
  else
    packet = _sceRpcGetFPacket(&_data_table);
  EIntr();
  packet->client = server->client;
  packet->body.end.command = 0x8000000a;
  if (server->reply) {
    while (!sceSifSendCmd(0x80000008, packet, 64, result, server->receive, size)) {
    }
  } else {
    packet->id = 0;
    packet->flags = 0;
    count = 0;
    if (size > 0) {
      transfer[0].source = (unsigned)result;
      transfer[0].destination = (unsigned)server->receive;
      transfer[0].size = size;
      transfer[0].attributes = 0;
      count++;
    }
    transfer[count].source = (unsigned)packet;
    transfer[count].destination = (unsigned)server->packet;
    transfer[count].size = 64;
    transfer[count].attributes = 0;
    count++;
    do {
      sent = sceSifSetDma(transfer, count);
      if (!sent)
        for (delay = 0x100000; delay--;) {
        }
    } while (!sent);
  }
}

void sceSifRpcLoop(RpcQueue* queue) {
  RpcServer* server;
  for (;;) {
    while ((server = sceSifGetNextRequest(queue)))
      sceSifExecRequest(server);
    SleepThread();
  }
}
