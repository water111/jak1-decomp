#include "sce/common/libcdvd/rpc.h"
#include "sce/ee/eeasm.h"
#include "sce/ee/libcdvd/libcdvd.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/libkernl/sif.h"
#include "sce/ee/memory.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

enum {
  CD_NCMD_CHECK_SEARCH_FILE = 1,
  CD_NCMD_CHECK_DISK_READY = 2,
  CD_NCMD_CHECK_READ_CHAIN = 3,
  CD_NCMD_CHECK_READ = 4,
  CD_NCMD_CHECK_READ_IOP = 5,
  CD_NCMD_CHECK_GET_TOC = 8,
  CD_NCMD_CHECK_SEEK = 9,
  CD_NCMD_CHECK_STANDBY = 10,
  CD_NCMD_CHECK_STOP = 11,
  CD_NCMD_CHECK_PAUSE = 12,
  CD_NCMD_CHECK_APPLY_NCMD = 14,
  CD_NCMD_CHECK_STREAM = 15,
};

enum {
  CD_SCMD_CHECK_GET_DISK_TYPE = 1,
  CD_SCMD_CHECK_STATUS = 2,
  CD_SCMD_CHECK_GET_ERROR = 3,
  CD_SCMD_CHECK_RI = 4,
  CD_SCMD_CHECK_RM = 5,
  CD_SCMD_CHECK_READ_CLOCK = 15,
  CD_SCMD_CHECK_TRAY_REQ = 26,
  CD_SCMD_CHECK_BREAK = 30,
  CD_SCMD_CHECK_POWEROFF = 33,
  CD_SCMD_CHECK_MMODE = 34,
  CD_SCMD_CHECK_CHANGE_THREAD_PRIORITY = 35,
};

static char __ps2_libinfo__[16] = "PsIIlibcdvd 2210";
int SCE_CD_debug = 0;
static int scmd_sema_keep_cmd = 0, ncmd_sema_keep_cmd = 0;
static volatile int cb_thid = 0, ncmd_semid = -1, scmd_semid = -1, cb_semid = -1;
static volatile int c_cb_sem = 0, Init_seq = 0;

static CdCallback sceCdCbfunc;
static void (*sceCdPoffCbfunc)(void*);
static void* sceCdPoffCbdata;
static volatile int sceCdCbfunc_num, sceCdCbfunc_number, sceCdPoffCbfunc_num;

static void CB_DelayTh(int alarm, unsigned short time, int semaphore) {
  iSignalSema(semaphore);
  EE_ENABLE_INTERRUPTS();
}

void sceCdDelayThread(unsigned short time) {
  Semaphore semaphore;
  int id;
  semaphore.initial = 1;
  semaphore.maximum = 0;
  semaphore.option = 0;
  id = CreateSema(&semaphore);
  SetAlarm(time, CB_DelayTh, id);
  WaitSema(id);
  DeleteSema(id);
}

CdCallback sceCdCallback(CdCallback callback) {
  CdCallback previous;
  if (sceCdSync(1))
    return 0;
  DIntr();
  previous = sceCdCbfunc;
  sceCdCbfunc = callback;
  EIntr();
  return previous;
}

static void cd_callback(void* argument) {
  sceCdCbfunc_num = *(int*)argument;
  sceCdCbfunc_number = sceCdCbfunc_num;
  if (sceCdCbfunc_num == 11) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
  } else {
    iSignalSema(ncmd_semid);
    if (cb_thid && sceCdCbfunc)
      iSignalSema(cb_semid);
    else
      c_cb_sem = 0;
    sceCdCbfunc_num = 0;
  }
}

void _Cdvd_cbLoop(void) {
  for (;;) {
    WaitSema(cb_semid);
    if (sceCdCbfunc_num == -1) {
      c_cb_sem = 0;
      sceCdCbfunc_num = 0;
      cb_thid = 0;
      sceCdPoffCbfunc_num = 0;
      ExitDeleteThread();
    }
    if (SCE_CD_debug > 0)
      printf("sceCdCbfunc= %d sceCdCbfunc_num= %d\n", sceCdCbfunc, sceCdCbfunc_number);
    if (sceCdCbfunc && sceCdCbfunc_number)
      sceCdCbfunc(sceCdCbfunc_number);
    c_cb_sem = 0;
  }
}

static int my_thid;
static Thread my_th_info, cb_tp;
extern char _gp[];

int sceCdInitEeCB(int priority, void* stack, int size) {
  int result = 1;
  if (!(*(int*)&cb_thid)) {
    my_thid = GetThreadId();
    ReferThreadStatus(my_thid, &my_th_info);
    cb_tp.stack_size = size;
    cb_tp.gp = _gp;
    cb_tp.entry = _Cdvd_cbLoop;
    cb_tp.stack = stack;
    cb_tp.priority = priority;
    (*(int*)&cb_thid) = CreateThread(&cb_tp);
    StartThread((*(int*)&cb_thid), 0);
  } else {
    ChangeThreadPriority((*(int*)&cb_thid), priority);
    result = 0;
  }
  return result;
}

typedef struct {
  int head_size, tail_size;
  unsigned char *head_destination, *tail_destination;
  unsigned char head[64], tail[64];
} ReadReply;

static void cd_read_intr(void* argument) {
  ReadReply* reply = (ReadReply*)EE_UNCACHED_ADDR(argument);
  unsigned char* destination;
  int i;
  if (reply->head_size > 0) {
    destination = reply->head_destination;
    for (i = 0; i < reply->head_size; i++)
      destination[i] = reply->head[i];
  }
  if (reply->tail_size > 0) {
    destination = reply->tail_destination;
    for (i = 0; i < reply->tail_size; i++)
      destination[i] = reply->tail[i];
  }
  cd_callback((void*)&sceCdCbfunc_num);
}

static void cmd_sem_init(void) {
  if (ncmd_semid == -1 || scmd_semid == -1) {
    Semaphore semaphore;
    semaphore.option = 0;
    semaphore.maximum = 1;
    semaphore.initial = 1;
    ncmd_semid = CreateSema(&semaphore);
    scmd_semid = CreateSema(&semaphore);
    semaphore.maximum = 0;
    cb_semid = CreateSema(&semaphore);
    c_cb_sem = 0;
  }
}

static void cdvd_exit(void) {
  if (cb_thid) {
    sceCdCbfunc_num = -1;
    SignalSema(cb_semid);
  }
  DeleteSema(ncmd_semid);
  DeleteSema(scmd_semid);
  DeleteSema(cb_semid);
  DIntr();
  sceSifRemoveCmdHandler(CD_CMD_POWEROFF);
  EIntr();
}

sceCdlLOCCD* sceCdIntToPos(int sector, sceCdlLOCCD* position) {
  sector += 150;
  position->sector = (sector % 75 / 10 << 4) + sector % 75 % 10;
  sector /= 75;
  position->second = (sector % 60 / 10 << 4) + sector % 60 % 10;
  sector /= 60;
  position->minute = (sector / 10 << 4) + sector % 10;
  return position;
}

int sceCdPosToInt(const sceCdlLOCCD* position) {
  int minute = (position->minute >> 4) * 10 + (position->minute & 15);
  int second = (position->second >> 4) * 10 + (position->second & 15);
  int sector = (position->sector >> 4) * 10 + (position->sector & 15);
  return (minute * 60 + second) * 75 + sector - 150;
}

static int _icmd_bind = -1;

static int PowerOffCB(void);

PowerOffCallback sceCdPOffCallback(PowerOffCallback callback, void* data) {
  PowerOffCallback previous;
  if (_icmd_bind < 0)
    PowerOffCB();
  DIntr();
  previous = sceCdPoffCbfunc;
  sceCdPoffCbdata = data;
  sceCdPoffCbfunc = callback;
  EIntr();
  return previous;
}

static void _sceCd_Poff_Intr(void* packet, void* data) {
  if (sceCdPoffCbfunc && !Init_seq)
    sceCdPoffCbfunc(sceCdPoffCbdata);
}

static unsigned char ncmdrdata[96] __attribute__((aligned(64)));
static SifRpcClient icmdcd;
static int func_num;

static int PowerOffCB(void) {
  int retries, delay;
  sceSifInitRpc(0);
  Init_seq = 1;
  DIntr();
  sceSifAddCmdHandler(CD_CMD_POWEROFF, _sceCd_Poff_Intr, 0);
  EIntr();
  if (_icmd_bind < 0) {
    retries = 0;
    for (;;) {
      if (sceSifBindRpc(&icmdcd, CD_RPC_POWEROFF, 0) < 0) {
        if (SCE_CD_debug > 0)
          printf("Libcdvd bind err PowerOffCB\n");
        for (delay = 0x100000; delay--;)
          ;
        continue;
      }
      if (icmdcd.server) {
        _icmd_bind = 0;
        break;
      }
      for (delay = 0x100000; delay--;)
        ;
      if (retries++ > 16) {
        Init_seq = 0;
        return 0;
      }
    }
  }
  func_num = 11;
  if (sceSifCallRpc(&icmdcd, 1, 1, 0, 0, 0, 0, 0, 0) < 0) {
    Init_seq = 0;
    return 0;
  }
  Init_seq = 0;
  return 1;
}

typedef struct {
  sceCdlFILE file;
  char name[256];
  void* destination;
} SearchRequest;

static unsigned char icmdrdata[64] __attribute__((aligned(64)));
static SearchRequest _sf_data __attribute__((aligned(64)));
static int sfrdata[16] __attribute__((aligned(64)));
static SifRpcClient cdrd, cdrdsf;

static int _sf_bind = -1;

int sceCdSearchFile(sceCdlFILE* file, const char* name) {
  int delay, i, result;
  cmd_sem_init();
  result = PollSema(ncmd_semid);
  if (ncmd_semid != result)
    return 0;
  ncmd_sema_keep_cmd = CD_NCMD_CHECK_SEARCH_FILE;
  ReferThreadStatus(my_thid, &my_th_info);
  if (sceCdSync(1)) {
    SignalSema(ncmd_semid);
    return 0;
  }
  sceSifInitRpc(0);
  if (_sf_bind < 0) {
    for (;;) {
      if (sceSifBindRpc(&cdrdsf, CD_RPC_SEARCH_FILE, 0) < 0) {
        if (SCE_CD_debug > 0)
          printf("Libcdvd bind err CdSearchFile\n");
        for (delay = 0x100000; delay--;)
          ;
        continue;
      }
      if (cdrdsf.server) {
        _sf_bind = 0;
        break;
      }
      for (delay = 0x100000; delay--;)
        ;
    }
  }
  for (i = 0; i < 256 && (_sf_data.name[i] = name[i]); i++)
    ;
  if (i == 256)
    _sf_data.name[255] = 0;
  _sf_data.destination = &_sf_data;
  if (SCE_CD_debug > 0)
    printf("ee call cmd search %s\n", _sf_data.name);
  sceSifWriteBackDCache(&_sf_data, sizeof(_sf_data));
  if (sceSifCallRpc(&cdrdsf, 0, 0, &_sf_data, sizeof(_sf_data), sfrdata, 4, 0, 0) < 0) {
    SignalSema(ncmd_semid);
    return 0;
  }
  memcpy(file, EE_UNCACHED_ADDR(&_sf_data), sizeof(*file));
  if (SCE_CD_debug > 0)
    printf("search name %s\n", file->name);
  if (SCE_CD_debug > 0)
    printf("search size %d\n", file->size);
  if (SCE_CD_debug > 0)
    printf("search loc lbn %d\n", file->lsn);
  result = *(int*)EE_UNCACHED_ADDR(sfrdata);
  SignalSema(ncmd_semid);
  return result;
}

static int _rd_bind = -1;

static int ncmd_prechk(int command) {
  int delay, result;
  cmd_sem_init();
  result = PollSema(ncmd_semid);
  if (ncmd_semid != result) {
    if (SCE_CD_debug > 0)
      printf("Ncmd fail sema cur_cmd:%d keep_cmd:%d\n", command, ncmd_sema_keep_cmd);
    return 0;
  }
  ncmd_sema_keep_cmd = command;
  ReferThreadStatus(my_thid, &my_th_info);
  if (sceCdSync(1)) {
    SignalSema(ncmd_semid);
    return 0;
  }
  sceSifInitRpc(0);
  if (_rd_bind < 0) {
    for (;;) {
      if (sceSifBindRpc(&cdrd, CD_RPC_NCMD, 0) < 0) {
        if (SCE_CD_debug > 0)
          printf("Libcdvd bind err N CMD\n");
        for (delay = 0x100000; delay--;)
          ;
        continue;
      }
      if (cdrd.server)
        break;
      for (delay = 0x100000; delay--;)
        ;
    }
    _rd_bind = 0;
  }
  return 1;
}

int sceCdNcmdDiskReady(void) {
  if (!ncmd_prechk(CD_NCMD_CHECK_DISK_READY))
    return 0;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_DISK_READY, 0, 0, 0, ncmdrdata, 4, 0, 0) < 0) {
    SignalSema(ncmd_semid);
    return 0;
  }
  SignalSema(ncmd_semid);
  return *(int*)EE_UNCACHED_ADDR(ncmdrdata);
}

typedef struct {
  unsigned sector, count;
  void* destination;
  sceCdRMode mode;
  ReadReply* reply;
  unsigned* position;
} ReadRequest;

typedef struct {
  unsigned entries[195];
  sceCdRMode mode;
  unsigned* position;
} ChainRequest;

static ReadReply _rd_intr_data __attribute__((aligned(64)));
static ReadRequest _rd_data __attribute__((aligned(64)));
static ChainRequest _rdc_data __attribute__((aligned(64)));
static unsigned _Read_cur_pos __attribute__((aligned(64)));

unsigned sceCdGetReadPos(void) {
  if (sceCdCbfunc_num == SCECdFuncRead)
    return *(unsigned*)EE_UNCACHED_ADDR(&_Read_cur_pos);
  return 0;
}

int sceCdReadChain(const unsigned* chain, const sceCdRMode* mode) {
  int i, count, sector_size;
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_READ_CHAIN))
    return 0;
  if (SCE_CD_debug > 0)
    printf("call CdReadChain cmd 0\n");
  for (i = 0; i < 192 && *chain != 0xffffffffU; i++)
    _rdc_data.entries[i] = *chain++;
  count = i / 3 * 3;
  _rdc_data.entries[count] = 0xffffffffU;
  _rdc_data.entries[count + 1] = 0xffffffffU;
  _rdc_data.entries[count + 2] = 0xffffffffU;
  _rdc_data.mode.trycount = mode->trycount;
  _rdc_data.mode.spindlctrl = mode->spindlctrl;
  _rdc_data.mode.datapattern = mode->datapattern;
  _rdc_data.position = &_Read_cur_pos;
  switch (mode->datapattern) {
    case SCECdSecS2328:
      sector_size = 2328;
      break;
    case SCECdSecS2340:
      sector_size = 2340;
      break;
    case SCECdSecS2048:
      sector_size = 2048;
      break;
    default:
      sector_size = 2048;
      break;
  }
  _Read_cur_pos = 0;
  if (SCE_CD_debug > 0)
    printf("call CdReadChain cmd 1\n");
  for (i = 1; i < count; i += 3) {
    if (!(_rdc_data.entries[i + 1] & 1)) {
      if (SCE_CD_debug > 0)
        printf("sceSifWriteBackDCache addr= 0x%08x size= %d\n", _rdc_data.entries[i + 1],
               _rdc_data.entries[i] * sector_size);
      sceSifWriteBackDCache((void*)_rdc_data.entries[i + 1], _rdc_data.entries[i] * sector_size);
    }
  }
  sceSifWriteBackDCache(&_rdc_data, 24);
  sceSifWriteBackDCache(&_Read_cur_pos, 4);
  if (SCE_CD_debug > 0)
    printf("call CdReadChain cmd 2\n");
  sceCdCbfunc_num = SCECdFuncRead;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_READ_CHAIN, 1, &_rdc_data, sizeof(_rdc_data), 0, 0,
                    cd_callback, (void*)&sceCdCbfunc_num) < 0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  if (SCE_CD_debug > 0)
    printf("cdread end\n");
  return 1;
}

int sceCdRead(unsigned sector, unsigned count, void* destination, const sceCdRMode* mode) {
  int size;
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_READ))
    return 0;
  _rd_data.sector = sector;
  _rd_data.count = count;
  _rd_data.destination = destination;
  _rd_data.mode.trycount = mode->trycount;
  _rd_data.mode.spindlctrl = mode->spindlctrl;
  _rd_data.mode.datapattern = mode->datapattern;
  _rd_data.reply = &_rd_intr_data;
  _rd_data.position = &_Read_cur_pos;
  switch (mode->datapattern) {
    case SCECdSecS2328:
      size = count * 2328;
      break;
    case SCECdSecS2340:
      size = count * 2340;
      break;
    case SCECdSecS2048:
      size = count * 2048;
      break;
    default:
      size = count * 2048;
      break;
  }
  _Read_cur_pos = 0;
  sceSifWriteBackDCache(destination, size);
  sceSifWriteBackDCache(&_rd_intr_data, sizeof(_rd_intr_data));
  sceSifWriteBackDCache(&_rd_data, sizeof(_rd_data));
  sceSifWriteBackDCache(&_Read_cur_pos, 4);
  if (SCE_CD_debug > 0)
    printf("call cdread cmd\n");
  sceCdCbfunc_num = SCECdFuncRead;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_READ, 1, &_rd_data, sizeof(_rd_data), 0, 0, cd_read_intr,
                    &_rd_intr_data) < 0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  if (SCE_CD_debug > 0)
    printf("cdread end\n");
  return 1;
}

int sceCdReadIOPm(unsigned sector, unsigned count, void* destination, const sceCdRMode* mode) {
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_READ_IOP))
    return 0;
  _rd_data.sector = sector;
  _rd_data.count = count;
  _rd_data.destination = destination;
  _rd_data.mode.trycount = mode->trycount;
  _rd_data.mode.spindlctrl = mode->spindlctrl;
  _rd_data.mode.datapattern = mode->datapattern;
  _rd_data.reply = &_rd_intr_data;
  _rd_data.position = &_Read_cur_pos;
  sceSifWriteBackDCache(&_rd_data, sizeof(_rd_data));
  sceCdCbfunc_num = SCECdFuncRead;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_READ_IOP, 1, &_rd_data, sizeof(_rd_data), 0, 0, cd_callback,
                    (void*)&sceCdCbfunc_num) < 0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  if (SCE_CD_debug > 0)
    printf("cdread end\n");
  return 1;
}

static unsigned _gt_data[3] __attribute__((aligned(16)));
static unsigned char tocbf[2064] __attribute__((aligned(64)));
static unsigned _lbn_prm;

int sceCdGetToc(void* destination) {
  int result;
  if (!ncmd_prechk(CD_NCMD_CHECK_GET_TOC))
    return 0;
  _gt_data[0] = (unsigned)tocbf;
  sceSifWriteBackDCache(tocbf, sizeof(tocbf));
  sceSifWriteBackDCache(_gt_data, sizeof(_gt_data));
  sceCdCbfunc_num = 3;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_GET_TOC, 0, _gt_data, sizeof(_gt_data), ncmdrdata, 8, 0, 0) <
      0) {
    sceCdCbfunc_num = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  sceCdCbfunc_num = 0;
  result = *(int*)ncmdrdata;
  if (((int*)ncmdrdata)[1] == 0)
    memcpy(destination, EE_UNCACHED_ADDR(tocbf), 1024);
  else
    memcpy(destination, EE_UNCACHED_ADDR(tocbf), 2064);
  SignalSema(ncmd_semid);
  return result;
}

static int _ncmd_bind = -1;

int sceCdSeek(unsigned sector) {
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_SEEK))
    return 0;
  _lbn_prm = sector;
  sceSifWriteBackDCache(&_lbn_prm, 4);
  sceCdCbfunc_num = SCECdFuncSeek;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_SEEK, 1, &_lbn_prm, 4, 0, 0, cd_callback,
                    (void*)&sceCdCbfunc_num) < 0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  return 1;
}

int sceCdStandby(void) {
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_STANDBY))
    return 0;
  sceCdCbfunc_num = SCECdFuncStandby;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_STANDBY, 1, 0, 0, 0, 0, cd_callback,
                    (void*)&sceCdCbfunc_num) < 0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  return 1;
}

int sceCdStop(void) {
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_STOP))
    return 0;
  sceCdCbfunc_num = SCECdFuncStop;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_STOP, 1, 0, 0, 0, 0, cd_callback, (void*)&sceCdCbfunc_num) <
      0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  return 1;
}

int sceCdPause(void) {
  if (sceCdNcmdDiskReady() == SCECdNotReady)
    return 0;
  if (!ncmd_prechk(CD_NCMD_CHECK_PAUSE))
    return 0;
  sceCdCbfunc_num = SCECdFuncPause;
  c_cb_sem = 1;
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_PAUSE, 1, 0, 0, 0, 0, cd_callback, (void*)&sceCdCbfunc_num) <
      0) {
    sceCdCbfunc_num = 0;
    c_cb_sem = 0;
    SignalSema(ncmd_semid);
    return 0;
  }
  return 1;
}

int sceCdSync(int mode) {
  if (mode == 0) {
    if (SCE_CD_debug > 0)
      printf("N cmd wait\n");
    while (c_cb_sem || sceSifCheckStatRpc(&cdrd))
      sceCdDelayThread(60);
    return 0;
  }
  if (c_cb_sem || sceSifCheckStatRpc(&cdrd))
    return 1;
  return 0;
}

static unsigned char scmdrdata[1056] __attribute__((aligned(64)));
static SifRpcClient cdrc;

static int _scmd_bind = -1, _scmd_dr_bind = -1;

int sceCdSyncS(int mode) {
  if (mode == 0) {
    if (SCE_CD_debug > 0)
      printf("S cmd wait\n");
    while (sceSifCheckStatRpc(&cdrc))
      sceCdDelayThread(60);
    return 0;
  }
  return sceSifCheckStatRpc(&cdrc);
}

static int scmd_prechk(int command) {
  int delay, result;
  cmd_sem_init();
  result = PollSema(scmd_semid);
  if (scmd_semid != result) {
    if (SCE_CD_debug > 0)
      printf("Scmd fail sema cur_cmd:%d keep_cmd:%d\n", command, scmd_sema_keep_cmd);
    return 0;
  }
  scmd_sema_keep_cmd = command;
  ReferThreadStatus(my_thid, &my_th_info);
  if (sceCdSyncS(1)) {
    SignalSema(scmd_semid);
    return 0;
  }
  sceSifInitRpc(0);
  if (_scmd_bind < 0) {
    for (;;) {
      if (sceSifBindRpc(&cdrc, CD_RPC_SCMD, 0) < 0) {
        if (SCE_CD_debug > 0)
          printf("Libcdvd bind err S cmd\n");
        for (delay = 0x100000; delay--;)
          ;
        continue;
      }
      if (cdrc.server)
        break;
      for (delay = 0x100000; delay--;)
        ;
    }
    _scmd_bind = 0;
  }
  return 1;
}

static SifRpcClient cdit, cddr;
static int _i_mode __attribute__((aligned(16)));

static int _it_bind = -1;

int sceCdInit(int mode) {
  static int c_cnt = 0;
  unsigned char* reply;
  int delay, bind_result, status, cd_version, dvd_version, result;
  if (sceCdSyncS(1))
    return 0;
  sceSifInitRpc(0);
  my_thid = GetThreadId();
  Init_seq = 1;
  _sf_bind = -1;
  _rd_bind = -1;
  _ncmd_bind = -1;
  _scmd_bind = -1;
  _scmd_dr_bind = -1;
  c_cnt++;
  _it_bind = -1;
  _icmd_bind = -1;
  while (1) {
    bind_result = sceSifBindRpc(&cdit, CD_RPC_INIT, 0);
    if (bind_result < 0) {
      if (SCE_CD_debug > 0)
        printf("Libcdvd bind err %d CD_Init %d\n", bind_result, c_cnt);
      for (delay = 0x100000; delay--;)
        ;
    } else if (cdit.server) {
      _i_mode = mode;
      _it_bind = 0;
      sceSifWriteBackDCache(&_i_mode, 4);
      reply = scmdrdata;
      if (sceSifCallRpc(&cdit, 0, 0, &_i_mode, 4, reply, 16, 0, 0) < 0) {
        Init_seq = 0;
        return 0;
      }
      break;
    } else {
      for (delay = 0x100000; delay--;)
        ;
    }
  }
  cd_version = *(int*)EE_UNCACHED_ADDR(reply + 4);
  dvd_version = *(int*)EE_UNCACHED_ADDR(reply + 8);
  status = *(int*)EE_UNCACHED_ADDR(reply + 12);
  result = 1;
  if (status == 255) {
  } else if (status == 254) {
    SCE_CD_debug = 1;
  } else if (cd_version / 256 < 2 || dvd_version / 256 < 2) {
    result = 2;
  }
  Init_seq = 0;
  switch (mode) {
    case SCECdEXIT:
      if (SCE_CD_debug > 0)
        printf("Libcdvd Exit\n");
      cdvd_exit();
      ncmd_semid = -1;
      scmd_semid = -1;
      cb_semid = -1;
      break;
    case SCECdINIT:
    case SCECdINoD:
    default:
      cmd_sem_init();
      PowerOffCB();
      break;
  }
  return result;
}

static int _dr_mode __attribute__((aligned(16)));

int sceCdDiskReady(int mode) {
  int result, delay;
  if (SCE_CD_debug > 0)
    printf("DiskReady 0\n");
  cmd_sem_init();
  result = PollSema(scmd_semid);
  if (scmd_semid != result)
    return SCECdNotReady;
  if (sceCdSyncS(1)) {
    SignalSema(scmd_semid);
    if (mode == 8)
      return -1;
    return SCECdNotReady;
  }
  sceSifInitRpc(0);
  if (_scmd_dr_bind < 0) {
    for (;;) {
      if (sceSifBindRpc(&cddr, CD_RPC_DISK_READY, 0) < 0) {
        if (SCE_CD_debug > 0)
          printf("Libcdvd bind err CdDiskReady\n");
        for (delay = 0x100000; delay--;)
          ;
        continue;
      }
      if (cddr.server) {
        _scmd_dr_bind = 0;
        break;
      }
      for (delay = 0x100000; delay--;)
        ;
    }
  }
  _dr_mode = mode;
  sceSifWriteBackDCache(&_dr_mode, 4);
  if (sceSifCallRpc(&cddr, 0, 0, &_dr_mode, 4, scmdrdata, 4, 0, 0) < 0) {
    SignalSema(scmd_semid);
    if (mode == 8)
      return -1;
    return SCECdNotReady;
  }
  if (SCE_CD_debug > 0)
    printf("DiskReady ended\n");
  SignalSema(scmd_semid);
  return *(int*)EE_UNCACHED_ADDR(scmdrdata);
}

int sceCdBreak(void) {
  if (!scmd_prechk(CD_SCMD_CHECK_BREAK))
    return 0;
  sceCdCbfunc_num = SCECdFuncBreak;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_BREAK, 0, 0, 0, scmdrdata, 4, 0, 0) < 0) {
    SignalSema(scmd_semid);
    sceCdCbfunc_num = 0;
    return 0;
  }
  sceCdCbfunc_num = 0;
  SignalSema(scmd_semid);
  return *(int*)EE_UNCACHED_ADDR(scmdrdata);
}

int sceCdGetDiskType(void) {
  if (!scmd_prechk(CD_SCMD_CHECK_GET_DISK_TYPE))
    return 0;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_GET_DISK_TYPE, 0, 0, 0, scmdrdata, 4, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return 0;
  }
  SignalSema(scmd_semid);
  return *(int*)EE_UNCACHED_ADDR(scmdrdata);
}

int sceCdStatus(void) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_STATUS))
    return -1;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_STATUS, 0, 0, 0, scmdrdata, 4, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return -1;
  }
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  if (SCE_CD_debug > 1)
    printf("status called\n");
  return result;
}

int sceCdGetError(void) {
  if (!scmd_prechk(CD_SCMD_CHECK_GET_ERROR))
    return -1;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_GET_ERROR, 0, 0, 0, scmdrdata, 4, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return -1;
  }
  SignalSema(scmd_semid);
  return *(int*)EE_UNCACHED_ADDR(scmdrdata);
}

int _sceCdRI(void* data, unsigned* status) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_RI))
    return 0;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_RI, 0, 0, 0, scmdrdata, 16, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return 0;
  }
  memcpy(data, EE_UNCACHED_ADDR(scmdrdata + 8), 8);
  *status = *(unsigned*)EE_UNCACHED_ADDR(scmdrdata + 4);
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

int _sceCdRM(void* data, unsigned* status) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_RM))
    return 0;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_RM, 0, 0, 0, scmdrdata, 24, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return 0;
  }
  memcpy(data, EE_UNCACHED_ADDR(scmdrdata + 8), 16);
  *status = *(unsigned*)EE_UNCACHED_ADDR(scmdrdata + 4);
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

int sceCdPowerOff(unsigned* status) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_POWEROFF))
    return 0;
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_POWEROFF, 0, 0, 0, scmdrdata, 8, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return 0;
  }
  *status = *(unsigned*)EE_UNCACHED_ADDR(scmdrdata + 4);
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

static int _mmode __attribute__((aligned(16)));

int sceCdMmode(int value) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_MMODE))
    return 0;
  _mmode = value;
  sceSifWriteBackDCache(&_mmode, 4);
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_MMODE, 0, &_mmode, 4, scmdrdata, 4, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return 0;
  }
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

static int _prio __attribute__((aligned(16)));

int sceCdChangeThreadPriority(int value) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_CHANGE_THREAD_PRIORITY))
    return -1;
  _prio = value;
  sceSifWriteBackDCache(&_prio, 4);
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_CHANGE_THREAD_PRIORITY, 0, &_prio, 4, scmdrdata, 4, 0, 0) <
      0) {
    SignalSema(scmd_semid);
    return -1;
  }
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

int sceCdReadClock(sceCdCLOCK* clock) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_READ_CLOCK))
    return 0;
  if (SCE_CD_debug > 0)
    printf("Libcdvd call Clock read 1\n");
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_READ_CLOCK, 0, 0, 0, scmdrdata, 16, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return 0;
  }
  memcpy(clock, EE_UNCACHED_ADDR(scmdrdata + 4), 8);
  if (SCE_CD_debug > 0)
    printf("Libcdvd call Clock read 2\n");
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

static struct {
  unsigned short command, size;
  unsigned char data[128];
} _acn_data __attribute__((aligned(16)));

int sceCdApplyNCmd(unsigned char command, const void* data, int size) {
  int delay;
  WaitSema(ncmd_semid);
  ncmd_sema_keep_cmd = CD_NCMD_CHECK_APPLY_NCMD;
  sceCdSync(0);
  if (SCE_CD_debug > 0)
    printf("Apply_N_Cmd 0\n");
  sceSifInitRpc(0);
  if (_rd_bind < 0) {
    for (;;) {
      if (sceSifBindRpc(&cdrd, CD_RPC_NCMD, 0) < 0) {
        if (SCE_CD_debug > 0)
          printf("Libcdvd bind err N cmd\n");
        for (delay = 0x100000; delay--;)
          ;
        continue;
      }
      if (cdrd.server) {
        _rd_bind = 0;
        break;
      }
      for (delay = 0x100000; delay--;)
        ;
    }
  }
  _acn_data.command = command;
  _acn_data.size = size;
  memcpy(_acn_data.data, data, size);
  if (SCE_CD_debug > 0)
    printf("Apply_N_Cmd 1\n");
  sceSifWriteBackDCache(&_acn_data, sizeof(_acn_data));
  while (sceSifCallRpc(&cdrd, CD_RPC_NCMD_APPLY_NCMD, 0, &_acn_data, sizeof(_acn_data), ncmdrdata,
                       4, 0, 0) < 0)
    ;
  if (SCE_CD_debug > 0)
    printf("Apply_N_Cmd 2\n");
  SignalSema(ncmd_semid);
  return 1;
}

static int TrayReq __attribute__((aligned(16)));

int sceCdTrayReq(int mode, unsigned* status) {
  int result;
  if (!scmd_prechk(CD_SCMD_CHECK_TRAY_REQ))
    return -1;
  TrayReq = mode;
  sceSifWriteBackDCache(&TrayReq, 4);
  if (sceSifCallRpc(&cdrc, CD_RPC_SCMD_TRAY_REQ, 0, &TrayReq, 4, scmdrdata, 8, 0, 0) < 0) {
    SignalSema(scmd_semid);
    return -1;
  }
  if (status)
    *status = *(unsigned*)EE_UNCACHED_ADDR(scmdrdata + 4);
  result = *(int*)EE_UNCACHED_ADDR(scmdrdata);
  SignalSema(scmd_semid);
  return result;
}

static int stm_status = 0;

static sceCdRMode dum_mode __attribute__((aligned(8)));

int sceCdStInit(unsigned size, unsigned banks, void* buffer) {
  stm_status = 0;
  return sceCdStream(size, banks, buffer, CD_STREAM_INIT, &dum_mode);
}

int sceCdStStart(unsigned sector, const sceCdRMode* mode) {
  stm_status = 1;
  return sceCdStream(sector, 0, 0, CD_STREAM_START, mode);
}

int sceCdStSeekF(unsigned sector) {
  return sceCdStream(sector, 0, 0, CD_STREAM_SEEK_FAST, &dum_mode);
}

int sceCdStSeek(unsigned sector) {
  return sceCdStream(sector, 0, 0, CD_STREAM_SEEK, &dum_mode);
}

int sceCdStStop(void) {
  stm_status = 0;
  return sceCdStream(0, 0, 0, CD_STREAM_STOP, &dum_mode);
}

unsigned sceCdStRead(unsigned count, void* buffer, unsigned mode, unsigned* error) {
  unsigned result, total;
  register unsigned read __asm__("$17");
  register unsigned saved_error __asm__("$23");
  if (SCE_CD_debug > 0)
    printf("sceCdStRead call read size= %d mode= %d\n", count, mode);
  if (!stm_status)
    return 0;
  saved_error = 0;
  total = 0;
  sceSifWriteBackDCache(buffer, count * 2048);
  if (mode) {
    do {
      result =
          sceCdStream(0, count - total, (char*)buffer + total * 2048, CD_STREAM_READ, &dum_mode);
      mode = result >> 16;
      read = result & 0xffff;
      total += read;
      if (mode) {
        saved_error = (unsigned short)mode;
        if (SCE_CD_debug > 0)
          printf("sceCdStRead BLK Read cur_size= %d read_size= %d req_size= %d err 0x%x\n", total,
                 read, count, mode);
      } else if (!read) {
        sceCdDelayThread(8);
      }
    } while (total != count && (!mode || read));
    if (SCE_CD_debug > 0)
      printf("sceCdStRead BLK Read Ended\n");
    *error = saved_error;
  } else {
    result = sceCdStream(0, count, buffer, CD_STREAM_READ, &dum_mode);
    mode = result >> 16;
    total = result & 0xffff;
    *error = mode;
  }
  return total;
}

int sceCdStPause(void) {
  stm_status = 0;
  if (SCE_CD_debug > 0)
    printf("sceCdStPause call\n");
  return sceCdStream(0, 0, 0, CD_STREAM_PAUSE, &dum_mode);
}

int sceCdStResume(void) {
  stm_status = 1;
  if (SCE_CD_debug > 0)
    printf("sceCdStResume call\n");
  return sceCdStream(0, 0, 0, CD_STREAM_RESUME, &dum_mode);
}

int sceCdStStat(void) {
  if (SCE_CD_debug > 0)
    printf("sceCdStStat call\n");
  return sceCdStream(0, 0, 0, CD_STREAM_STAT, &dum_mode);
}

static struct {
  unsigned sector, count;
  void* buffer;
  unsigned command;
  sceCdRMode mode;
} _rs_data __attribute__((aligned(16)));

int sceCdStream(unsigned sector,
                unsigned count,
                void* buffer,
                unsigned command,
                const sceCdRMode* mode) {
  int result;
  if (!ncmd_prechk(CD_NCMD_CHECK_STREAM))
    return 0;
  if (SCE_CD_debug > 0)
    printf("call cdreadstm call\n");
  _rs_data.sector = sector;
  _rs_data.count = count;
  _rs_data.buffer = buffer;
  _rs_data.command = command;
  if (mode) {
    _rs_data.mode.trycount = mode->trycount;
    _rs_data.mode.spindlctrl = mode->spindlctrl;
    _rs_data.mode.datapattern = mode->datapattern;
  }
  if (SCE_CD_debug > 0)
    printf("call cdreadstm cmd\n");
  sceSifWriteBackDCache(&_rs_data, sizeof(_rs_data));
  if (sceSifCallRpc(&cdrd, CD_RPC_NCMD_STREAM, 0, &_rs_data, sizeof(_rs_data), ncmdrdata, 4, 0, 0) <
      0) {
    SignalSema(ncmd_semid);
    return 0;
  }
  if (SCE_CD_debug > 0)
    printf("cdread end\n");
  result = *(int*)EE_UNCACHED_ADDR(ncmdrdata);
  SignalSema(ncmd_semid);
  return result;
}
