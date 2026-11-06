#include "sce/ee/eeasm.h"
#include "sce/ee/eeregs.h"
#include "sce/ee/libgraph/libgraph.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/newlib/stdio.h"

#define REG64(address) (*(volatile GsWord*)(address))

#ifdef F_resetgraph
static char __ps2_libinfo__[16] = "PsIIlibgraph2200";

void sceGsResetGraph(short mode, short inter, short omode, short ffmode) {
  sceGsGParam* param;
  switch (mode) {
    case 0:
      param = sceGsGetGParam();
      (*GS_CSR) = 0x200;
      param->interlace = inter;
      param->output = omode;
      param->version = (unsigned short)(((*GS_CSR) >> 16) & 0xff);
      GsPutIMR(0xff00);
      param->frame = (ffmode == 0) ? 0 : 1;
      if (param->callback) {
        DisableIntc(2);
        RemoveIntcHandler(2, param->callback_id);
        param->callback = 0;
        param->callback_id = 0;
      }
      SetGsCrt(inter & 1, omode & 0xff, ffmode & 1);
      break;
    case 1:
      (*GS_CSR) = 0x100;
      break;
  }
}

sceGsGParam* sceGsGetGParam(void) {
  static sceGsGParam gp = {1, 2, 1, 3, 0, 0};
  return &gp;
}
#endif

#ifdef F_resetpath
void sceGsResetPath(void) {
  static unsigned init_vif_regs[8] __attribute__((aligned(16))) = {
      0x01000404, 0x20000000, 0, 0x05000000, 0x06000000, 0x03000000, 0x02000000, 0x04000000};
  register unsigned status __asm__("$4") = 0;
  (*VIF1_FBRST) = 1;
  (*VIF1_ERR) = 2;
  EE_RESET_VU1(status);
  (*VIF1_FIFO) = ((GsQuad*)init_vif_regs)[0];
  (*VIF1_FIFO) = ((GsQuad*)init_vif_regs)[1];
  (*GIF_CTRL) = 1;
}
#endif

#ifdef F_putdispenv
void sceGsPutDispEnv(sceGsDispEnv* env) {
  if (sceGsGetGParam()->version == 1) {
    (*GS_PMODE) = *(GsWord*)&env->pmode;
    (*GS_DISPFB1) = *(GsWord*)&env->dispfb;
    (*GS_DISPLAY1) = *(GsWord*)&env->display;
    (*GS_EXTDATA) = *(GsWord*)&env->bgcolor;
  } else {
    (*GS_PMODE) = *(GsWord*)&env->pmode;
    (*GS_SMODE2) = *(GsWord*)&env->smode2;
    (*GS_DISPFB2) = *(GsWord*)&env->dispfb;
    (*GS_DISPLAY2) = *(GsWord*)&env->display;
    (*GS_BGCOLOR) = *(GsWord*)&env->bgcolor;
  }
}
#endif

#ifdef F_syncv
int sceGsSyncV(int mode) {
  sceGsGParam* param = sceGsGetGParam();
  if (!param->callback) {
    VSync();
    if (param->interlace == 1)
      return ((*GS_CSR) >> 13) & 1;
  } else {
    long field = (VSync2() >> 13) & 1;
    if (param->interlace == 1)
      return field;
  }
  return 1;
}
#endif

#ifdef F_putimr
GsWord sceGsPutIMR(GsWord mask) {
  GsWord old = GsGetIMR();
  GsPutIMR(mask);
  return old;
}
#endif

#ifdef F_getimr
GsWord sceGsGetIMR(void) {
  return GsGetIMR();
}
#endif

#ifdef F_syncpath
#define DUMP_PATH()                           \
  printf("\t<D1_CHCR=%08x:", (*D1_CHCR));     \
  printf("D1_TADR=%08x:", (*D1_TADR));        \
  printf("D1_MADR=%08x:", (*D1_MADR));        \
  printf("D1_QWC=%08x>\r\n", (*D1_QWC));      \
  printf("\t<D2_CHCR=%08x:", (*D2_CHCR));     \
  printf("D2_TADR=%08x:", (*D2_TADR));        \
  printf("D2_MADR=%08x:", (*D2_MADR));        \
  printf("D2_QWC=%08x>\r\n", (*D2_QWC));      \
  printf("\t<VIF1_STAT=%08x:", (*VIF1_STAT)); \
  printf("GIF_STAT=%08x>\r\n", (*GIF_STAT))
#define PATH_TIMEOUT(message) \
  if (count++ > 0x1000000) {  \
    printf(message);          \
    DUMP_PATH();              \
    return -1;                \
  }

int sceGsSyncPath(int mode, unsigned short timeout) {
  unsigned count = 0;
  unsigned status;
  int result = 0;
  if (mode == 0) {
    while ((*D1_CHCR) & 0x100) {
      PATH_TIMEOUT("sceGsSyncPath: DMA Ch.1 does not terminate\r\n");
    }
    while ((*D2_CHCR) & 0x100) {
      PATH_TIMEOUT("sceGsSyncPath: DMA Ch.2 does not terminate\r\n");
    }
    while ((*VIF1_STAT) & 0x1f000003) {
      PATH_TIMEOUT("sceGsSyncPath: VIF1 does not terminate\r\n");
    }
    status = EE_GET_VPU_STAT();
    while (status & VPU_STAT_VU1_BUSY) {
      PATH_TIMEOUT("sceGsSyncPath: VU1 does not terminate\r\n");
      status = EE_GET_VPU_STAT();
    }
    while ((*GIF_STAT) & 0xc00) {
      PATH_TIMEOUT("sceGsSyncPath: GIF does not terminate\r\n");
    }
    return 0;
  }
  if ((*D1_CHCR) & 0x100)
    result |= 1;
  if ((*D2_CHCR) & 0x100)
    result |= 2;
  if ((*VIF1_STAT) & 0x1f000003)
    result |= 4;
  status = EE_GET_VPU_STAT();
  if (status & VPU_STAT_VU1_BUSY)
    result |= 8;
  if ((*GIF_STAT) & 0xc00)
    result |= 16;
  return result;
}
#endif

#ifdef F_execloadimage
#define LOAD_WAIT()                                                  \
  while ((*D2_CHCR) & 0x100) {                                       \
    if (count++ > 0x1000000) {                                       \
      printf("sceGsExecLoadImage: DMA Ch.2 does not terminate\r\n"); \
      return -1;                                                     \
    }                                                                \
  }
#define LOAD_ADDRESS(address)                                     \
  if (((unsigned)(address) & 0x70000000) == 0x70000000)           \
    (*D2_MADR) = ((unsigned)(address) & 0x0fffffff) | 0x80000000; \
  else                                                            \
    (*D2_MADR) = (unsigned)(address) & 0x0fffffff

int sceGsExecLoadImage(GsQuad* packet, GsQuad* source) {
  unsigned count = 0;
  LOAD_WAIT();
  (*D2_QWC) = 6;
  LOAD_ADDRESS(packet);
  (*D2_CHCR) = 0x101;
  LOAD_WAIT();
  (*D2_QWC) = (int)(((GsWord*)packet)[10] & 0x7fff);
  LOAD_ADDRESS(source);
  (*D2_CHCR) = 0x101;
  return 0;
}
#endif

#ifdef F_setdefloadimage
int sceGsSetDefLoadImage(sceGsLoadImage* packet,
                         short dbp,
                         short dbw,
                         short dpsm,
                         short x,
                         short y,
                         short w,
                         short h) {
  GsWord blt;
  int qwc = 0;
  switch (dpsm) {
    case SCE_GS_PSMCT32:
    case SCE_GS_PSMZ32:
      qwc = (w * h) >> 2;
      break;
    case SCE_GS_PSMCT24:
    case SCE_GS_PSMZ24:
      qwc = (w * h * 3) >> 4;
      break;
    case SCE_GS_PSMCT16:
    case SCE_GS_PSMCT16S:
    case SCE_GS_PSMZ16:
    case SCE_GS_PSMZ16S:
      qwc = (w * h) >> 3;
      break;
    case SCE_GS_PSMT8:
    case SCE_GS_PSMT8H:
      qwc = (w * h) >> 4;
      break;
    case SCE_GS_PSMT4:
    case SCE_GS_PSMT4HL:
    case SCE_GS_PSMT4HH:
      qwc = (w * h) >> 5;
      break;
  }
  if (qwc > 0x7fff) {
    printf("sceGsSetDefLoadImage: too big size\r\n");
    return 0;
  }
  *(GsQuad*)&packet->setup = 0;
  *(GsQuad*)&packet->image = 0;
  packet->image.loops = qwc;
  packet->setup.loops = 4;
  packet->image.end = 1;
  packet->setup.count = 1;
  packet->setup.reg = 14;
  blt = ((GsWord)dbp << 32) | ((GsWord)dbw << 48) | ((GsWord)dpsm << 56);
  packet->bitbltbuf = blt;
  packet->bitbltbuf_address = 0x50;
  packet->position = ((GsWord)x << 32) | ((GsWord)y << 48);
  packet->position_address = 0x51;
  packet->dimensions = (GsWord)w | ((GsWord)h << 32);
  packet->dimensions_address = 0x52;
  packet->direction_address = 0x53;
  packet->direction = 0;
  packet->image.format = 2;
  EE_SYNC_L();
  return 6;
}
#endif

#ifdef F_execstoreimage
#define STORE_RESET() \
  (*GS_CSR) = 0x100;  \
  (*GS_BUSDIR) = 0;   \
  (*GIF_CTRL) = 1;    \
  (*VIF1_FBRST) = 1
#define STORE_DMA_WAIT()                                              \
  while ((*D1_CHCR) & 0x100) {                                        \
    if (count++ > 0x1000000) {                                        \
      printf("sceGsExecStoreImage: DMA Ch.1 does not terminate\r\n"); \
      return -1;                                                      \
    }                                                                 \
  }
#define STORE_FIFO_WAIT()                                               \
  while (!((*VIF1_STAT) & 0x1f000000)) {                                \
    if (count++ > 0x1000000) {                                          \
      printf("sceGsExecStoreImage: Enough data does not reach VIF1\n"); \
      STORE_RESET();                                                    \
      return -1;                                                        \
    }                                                                   \
  }
#define STORE_ADDRESS(address)                                    \
  if (((unsigned)(address) & 0x70000000) == 0x70000000)           \
    (*D1_MADR) = ((unsigned)(address) & 0x0fffffff) | 0x80000000; \
  else                                                            \
    (*D1_MADR) = (unsigned)(address) & 0x0fffffff

int sceGsExecStoreImage(GsQuad* packet, GsQuad* destination) {
  static unsigned init_mp3[4] __attribute__((aligned(16))) = {0x06000000, 0, 0, 0};
  int extra = 0;
  int bytes = 0;
  unsigned bulk = 0;
  int tail = 0;
  unsigned count = 0;
  GsQuad last[1];
  int width = (((GsWord*)packet)[8]) & 0xfff;
  int height = (((GsWord*)packet)[8] >> 32) & 0xfff;
  int format = (((GsWord*)packet)[4] >> 24) & 0x3f;
  int padded_height = 0;
  int total;
  int shifted_total;
  int padded_total;
  int i;
  GsWord imr;
  switch (format) {
    case SCE_GS_PSMCT32:
    case SCE_GS_PSMZ32:
      total = width * height * 4;
      bytes = total & 15;
      tail = (total >> 4) & 7;
      bulk = (total >> 4) & 0xfffffff8U;
      if (!bytes) {
        padded_height = height;
        extra = 0;
      } else {
        padded_height = (height + 3) & 0xfffffffcU;
        padded_total = width * padded_height;
        shifted_total = padded_total >> 2;
        extra = shifted_total - bulk - tail - 1;
      }
      break;
    case SCE_GS_PSMCT24:
    case SCE_GS_PSMZ24:
      total = width * height * 3;
      bytes = total & 15;
      tail = (total >> 4) & 7;
      bulk = (total >> 4) & 0xfffffff8U;
      if (!bytes) {
        padded_height = height;
        extra = 0;
      } else {
        padded_height = (height + 15) & 0xfffffff0U;
        padded_total = width * padded_height;
        shifted_total = (padded_total * 3) >> 4;
        padded_total = shifted_total - bulk - tail;
        extra = padded_total - 1;
      }
      break;
    case SCE_GS_PSMCT16:
    case SCE_GS_PSMCT16S:
    case SCE_GS_PSMZ16:
    case SCE_GS_PSMZ16S:
      total = width * height * 2;
      bytes = total & 15;
      tail = (total >> 4) & 7;
      bulk = (total >> 4) & 0xfffffff8U;
      if (!bytes) {
        padded_height = height;
        extra = 0;
      } else {
        padded_height = (height + 7) & 0xfffffff8U;
        padded_total = width * padded_height;
        shifted_total = padded_total >> 3;
        extra = shifted_total - bulk - tail - 1;
      }
      break;
    case SCE_GS_PSMT8:
    case SCE_GS_PSMT8H:
      total = width * height;
      bytes = total & 15;
      tail = (total >> 4) & 7;
      bulk = (total >> 4) & 0xfffffff8U;
      if (!bytes) {
        padded_height = height;
        extra = 0;
      } else {
        padded_height = (height + 7) & 0xfffffff8U;
        padded_total = width * padded_height;
        shifted_total = padded_total >> 4;
        extra = shifted_total - bulk - tail - 1;
      }
      break;
    case SCE_GS_PSMT4:
    case SCE_GS_PSMT4HL:
    case SCE_GS_PSMT4HH:
      total = (width * height) >> 1;
      bytes = total & 15;
      tail = (total >> 4) & 7;
      bulk = (total >> 4) & 0xfffffff8U;
      if (!bytes) {
        padded_height = height;
        extra = 0;
      } else {
        padded_height = (height + 7) & 0xfffffff8U;
        padded_total = width * padded_height;
        shifted_total = padded_total >> 5;
        extra = shifted_total - bulk - tail - 1;
      }
      break;
  }
  if (bytes) {
    REG64((unsigned)&((GsWord*)packet)[8] | 0x20000000) =
        (GsWord)(int)(((GsWord*)packet)[8] & 0xfff) | ((GsWord)padded_height << 32);
  }
  STORE_DMA_WAIT();
  imr = GsPutIMR(GsGetIMR() | 0x200);
  (*GS_CSR) = 2;
  (*D1_QWC) = 7;
  STORE_ADDRESS(packet);
  (*D1_CHCR) = 0x101;
  STORE_DMA_WAIT();
  while (!((*GS_CSR) & 2)) {
    if (count++ > 0x1000000) {
      printf("sceGsExecStoreImage: GS does not terminate\r\n");
      (*VIF1_FIFO) = *(GsQuad*)init_mp3;
      return -1;
    }
  }
  (*VIF1_STAT) = 0x800000;
  (*GS_BUSDIR) = 1;
  if (bulk) {
    (*D1_QWC) = bulk;
    STORE_ADDRESS(destination);
    (*D1_CHCR) = 0x100;
    while ((*D1_CHCR) & 0x100) {
      if (count++ > 0x1000000) {
        printf("sceGsExecStoreImage: DMA Ch.1 (GS->MEM) does not terminate\r\n");
        STORE_RESET();
        return -1;
      }
    }
  }
  for (i = 0; i < tail; ++i) {
    STORE_FIFO_WAIT();
    destination[bulk + i] = (*VIF1_FIFO);
  }
  if (bytes) {
    STORE_FIFO_WAIT();
    last[0] = (*VIF1_FIFO);
    for (i = 0; i < bytes; ++i)
      ((unsigned char*)&destination[bulk + tail])[i] = ((unsigned char*)last)[i];
    for (i = 0; i < extra; ++i) {
      STORE_FIFO_WAIT();
      last[0] = (*VIF1_FIFO);
    }
  }
  (*VIF1_STAT) = 0;
  (*GS_BUSDIR) = 0;
  GsPutIMR(imr);
  (*GS_CSR) = 2;
  (*VIF1_FIFO) = *(GsQuad*)init_mp3;
  return 0;
}
#endif
