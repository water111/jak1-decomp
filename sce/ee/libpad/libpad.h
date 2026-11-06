#ifndef SCE_EE_LIBPAD_LIBPAD_H
#define SCE_EE_LIBPAD_LIBPAD_H

#ifdef __cplusplus
extern "C" {
#endif

#define scePadStateDiscon 0
#define scePadStateFindPad 1
#define scePadStateFindCTP1 2
#define scePadStateExecCmd 5
#define scePadStateStable 6
#define scePadStateError 7
#define scePadStateClosed 99
#define scePadReqStateComplete 0
#define scePadReqStateFailed 1
#define scePadReqStateBusy 2
#define InfoModeCurID 1
#define InfoModeCurExID 2
#define InfoModeCurExOffs 3
#define InfoModeIdTable 4
#define InfoActFunc 1
#define InfoActSub 2
#define InfoActSize 3
#define InfoActCurr 4
#define InfoActSign 5

int scePadInit(int mode);
int scePadEnd(void);
int scePadPortOpen(int port, int slot, void* address);
int scePadPortClose(int port, int slot);
int scePadGetFrameCount(int port, int slot);
int scePadRead(int port, int slot, unsigned char* data);
int scePadGetState(int port, int slot);
void scePadStateIntToStr(int state, char* text);
int scePadGetReqState(int port, int slot);
void scePadReqIntToStr(int state, char* text);
int scePadInfoAct(int port, int slot, int actuator, int term);
int scePadInfoComb(int port, int slot, int combination, int offset);
int scePadInfoMode(int port, int slot, int term, int offset);
int scePadSetMainMode(int port, int slot, int offset, int lock);
int scePadSetActDirect(int port, int slot, const unsigned char* data);
int scePadSetActAlign(int port, int slot, const unsigned char* data);
int scePadInfoPressMode(int port, int slot);
int scePadEnterPressMode(int port, int slot);
int scePadExitPressMode(int port, int slot);
int scePadGetSlotMax(int port);
int scePadSetWarningLevel(int level);

#define SCE_PAD_DMA_BUFFER_SIZE 256

#ifdef __cplusplus
}
#endif

#endif
