#include "sce/iop/989snd/movie.h"

#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/loader_memory.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/stdio/stdio.h"

#define MOVIE_BATCH_SET_PARAM 1
#define MOVIE_CORE_ATTRIBUTE 0x800
#define MOVIE_VOLUME_LEFT 0xf80
#define MOVIE_VOLUME_RIGHT 0x1080
#define MOVIE_TRANSFER_STOP 2

int gMovieDMAChannel = -1;
void* gMovieSoundBuffer = 0;
int gMovieSoundBufferSize = 0;
int gMovieVol = 0;
int gMoviePan = 0;
int gMovieVolGroup = 0;

void* snd_InitMovieSound(int size, int volume, int pan, int group) {
  int waiting = 0;
  if (size & 3) {
#if SND989_VERSION == 0x020604
    printf("snd_InitMovieSound: buff_size must be divisible by 4!\n");
#else
    snd_ShowError(30, 0, 0, 0, 0);
#endif
    return 0;
  }
  gMovieSoundBuffer = gAllocProc(size, 6, 0);
  if (!gMovieSoundBuffer) {
#if SND989_VERSION == 0x020604
    printf("snd_InitMovieSound: Couldn't allocate %d bytes of IOP memory!\n", size);
#else
    snd_ShowError(30, size, 0, 0, 0);
#endif
    return 0;
  }
  while ((gMovieDMAChannel = snd_GetFreeSPUDMA()) == -1) {
    if (!waiting) {
      waiting = 1;
#if SND989_VERSION == 0x020604
      printf("snd_InitMovieSound: Waiting for a free DMA channel!\n");
#endif
    }
#if SND989_VERSION == 0x020604
    while (gMovieDMAChannel < 1000)
      ++gMovieDMAChannel;
#else
    do {
      ++gMovieDMAChannel;
    } while (gMovieDMAChannel < 1000);
#endif
  }
#if SND989_VERSION == 0x020604
  if (waiting == 1)
    printf("snd_InitMovieSound: Got free DMA channel... continueing.\n");
#endif
  sceSdSetTransIntrHandler(gMovieDMAChannel, 0, 0);
  gMovieVol = (volume * 127) >> 10;
  gMoviePan = pan;
  gMovieVolGroup = group;
  snd_ClearMovieSoundBuffer();
  return gMovieSoundBuffer;
}

void snd_ClearMovieSoundBuffer(void) {
  int* word = gMovieSoundBuffer;
  int length = gMovieSoundBufferSize / 4;
  int i;
  for (i = 0; i < length; ++i)
    *word++ = 0;
  sceSdBlockTrans(gMovieDMAChannel, 0, gMovieSoundBuffer, 1024);
}

void snd_CloseMovieSound(void) {
  snd_ResetMovieSound();
  if ((u_int)gMovieDMAChannel < 2)
    snd_FreeSPUDMA(gMovieDMAChannel);
  gMovieDMAChannel = -1;
  if (gMovieSoundBuffer)
    gFreeProc(gMovieSoundBuffer);
}

void snd_ResetMovieSound(void) {
  sceSdBatch batch[3];
  if (gMovieDMAChannel != -1) {
    batch[0].func = MOVIE_BATCH_SET_PARAM;
    batch[0].entry = gMovieDMAChannel | MOVIE_VOLUME_LEFT;
    batch[0].value = 0;
    batch[1].func = MOVIE_BATCH_SET_PARAM;
    batch[1].entry = gMovieDMAChannel | MOVIE_VOLUME_RIGHT;
    batch[1].value = 0;
    batch[2].func = MOVIE_BATCH_SET_PARAM;
    batch[2].entry = gMovieDMAChannel | MOVIE_CORE_ATTRIBUTE;
    batch[2].value = sceSdGetParam(gMovieDMAChannel | MOVIE_CORE_ATTRIBUTE) & ~0xc0;
    sceSdProcBatch(batch, 0, 3);
    sceSdBlockTrans(gMovieDMAChannel, MOVIE_TRANSFER_STOP, 0, 0);
  }
}

void snd_StartMovieSound(void* buffer, int size, int offset) {
  sceSdBatch batch[3];
  VolPair volume;
  volume.right = 0;
  volume.left = 0;
  snd_MakeVolumes(gMovieVol, gMoviePan, 127, 0, 127, 0, &volume);
  sceSdBlockTrans(gMovieDMAChannel, 0x13, buffer, size, offset);
  batch[0].func = MOVIE_BATCH_SET_PARAM;
  batch[0].entry = gMovieDMAChannel | MOVIE_VOLUME_LEFT;
  batch[0].value = (u_short)snd_AdjustVolToGroup(volume.left, gMovieVolGroup) * 2;
  batch[1].func = MOVIE_BATCH_SET_PARAM;
  batch[1].entry = gMovieDMAChannel | MOVIE_VOLUME_RIGHT;
  batch[1].value = (u_short)snd_AdjustVolToGroup(volume.right, gMovieVolGroup) * 2;
  if (batch[0].value > 0x7fff)
    batch[0].value = 0x7fff;
  if (batch[1].value > 0x7fff)
    batch[1].value = 0x7fff;
  batch[2].func = MOVIE_BATCH_SET_PARAM;
  batch[2].entry = gMovieDMAChannel | MOVIE_CORE_ATTRIBUTE;
  batch[2].value = (u_short)(sceSdGetParam(gMovieDMAChannel | MOVIE_CORE_ATTRIBUTE) | 0xc0);
  sceSdProcBatch(batch, 0, 3);
}

u_int snd_GetMovieBufferPos(void) {
  return sceSdBlockTransStatus(gMovieDMAChannel, 0);
}

u_int snd_PauseMovieSound(void) {
  sceSdBatch batch[2];
  batch[0].func = MOVIE_BATCH_SET_PARAM;
  batch[0].entry = gMovieDMAChannel | MOVIE_VOLUME_LEFT;
  batch[0].value = 0;
  batch[1].func = MOVIE_BATCH_SET_PARAM;
  batch[1].entry = gMovieDMAChannel | MOVIE_VOLUME_RIGHT;
  batch[1].value = 0;
  sceSdProcBatch(batch, 0, 2);
  return sceSdBlockTrans(gMovieDMAChannel, MOVIE_TRANSFER_STOP, 0, 0) & 0xffffff;
}
