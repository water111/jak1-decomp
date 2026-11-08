#ifndef SCE_IOP_CDVDSTM_H
#define SCE_IOP_CDVDSTM_H

#include "sce/iop/cdvdman/libcdvd.h"

#define CDVDSTM_SECTOR_SIZE 2048
#define CDVDSTM_MAX_BANKS 512
#define CDVDSTM_EVENT_DATA 8
#define CDVDSTM_ERROR_STOPPED 0x111
#define CDVDSTM_ERROR_MEDIA 0xfd
#define CDVDSTM_DEVCTL_STREAM 0x4393
#define CDVDSTM_DEVCTL_READ 0x4394
#define CDVDSTM_DEVCTL_EE_STREAM 0x4396

enum CdvdStreamCommand {
  CDVDSTM_START = 1,
  CDVDSTM_READ = 2,
  CDVDSTM_STOP = 3,
  CDVDSTM_SEEK = 4,
  CDVDSTM_INIT = 5,
  CDVDSTM_STAT = 6,
  CDVDSTM_PAUSE = 7,
  CDVDSTM_RESUME = 8,
  CDVDSTM_SEEK_FAST = 9
};

struct CdvdStreamRequest {
  unsigned int lsn;
  unsigned int sectors;
  void* buffer;
  unsigned int command;
  sceCdRMode mode;
  int error;
};

int cdvdstm_dummy(void);
int cdvdstm_shutdown(void);
#endif
