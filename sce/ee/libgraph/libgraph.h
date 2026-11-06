#ifndef SCE_EE_LIBGRAPH_LIBGRAPH_H
#define SCE_EE_LIBGRAPH_LIBGRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct GsPmode {
  unsigned EN1 : 1, EN2 : 1, CRTMD : 3, MMOD : 1, AMOD : 1, SLBG : 1, ALP : 8, reserved : 16;
  unsigned reserved_high;
} GsPmode;

typedef struct GsSmode2 {
  unsigned INT : 1, FFMD : 1, DPMS : 2, reserved : 28;
  unsigned reserved_high;
} GsSmode2;

typedef struct GsDispfb {
  unsigned FBP : 9, FBW : 6, PSM : 5, reserved_low : 12;
  unsigned DBX : 11, DBY : 11, reserved_high : 10;
} GsDispfb;

typedef struct GsDisplay {
  unsigned DX : 12, DY : 11, MAGH : 4, MAGV : 2, reserved_low : 3;
  unsigned DW : 12, DH : 11, reserved_high : 9;
} GsDisplay;

typedef struct GsBgcolor {
  unsigned char R, G, B, reserved[5];
} GsBgcolor;

// Display environment passed to libgraph.
typedef struct sceGsDispEnv {
  GsPmode pmode;
  GsSmode2 smode2;
  GsDispfb dispfb;
  GsDisplay display;
  GsBgcolor bgcolor;
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

#define GS_REGISTER_BASE 0x12000000
#define GS_INTERLACED 1
#define GS_NTSC 2
#define GS_PAL 3
#define GS_PSMCT32 0

#ifdef __cplusplus
}
#endif

#endif
