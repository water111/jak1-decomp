#ifndef _LIBSD_H_
#define _LIBSD_H_

#include "sce/iop/libsd/sdmacro.h"
#include "sce/iop/types.h"

extern int sceSdVoiceTrans(short channel, u_short mode, u_char* m_addr, u_int s_addr, u_int size);

extern sceSdTransIntrHandler sceSdSetTransIntrHandler(int channel,
                                                      sceSdTransIntrHandler func,
                                                      void* arg);

extern u_int sceSdGetSwitch(u_short entry);
extern int sceSdProcBatch(sceSdBatch* batch, u_int returns[], u_int num);
extern u_int sceSdGetAddr(u_short entry);

typedef int (*sceSdCallback)(void* argument);
typedef int (*sceSdSpu2IntrHandler)(int cores, void* argument);

void sceSdSetParam(u_short entry, u_short value);
u_short sceSdGetParam(u_short entry);
void sceSdSetSwitch(u_short entry, u_int value);
void sceSdSetAddr(u_short entry, u_int value);
u_short sceSdNote2Pitch(int center_note, u_short center_fine, int note, short fine);
u_short sceSdPitch2Note(int center_note, int center_fine, u_short pitch);
void sceSdSetCoreAttr(u_short entry, u_short value);
u_short sceSdGetCoreAttr(u_short entry);
sceSdCallback sceSdSetTransCallback(u_short core, sceSdCallback callback);
sceSdCallback sceSdSetIRQCallback(sceSdCallback callback);
sceSdSpu2IntrHandler sceSdSetSpu2IntrHandler(sceSdSpu2IntrHandler handler, void* argument);

typedef struct {
  int core;
  int mode;
  short depth_left, depth_right;
  int delay;
  int feedback;
} SdEffectAttr;

int sceSdInit(u_int mode);
int sceSdQuit(void);
int sceSdVoiceTransStatus(short core, short wait);
int sceSdBlockTrans(short channel, u_short mode, void* memory, u_int size, ...);
u_int sceSdBlockTransStatus(short core, short wait);
void sceSdGetEffectAttr(int core, SdEffectAttr* attr);
int sceSdSetEffectAttr(int core, SdEffectAttr* attr);
int sceSdClearEffectWorkArea(int core, int channel, u_int effect);

int sceSdProcBatchEx(sceSdBatch* batch, u_int returns[], u_int num, u_int voices);
void* sceSdGetTransIntrHandlerArgument(int channel);
void* sceSdGetSpu2IntrHandlerArgument(void);
int sceSdStopTrans(int channel);
int sceSdCleanEffectWorkArea(int core, int channel, int effect_mode);
int sceSdSetEffectMode(int core, SdEffectAttr* attr);
int sceSdSetEffectModeParams(int core, SdEffectAttr* attr);

#endif
