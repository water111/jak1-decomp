#ifndef NEXT_KERNEL_KMALLOC_H
#define NEXT_KERNEL_KMALLOC_H

struct kheapinfo {
  char* base;
  char* top;
  char* current;
  char* top_base;
};

extern kheapinfo* kglobalheap;
extern kheapinfo* kdebugheap;
extern kheapinfo* klinkheap;
extern kheapinfo* ksystemheap;

#define KMALLOC_TOP 0x2000
#define KMALLOC_MEMSET 0x1000
#define KMALLOC_ALIGN_16 0x10
#define KMALLOC_ALIGN_64 0x40
#define KMALLOC_ALIGN_256 0x100

char* kmalloc(kheapinfo* heap, int size, unsigned int flags, const char* name);
kheapinfo* kinitheap(kheapinfo*, void*, int);
void kfree(void*);
kheapinfo* kheapstatus(kheapinfo*);
unsigned int kheapused(kheapinfo*);
char* ksmalloc(void*, int, unsigned int, const char*);

#endif
