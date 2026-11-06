#ifndef SCE_EE_LIBKERNL_DECI2_H
#define SCE_EE_LIBKERNL_DECI2_H

#ifdef __cplusplus
extern "C" {
#endif

// Events delivered to a registered DECI2 protocol handler.
enum Deci2Event {
  DECI2_READ = 1,
  DECI2_READDONE = 2,
  DECI2_WRITE = 3,
  DECI2_WRITEDONE = 4,
  DECI2_CHSTATUS = 5,
  DECI2_ERROR = 6
};

// Register and close a protocol, request a send, and transfer event data.
int sceDeci2Open(unsigned short protocol, void* opt, void (*handler)(int, int, void*));
int sceDeci2Close(int socket);
int sceDeci2ReqSend(int socket, char destination);
int sceDeci2ExRecv(int socket, void* buffer, unsigned short size);
int sceDeci2ExSend(int socket, void* buffer, unsigned short size);

#ifdef __cplusplus
}
#endif

#endif
