#ifndef _LIBSD_H_
#define _LIBSD_H_

#include "sce/iop/sdmacro.h"
#include "sce/iop/types.h"

extern int     sceSdVoiceTrans( short channel, u_short mode, u_char *m_addr, u_int s_addr, u_int size );

extern sceSdTransIntrHandler sceSdSetTransIntrHandler (int channel, sceSdTransIntrHandler func, void *arg);

#endif
