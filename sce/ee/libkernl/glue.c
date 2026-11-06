#include "sce/ee/newlib/sys/stat.h"

extern int sceTtyInit(void);
extern int sceTtyRead(void*, int);
extern int sceTtyWrite(const void*, int);
extern void SetVSyncFlag(volatile int*, volatile unsigned long*);
extern void* EndOfHeap(void);
extern int* __errno(void);
extern void Exit(int);
extern char _end[];

static int ttyinit = 0;

void sceResetttyinit(void) {
  ttyinit = 0;
  sceTtyInit();
}

#define INTC_STAT (*(volatile unsigned*)0x1000f000)

void VSync(void) {
  INTC_STAT = 4;
  while (1) {
    unsigned status;
    __asm__ volatile("lw %0,0x1000f000" : "=r"(status));
    if (status & 4)
      break;
  }
  __asm__ volatile("sw %0,0x1000f000" : : "r"(4) : "memory");
}

unsigned long VSync2(void) {
  struct {
    volatile int ready;
    volatile unsigned long field;
  } sync;

  sync.ready = 0;
  SetVSyncFlag(&sync.ready, &sync.field);
  INTC_STAT = 4;
  while (1) {
    unsigned status;
    __asm__ volatile("lw %0,0x1000f000" : "=r"(status));
    if ((status & 4) || sync.ready)
      break;
  }
  __asm__("sw %1,0x1000f000" : "=X"(INTC_STAT) : "r"(4));
  return sync.field;
}

int write(int fd, const void* buffer, int size) {
  if (fd == 1 || fd == 2) {
    if (!ttyinit) {
      if (!sceTtyInit())
        return -1;
      ttyinit = 1;
    }
    return sceTtyWrite(buffer, size);
  }
  return -1;
}

int read(int fd, void* buffer, int size) {
  if (fd == 0) {
    if (!ttyinit) {
      if (!sceTtyInit())
        return -1;
      ttyinit = 1;
    }
    return sceTtyRead(buffer, size);
  }
  return -1;
}

int open(const char* path, int flags, int mode) {
  *__errno() = 5;
  return -1;
}

int close(int fd) {
  return -1;
}

int ioctl(int fd, int request, void* arg) {
  return -1;
}

long lseek(int fd, long offset, int whence) {
  return -1;
}

void* sbrk(int size) {
  static char* heap_ptr = _end;
  register unsigned status __asm__("$17");
  char* next;
  char* previous;
  __asm__ volatile("mfc0 %0,$12" : "=r"(status));
  status &= 0x10000;
  if (status) {
    register unsigned current __asm__("$2");
    register unsigned mask __asm__("$3");
    do {
      __asm__ volatile("di");
      __asm__ volatile("sync.p");
      __asm__ volatile("mfc0 %0,$12" : "=r"(current) : : "$3");
      mask = 0x10000;
      current &= mask;
    } while (current);
  }
  next = heap_ptr + size;
  if (next > (char*)EndOfHeap()) {
    *__errno() = 12;
    if (status)
      __asm__ volatile("ei");
    return (void*)-1;
  }
  previous = heap_ptr;
  heap_ptr = next;
  if (status)
    __asm__ volatile("ei");
  return previous;
}

int isatty(int fd) {
  return 1;
}

int fstat(int fd, struct stat* st) {
  st->st_mode = 0x2000;
  st->st_blksize = 0;
  return 0;
}

int getpid(void) {
  return 1;
}

int kill(int pid, int signal) {
  if (pid == 1)
    Exit(signal);
  return 0;
}

int stat(const char* path, struct stat* st) {
  *__errno() = 5;
  return -1;
}

int unlink(const char* path) {
  *__errno() = 5;
  return -1;
}
