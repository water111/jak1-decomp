#ifndef _STDIO_H
#define _STDIO_H

#include "sce/iop/types.h"

extern int open(const char* filename, int flag, /* u_int mode */...);
extern int sprintf(char* buf, const char* fmt, ...);
extern int printf(const char* fmt, ...);
extern int read(int fd, void* buf, size_t nbyte);
extern int close(int fd);

#endif
