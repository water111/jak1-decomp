__asm__(".local srcfile,eenull,setup,Copy,kCopy,SysEntry,GetEntryAddress");

extern int DIntr(void), EIntr(void);

#define INTERRUPT_WRAPPER(name)                     \
  extern int _##name(int);                          \
  int name(int channel) {                           \
    unsigned status;                                \
    int result;                                     \
    __asm__ volatile("mfc0 %0,$12" : "=r"(status)); \
    status &= 0x10000;                              \
    if (status)                                     \
      DIntr();                                      \
    result = _##name(channel);                      \
    __asm__ volatile("sync");                       \
    if (status)                                     \
      EIntr();                                      \
    return result;                                  \
  }

INTERRUPT_WRAPPER(DisableIntc)
INTERRUPT_WRAPPER(EnableIntc)
INTERRUPT_WRAPPER(DisableDmac)
INTERRUPT_WRAPPER(EnableDmac)

#define INTERRUPT_SAFE_WRAPPER(name) \
  extern int _##name(int);           \
  int name(int channel) {            \
    int result = _##name(channel);   \
    __asm__ volatile("sync");        \
    return result;                   \
  }

INTERRUPT_SAFE_WRAPPER(iEnableIntc)
INTERRUPT_SAFE_WRAPPER(iDisableIntc)
INTERRUPT_SAFE_WRAPPER(iEnableDmac)
INTERRUPT_SAFE_WRAPPER(iDisableDmac)

#define SYSCALL(name, number)                                       \
  __asm__(".text\n.set noreorder\n.align 3\n.ent " #name "\n" #name \
          ":\n.frame $sp,0,$31\n"                                   \
          "li $3," #number "\nsyscall\njr $31\nnop\n.end " #name "\n.set reorder");

SYSCALL(setup, 116)
SYSCALL(Copy, 90)

static int kCopy(unsigned* destination, const unsigned* source, unsigned size) {
  unsigned i;
  size >>= 2;
  for (i = 0; i < size; i++)
    *destination++ = *source++;
  return 0;
}

SYSCALL(GetEntryAddress, 91)

extern void setup(int, void*);
extern void Copy(void*, const void*, unsigned);
extern void* GetEntryAddress(int);
extern void FlushCache(int);
extern unsigned char eenull[32];

#include ALARM_PAYLOAD

__asm__(
    ".data\n.align 3\n.set noreorder\n"
    ".type eenull,@object\n.size eenull,32\neenull:\n"
    "lui $29,0x8\njalr $3\naddiu $29,$29,0x1fc0\n"
    "li $3,-8\nsyscall\nnop\nnop\nnop\n.set reorder\n.data");

static struct {
  int number;
  void* address;
} SysEntry[] = {{90, kCopy}, {91, (void*)0x80076000}, {24, 0}, {30, 0}, {25, 0}, {31, 0}, {300, 0},
                {8, 0}};

void InitAlarm(void) {
  unsigned i;
  if (!(*(volatile unsigned*)0x10001810 & 0x100)) {
    setup(SysEntry[0].number, SysEntry[0].address);
    Copy((void*)0x80076000, srcfile, sizeof(srcfile));
    Copy((void*)0x82000, eenull, sizeof(eenull));
    FlushCache(0);
    FlushCache(2);
    setup(SysEntry[1].number, SysEntry[1].address);
    for (i = 2; i < 8; i++)
      setup(SysEntry[i].number, GetEntryAddress(SysEntry[i].number));
  }
}
