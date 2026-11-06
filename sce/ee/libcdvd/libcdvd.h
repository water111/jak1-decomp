#ifndef SCE_EE_LIBCDVD_LIBCDVD_H
#define SCE_EE_LIBCDVD_LIBCDVD_H

#ifdef __cplusplus
extern "C" {
#endif

#define SCECdSecS2048 0
#define SCECdSecS2328 1
#define SCECdSecS2340 2
#define SCECdComplete 2
#define SCECdNotReady 6
#define SCECdFuncRead 1
#define SCECdFuncSeek 4
#define SCECdFuncStandby 5
#define SCECdFuncStop 6
#define SCECdFuncPause 7
#define SCECdFuncBreak 8
#define SCECdINIT 0
#define SCECdINoD 1
#define SCECdEXIT 5

typedef struct {
  unsigned char status, second, minute, hour, reserved, day, month, year;
} sceCdCLOCK;

typedef struct {
  unsigned char trycount, spindlctrl, datapattern, pad;
} sceCdRMode;

typedef struct {
  unsigned char minute, second, sector, track;
} sceCdlLOCCD;

typedef struct {
  unsigned lsn, size;
  char name[16];
  unsigned char date[8];
  unsigned flag;
} sceCdlFILE;

typedef void (*CdCallback)(int);
typedef void (*PowerOffCallback)(void*);

void sceCdDelayThread(unsigned short time);
CdCallback sceCdCallback(CdCallback callback);
void _Cdvd_cbLoop(void);
int sceCdInitEeCB(int priority, void* stack, int size);
sceCdlLOCCD* sceCdIntToPos(int sector, sceCdlLOCCD* position);
int sceCdPosToInt(const sceCdlLOCCD* position);
PowerOffCallback sceCdPOffCallback(PowerOffCallback callback, void* data);
int sceCdSearchFile(sceCdlFILE* file, const char* name);
int sceCdNcmdDiskReady(void);
unsigned sceCdGetReadPos(void);
int sceCdReadChain(const unsigned* chain, const sceCdRMode* mode);
int sceCdRead(unsigned sector, unsigned count, void* destination, const sceCdRMode* mode);
int sceCdReadIOPm(unsigned sector, unsigned count, void* destination, const sceCdRMode* mode);
int sceCdGetToc(void* destination);
int sceCdSeek(unsigned sector);
int sceCdStandby(void);
int sceCdStop(void);
int sceCdPause(void);
int sceCdSync(int mode);
int sceCdSyncS(int mode);
int sceCdInit(int mode);
int sceCdDiskReady(int mode);
int sceCdBreak(void);
int sceCdGetDiskType(void);
int sceCdStatus(void);
int sceCdGetError(void);
int _sceCdRI(void* data, unsigned* status);
int _sceCdRM(void* data, unsigned* status);
int sceCdPowerOff(unsigned* status);
int sceCdMmode(int value);
int sceCdChangeThreadPriority(int value);
int sceCdReadClock(sceCdCLOCK* clock);
int sceCdApplyNCmd(unsigned char command, const void* data, int size);
int sceCdTrayReq(int mode, unsigned* status);
int sceCdStInit(unsigned size, unsigned banks, void* buffer);
int sceCdStStart(unsigned sector, const sceCdRMode* mode);
int sceCdStSeekF(unsigned sector);
int sceCdStSeek(unsigned sector);
int sceCdStStop(void);
unsigned sceCdStRead(unsigned count, void* buffer, unsigned mode, unsigned* error);
int sceCdStPause(void);
int sceCdStResume(void);
int sceCdStStat(void);
int sceCdStream(unsigned sector,
                unsigned count,
                void* buffer,
                unsigned command,
                const sceCdRMode* mode);

#define SCECdCDROM 1
#define SCECdDVD 2

#ifdef __cplusplus
}
#endif

#endif
