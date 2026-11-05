#include "sce/ee/libdma/libdma.h"
#include "sce/ee/eeregs.h"

#include "sce/ee/newlib/stdio.h"

#define REG(address) (*(volatile unsigned*)(address))
#define CHANNEL(d) (*(volatile sceDmaChan*)(d))

sceDmaChan* dch[10] = {(sceDmaChan*)D0_CHCR, (sceDmaChan*)D1_CHCR, (sceDmaChan*)D2_CHCR,
                       (sceDmaChan*)D3_CHCR, (sceDmaChan*)D4_CHCR, (sceDmaChan*)D5_CHCR,
                       (sceDmaChan*)D6_CHCR, (sceDmaChan*)D7_CHCR, (sceDmaChan*)D8_CHCR,
                       (sceDmaChan*)D9_CHCR};
int sceDmaDebugMode = 0;

static void memclr(void* addr, int size) {
  unsigned char* p = addr;
  while (size--)
    *p++ = 0;
}

static char __ps2_libinfo__[16] = "PsIIlibdma  2200";
static int isclr[10] = {1, 1, 1, 1, 1, 0, 0, 0, 1, 1};
static unsigned char ststbl[10] = {0, 0, 0, 3, 0, 1, 0, 0, 2, 0};
static unsigned char stdtbl[10] = {0, 1, 2, 0, 0, 0, 3, 0, 0, 0};
static unsigned char mfdtbl[10] = {0, 2, 3, 0, 0, 0, 0, 0, 0, 0};
sceDmaEnv sceDmaCurrentEnv = {0};

sceDmaChan* sceDmaGetChan(int id) {
  if ((unsigned)id < 10)
    return dch[id];
  return 0;
}

int sceDmaReset(int mode) {
  sceDmaEnv env;
  int enabled = (*D_CTRL) & 1;
  int i;
  for (i = 0; i < 10; ++i) {
    if (isclr[i]) {
      volatile sceDmaChan* d = dch[i];
      d->sadr = 0;
      *(volatile unsigned*)&d->chcr = 0;
      d->tadr = 0;
      d->madr = 0;
      d->as1 = 0;
      d->as0 = 0;
    }
  }
  (*D_STAT) = 0xff1f;
  (*D_STAT) &= 0xff1f0000;
  memclr(&env, sizeof(env));
  sceDmaPutEnv(&env);
  if (mode == 1)
    (*D_CTRL) |= 1;
  return enabled;
}

int sceDmaDebug(int mode) {
  int old = sceDmaDebugMode;
  sceDmaDebugMode = mode;
  return old;
}

int sceDmaPutEnv(sceDmaEnv* env) {
  unsigned ctrl = (*D_CTRL);
  unsigned pcr = (*D_PCR);
  unsigned sqwc = (*D_SQWC);
  unsigned rbadr = (*D_RBOR);
  unsigned rbmsk = (*D_RBSR);
  if (env->sts >= 10)
    return -1;
  if (env->std >= 10)
    return -2;
  if (env->mfd >= 10)
    return -3;
  if (env->rcycle >= 7)
    return -4;
  ctrl = (ctrl & ~0x30) | (ststbl[env->sts] << 4);
  ctrl = (ctrl & ~0xc0) | (stdtbl[env->std] << 6);
  ctrl = (ctrl & ~0x0c) | (mfdtbl[env->mfd] << 2);
  if (env->rcycle) {
    ctrl |= 2;
    ctrl = (ctrl & ~0x300) | ((env->rcycle - 1) << 8);
  } else
    ctrl &= ~2;
  pcr = (env->express << 16) | env->notify;
  sqwc = (env->tqwc << 16) | env->sqwc;
  rbadr = (unsigned)env->rbadr;
  rbmsk = env->rbmsk;
  (*D_CTRL) = ctrl;
  (*D_PCR) = pcr;
  (*D_SQWC) = sqwc;
  (*D_RBOR) = rbadr;
  (*D_RBSR) = rbmsk;
  sceDmaCurrentEnv = *env;
  return 0;
}

sceDmaEnv* sceDmaGetEnv(sceDmaEnv* env) {
  *env = sceDmaCurrentEnv;
  return env;
}

void* sceDmaPutStallAddr(void* addr) {
  void* old = (void*)(*D_STADR);
  if (addr != (void*)-1)
    (*D_STADR) = (unsigned)addr;
  return old;
}

#define WAIT(d, timeout, condition)     \
  while (condition) {                   \
    if (--timeout < 0) {                \
      DmaControl control;               \
      printf("libdma: sync timeout\n"); \
      control = CHANNEL(d).chcr;        \
      while (control.start) {           \
        control.start = 0;              \
        d->chcr = control;              \
      }                                 \
    }                                   \
  }

#define START(d, mode_value, direction_value) \
  {                                           \
    DmaControl control = CHANNEL(d).chcr;     \
    control.mode = mode_value;                \
    control.direction = direction_value;      \
    control.start = 1;                        \
    CHANNEL(d).chcr = control;                \
  }

void sceDmaSend(sceDmaChan* d, void* tag) {
  int timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  if (d->tadr != (void*)-1)
    d->tadr = tag;
  d->qwc = 0;
  START(d, 1, 1);
}

void sceDmaSendN(sceDmaChan* d, void* addr, int size) {
  int timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  if (d->madr != (void*)-1)
    d->madr = addr;
  d->qwc = size;
  START(d, 0, 1);
}

void sceDmaSendI(sceDmaChan* d, void* addr, int size) {
  int timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  if (d->madr != (void*)-1)
    d->madr = addr;
  d->qwc = size;
  START(d, 2, 1);
}

void sceDmaRecv(sceDmaChan* d) {
  int timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  d->qwc = 0;
  START(d, 1, 0);
}

void sceDmaRecvN(sceDmaChan* d, void* addr, int size) {
  int timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  if (d->madr != (void*)-1)
    d->madr = addr;
  d->qwc = size;
  START(d, 0, 0);
}

void sceDmaRecvI(sceDmaChan* d, void* addr, int size) {
  int timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  if (d->madr != (void*)-1)
    d->madr = addr;
  d->qwc = size;
  START(d, 2, 0);
}

int sceDmaSync(sceDmaChan* d, int mode, int timeout) {
  if (mode == 1)
    return (REG(d) >> 8) & 1;
  if (!timeout)
    timeout = 0x1000000;
  WAIT(d, timeout, REG(d) & 0x100);
  return 0;
}

int sceDmaWatch(sceDmaChan* d, void* addr, int mode, int timeout) {
  if (mode == 1)
    return CHANNEL(d).madr < addr;
  if (!timeout)
    timeout = 0x1000000;
  WAIT(d, timeout, CHANNEL(d).madr < addr);
  return 0;
}

unsigned sceDmaPause(sceDmaChan* d) {
  DmaControl old = CHANNEL(d).chcr;
  DmaControl control = old;
  control.start = 0;
  d->chcr = control;
  return old.start;
}

int sceDmaRestart(sceDmaChan* d, unsigned chcr) {
  DmaControl old = CHANNEL(d).chcr;
  DmaControl control = old;
  control.start = 0;
  d->chcr = control;
  return old.start;
}
