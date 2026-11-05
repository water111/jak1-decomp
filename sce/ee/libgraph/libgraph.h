#ifndef SCE_EE_LIBGRAPH_LIBGRAPH_H
#define SCE_EE_LIBGRAPH_LIBGRAPH_H

#define SCE_GS_PSMCT32 0
#define SCE_GS_PSMCT24 1
#define SCE_GS_PSMCT16 2
#define SCE_GS_PSMCT16S 10
#define SCE_GS_PSMT8 19
#define SCE_GS_PSMT4 20
#define SCE_GS_PSMT8H 27
#define SCE_GS_PSMT4HL 36
#define SCE_GS_PSMT4HH 44
#define SCE_GS_PSMZ32 48
#define SCE_GS_PSMZ24 49
#define SCE_GS_PSMZ16 50
#define SCE_GS_PSMZ16S 58

typedef unsigned long GsWord;
typedef unsigned int GsQuad __attribute__((mode(TI)));

typedef struct {
  short interlace, output, frame, version;
  volatile int (*callback)(int);
  int callback_id;
} sceGsGParam __attribute__((aligned(16)));

typedef struct {
  GsWord pmode, smode2, dispfb, display, bgcolor;
} sceGsDispEnv;

typedef struct {
  GsWord loops : 15;
  GsWord end : 1;
  GsWord reserved : 30;
  GsWord pre : 1;
  GsWord primitive : 11;
  GsWord format : 2;
  GsWord count : 4;
  GsWord reg : 4;
  GsWord other_regs : 60;
} GsTag __attribute__((aligned(16)));

typedef struct {
  GsTag setup;
  GsWord bitbltbuf, bitbltbuf_address;
  GsWord position, position_address;
  GsWord dimensions, dimensions_address;
  GsWord direction, direction_address;
  GsTag image;
} sceGsLoadImage __attribute__((aligned(16)));

void sceGsResetGraph(short mode, short inter, short omode, short ffmode);
sceGsGParam* sceGsGetGParam(void);
void sceGsResetPath(void);
void sceGsPutDispEnv(sceGsDispEnv* env);
int sceGsSyncV(int mode);
int sceGsSyncPath(int mode, unsigned short timeout);
GsWord sceGsPutIMR(GsWord mask);
GsWord sceGsGetIMR(void);

int sceGsSetDefLoadImage(sceGsLoadImage* packet,
                         short dbp,
                         short dbw,
                         short dpsm,
                         short x,
                         short y,
                         short w,
                         short h);
int sceGsExecLoadImage(GsQuad* packet, GsQuad* source);
int sceGsExecStoreImage(GsQuad* packet, GsQuad* destination);

#endif
