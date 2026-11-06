extern int scePrintf(const char*, ...);
extern void SetVTLBRefillHandler(int, void*);
extern void SetVCommonHandler(int, void*);
extern void _kTLBException(void), _kDebugException(void), _kExitTLBHandler(void);
void* _kTLBRefillHandler = 0;
void* _kDebugHandler[16] = {0};

#define READ_CP0(reg, value) __asm__ volatile("mfc0 %0,$" #reg : "=r"(value))
#define WRITE_CP0(reg, value) __asm__ volatile("mtc0 %0,$" #reg ::"r"(value))
#define TLB(op) __asm__ volatile("sync.p\n" #op "\nsync.p")

void _DumpTLB(void) {
  int i, wired;
  unsigned mask, hi, lo0, lo1;
  READ_CP0(6, wired);
  for (i = 0; i < 48; i++) {
    if (i == wired)
      scePrintf("=========================<wired=%02d>=========================\n", i);
    WRITE_CP0(0, i);
    TLB(tlbr);
    READ_CP0(2, lo0);
    READ_CP0(3, lo1);
    READ_CP0(5, mask);
    READ_CP0(10, hi);
    scePrintf("idx=%02d: pm=%08x, hi=%08x, lo0=%08x, lo1=%08x\n", i, mask, hi, lo0, lo1);
  }
}

static int kPutTLBEntry(unsigned mask, unsigned hi, unsigned lo0, unsigned lo1) {
  unsigned segment = hi >> 24;
  int index;
  switch (segment) {
    case 0:
    case 0x20:
    case 0x30:
      if ((segment & 15) < 8)
        return -1;
      break;
    case 0x10:
    case 0x50:
    case 0x60:
    case 0x70:
      return -1;
    case 0x40:
      break;
  }
  WRITE_CP0(5, mask);
  WRITE_CP0(10, hi);
  WRITE_CP0(2, lo0);
  WRITE_CP0(3, lo1);
  TLB(tlbwr);
  __asm__ volatile("tlbp\nsync.p");
  READ_CP0(0, index);
  return index;
}

static int kSetTLBEntry(int index, unsigned mask, unsigned hi, unsigned lo0, unsigned lo1) {
  int wired;
  READ_CP0(6, wired);
  if (index < wired || index >= 48)
    return -1;
  WRITE_CP0(0, index);
  WRITE_CP0(5, mask);
  WRITE_CP0(10, hi);
  WRITE_CP0(2, lo0);
  WRITE_CP0(3, lo1);
  TLB(tlbwi);
  return index;
}

static int kGetTLBEntry(unsigned index,
                        unsigned* mask,
                        unsigned* hi,
                        unsigned* lo0,
                        unsigned* lo1) {
  if (index >= 48)
    return -1;
  WRITE_CP0(0, index);
  TLB(tlbr);
  READ_CP0(5, *mask);
  READ_CP0(10, *hi);
  READ_CP0(2, *lo0);
  READ_CP0(3, *lo1);
  return index;
}

static int kProbeTLBEntry(unsigned hi, unsigned* mask, unsigned* lo0, unsigned* lo1) {
  int index;
  WRITE_CP0(10, hi);
  TLB(tlbp);
  READ_CP0(0, index);
  if (index < 0)
    index = -1;
  else {
    __asm__ volatile("tlbr\nsync.p");
    READ_CP0(5, *mask);
    READ_CP0(2, *lo0);
    READ_CP0(3, *lo1);
  }
  return index;
}

static int kExpandScratchPad(unsigned address) {
  unsigned entry[3];
  unsigned hi;
  register int index __asm__("$5");
  int wired;
  if ((address & 4095) || address - 1 <= 0xffffe)
    return -1;
  index = kProbeTLBEntry(0x70004000, &entry[0], &entry[1], &entry[2]);
  if (index < 0) {
    if (!address)
      return 0;
    READ_CP0(6, index);
    wired = index + 1;
    WRITE_CP0(6, wired);
    __asm__ volatile("sync.p");
  } else if (!address) {
    hi = ((index - 1) << 13) + 0xe0010000;
    READ_CP0(6, wired);
    wired--;
    WRITE_CP0(6, wired);
    WRITE_CP0(0, index);
    __asm__ volatile("mtc0 $0,$5");
    WRITE_CP0(10, hi);
    __asm__ volatile("mtc0 $0,$2\nmtc0 $0,$3");
    TLB(tlbwi);
    return 0;
  }
  entry[0] = 0;
  entry[1] = ((address & 0xfffff000) >> 6) | 31;
  entry[2] = (((address + 4096) & 0xfffff000) >> 6) | 31;
  hi = 0x70004000;
  WRITE_CP0(0, index);
  __asm__ volatile("mtc0 %0,$5\nmtc0 %1,$10\nmtc0 %2,$2\nmtc0 %3,$3" ::"r"(entry[0]), "r"(hi),
                   "r"(entry[1]), "r"(entry[2]));
  TLB(tlbwi);
  return index;
}

void* SetTLBHandler(void* handler) {
  _kTLBRefillHandler = handler;
  SetVTLBRefillHandler(1, _kTLBException);
  SetVTLBRefillHandler(2, _kTLBException);
  SetVTLBRefillHandler(3, _kTLBException);
  return handler;
}

void* SetDebugHandler(unsigned cause, void* handler) {
  void* old;
  if (cause - 1 >= 13)
    return (void*)0xffffffff;
  old = _kDebugHandler[cause];
  _kDebugHandler[cause] = handler;
  if (cause - 1 < 3)
    SetVTLBRefillHandler(cause, _kDebugException);
  else
    SetVCommonHandler(cause, _kDebugException);
  return old;
}

#define SYSCALL(name, number)                                       \
  __asm__(".text\n.align 3\n.set noreorder\n.ent " #name "\n" #name \
          ":\n.frame $sp,0,$31\n"                                   \
          "li $3," #number "\nsyscall\njr $31\nnop\n.end " #name "\n.set reorder");
__asm__(".local SysEntry");
SYSCALL(setup, 116)
extern void setup(int, void*);

static struct {
  int number;
  void* address;
} SysEntry[] = {{84, _kExitTLBHandler}, {85, kPutTLBEntry},   {86, kSetTLBEntry},
                {87, kGetTLBEntry},     {88, kProbeTLBEntry}, {89, kExpandScratchPad}};

void InitTLBFunctions(void) {
  unsigned i;
  for (i = 0; i < 6; i++)
    setup(SysEntry[i].number, SysEntry[i].address);
}

__asm__(
    ".globl PutTLBEntry\n.globl iPutTLBEntry\n.globl SetTLBEntry\n.globl iSetTLBEntry\n.globl "
    "GetTLBEntry\n.globl iGetTLBEntry\n.globl ProbeTLBEntry\n.globl iProbeTLBEntry\n.globl "
    "ExpandScratchPad");
SYSCALL(PutTLBEntry, 85)
SYSCALL(iPutTLBEntry, -85)
SYSCALL(SetTLBEntry, 86)
SYSCALL(iSetTLBEntry, -86)
SYSCALL(GetTLBEntry, 87)
SYSCALL(iGetTLBEntry, -87)
SYSCALL(ProbeTLBEntry, 88)
SYSCALL(iProbeTLBEntry, -88)
SYSCALL(ExpandScratchPad, 89)
