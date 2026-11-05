#ifndef SCE_IOP_989SND_MOVIE_H
#define SCE_IOP_989SND_MOVIE_H

#include "sce/iop/types.h"

void snd_StartMovieSound(void* buffer, int size, int offset);
void* snd_InitMovieSound(int size, int volume, int pan, int group);
void snd_CloseMovieSound(void);
void snd_ResetMovieSound(void);
u_int snd_GetMovieBufferPos(void);
u_int snd_PauseMovieSound(void);
void snd_ClearMovieSoundBuffer(void);
extern int gMovieDMAChannel;
extern void* gMovieSoundBuffer;
#endif
