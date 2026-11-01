#ifndef _LIBSD_H_
#define _LIBSD_H_

#include "sce/iop/sdmacro.h"
#include "sce/iop/types.h"

extern int sceSdVoiceTrans(short channel, u_short mode, u_char* m_addr, u_int s_addr, u_int size);

extern sceSdTransIntrHandler sceSdSetTransIntrHandler(int channel,
                                                      sceSdTransIntrHandler func,
                                                      void* arg);

extern u_int sceSdGetSwitch(u_short entry);
extern int sceSdProcBatch(sceSdBatch* batch, u_int returns[], u_int num);
extern u_int sceSdGetAddr(u_short entry);

#endif
