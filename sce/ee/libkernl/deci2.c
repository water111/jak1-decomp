extern int Deci2Call(int, unsigned*);

static unsigned userarea[9];

int sceDeci2Open(unsigned short protocol, void* option, void (*handler)(int, int, void*)) {
  unsigned args[4];
  args[0] = protocol;
  args[1] = (unsigned)option;
  args[2] = (unsigned)handler;
  args[3] = (unsigned)userarea | 0x20000000;
  return Deci2Call(1, args);
}

int sceDeci2Close(int socket) {
  unsigned args[1];
  args[0] = socket;
  return Deci2Call(2, args);
}

int sceDeci2ReqSend(int socket, char destination) {
  unsigned args[2];
  args[0] = socket;
  args[1] = destination;
  return Deci2Call(3, args);
}

void sceDeci2Poll(int socket) {
  unsigned args[1];
  args[0] = socket;
  Deci2Call(4, args);
}

int sceDeci2ExRecv(int socket, void* buffer, unsigned short size) {
  unsigned args[3];
  args[0] = socket;
  args[1] = (unsigned)buffer;
  args[2] = size;
  return Deci2Call(-5, args);
}

int sceDeci2ExSend(int socket, void* buffer, unsigned short size) {
  unsigned args[3];
  args[0] = socket;
  args[1] = (unsigned)buffer;
  args[2] = size;
  return Deci2Call(-6, args);
}

int sceDeci2ExReqSend(int socket, char destination) {
  unsigned args[2];
  args[0] = socket;
  args[1] = destination;
  return Deci2Call(-7, args);
}

int sceDeci2ExLock(int socket) {
  unsigned args[1];
  args[0] = socket;
  return Deci2Call(-8, args);
}

int sceDeci2ExUnLock(int socket) {
  unsigned args[1];
  args[0] = socket;
  return Deci2Call(-9, args);
}

int kputs(const char* text) {
  unsigned args[1];
  args[0] = (unsigned)text;
  return Deci2Call(16, args);
}
