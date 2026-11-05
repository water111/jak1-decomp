#ifndef SCE_EE_LIBDMA_LIBDMA_H
#define SCE_EE_LIBDMA_LIBDMA_H

typedef struct {
  unsigned direction : 1;
  unsigned reserved0 : 1;
  unsigned mode : 2;
  unsigned stack : 2;
  unsigned tag_transfer : 1;
  unsigned tag_interrupt : 1;
  unsigned start : 1;
  unsigned reserved1 : 7;
  unsigned tag : 16;
} DmaControl;

typedef struct {
  DmaControl chcr;
  unsigned pad0[3];
  void* madr;
  unsigned pad1[3];
  unsigned qwc;
  unsigned pad2[3];
  void* tadr;
  unsigned pad3[3];
  void* as0;
  unsigned pad4[3];
  void* as1;
  unsigned pad5[11];
  void* sadr;
  unsigned pad6[3];
} sceDmaChan;

typedef struct {
  unsigned char sts, std, mfd, rcycle;
  unsigned short express, notify, sqwc, tqwc;
  void* rbadr;
  unsigned rbmsk;
} sceDmaEnv;

#ifdef __cplusplus
extern "C" {
#endif

int sceDmaReset(int mode);
int sceDmaDebug(int mode);
int sceDmaPutEnv(sceDmaEnv* env);
sceDmaEnv* sceDmaGetEnv(sceDmaEnv* env);
void* sceDmaPutStallAddr(void* addr);
sceDmaChan* sceDmaGetChan(int id);
void sceDmaSend(sceDmaChan* channel, void* tag);
void sceDmaSendN(sceDmaChan* channel, void* addr, int size);
void sceDmaSendI(sceDmaChan* channel, void* addr, int size);
void sceDmaRecv(sceDmaChan* channel);
void sceDmaRecvN(sceDmaChan* channel, void* addr, int size);
void sceDmaRecvI(sceDmaChan* channel, void* addr, int size);
int sceDmaSync(sceDmaChan* channel, int mode, int timeout);
int sceDmaWatch(sceDmaChan* channel, void* addr, int mode, int timeout);
unsigned sceDmaPause(sceDmaChan* channel);
int sceDmaRestart(sceDmaChan* channel, unsigned chcr);

#ifdef __cplusplus
}
#endif

#endif
