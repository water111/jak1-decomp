#ifndef _STRING_H
#define _STRING_H

#include "sce/iop/types.h"

extern char* strcat(char* dest, const char* src);
extern int strncmp(const char* s1, const char* s2, size_t n);

void bcopy(const void* source, void* destination, size_t size);

void* memmove(void* dest, const void* src, size_t size);

void* memset(void* dest, int value, size_t size);

void* memcpy(void* dest, const void* src, size_t size);

#endif
