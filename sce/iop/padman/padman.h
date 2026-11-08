#ifndef SCE_IOP_PADMAN_H
#define SCE_IOP_PADMAN_H
#include "sce/iop/types.h"

#ifndef PADMAN_VERSION
#define PADMAN_VERSION 0x0402
#endif
int padInit(void* ee_buffer);
int padEnd(void);
int padPortOpen(int port, int slot, void* ee_buffer, void** iop_buffer);
int padPortClose(int port, int slot, int wait);
int padGetInBuffer(int port, int slot, u_char* buffer);
int padGetModeConfig(int port, int slot);
int padInfoAct(int port, int slot, int actuator, int field);
int padInfoComb(int port, int slot, int combination, int field);
int padInfoMode(int port, int slot, int term, int offset);
int padSetMainMode(int port, int slot, int mode, int lock);
int padSetActDirect(int port, int slot, u_char* values);
int padSetActAlign(int port, int slot, const u_char* values);
u_int padGetButtonMask(int port, int slot);
int padSetButtonInfo(int port, int slot, u_int buttons);
int padSetVrefParam(int port, int slot, const u_char* values);
int padGetPortMax(void);
int padGetSlotMax(int port);
u_int padGetModVersion(void);
int padSetDebug(int enabled);
#if PADMAN_VERSION == 0x0403
int pad_capture_init(void);
int pad_capture_shutdown(void);
#endif
#endif
