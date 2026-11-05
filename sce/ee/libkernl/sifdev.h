#ifndef SCE_EE_LIBKERNL_SIFDEV_H
#define SCE_EE_LIBKERNL_SIFDEV_H

int sceOpen(const char* filename, int flag, ...);
int sceClose(int fd);
int sceRead(int fd, void* buf, int nbyte);

#endif
