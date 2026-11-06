extern int DIntr(void);
extern int EIntr(void);

#define CACHE(op, way, index) \
  __asm__ volatile("sync\n\tcache " #op "," #way "(%0)\n\tsync" : : "r"(index) : "memory")
#define CACHE_WAY(op, way)                     \
  CACHE(0x10, way, index);                     \
  __asm__ volatile("mfc0 %0,$28" : "=r"(tag)); \
  address = (tag & 0xfffff000U) + index;       \
  if (address >= start && address <= end) {    \
    CACHE(op, way, index);                     \
  }
#define CACHE_SCAN(name, op)                     \
  void name(unsigned start, unsigned end) {      \
    int index;                                   \
    unsigned tag, address;                       \
    for (index = 0; index < 4096; index += 64) { \
      CACHE_WAY(op, 0);                          \
      CACHE_WAY(op, 1);                          \
      __asm__ volatile("sync");                  \
    }                                            \
  }
#define CACHE_WRAPPERS(name, scan)                                    \
  void name(void* start, void* end) {                                 \
    unsigned status;                                                  \
    __asm__ volatile("mfc0 %0,$12" : "=r"(status));                   \
    status &= 0x10000;                                                \
    if (status)                                                       \
      DIntr();                                                        \
    scan((unsigned)start & 0xffffffc0U, (unsigned)end & 0xffffffc0U); \
    if (status)                                                       \
      EIntr();                                                        \
  }                                                                   \
  void i##name(void* start, void* end) {                              \
    scan((unsigned)start & 0xffffffc0U, (unsigned)end & 0xffffffc0U); \
  }

CACHE_SCAN(_sceSDC, 0x14)
CACHE_WRAPPERS(SyncDCache, _sceSDC)
CACHE_SCAN(_sceIDC, 0x16)
CACHE_WRAPPERS(InvalidDCache, _sceIDC)
