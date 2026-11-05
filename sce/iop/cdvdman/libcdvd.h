#ifndef _LIBCDVD_H
#define _LIBCDVD_H

#include "sce/iop/types.h"

#define SCECdSpinNom 1
#define SCECdSpinStm 0

typedef struct {
  u_char trycount;
  u_char spindlctrl;
  u_char datapattern;
  u_char pad;
} sceCdRMode;

int sceCdRead(u_int lbn, u_int sectors, void* buf, sceCdRMode* mode);

int sceCdTrayReq(int request, unsigned int* tray);
int sceCdSync(int mode);
int sceCdGetError(void);
int sceCdGetDiskType(void);
int sceCdMmode(int media);
int sceCdBreak(void);
int sceCdDiskReady(int mode);

#define SCECdDETCT 0x01

#define SCECdIllgalMedia 0xff
#define SCECdDVDV 0xfe
#define SCECdCDDA 0xfd
#define SCECdPS2DVD 0x14
#define SCECdPS2CDDA 0x13
#define SCECdPS2CD 0x12
#define SCECdPSCDDA 0x11
#define SCECdPSCD 0x10
#define SCECdNODISC 0x00

#define SCECdDVD 2

#define SCECdNotReady 0x06

typedef struct {
  u_char status, second, minute, hour, reserved, day, month, year;
} sceCdCLOCK;

int _sceCdRC(sceCdCLOCK* clock);

#endif
