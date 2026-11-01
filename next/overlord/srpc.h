#ifndef SRPC_H
#define SRPC_H

extern unsigned int gFrameNum;
extern int gMusicTweak;
extern int gDupSeg;
extern unsigned int gNoCD;
extern unsigned int gDirtyCD;
extern unsigned int gDiskSpeed[2];
extern unsigned int gLastSpeed;

int VBlank_Handler(void* arg);
unsigned int Thread_Player(void);
unsigned int Thread_Loader(void);

#endif
