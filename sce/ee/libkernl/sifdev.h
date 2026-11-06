#ifndef SCE_EE_LIBKERNL_SIFDEV_H
#define SCE_EE_LIBKERNL_SIFDEV_H

#ifdef __cplusplus
extern "C" {
#endif

int sceOpen(const char* filename, int flag, ...);
int sceClose(int fd);
int sceRead(int fd, void* buf, int nbyte);

#define SCE_RDONLY 1
#define SCE_WRONLY 2
#define SCE_CREAT 0x200
#define SCE_TRUNC 0x400
#define SCE_SEEK_CUR 1
#define SCE_SEEK_SET 0
#define SCE_SEEK_END 2
int sceLseek(int, int, int);
int sceWrite(int, const void*, int);

#ifdef __cplusplus
}
#endif

#endif
