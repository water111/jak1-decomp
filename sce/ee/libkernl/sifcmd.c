#include "sce/ee/libkernl/sif.h"

extern int DIntr(void), EIntr(void);
extern void FlushCache(int);
extern void sceSifSetDChain(void), isceSifSetDChain(void);
extern int AddDmacHandler(int, int (*)(int), int);
extern int RemoveDmacHandler(int, int);
extern int EnableDmac(int), DisableDmac(int);
extern unsigned isceSifSetDma(SifDmaTransfer*, int);

typedef void (*CommandCallback)(void*, void*);

typedef struct {
  CommandCallback callback;
  void* argument;
} CommandHandler;

typedef struct {
  void* packet;
  void *send_buffer, *iop_buffer;
  CommandHandler* system;
  int system_count;
  CommandHandler* user;
  int user_count;
  unsigned* registers;
} CommandState;

typedef unsigned int Quad __attribute__((mode(TI)));

static int _cmd_init_check = 0;
static unsigned char _pckt_buffer[128] __attribute__((aligned(64)));
static unsigned char _send_buffer[64] __attribute__((aligned(64)));

static struct {
  SifCommandHeader header;
  void* address;
} _csdata;

static int sif0_handleid;
static CommandState _data_table;
static CommandHandler _sys_buffer[32] __attribute__((aligned(64)));
static unsigned soft_reg[32];

static int _sceSifCmdIntrHdlr(int);
unsigned sceSifSendCmd(unsigned, void*, int, void*, void*, int);

static void _set_sreg(void* packet, void* argument) {
  struct {
    SifCommandHeader header;
    int index;
    unsigned value;
  }* command = packet;

  CommandState* state = argument;
  state->registers[command->index] = command->value;
}

static void _change_addr(void* packet, void* argument) {
  struct {
    SifCommandHeader header;
    void* address;
  }* command = packet;

  CommandState* state = argument;
  state->iop_buffer = command->address;
}

unsigned sceSifGetSreg(int index) {
  return soft_reg[index];
}

unsigned sceSifSetSreg(int index, unsigned value) {
  return soft_reg[index] = value;
}

void* sceSifGetDataTable(void) {
  return &_data_table;
}

void sceSifInitCmd(void) {
  int i;
  DIntr();
  if (_cmd_init_check) {
    EIntr();
    return;
  }
  _cmd_init_check = 1;
  _data_table.packet = (void*)((unsigned)_pckt_buffer | 0x20000000);
  _data_table.send_buffer = (void*)((unsigned)_send_buffer | 0x20000000);
  _data_table.iop_buffer = 0;
  _data_table.system = _sys_buffer;
  _data_table.system_count = 32;
  _data_table.user = 0;
  _data_table.user_count = 0;
  _data_table.registers = soft_reg;
  for (i = 0; i < 32; i++) {
    _sys_buffer[i].callback = 0;
    _sys_buffer[i].argument = 0;
  }
  for (i = 31; i >= 0; i--)
    soft_reg[i] = 0;
  _sys_buffer[0].callback = _change_addr;
  _sys_buffer[0].argument = &_data_table;
  _sys_buffer[1].callback = _set_sreg;
  _sys_buffer[1].argument = &_data_table;
  i = 32;
  EIntr();
  FlushCache(0);
  if (*(volatile unsigned*)0x1000e010 & 0x20)
    *(volatile unsigned*)0x1000e010 = i;
  if (!(*(volatile unsigned*)0x1000c000 & 0x100))
    sceSifSetDChain();
  sif0_handleid = AddDmacHandler(5, _sceSifCmdIntrHdlr, 0);
  EnableDmac(5);
  _data_table.iop_buffer = (void*)sceSifGetReg(0x80000000);
  if (_data_table.iop_buffer) {
    _csdata.address = _pckt_buffer;
    sceSifSendCmd(0x80000000, &_csdata, sizeof(_csdata), 0, 0, 0);
    return;
  } else {
    while (!(sceSifGetReg(4) & 0x20000)) {
    }
    _data_table.iop_buffer = (void*)sceSifGetReg(2);
    sceSifSetReg(0x80000000, (unsigned)_data_table.iop_buffer);
    sceSifSetReg(0x80000001, (unsigned)&_data_table);
    _csdata.address = _pckt_buffer;
    _csdata.header.option = 0;
    sceSifSendCmd(0x80000002, &_csdata, sizeof(_csdata), 0, 0, 0);
  }
}

void sceSifExitCmd(void) {
  DisableDmac(5);
  RemoveDmacHandler(5, sif0_handleid);
  _cmd_init_check = 0;
}

CommandHandler* sceSifSetCmdBuffer(CommandHandler* buffer, int count) {
  CommandHandler* previous = _data_table.user;
  _data_table.user = buffer;
  _data_table.user_count = count;
  return previous;
}

CommandHandler* sceSifSetSysCmdBuffer(CommandHandler* buffer, int count) {
  CommandHandler* previous = _data_table.system;
  _data_table.system = buffer;
  _data_table.system_count = count;
  return previous;
}

void sceSifAddCmdHandler(unsigned command, CommandCallback callback, void* argument) {
  register CommandHandler* handlers __asm__("$4");
  register unsigned offset __asm__("$3") = (command & 0x7fffffff) * sizeof(CommandHandler);
  if (command & 0x80000000)
    handlers = _data_table.system;
  else
    handlers = _data_table.user;
  offset += (unsigned)handlers;
  ((CommandHandler*)offset)->callback = callback;
  ((CommandHandler*)offset)->argument = argument;
}

void sceSifRemoveCmdHandler(unsigned command) {
  register CommandHandler* handlers __asm__("$4");
  register unsigned offset __asm__("$3") = (command & 0x7fffffff) * sizeof(CommandHandler);
  if (command & 0x80000000)
    handlers = _data_table.system;
  else
    handlers = _data_table.user;
  offset += (unsigned)handlers;
  ((CommandHandler*)offset)->callback = 0;
}

static unsigned _sceSifSendCmd(unsigned command,
                               unsigned mode,
                               void* packet,
                               int size,
                               void* source,
                               void* destination,
                               int data_size) {
  SifDmaTransfer transfer[2];
  int count;
  if ((unsigned)(size - 16) > 96)
    return 0;
  count = 0;
  if (data_size > 0) {
    ((SifCommandHeader*)packet)->data_size = data_size;
    transfer[count].source = (unsigned)source;
    transfer[count].destination = (unsigned)destination;
    ((SifCommandHeader*)packet)->destination = (unsigned)destination;
    transfer[count].size = data_size;
    transfer[count].attributes = 0;
    count++;
    if (mode & 4)
      sceSifWriteBackDCache(source, data_size);
  } else {
    ((SifCommandHeader*)packet)->data_size = 0;
    ((SifCommandHeader*)packet)->destination = 0;
  }
  transfer[count].source = (unsigned)packet;
  transfer[count].destination = (unsigned)_data_table.iop_buffer;
  transfer[count].size = size;
  ((SifCommandHeader*)packet)->command = command;
  ((SifCommandHeader*)packet)->packet_size = size;
  transfer[count].attributes = 0x44;
  count++;
  sceSifWriteBackDCache(packet, size);
  if (mode & 1)
    return isceSifSetDma(transfer, count);
  return sceSifSetDma(transfer, count);
}

unsigned sceSifSendCmd(unsigned command,
                       void* packet,
                       int size,
                       void* source,
                       void* destination,
                       int data_size) {
  return _sceSifSendCmd(command, 0, packet, size, source, destination, data_size);
}

unsigned isceSifSendCmd(unsigned command,
                        void* packet,
                        int size,
                        void* source,
                        void* destination,
                        int data_size) {
  return _sceSifSendCmd(command, 1, packet, size, source, destination, data_size);
}

static int _sceSifCmdIntrHdlr(int channel) {
  Quad copy[7];
  CommandState* state;
  register int flags __asm__("$3");
  register CommandCallback callback;
  register volatile SifCommandHeader* packet __asm__("$7");
  int value;
  int count;
  register int i __asm__("$4");
  register Quad* source __asm__("$6");
  Quad* destination;
  EIntr();
  state = &_data_table;
  packet = state->packet;
  value = packet->packet_size;
  if (!value)
    return 0;
  value = (value + 15) / 16;
  count = value;
  packet->packet_size = 0;
  source = (Quad*)packet;
  if (count > 0) {
    destination = copy;
    for (i = count; i; i--)
      *destination++ = *source++;
  }
  isceSifSetDChain();
  flags = ((volatile SifCommandHeader*)copy)->command;
  if (flags & 0x80000000) {
    value = ((volatile SifCommandHeader*)copy)->command & 0x7fffffff;
    if (value < state->system_count) {
      callback = state->system[value].callback;
      if (callback)
        callback(copy, state->system[value].argument);
    }
  } else {
    value = ((volatile SifCommandHeader*)copy)->command;
    if (value < state->user_count) {
      callback = state->user[value].callback;
      if (callback)
        callback(copy, state->user[value].argument);
    }
  }
  __asm__ volatile("sync");
  __asm__ volatile("ei");
  return 0;
}

void sceSifWriteBackDCache(void* address, int size) {
  __asm__ volatile(
      ".set noreorder\n"
      "lui $25,0xffff\n\tori $25,$25,0xffc0\n"
      "blez $5,last\n\taddu $10,$4,$5\n"
      "and $8,$4,$25\n\taddiu $10,$10,-1\n\tand $9,$10,$25\n"
      "subu $10,$9,$8\n\tsrl $11,$10,6\n\taddiu $11,$11,1\n"
      "andi $9,$11,7\n\tbeqz $9,eight\n\tsrl $10,$11,3\n"
      "loop1: sync\n\tcache 0x18,0($8)\n\tsync\n"
      "addiu $9,$9,-1\n\tnop\n\tbgtz $9,loop1\n\taddiu $8,$8,64\n"
      "eight: beqz $10,last\n"
      "loop8: addiu $10,$10,-1\n\tsync\n"
      "cache 0x18,0($8)\n\tsync\n\tcache 0x18,64($8)\n\tsync\n"
      "cache 0x18,128($8)\n\tsync\n\tcache 0x18,192($8)\n\tsync\n"
      "cache 0x18,256($8)\n\tsync\n\tcache 0x18,320($8)\n\tsync\n"
      "cache 0x18,384($8)\n\tsync\n\tcache 0x18,448($8)\n\tsync\n"
      "bgtz $10,loop8\n\taddiu $8,$8,512\n"
      "last: jr $31\n\tnop"
      :
      :
      : "$8", "$9", "$10", "$11", "$25", "memory");
}
