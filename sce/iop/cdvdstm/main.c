#include "sce/iop/cdvdstm/internal.h"

static unsigned int stream_callback(void* argument);
static unsigned int read_timeout(void* argument);
static unsigned int stream_command(unsigned int lsn,
                                   unsigned int bytes,
                                   unsigned char* destination,
                                   int command,
                                   sceCdRMode* mode,
                                   int* error);
static int driver_init(void* driver);
static int driver_deinit(void);
static int driver_devctl(void* file,
                         const char* name,
                         int command,
                         void* argument,
                         int size,
                         void* result,
                         int result_size);
static int unsupported(void);
static long long unsupported_seek64(void);

char cdvdstm_build_id[16] = "PsIIcdvdstm 2710";
struct ModuleInfo Module = {"cdvd_st_driver", 0x0201};
int cdvdstm_debug = 0;
static int stream_priority = 81;
static int unloading = 0;
static volatile unsigned int buffer_sectors = 0;
static volatile unsigned int bank_bytes = 0;
static volatile unsigned int bank_count = 0;
static unsigned char* volatile stream_buffer = 0;
static volatile unsigned int bank_sectors = 0;
static int drive_error = 0;
static volatile int stream_error = 0;
static int retries = 0;

static void* operations[] = {
    driver_init, driver_deinit, unsupported, unsupported, unsupported,        unsupported,
    unsupported, unsupported,   unsupported, unsupported, unsupported,        unsupported,
    unsupported, unsupported,   unsupported, unsupported, unsupported,        unsupported,
    unsupported, unsupported,   unsupported, unsupported, unsupported_seek64, driver_devctl,
    unsupported, unsupported,   unsupported};

static unsigned int last_read_lsn;
static int semaphore;
static int event_id;
static volatile unsigned char bank_ready[CDVDSTM_MAX_BANKS];
static volatile unsigned int next_lsn;
static volatile unsigned int write_bank;
static volatile unsigned int read_bank;
static volatile unsigned int read_offset;
static volatile sceCdRMode read_mode;
static volatile int stream_state;
static struct SysClock alarm_clock;
sceCdRMode default_read_mode;

static void cancel_alarm(unsigned int (*callback)(void*), void* argument) {
  if (!QueryIntrContext())
    CancelAlarm(callback, argument);
  else
    iCancelAlarm(callback, argument);
}

static void signal_data(void) {
  if (QueryIntrContext())
    iSetEventFlag(event_id, CDVDSTM_EVENT_DATA);
  else
    SetEventFlag(event_id, CDVDSTM_EVENT_DATA);
}

static void clear_data(void) {
  if (QueryIntrContext())
    iClearEventFlag(event_id, ~CDVDSTM_EVENT_DATA);
  else
    ClearEventFlag(event_id, ~CDVDSTM_EVENT_DATA);
}

int cdvdstm_dummy(void) {
  if (cdvdstm_debug > 0)
    printf("Dummy Entry Called\n");
  return 0;
}

int cdvdstm_shutdown(void) {
  struct CdvdStreamRequest request;
  unsigned int result[8];
  request.lsn = 0;
  request.sectors = 0;
  request.buffer = 0;
  request.command = CDVDSTM_STOP;
  request.mode.datapattern = 0;
  request.mode.spindlctrl = 0;
  request.mode.trycount = 0;
  cdvdstm_ee_stream(&request, 20, result);
  sceCdStStop();
  return 0;
}

static unsigned int read_timeout(void* argument) {
  struct SysClock* clock = argument;
  Kprintf("Stm Iop Read Time Out %d(msec)\n", clock->low / CDVDSTM_CLOCKS_PER_MSEC);
  return !sceCdBreak();
}

static int read_bytes(int bytes, unsigned char* destination, int mode, int* error) {
  int status;
  unsigned int events;
  int current_error;
  int total;
  int amount;
  int last_error;
  if (cdvdstm_debug > 0)
    Kprintf("sceCdStream0 call read size= %d mode= %d addr= %08x\n", bytes, mode, destination);
  if (!_sceCdSC(CDVD_SC_STATUS, &status))
    return 0;
  total = 0;
  last_error = 0;
  if (mode) {
    signal_data();
    do {
      WaitEventFlag(event_id, CDVDSTM_EVENT_DATA, EW_AND, &events);
      amount = stream_command(0, bytes - total, destination + total, CDVDSTM_READ,
                              &default_read_mode, &current_error);
      if (bytes - total != amount)
        clear_data();
      total += amount;
      if (current_error)
        last_error = current_error;
      if (cdvdstm_debug > 0)
        Kprintf("sceCdStream0 BLK cur_size= %d req_size= %d err 0x%x\n", total, bytes,
                current_error);
    } while (total != bytes && (!current_error || amount));
    *error = last_error;
  } else {
    return stream_command(0, bytes, destination, CDVDSTM_READ, &default_read_mode, error);
  }
  return total;
}

static int read_sectors(int sectors, unsigned char* destination, int mode, int* error) {
  return read_bytes(sectors * CDVDSTM_SECTOR_SIZE, destination, mode, error) / CDVDSTM_SECTOR_SIZE;
}

static unsigned int stream_command(unsigned int lsn,
                                   unsigned int bytes,
                                   unsigned char* destination,
                                   int command,
                                   sceCdRMode* mode,
                                   int* error) {
  int intr_state;
  int started = 0;
  unsigned int previous, result, remaining, chunk;
  unsigned char* output;
  if (cdvdstm_debug > 0)
    Kprintf("CD Stream Call mode= %d\n", command);
  *error = 0;
  if (stream_state == 2 && command != CDVDSTM_SEEK_FAST && command != CDVDSTM_STOP)
    return 0;
  if (command == CDVDSTM_RESUME) {
    _sceCdSC(CDVD_STREAM_IOP, &drive_error);
    if (!sceCdNop()) {
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      return 0;
    }
    return 1;
  }
  if (command == CDVDSTM_PAUSE) {
    CpuSuspendIntr(&intr_state);
    cancel_alarm(stream_callback, &alarm_clock);
    _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
    CpuResumeIntr(intr_state);
    sceCdSync(0);
    cancel_alarm(read_timeout, &alarm_clock);
    return 1;
  }
  if (command == CDVDSTM_STAT) {
    unsigned int current_bank = read_bank;
    unsigned int saved_write = write_bank;
    unsigned int saved_read = current_bank;
    if (!bank_ready[saved_read]) {
      current_bank = 1;
      current_bank += saved_read;
      if (current_bank >= bank_count)
        current_bank = 0;
      if (!bank_ready[current_bank])
        current_bank = saved_read;
    } else
      current_bank = saved_read;
    for (command = 0; (unsigned int)command < bank_count; command++) {
      if (!bank_ready[current_bank] || saved_write == current_bank)
        break;
      if (++current_bank >= bank_count)
        current_bank = 0;
    }
    return command * (bank_bytes / CDVDSTM_SECTOR_SIZE);
  }
  if (command == CDVDSTM_INIT) {
    _sceCdstm0Cb(stream_callback);
    buffer_sectors = lsn;
    bank_sectors = lsn / bytes;
    bank_bytes = bank_sectors * CDVDSTM_SECTOR_SIZE;
    stream_buffer = destination;
    bank_count = bytes;
    Kprintf("Stream Buffer 1Bank %dbyte %dbanks addr:%08x %dbyte used.\n", bank_bytes, bank_count,
            stream_buffer, bank_bytes * bank_count);
    return 1;
  }
  if (command == CDVDSTM_STOP) {
    CpuSuspendIntr(&intr_state);
    stream_state = 0;
    cancel_alarm(stream_callback, &alarm_clock);
    _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
    CpuResumeIntr(intr_state);
    sceCdBreak();
    for (command = 0; (unsigned int)command < bank_count; command++)
      bank_ready[command] = 0;
    read_offset = 0;
    read_bank = 0;
    write_bank = 0;
    sceCdSync(0);
    drive_error = 0;
    _sceCdSC(CDVD_SC_SET_ERROR, &drive_error);
    cancel_alarm(read_timeout, &alarm_clock);
    return 1;
  }
  if (command == CDVDSTM_START) {
    read_mode.datapattern = mode->datapattern;
    read_mode.trycount = mode->trycount;
    read_mode.spindlctrl = mode->spindlctrl;
    stream_error = 0;
  }
  if (command == CDVDSTM_SEEK_FAST) {
    if (!_sceCdSC(CDVD_SC_STATUS, &drive_error))
      return 0;
    CpuSuspendIntr(&intr_state);
    next_lsn = lsn;
    for (command = 0; (unsigned int)command < bank_count; command++)
      bank_ready[command] = 0;
    stream_state = 2;
    CpuResumeIntr(intr_state);
    return 1;
  }
  if (command == CDVDSTM_SEEK) {
    started = 1;
    CpuSuspendIntr(&intr_state);
    cancel_alarm(stream_callback, &alarm_clock);
    _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
    CpuResumeIntr(intr_state);
    bytes = 0;
    command = CDVDSTM_START;
    next_lsn = lsn;
    read_offset = 0;
    read_bank = 0;
    write_bank = 0;
    sceCdSync(0);
    cancel_alarm(read_timeout, &alarm_clock);
  }
  output = destination;
  if (command == CDVDSTM_START) {
    CpuSuspendIntr(&intr_state);
    started = 1;
    cancel_alarm(stream_callback, &alarm_clock);
    _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
    CpuResumeIntr(intr_state);
    for (command = 0; (unsigned int)command < bank_count; command++)
      bank_ready[command] = 0;
    next_lsn = lsn;
    read_offset = 0;
    read_bank = 0;
    write_bank = 0;
    sceCdSync(0);
    cancel_alarm(read_timeout, &alarm_clock);
    chunk = bytes / bank_bytes;
    if (bytes % bank_bytes)
      chunk++;
    for (write_bank = 0; (int)write_bank < (int)chunk; write_bank++) {
      result = _sceCdRE(next_lsn, bank_sectors, stream_buffer, (sceCdRMode*)&read_mode);
      sceCdSync(3);
      _sceCdSC(CDVD_SC_STATUS, &drive_error);
      if (drive_error || !result) {
        _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
        return 0;
      }
      next_lsn += bank_sectors;
      bank_ready[write_bank] = 1;
    }
    stream_state = 1;
    _sceCdSC(CDVD_STREAM_IOP, &drive_error);
    if (!sceCdNop()) {
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      return 0;
    }
  }
  CpuSuspendIntr(&intr_state);
  result = -1;
  remaining = bytes;
  command = 0;
  if (remaining)
    do {
      if (!bank_ready[read_bank]) {
        if (cdvdstm_debug > 0)
          Kprintf("CD read buffer over run %d %d %d %d %d gp %d pp %d\n", bank_ready[0],
                  bank_ready[1], bank_ready[2], bank_ready[3], bank_ready[4], write_bank,
                  read_bank);
        previous = read_bank;
        if (++read_bank >= bank_count)
          read_bank = 0;
        if (!bank_ready[read_bank])
          read_bank = previous;
        result = command;
        break;
      }
      if (read_offset >= bank_bytes) {
        read_offset = 0;
        bank_ready[read_bank] = 0;
        previous = read_bank;
        if (++read_bank >= bank_count)
          read_bank = 0;
        if (!bank_ready[read_bank] || write_bank == read_bank) {
          read_bank = previous;
          if (cdvdstm_debug > 0)
            Kprintf("CD read buffer over run %d %d %d %d %d gp %d pp %d\n", bank_ready[0],
                    bank_ready[1], bank_ready[2], bank_ready[3], bank_ready[4], write_bank,
                    read_bank);
          result = command;
          break;
        }
      }
      chunk = CDVDSTM_SECTOR_SIZE;
      if (remaining < chunk)
        chunk = remaining;
      {
        register unsigned char* copy_destination asm("$4");
        register unsigned int copy_size asm("$6");
        copy_destination = output;
        copy_size = CDVDSTM_SECTOR_SIZE;
        // Preserve GCC 2.8.1 argument setup before updating the byte count; emits no code.
        asm("" : "+r"(command) : "r"(copy_destination), "r"(copy_size));
        command += chunk;
        cdvdstm_copy(copy_destination, stream_buffer + read_bank * bank_bytes + read_offset,
                     copy_size);
      }
      output += chunk;
      read_offset += chunk;
      remaining -= chunk;
    } while (command < bytes);
  if (result == (unsigned int)-1)
    result = bytes;
  CpuResumeIntr(intr_state);
  if (started)
    return 1;
  if (_sceCdSC(CDVD_SC_STATUS, &drive_error) != CDVD_STREAM_IOP && !result && !stream_error)
    stream_error = CDVDSTM_ERROR_STOPPED;
  *error = stream_error ? stream_error : 0;
  stream_error = 0;
  return result;
}

static unsigned int stream_callback(void* argument) {
  int error;
  int disc_type;
  unsigned int i, previous, target;
  if (cdvdstm_debug > 0)
    Kprintf("Intr Read call\n");
  iCancelAlarm(read_timeout, &alarm_clock);
  iCancelAlarm(stream_callback, &alarm_clock);
  _sceCdSC(CDVD_SC_STATUS, &error);
  disc_type = sceCdGetDiskType();
  if (!error && disc_type != SCECdPS2DVD && disc_type != SCECdPS2CD && disc_type != SCECdPS2CDDA &&
      disc_type != SCECdPSCD && disc_type != SCECdPSCDDA)
    error = CDVDSTM_ERROR_MEDIA;
  alarm_clock.hi = 0;
  if (stream_state)
    retries = 0;
  if (stream_state || error || retries) {
    if (cdvdstm_debug > 0)
      Kprintf("Stm Rtry stmstart:%d err:%02x retry:%d\n", stream_state, error, retries);
    alarm_clock.low = CDVDSTM_TIMEOUT_RETRY;
  } else
    alarm_clock.low = _sceCdSC(CDVD_SC_TIMEOUT, &error) * CDVDSTM_CLOCKS_PER_MSEC;
  if (error) {
    if (cdvdstm_debug > 0)
      Kprintf("IOP Stream read Error code= 0x%02x retry= %d\n", error, retries);
    if (error == SCECdErREAD || error == SCECdErABRT) {
      if (cdvdstm_debug > 0)
        Kprintf("On err %08x\n", error);
      if (retries)
        stream_error = error;
      retries = CDVDSTM_RETRY_COUNT;
    } else {
      stream_error = error;
      retries = 1;
    }
  }
  if (retries) {
    retries--;
    target = last_read_lsn < retries * CDVDSTM_RETRY_SECTOR_STEP
                 ? last_read_lsn + retries * CDVDSTM_RETRY_SECTOR_STEP
                 : last_read_lsn - retries * CDVDSTM_RETRY_SECTOR_STEP;
    if (cdvdstm_debug > 0)
      Kprintf("Stm Rtry Start Tgt=%d Cur= %d\n", last_read_lsn, target);
    if (_sceCdRE(target, bank_sectors, stream_buffer + write_bank * bank_bytes,
                 (sceCdRMode*)&read_mode)) {
      iSetAlarm(&alarm_clock, read_timeout, &alarm_clock);
    } else {
      alarm_clock.low = CDVDSTM_POLL_BUSY;
      if (iSetAlarm(&alarm_clock, stream_callback, &alarm_clock) && !sceCdNop())
        _sceCdSC(CDVD_STREAM_DISABLED, &error);
      retries++;
    }
    return 0;
  }
  if (!stream_state) {
    bank_ready[write_bank] = 1;
    previous = write_bank;
    if (++write_bank >= bank_count)
      write_bank = 0;
    if (bank_ready[write_bank] || read_bank == write_bank) {
      write_bank = previous;
      bank_ready[write_bank] = 0;
      if (cdvdstm_debug > 0)
        Kprintf("read Full %d %d %d %d %d gp %d pp %d spn %d\n", bank_ready[0], bank_ready[1],
                bank_ready[2], bank_ready[3], bank_ready[4], write_bank, read_bank,
                read_mode.spindlctrl);
      alarm_clock.low = CDVDSTM_POLL_FULL;
      if (iSetAlarm(&alarm_clock, stream_callback, &alarm_clock) && !sceCdNop())
        _sceCdSC(CDVD_STREAM_DISABLED, &error);
      return 0;
    }
  }
  if (stream_state == 2) {
    read_offset = 0;
    read_bank = 0;
    write_bank = 0;
    for (i = 0; i < bank_count; i++)
      bank_ready[i] = 0;
  }
  stream_state = 0;
  last_read_lsn = next_lsn;
  if (_sceCdRE(next_lsn, bank_sectors, stream_buffer + write_bank * bank_bytes,
               (sceCdRMode*)&read_mode)) {
    iSetAlarm(&alarm_clock, read_timeout, &alarm_clock);
  } else {
    alarm_clock.low = CDVDSTM_POLL_BUSY;
    if (iSetAlarm(&alarm_clock, stream_callback, &alarm_clock) && !sceCdNop())
      _sceCdSC(CDVD_STREAM_DISABLED, &error);
    retries = 1;
  }
  next_lsn += bank_sectors;
  return 0;
}

static int driver_init(void* device) {
  struct SemaParam param;
  param.attr = 1;
  param.initCount = 1;
  param.maxCount = 1;
  param.option = 0;
  semaphore = CreateSema(&param);
  return 0;
}

static int driver_deinit(void) {
  SignalSema(semaphore);
  DeleteSema(semaphore);
  return 0;
}

static int driver_devctl(void* file,
                         const char* name,
                         int command,
                         void* argument,
                         int size,
                         void* result,
                         int result_size) {
  struct CdvdStreamRequest* request;
  int status = 0;
  WaitSema(semaphore);
  if (unloading) {
    SignalSema(semaphore);
    return CDVDSTM_DRIVER_ERROR;
  }
  request = argument;
  switch (command) {
    case CDVDSTM_DEVCTL_STREAM:
      if (((struct CdvdStreamRequest*)argument)->command == CDVDSTM_PAUSE ||
          ((struct CdvdStreamRequest*)argument)->command == CDVDSTM_RESUME ||
          ((struct CdvdStreamRequest*)argument)->command == CDVDSTM_INIT ||
          ((struct CdvdStreamRequest*)argument)->command == CDVDSTM_STOP)
        signal_data();
      *(unsigned int*)result = stream_command(request->lsn, request->sectors, request->buffer,
                                              request->command, &request->mode, &request->error);
      break;
    case CDVDSTM_DEVCTL_READ:
      *(int*)result = read_sectors(((struct CdvdStreamRequest*)argument)->sectors,
                                   ((struct CdvdStreamRequest*)argument)->buffer,
                                   ((struct CdvdStreamRequest*)argument)->command,
                                   &((struct CdvdStreamRequest*)argument)->error);
      break;
    case CDVDSTM_DEVCTL_EE_STREAM:
      cdvdstm_ee_stream(argument, size, result);
      break;
    default:
      printf("Un-support devctl %08x\n", command);
      status = CDVDSTM_DRIVER_ERROR;
      break;
  }
  SignalSema(semaphore);
  return status;
}

static int unsupported(void) {
  printf("nulldev0 call\n");
  return CDVDSTM_DRIVER_ERROR;
}

static long long unsupported_seek64(void) {
  printf("nulldev0 call\n");
  return CDVDSTM_DRIVER_ERROR;
}

static struct IopDevice driver = {"cdrom_stm", 0x10000010, 1, "CD-ROM_STM ", operations};

int start(int argc, char** argv) {
  int status;
  int intr_state;
  int result;
  if (argc >= 0) {
    if (RegisterLibraryEntries(cdvdstm_stub))
      return 1;
    DelDrv("cdrom_stm");
    if (AddDrv(&driver)) {
      driver_deinit();
      return 1;
    }
    event_id = _sceCdSC(CDVD_SC_EVENT_ID, &status);
    return 2;
  } else {
    if (_sceCdSC(CDVD_SC_STATUS, &status))
      return 2;
    unloading = 1;
    DelDrv("cdrom_stm");
    CpuSuspendIntr(&intr_state);
    result = ReleaseLibraryEntries(cdvdstm_stub);
    CpuResumeIntr(intr_state);
    if (!result || result == -213)
      return 1;
    unloading = 0;
    return 2;
  }
  return 1;
}
