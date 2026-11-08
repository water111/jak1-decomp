#include "sce/iop/cdvdstm/internal.h"
#include "sce/iop/sysclib/string.h"

static unsigned int stream_callback(void* argument);
static unsigned int read_timeout(void* argument);

static int drive_error = 0;
static volatile unsigned int buffer_sectors = 0;
static volatile unsigned int bank_bytes = 0;
static volatile unsigned int bank_count = 0;
static unsigned char* volatile stream_buffer = 0;
static volatile unsigned int bank_sectors = 0;
static volatile int stream_error = 0;
static int retries = 0;

static sceSifDmaData transfer;
static unsigned int last_read_lsn;
static volatile unsigned char bank_ready[CDVDSTM_MAX_BANKS];
static volatile unsigned int next_lsn;
static volatile unsigned int write_bank;
static volatile unsigned int read_bank;
static volatile unsigned int read_offset;
static volatile sceCdRMode read_mode;
static volatile int stream_state;
static struct SysClock alarm_clock;

static unsigned int read_timeout(void* argument) {
  struct SysClock* clock = argument;
  unsigned int milliseconds = clock->low / CDVDSTM_CLOCKS_PER_MSEC;
  Kprintf("Stm EE Read Time Out %d(msec)\n", milliseconds);
  _sceCdSC(CDVD_SC_TIMEOUT_OCCURRED, (int*)&milliseconds);
  return !sceCdBreak();
}

void cdvdstm_ee_stream(struct CdvdStreamRequest* request, int size, void* reply_data) {
  void* reply = reply_data;
  unsigned int lsn = request->lsn;
  unsigned char* destination = request->buffer;
  unsigned int bytes = request->sectors;
  unsigned int command = request->command;

  unsigned int previous, wanted, remaining, chunk;

  int started = 0;
  unsigned int i;
  int intr_state;
  unsigned int result;
  int read_result;
  int dma_id;
  bytes *= CDVDSTM_SECTOR_SIZE;
  if (stream_state == 2 && command != CDVDSTM_SEEK_FAST && command != CDVDSTM_STOP) {
    goto stream_failed;

  } else if (command == CDVDSTM_RESUME) {
    _sceCdSC(CDVD_STREAM_EE, &drive_error);
    if (sceCdNop()) {
      result = 1;
    } else {
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      result = 0;
    }
    memcpy(reply, &result, sizeof(result));
    return;
  } else if (command == CDVDSTM_PAUSE) {
    CpuSuspendIntr(&intr_state);
    CancelAlarm(stream_callback, &alarm_clock);
    _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
    CpuResumeIntr(intr_state);
    sceCdSync(0);
    result = 1;
    memcpy(reply, &result, sizeof(result));
    CancelAlarm(read_timeout, &alarm_clock);
    return;
  } else if (command == CDVDSTM_STAT) {
    unsigned int current_bank = read_bank;
    // Keep the cached bank indices in the registers used by this SDK build.
    register unsigned int saved_write asm("$6") = write_bank;
    register unsigned int saved_read asm("$5") = current_bank;
    if (!bank_ready[current_bank]) {
      if ((current_bank = saved_read + 1) >= bank_count)
        current_bank = 0;
      if (!bank_ready[current_bank])
        current_bank = saved_read;
    }
    for (i = 0; i < bank_count; i++) {
      if (!bank_ready[current_bank] || saved_write == current_bank)
        break;
      if (++current_bank >= bank_count)
        current_bank = 0;
    }
    i = i * (bank_bytes / CDVDSTM_SECTOR_SIZE);
    memcpy(reply, &i, sizeof(i));
    return;
  } else if (command == CDVDSTM_INIT) {
    unsigned int banks;
    _sceCdstm1Cb(stream_callback);
    banks = bytes / CDVDSTM_SECTOR_SIZE;
    wanted = lsn / banks;
    buffer_sectors = lsn;
    bank_sectors = wanted;
    bank_bytes = wanted * CDVDSTM_SECTOR_SIZE;
    stream_buffer = destination;
    bank_count = banks;
    printf("Stream Buffer 1Bank %dbyte %dbanks %dbyte used\n", bank_bytes, bank_count,
           bank_bytes * bank_count);
    result = 1;
  reply_simple:
    memcpy(reply, &result, sizeof(result));
    return;
  } else {
    if (command == CDVDSTM_STOP) {
      CpuSuspendIntr(&intr_state);
      stream_state = 0;
      CancelAlarm(stream_callback, &alarm_clock);
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      CpuResumeIntr(intr_state);
      sceCdBreak();
      i = 0;
      if (bank_count)
        do {
          bank_ready[i] = 0;
          i++;
        } while (i < bank_count);
      read_offset = 0;
      read_bank = 0;
      write_bank = 0;
      result = 1;
      memcpy(reply, &result, sizeof(result));
      sceCdSync(0);
      drive_error = 0;
      _sceCdSC(CDVD_SC_SET_ERROR, &drive_error);
      CancelAlarm(read_timeout, &alarm_clock);
      return;
    }
    if (command == CDVDSTM_START) {
      read_mode.datapattern = request->mode.datapattern;
      read_mode.trycount = request->mode.trycount;
      read_mode.spindlctrl = request->mode.spindlctrl;
      stream_error = 0;
    }
    if (command == CDVDSTM_SEEK_FAST) {
      if (!_sceCdSC(CDVD_SC_STATUS, &drive_error)) {
        goto stream_failed;
      }
      CpuSuspendIntr(&intr_state);
      next_lsn = lsn;
      i = 0;
      if (bank_count)
        do {
          bank_ready[i] = 0;
          i++;
        } while (i < bank_count);
      stream_state = 2;
      CpuResumeIntr(intr_state);
      result = 1;
      memcpy(reply, &result, sizeof(result));
      return;
    }
    if (command == CDVDSTM_SEEK) {
      started = 1;
      CpuSuspendIntr(&intr_state);
      CancelAlarm(stream_callback, &alarm_clock);
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      bytes = 0;
      command = CDVDSTM_START;
      CpuResumeIntr(intr_state);
      next_lsn = lsn;
      read_offset = 0;
      read_bank = 0;
      write_bank = 0;
      sceCdSync(0);
      CancelAlarm(read_timeout, &alarm_clock);
    }
    if (command == CDVDSTM_START) {
      CpuSuspendIntr(&intr_state);
      CancelAlarm(stream_callback, &alarm_clock);
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      CpuResumeIntr(intr_state);
      started = 1;
      i = 0;
      if (bank_count)
        do {
          bank_ready[i] = 0;
          i++;
        } while (i < bank_count);
      next_lsn = lsn;
      read_offset = 0;
      read_bank = 0;
      sceCdSync(0);
      CancelAlarm(read_timeout, &alarm_clock);
      read_result = bytes / bank_bytes;
      if (bytes % bank_bytes)
        read_result++;
      for (write_bank = 0; (int)write_bank < read_result; write_bank++) {
        result = _sceCdRE(next_lsn, bank_sectors, stream_buffer, (sceCdRMode*)&read_mode);
        sceCdSync(3);
        _sceCdSC(CDVD_SC_STATUS, &drive_error);
        if (drive_error || !result) {
          _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
          goto stream_failed;
        }
        next_lsn += bank_sectors;
        bank_ready[write_bank] = 1;
      }
      stream_state = 1;
      _sceCdSC(CDVD_STREAM_EE, &drive_error);
      if (!sceCdNop()) {
        _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      stream_failed:
        result = 0;
        goto reply_simple;
      }
    }
    result = -1;
    remaining = bytes;
    while (remaining) {
      unsigned int available;
      if (!bank_ready[read_bank]) {
        if (cdvdstm_debug > 0)
          Kprintf("CD read buffer over run %d %d %d %d %d gp %d pp %d\n", bank_ready[0],
                  bank_ready[1], bank_ready[2], bank_ready[3], bank_ready[4], write_bank,
                  read_bank);
        CpuSuspendIntr(&intr_state);
        previous = read_bank;
        if (++read_bank >= bank_count)
          read_bank = 0;
        if (!bank_ready[read_bank])
          read_bank = previous;
        result = bytes - remaining;
        CpuResumeIntr(intr_state);
        break;
      }
      available = bank_bytes - read_offset;
      chunk = remaining;
      if (available < chunk)
        chunk = available;
      transfer.data = (unsigned int)(stream_buffer + read_bank * bank_bytes + read_offset);
      transfer.addr = (unsigned int)destination;
      transfer.size = chunk;
      transfer.mode = 0;
      if (chunk) {
        for (;;) {
          CpuSuspendIntr(&intr_state);
          dma_id = sceSifSetDma(&transfer, 1);
          CpuResumeIntr(intr_state);
          if (dma_id)
            break;
          DelayThread(CDVDSTM_DMA_RETRY_USEC);
        }
        read_offset += chunk;
        while (sceSifDmaStat(dma_id) >= 0) {
        }
      }
      if (read_offset >= bank_bytes) {
        CpuSuspendIntr(&intr_state);
        read_offset = 0;
        bank_ready[read_bank] = 0;
        previous = read_bank;
        if (++read_bank >= bank_count)
          read_bank = 0;
        if (!bank_ready[read_bank] || write_bank == read_bank) {
          read_bank = previous;
          CpuResumeIntr(intr_state);
          if (cdvdstm_debug > 0)
            Kprintf("CD read buffer over run %d %d %d %d %d gp %d pp %d\n", bank_ready[0],
                    bank_ready[1], bank_ready[2], bank_ready[3], bank_ready[4], write_bank,
                    read_bank);
          remaining -= chunk;
          result = bytes - remaining;
          break;
        }
        CpuResumeIntr(intr_state);
      }
      remaining -= chunk;
      destination += chunk;
    }
    if (result == (unsigned int)-1)
      result = bytes;
    result = (int)result / CDVDSTM_SECTOR_SIZE;
    if (started)
      result = 1;
    else {
      if (_sceCdSC(CDVD_SC_STATUS, &drive_error) != CDVD_STREAM_EE && !result && !stream_error)
        stream_error = CDVDSTM_ERROR_STOPPED;
      if (stream_error) {
        result &= 0xffff;
        result |= stream_error << 16;
        stream_error = 0;
      }
    }
    memcpy(reply, &result, sizeof(result));
    return;
  }
  memcpy(reply, &result, sizeof(result));
}

static unsigned int stream_callback(void* argument) {
  int disc_type;
  unsigned int i, previous, target;
  if (cdvdstm_debug > 0)
    Kprintf("Intr EE Stm Read call\n");
  iCancelAlarm(read_timeout, &alarm_clock);
  iCancelAlarm(stream_callback, &alarm_clock);
  _sceCdSC(CDVD_SC_STATUS, &drive_error);
  disc_type = sceCdGetDiskType();
  if (!drive_error && disc_type != SCECdPS2DVD && disc_type != SCECdPS2CD &&
      disc_type != SCECdPS2CDDA && disc_type != SCECdPSCD && disc_type != SCECdPSCDDA)
    drive_error = CDVDSTM_ERROR_MEDIA;
  alarm_clock.hi = 0;
  if (stream_state)
    retries = 0;
  if (stream_state || drive_error || retries) {
    alarm_clock.low = CDVDSTM_TIMEOUT_RETRY;
  } else
    alarm_clock.low = _sceCdSC(CDVD_SC_TIMEOUT, &drive_error) * CDVDSTM_CLOCKS_PER_MSEC;
  if (drive_error) {
    if (cdvdstm_debug > 0)
      Kprintf("EE Stream read LBN= %d Error code= 0x%02x retry= %d\n", last_read_lsn, drive_error,
              retries);
    if (drive_error == SCECdErREAD || drive_error == SCECdErABRT) {
      if (retries) {
        if (cdvdstm_debug > 0)
          Kprintf("On Retry retry %d err %08x\n", retries, drive_error);
        stream_error = drive_error;
      }
      retries = CDVDSTM_RETRY_COUNT;
    } else {
      stream_error = drive_error;
      retries = 1;
    }
  }
  if (retries) {
    retries--;
    target = last_read_lsn < retries * CDVDSTM_RETRY_SECTOR_STEP
                 ? last_read_lsn + retries * CDVDSTM_RETRY_SECTOR_STEP
                 : last_read_lsn - retries * CDVDSTM_RETRY_SECTOR_STEP;
    if (_sceCdRE(target, bank_sectors, stream_buffer + write_bank * bank_bytes,
                 (sceCdRMode*)&read_mode)) {
      iSetAlarm(&alarm_clock, read_timeout, &alarm_clock);
    } else {
      if (cdvdstm_debug > 0)
        Kprintf("Stm Read Call fail\n");
      alarm_clock.low = CDVDSTM_POLL_BUSY;
      if (iSetAlarm(&alarm_clock, stream_callback, &alarm_clock) && !sceCdNop())
        _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
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
        _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
      return 0;
    }
  }
  if (stream_state == 2) {
    read_offset = 0;
    read_bank = 0;
    write_bank = 0;
    i = 0;
    if (bank_count)
      do {
        bank_ready[i] = 0;
        i++;
      } while (i < bank_count);
  }
  stream_state = 0;
  last_read_lsn = next_lsn;
  if (_sceCdRE(next_lsn, bank_sectors, stream_buffer + write_bank * bank_bytes,
               (sceCdRMode*)&read_mode)) {
    iSetAlarm(&alarm_clock, read_timeout, &alarm_clock);
  } else {
    if (cdvdstm_debug > 0)
      Kprintf("Stm Read Call1 fail\n");
    alarm_clock.low = CDVDSTM_POLL_BUSY;
    if (iSetAlarm(&alarm_clock, stream_callback, &alarm_clock) && !sceCdNop())
      _sceCdSC(CDVD_STREAM_DISABLED, &drive_error);
    retries = 1;
  }
  next_lsn += bank_sectors;
  return 0;
}
