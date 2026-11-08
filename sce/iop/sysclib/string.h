#ifndef _STRING_H
#define _STRING_H

#include "sce/iop/types.h"

extern char* strcat(char* dest, const char* src);
extern int strncmp(const char* s1, const char* s2, size_t n);

void bzero(void* destination, size_t size);

void bcopy(const void* source, void* destination, size_t size);

void* memmove(void* dest, const void* src, size_t size);

void* memset(void* dest, int value, size_t size);

void* memcpy(void* dest, const void* src, size_t size);

int strcmp(const char* left, const char* right);
char* strcpy(char* destination, const char* source);
char* strncpy(char* destination, const char* source, size_t count);
size_t strlen(const char* text);

int memcmp(const void* left, const void* right, size_t count);
#endif
