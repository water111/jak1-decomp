// Memory-card requests and asynchronous EE callbacks.
#include "next/kernel/kmemcard.h"
#include "next/kernel/fileio.h"
#include "next/kernel/kdgo.h"
#include "sce/ee/libkernl/sifdev.h"
#include "sce/ee/libmc/libmc.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

// 20010903 september preview
// 20010920 japanese demo
// 20011002 cingular demo
// 20011019 october proto
// 20011031 ntsc retail     SMALLER SAVE
// 20011102 ops2m demo      SMALLER SAVE
// 20011104 pal retail      BIGGER SAVE
// 20011111 pal demo        SMALLER SAVE ????
// 20011121 jp retail       BIGGER SAVE
// 20011127 ntsc gh         BIGGER SAVE
// 20020121 korea           BIGGER SAVE

#if REVISION < 20011019
#define SAVE_SIZE 657
#elif REVISION == 20011104 || REVISION == 20011121 || REVISION == 20011127
#define SAVE_SIZE 692
#else
#define SAVE_SIZE 691
#endif

#define BANK_SIZE 0x10000
#define MEM_CARD_MAGIC 0x12345678

// TODO: share with IOP code??
#define RAMDISK_BYPASS_LOAD_FILE 4

#if REVISION < 20010920
#define ICON_LOCAL(name, early, demo, retail) name "." #early
#elif REVISION < 20011019
#define ICON_LOCAL(name, early, demo, retail) name "." #demo
#else
#define ICON_LOCAL(name, early, demo, retail) name "." #retail
#endif

typedef void (*McCallbackFunc)(int);
static McCallbackFunc callback;
// Callback state: p1 is the card slot; p2-p4 are reused for probe results,
// file indexes, file descriptors, save counters, and bank selection.
static int p1, p2, p3, p4;
#if REVISION < 20010920
static MemoryCardOperation op = {0, 0, 1, 0, 0, 0};
#else
static MemoryCardOperation op = {0, 0, 1, 0, 0, 0, 0};
#endif

// TODO: share with IOP code??
struct RPC_Ramdisk_LoadCmd {
  unsigned int pad;
  unsigned int file_id_or_ee_addr;
  unsigned int offset_into_file;
  unsigned int size;
  char name[16];
};

static RPC_Ramdisk_LoadCmd ramdisk_cmd;
static sceMcIconSys iconsys;
static MemoryCard mc[2];
static sceMcTblGetDir dirent;

// Each bank has identical 1 KiB headers before and after its save data.
struct McHeader {
  unsigned int save_count, checksum, magic;
  int preview_data[16];
  unsigned char data[944];
  unsigned int save_count2;
};

extern McHeader header;

static void cb_probe(int);
static void cb_getdir(int);
static void cb_check_open(int);
static void cb_check_read(int);
static void cb_check_close(int);
static void cb_reprobe(int);
static void cb_reprobe_format(int);
static void cb_format_complete(int);
static void cb_unformat(int);
static void cb_reprobe_createfile(int);
static void cb_wait_for_ramdisk(int);
static void cb_wait_for_ramdisk_load(int);
static void cb_createfile_erasing(int);
static void cb_createdir(int);
static void cb_createdfile(int);
static void cb_writtenfile(int);
static void cb_closedfile(int);
static void cb_reprobe_save(int);
#if REVISION < 20010920
static void cb_openedreinit(int);
static void cb_writtenreinit(int);
static void cb_closedreinit(int);
#endif
static void cb_openedsave(int);
static void cb_savedheader(int);
static void cb_saveddata(int);
static void cb_savedfooter(int);
static void cb_closedsave(int);
static void cb_reprobe_load(int);
static void cb_openedload(int);
static void cb_readload(int);
static void cb_closedload(int);
static void ASCII2SJIS(char*, char*);

#ifdef REGION_EU
#define PRODUCT_CODE "BESCES-50361"
#elif defined(REGION_JP) && defined(DEMO)
#define PRODUCT_CODE "BISCPS-20078"
#elif defined(REGION_JP)
#define PRODUCT_CODE "BISCPS-15021"
#else
#define PRODUCT_CODE "BASCUS-97124"
#endif
#define MEMCARD_GAME_ID PRODUCT_CODE "AYBABTU!"
#define MEMCARD_GAME_PATH "/" MEMCARD_GAME_ID

static char* filename[12] = {MEMCARD_GAME_PATH,
                             MEMCARD_GAME_PATH "/icon.sys",
                             MEMCARD_GAME_PATH "/icon.ico",
                             MEMCARD_GAME_PATH "/" MEMCARD_GAME_ID,
                             MEMCARD_GAME_PATH "/bank0.bin",
                             MEMCARD_GAME_PATH "/bank1.bin",
                             MEMCARD_GAME_PATH "/bank2.bin",
                             MEMCARD_GAME_PATH "/bank3.bin",
                             MEMCARD_GAME_PATH "/bank4.bin",
                             MEMCARD_GAME_PATH "/bank5.bin",
                             MEMCARD_GAME_PATH "/bank6.bin",
                             MEMCARD_GAME_PATH "/bank7.bin"};

#undef MEMCARD_GAME_PATH
#undef MEMCARD_GAME_ID
#undef PRODUCT_CODE

#if REVISION >= 20011019
static char* titles[6] = {"Jak and Daxter",
                          "Jak and Daxter",
                          "Jak und Daxter",
                          "Jak and Daxter",
                          "Jak and Daxter",
                          "\x83\x57\x83\x83\x83\x62\x83\x4e\x81\x7e\x83\x5f\x83"
                          "\x4e\x83\x58\x83\x5e\x81\x5b"};
#endif

#if REVISION >= 20011019
static int language = 0;
#endif

// Allocate a nonzero handle.
static int new_mc_handle() {
  static int next __asm__("next.525") = 0;
  next++;
  if (next == 0) {
    next++;
  }
  return next;
}

// Compute the checksum of save data one word at a time.
static unsigned int mc_checksum(void* data, int size) {
  unsigned int result = 0;
  size /= 4;
  for (int i = 0; i < size; i++) {
    unsigned int word = ((unsigned int*)data)[i];
    result = (result << 1) ^ ((int)result >> 31) ^ word ^ MEM_CARD_MAGIC;
  }
  return result ^ 0xedd1e666;
}

// Find a card in the requested state, or return -1.
static int handle_to_slot(int handle, int state) {
  if (mc[0].state == state && mc[0].handle == handle) {
    return 0;
  } else if (mc[1].state == state && mc[1].handle == handle) {
    return 1;
  }
  return -1;
}

// Run the memory card state machine.
void MC_run() {
#ifdef DEMO
  return;
#endif

  if (callback) {
    int sony_cmd, sony_status;
    int status = sceMcSync(1, &sony_cmd, &sony_status);
    if (status == sceMcExecRun) {
      return;
    } else if (status == sceMcExecFinish) {
      McCallbackFunc callback_for_sync = callback;
      callback = 0;
      (*callback_for_sync)(sony_status);
      if (callback) {
        return;
      }
    } else {
      McCallbackFunc callback_for_sync = callback;
      callback = 0;
      (*callback_for_sync)(0);
      if (callback) {
        return;
      }
    }
  }

  if (op.operation == MemoryCardOperationKind::FORMAT) {
    p1 = handle_to_slot(op.param, MemoryCardState::OPEN_BUT_UNFORMATTED);
    if (p1 == -1) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::BAD_HANDLE;
      return;
    }

    if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == sceMcResSucceed) {
      callback = cb_reprobe_format;
    }

    if (--op.retry_count == 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    }

    return;
  } else if (op.operation == MemoryCardOperationKind::UNFORMAT) {
    p1 = handle_to_slot(op.param, MemoryCardState::FORMATTED);
    if (p1 == -1) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::BAD_HANDLE;
      return;
    }

    if (sceMcUnformat(p1, 0) == sceMcResSucceed) {
      callback = cb_unformat;
    }
    if (--op.retry_count == 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    }

    return;
  } else if (op.operation == MemoryCardOperationKind::CREATE_FILE) {
    p1 = handle_to_slot(op.param, MemoryCardState::FORMATTED);
    if (p1 == -1) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::BAD_HANDLE;
      return;
    }

    if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == sceMcResSucceed) {
      callback = cb_reprobe_createfile;
    }

    if (--op.retry_count == 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    }

    return;
  } else if (op.operation == MemoryCardOperationKind::SAVE) {
    p1 = handle_to_slot(op.param, MemoryCardState::FORMATTED);
    if (p1 == -1) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::BAD_HANDLE;
      return;
    }

    if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == sceMcResSucceed) {
      callback = cb_reprobe_save;
    }

    if (--op.retry_count == 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    }

    return;
  } else if (op.operation == MemoryCardOperationKind::LOAD) {
    p1 = handle_to_slot(op.param, MemoryCardState::FORMATTED);
    if (p1 == -1) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::BAD_HANDLE;
      return;
    }

    if (!mc[p1].files[op.param2].present) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::NO_MEMORY;
      return;
    }

    if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == sceMcResSucceed) {
      callback = cb_reprobe_load;
    }
    if (--op.retry_count == 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    }

    return;
  }
  if (mc[0].state == MemoryCardState::UNKNOWN) {
    p1 = 0;
    if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == 0) {
      callback = cb_probe;
    }
    return;
  }
  if (mc[1].state == MemoryCardState::UNKNOWN) {
    p1 = 1;
    if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == 0) {
      callback = cb_probe;
    }
    return;
  }
  if (mc[0].state == MemoryCardState::KNOWN) {
    mc[0].countdown_to_check--;
    if (mc[0].countdown_to_check == 0) {
      mc[0].state = MemoryCardState::UNKNOWN;
    }
  } else {
    mc[0].countdown_to_check--;
    if (mc[0].countdown_to_check == 0) {
      p1 = 0;
      if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == 0) {
        callback = cb_reprobe;
      }
      return;
    }
  }
  if (mc[1].state == MemoryCardState::KNOWN) {
    mc[1].countdown_to_check--;
    if (mc[1].countdown_to_check == 0) {
      mc[1].state = MemoryCardState::UNKNOWN;
    }
  } else {
    mc[1].countdown_to_check--;
    if (mc[1].countdown_to_check == 0) {
      p1 = 1;
      if (sceMcGetInfo(p1, 0, &p2, &p3, &p4) == 0) {
        callback = cb_reprobe;
      }
      return;
    }
  }
}

// Select the language used for the PS2 browser title.
#if REVISION >= 20011019
void MC_set_language(int l) {
  printf("Language set to %d\n", l);
  language = l;
}
#endif

// Queue a format request if the memory-card interface is idle.
unsigned int MC_format(int card_idx) {
  if (op.operation != MemoryCardOperationKind::NO_OP) {
    return 0;
  }
  op.operation = MemoryCardOperationKind::FORMAT;
  op.param = card_idx;
  op.result = McStatusCode::BUSY;
  op.retry_count = 100;
#ifdef DEMO
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::BAD_HANDLE;
#endif
  return 1;
}

// Queue an unformat request if the memory-card interface is idle.
unsigned int MC_unformat(int card_idx) {
  if (op.operation != MemoryCardOperationKind::NO_OP) {
    return 0;
  }
  op.operation = MemoryCardOperationKind::UNFORMAT;
  op.param = card_idx;
  op.result = McStatusCode::BUSY;
  op.retry_count = 100;
#ifdef DEMO
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::BAD_HANDLE;
#endif
  return 1;
}

// Queue creation of the save directory, using the caller's scratch buffer.
unsigned int MC_createfile(int param, void* data) {
  if (op.operation != MemoryCardOperationKind::NO_OP) {
    return 0;
  }
  op.operation = MemoryCardOperationKind::CREATE_FILE;
  op.param = param;
  op.result = McStatusCode::BUSY;
  op.retry_count = 100;
  op.data_ptr = data;
#ifdef DEMO
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::BAD_HANDLE;
#endif
  return 1;
}

// Queue a save.
#if REVISION < 20010920
unsigned int MC_save(int card_idx, int file_idx, void* save_data) {
#else
unsigned int MC_save(int card_idx, int file_idx, void* save_data, int* save_summary_data) {
#endif
  if (op.operation != MemoryCardOperationKind::NO_OP) {
    return 0;
  }
  op.operation = MemoryCardOperationKind::SAVE;
  op.param = card_idx;
  op.param2 = file_idx;
  op.result = McStatusCode::BUSY;
  op.retry_count = 100;
  op.data_ptr = save_data;
#if REVISION >= 20010920
  op.data_ptr2 = save_summary_data;
#endif
#ifdef DEMO
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::BAD_HANDLE;
#endif
  return 1;
}

// Queue a load into the caller's buffer, reading both backup banks.
unsigned int MC_load(int card_idx, int file_idx, void* data) {
  if (op.operation != MemoryCardOperationKind::NO_OP) {
    return 0;
  }
  op.operation = MemoryCardOperationKind::LOAD;
  op.param = card_idx;
  op.param2 = file_idx;
  op.result = McStatusCode::BUSY;
  op.retry_count = 100;
  op.data_ptr = data;
#ifdef DEMO
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::BAD_HANDLE;
#endif
  return 1;
}

// Create a diagnostic file containing bytes from a hard-coded buffer address.
// (unused)
#if REVISION >= 20010920
void MC_makefile(int port, int size) {
  sceMcMkdir(port, 0, "/BASCUS-00000XXXXXXXX");

  int cmd, result, fd;
  sceMcSync(0, &cmd, &result);

  if (result != sceMcResSucceed && result != sceMcResNoEntry) {
    printf("Can\'t create garbage folder [%d]\n", result);
  } else {
    sceMcOpen(port, 0, "/BASCUS-00000XXXXXXXX/BASCUS-00000XXXXXXXX", SCE_CREAT | SCE_WRONLY);
    sceMcSync(0, &cmd, &fd);

    if (fd < 0) {
      printf("Can\'t open file on memcard [%d]\n", fd);
    } else {
      sceMcWrite(fd, (void*)0x1000000, size);
      sceMcSync(0, &cmd, &result);
      if (result != size) {
        printf("Only written %d bytes\n", result);
      }
      sceMcClose(fd);
      sceMcSync(0, &cmd, &result);
    }
  }
}
#endif

// Return the pending operation's status or the last completed result.
unsigned int MC_check_result() {
  return (unsigned int)op.result;
}

// Copy cached card and save-preview information to GOAL.
void MC_get_status(int slot, mc_slot_info* info) {
  info->handle = 0;
  info->known = 0;
  info->formatted = 0;
#if REVISION > 20011019
  info->initted = 0;
#endif
  for (int i = 0; i < 4; i++) {
    info->files[i].present = 0;
  }
  info->last_file = 0xffffffff;
  info->mem_required = SAVE_SIZE;
  info->mem_actual = 0;
#ifdef DEMO
  return;
#endif

  switch (mc[slot].state) {
    default:
      break;
#if REVISION > 20011019
    case MemoryCardState::KNOWN:
      info->known = 1;
      break;
#endif
    case MemoryCardState::OPEN_BUT_UNFORMATTED:
#if REVISION > 20011019
      info->known = 1;
#endif
      info->handle = mc[slot].handle;
      break;
    case MemoryCardState::FORMATTED:
#if REVISION <= 20011019
      info->handle = mc[slot].handle;
      info->known = 1;
#else
      info->known = 1;
      info->handle = mc[slot].handle;
      info->formatted = 1;
#endif
      if (mc[slot].inited != 0) {
#if REVISION <= 20011019
        info->formatted = 1;
#else
        info->initted = 1;
#endif

        for (int file = 0; file < 4; file++) {
          info->files[file].present = mc[slot].files[file].present;
#if REVISION >= 20010920
          for (int i = 0; i < 16; i++) {
            info->files[file].data[i] = mc[slot].files[file].data[i];
          }
#endif
        }
        info->last_file = mc[slot].last_file;
      } else {
        info->mem_actual = mc[slot].mem_size;
      }
      break;
  }
}

// Translate a Sony error and invalidate the card handle when it has
// disappeared.
static unsigned int cb_check(int sony_error, int goal_error) {
  if (sony_error >= 0) {
    return 0;
  }
  if (sony_error < -9) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::BAD_HANDLE;
    return 1;
  }
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = goal_error;
  return 1;
}

// Invalidate a card after a failed background probe.
static int cb_pcheck(int sync_result) {
  if (sync_result >= 0) {
    return 0;
  }
  mc[p1].state = MemoryCardState::UNKNOWN;
  return 1;
}

// Identify a card, then check its save directory if it is formatted.
void cb_probe(int sync_result) {
  if (sync_result < -9) {
    mc[p1].state = MemoryCardState::KNOWN;
    mc[p1].countdown_to_check = 100;
    return;
  }

  if (p2 != sceMcTypePS2) {
    mc[p1].state = MemoryCardState::KNOWN;
    mc[p1].countdown_to_check = 100;
    return;
  }

  mc[p1].handle = new_mc_handle();
  if (p4 == 0) {
    mc[p1].state = MemoryCardState::OPEN_BUT_UNFORMATTED;
    mc[p1].countdown_to_check = 100;
    return;
  }

  mc[p1].mem_size = p3;
  p2 = 0;

  if (sceMcGetDir(p1, 0, filename[p2], 0, 1, &dirent) != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
  } else {
    callback = cb_getdir;
  }
}

// Check the save files, then read the headers of all eight banks.
void cb_getdir(int sync_result) {
  if (sync_result == 0) {
    mc[p1].inited = 0;
    mc[p1].state = MemoryCardState::FORMATTED;
    mc[p1].countdown_to_check = 100;
    mc[p1].last_file = -1;
    return;
  } else if (sync_result == 1) {
    p2++;
    if (p2 == 12) {
      mc[p1].inited = 1;
      mc[p1].countdown_to_check = 100;
      mc[p1].last_file = -1;
      for (int i = 0; i < 4; i++) {
        mc[p1].files[i].present = 0;
      }
      p2 = 4;
      if (sceMcOpen(p1, 0, filename[p2], 1) != 0) {
        mc[p1].state = MemoryCardState::UNKNOWN;
      } else {
        callback = cb_check_open;
      }
      return;
    }
  } else {
    mc[p1].state = MemoryCardState::UNKNOWN;
    return;
  }
  if (sceMcGetDir(p1, 0, filename[p2], 0, 1, &dirent) != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
  } else {
    callback = cb_getdir;
  }
}

// Read the header of the bank opened by the background probe.
void cb_check_open(int sync_result) {
  if (cb_pcheck(sync_result)) {
    return;
  }
  p3 = sync_result;

  if (sceMcRead(p3, &header, sizeof(McHeader)) != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
  } else {
    callback = cb_check_read;
  }
}

// Keep the newest valid bank header for each save preview.
void cb_check_read(int sync_result) {
  if (cb_pcheck(sync_result)) {
    return;
  }
  if (header.save_count != 0 &&       // we've saved into this bank
      header.magic == MEM_CARD_MAGIC  // looks valid
  ) {
#if REVISION < 20010920
    mc[p1].files[(p2 - 4) / 2].present = 1;
    p2 |= 1;
#else
    int file_idx = (p2 - 4) / 2;
    if (mc[p1].files[file_idx].present == 0 ||  // haven't found any banks for this file
        (int)(header.save_count - mc[p1].files[file_idx].most_recent_save_count) >
            0  // newer than prev.
    ) {
      mc[p1].files[file_idx].present = 1;
      mc[p1].files[file_idx].most_recent_save_count = header.save_count;
      mc[p1].files[file_idx].last_saved_bank = p2 & 1;

      for (int i = 0; i < 16; i++) {
        mc[p1].files[file_idx].data[i] = header.preview_data[i];
      }
    }
#endif
  }

  if (sceMcClose(p3) != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
  } else {
    callback = cb_check_close;
  }
}

// Continue probing bank headers, or mark the card ready.
void cb_check_close(int sync_result) {
  if (cb_pcheck(sync_result)) {
    return;
  }

  p2++;
  if (p2 == 12) {
    mc[p1].state = MemoryCardState::FORMATTED;

    return;
  }

  if (sceMcOpen(p1, 0, filename[p2], 1) != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
  } else {
    callback = cb_check_open;
  }
}

// Refresh a known card's poll timer or invalidate it if it changed.
void cb_reprobe(int sync_result) {
  if (sync_result == sceMcResSucceed) {
    mc[p1].countdown_to_check = 100;
  } else {
    mc[p1].state = MemoryCardState::UNKNOWN;
  }
}

// Start formatting after confirming the card is still present.
void cb_reprobe_format(int sync_result) {
  if (sync_result != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::BAD_HANDLE;
    return;
  }

  if (sceMcFormat(p1, 0) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_format_complete;
  }
}

// Initialize cached card state after formatting.
void cb_format_complete(int sync_result) {
  if (sync_result != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::FORMAT_FAILED;
    return;
  }
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::OK;
  mc[p1].state = MemoryCardState::FORMATTED;
  mc[p1].countdown_to_check = 100;
  mc[p1].inited = 0;
  int i = 0;
  while (i < 4) {
    mc[p1].files[i].present = 0;
    i++;
  }
  mc[p1].last_file = -1;
  mc[p1].mem_size = 8000;
}

// Complete the unformat request and discard cached card state.
void cb_unformat(int sync_result) {
  if (sync_result != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::FORMAT_FAILED;
    return;
  }
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::OK;
  mc[p1].state = MemoryCardState::UNKNOWN;
}

// Wait for the ramdisk service before loading the browser icon.
void cb_reprobe_createfile(int sync_result) {
  if (sync_result != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::BAD_HANDLE;
    return;
  }

  if (RpcBusy(RAMDISK_RPC_CHANNEL)) {
    callback = cb_wait_for_ramdisk;
  } else {
    cb_wait_for_ramdisk(0);
  }
}

// Load SAVEGAME.ICO into the caller's scratch buffer through OVERLORD.
void cb_wait_for_ramdisk(int) {
  ramdisk_cmd.pad = 0;

  ramdisk_cmd.file_id_or_ee_addr = (unsigned int)op.data_ptr;
  ramdisk_cmd.offset_into_file = 0;
#if REVISION < 20011019
  ramdisk_cmd.size = 0x16000;
#else
  ramdisk_cmd.size = 0x1e800;
#endif
  memcpy(ramdisk_cmd.name, "SAVEGAME.ICO\0\0\0\0", 16);
  RpcCall(RAMDISK_RPC_CHANNEL, RAMDISK_BYPASS_LOAD_FILE, 1, &ramdisk_cmd, sizeof(ramdisk_cmd), 0,
          0);
  callback = cb_wait_for_ramdisk_load;
}

// Wait for icon loading, then begin deleting the old save files.
void cb_wait_for_ramdisk_load(int) {
  if (RpcBusy(RAMDISK_RPC_CHANNEL) != 0) {
    callback = cb_wait_for_ramdisk_load;
    return;
  }

  p2 = 11;  // filenames left to delete
  if (sceMcDelete(p1, 0, filename[p2]) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_createfile_erasing;
  }
}

// Delete the remaining save files and recreate their directory.
void cb_createfile_erasing(int sync_result) {
  if (sync_result != sceMcResSucceed && sync_result != sceMcResNoEntry &&
      sync_result != sceMcResNotEmpty) {
    if (sync_result == sceMcResDeniedPermit) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
      return;
    }
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::BAD_HANDLE;
    return;
  }
  mc[p1].inited = 0;

  if (p2 > 0) {
    p2--;
    if (sceMcDelete(p1, 0, filename[p2]) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_createfile_erasing;
    }
    return;
  }

  if (sceMcMkdir(p1, 0, filename[0]) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_createdir;
  }
}

// Prepare the browser metadata, then begin creating the save files.
void cb_createdir(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }

  static int bgcolor[4][4] __asm__(ICON_LOCAL("bgcolor", 604, 607, 610))
      __attribute__((aligned(16))) = {{0}};
#if REVISION < 20011019
  static float lightdir[3][4] __asm__(ICON_LOCAL("lightdir", 605, 608, 611))
      __attribute__((aligned(16))) = {{0.5, 0.5, 0.5, 0}, {0, -0.4, -0.1, 0}, {-0.5, -0.5, 0.5, 0}};
  static float lightcol[3][4] __asm__(ICON_LOCAL("lightcol", 606, 609, 612))
      __attribute__((aligned(16))) = {
          {0.48, 0.48, 0.03, 0}, {0.5, 0.33, 0.2, 0}, {0.14, 0.14, 0.38, 0}};
  static float ambient[4] __asm__(ICON_LOCAL("ambient", 607, 610, 613))
      __attribute__((aligned(16))) = {0.5, 0.5, 0.5, 0};
#else
  static float lightdir[3][4] __asm__(ICON_LOCAL("lightdir", 605, 608, 611))
      __attribute__((aligned(16))) = {
          {0.612, 0.5, 0.612, 0}, {-0.696, 0.174, -0.696, 0}, {0, 0, 0, 0}};
  static float lightcol[3][4] __asm__(ICON_LOCAL("lightcol", 606, 609, 612))
      __attribute__((aligned(16))) = {{0.5, 0.5, 0.5, 0}, {0.25, 0.3, 1, 0}, {0, 0, 0, 0}};
  static float ambient[4] __asm__(ICON_LOCAL("ambient", 607, 610, 613))
      __attribute__((aligned(16))) = {0.2, 0.2, 0.2, 0};
#endif

  memset(&iconsys, 0, sizeof(iconsys));
  kstrcpy(iconsys.Head, "PS2D");
#if REVISION < 20011019
  ASCII2SJIS(iconsys.TitleName, "Jak and Daxter");
#else
  if (language != 5) {
    ASCII2SJIS(iconsys.TitleName, titles[language]);
  } else {
    kstrcpy(iconsys.TitleName, titles[language]);
  }
#endif
  iconsys.OffsLF = 32;
  iconsys.TransRate = 0;
  memcpy(iconsys.BgColor, bgcolor, sizeof(bgcolor));
  memcpy(iconsys.LightDir, lightdir, sizeof(lightdir));
  memcpy(iconsys.LightColor, lightcol, sizeof(lightcol));
  memcpy(iconsys.Ambient, ambient, sizeof(ambient));
  kstrcpy(iconsys.FnameView, "icon.ico");
  kstrcpy(iconsys.FnameCopy, "icon.ico");
  kstrcpy(iconsys.FnameDel, "icon.ico");

  p2 = 1;

  if (sceMcOpen(p1, 0, filename[p2], 0x203) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_createdfile;
  }
  return;
}

// Write browser metadata, icon data, the message file, or an empty save bank.
void cb_createdfile(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  p3 = sync_result;
  if (p2 == 1) {
    if (sceMcWrite(p3, &iconsys, sizeof(iconsys)) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_writtenfile;
    }
  } else if (p2 == 2) {
#if REVISION < 20011019
    if (sceMcWrite(p3, op.data_ptr, 0x16000) != 0) {
#elif REVISION < 20011031
    if (sceMcWrite(p3, op.data_ptr, 0x1e800) != 0) {
#else
    if (sceMcWrite(p3, op.data_ptr, 0x1e5c8) != 0) {
#endif
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_writtenfile;
    }
  }

  else if (p2 == 3) {
    kstrcpy((char*)op.data_ptr, "Nope, the save game data isn\'t in this file!\n");
    if (sceMcWrite(p3, op.data_ptr, strlen((char*)op.data_ptr)) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_writtenfile;
    }
  } else {
    memset((unsigned char*)op.data_ptr, 0, 0x11800);
    if (sceMcWrite(p3, (unsigned char*)op.data_ptr, 0x11800) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_writtenfile;
    }
  }
}

// Close the newly written file.
void cb_writtenfile(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  if (sceMcClose(p3) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_closedfile;
  }
}

// Create the next file or complete save-directory initialization.
void cb_closedfile(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  p2++;
  if (p2 < 0xc) {
    if (sceMcOpen(p1, 0, filename[p2], 0x203) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_createdfile;
    }
    return;
  }

  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::OK;

  mc[p1].inited = 1;

  for (int i = 0; i < 4; i++) {
    mc[p1].files[i].present = 0;
  }
  mc[p1].last_file = -1;
}

// Select the alternate bank and increment its save counter.
void cb_reprobe_save(int sync_result) {
  if (sync_result != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::BAD_HANDLE;
    return;
  }
#if REVISION < 20010920
  if (mc[p1].last_file != op.param2) {
    p2 = 1;
    p4 = 0;
    if (sceMcOpen(p1, 0, filename[op.param2 * 2 + 4], 2) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_openedreinit;
    }
    return;
  } else {
    p2 = mc[p1].most_recent_save_count + 1;
    p4 = mc[p1].last_saved_bank ^ 1;
    if (p2 == 0) {
      p2 = 1;
    }
  }
#else
  if (mc[p1].files[op.param2].present) {
    p2 = mc[p1].files[op.param2].most_recent_save_count + 1;  // increment save count
    p4 = mc[p1].files[op.param2].last_saved_bank ^ 1;         // use the other bank
  } else {
    p2 = 0;  // save count 0
    p4 = 0;  // first bank for file
  }

  if (p2 == 0) {
    p2 = 1;
  }
#endif
  if (sceMcOpen(p1, 0, filename[op.param2 * 2 + (4 + p4)], 2) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_openedsave;
  }
}

#if REVISION < 20010920
// Initialize both banks before the first save to a file.
void cb_openedreinit(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  p3 = sync_result;
  memset(&header, 0, sizeof(McHeader));
  if (sceMcWrite(p3, &header, sizeof(McHeader)) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_writtenreinit;
  }
}

void cb_writtenreinit(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  if (sceMcClose(p3) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_closedreinit;
  }
}

void cb_closedreinit(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  p4++;
  if (p4 < 2) {
    if (sceMcOpen(p1, 0, filename[op.param2 * 2 + 5], 2) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_openedreinit;
    }
  } else {
    p4 = 0;
    if (sceMcOpen(p1, 0, filename[op.param2 * 2 + (4 + p4)], 2) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_openedsave;
    }
  }
}
#endif

// Build and write the save header, including checksum and preview.
void cb_openedsave(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  p3 = sync_result;
  memset(&header, 0, sizeof(McHeader));
  header.save_count = p2;
  header.checksum = mc_checksum(op.data_ptr, BANK_SIZE);
  header.magic = MEM_CARD_MAGIC;
  header.save_count2 = p2;
#if REVISION >= 20010920
  for (int i = 0; i < 16; i++) {
    header.preview_data[i] = op.data_ptr2[i];
  }
#endif
  if (sceMcWrite(p3, &header, sizeof(McHeader)) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_savedheader;
  }
}

// Write the save data after its header.
void cb_savedheader(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  if (sceMcWrite(p3, (unsigned char*)op.data_ptr, BANK_SIZE) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_saveddata;
  }
}

// Write a duplicate header after the save data.
void cb_saveddata(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  if (sceMcWrite(p3, &header, sizeof(McHeader)) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_savedfooter;
  }
}

// Close the completed save bank.
void cb_savedfooter(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  if (sceMcClose(p3) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_closedsave;
  }
}

// Publish the new save counter, bank, and preview to the cached card state.
void cb_closedsave(int sync_result) {
  if (cb_check(sync_result, McStatusCode::WRITE_ERROR)) {
    return;
  }
  op.operation = MemoryCardOperationKind::NO_OP;
  op.result = McStatusCode::OK;
  mc[p1].files[op.param2].present = 1;
#if REVISION < 20010920
  mc[p1].last_file = op.param2;
  mc[p1].most_recent_save_count = p2;
  mc[p1].last_saved_bank = p4;
#else
  mc[p1].files[op.param2].most_recent_save_count = p2;
  mc[p1].files[op.param2].last_saved_bank = p4;
  for (int i = 0; i < 16; i++) {
    mc[p1].files[op.param2].data[i] = op.data_ptr2[i];
  }
  mc[p1].last_file = op.param2;
#endif
}

// Begin loading the first bank after confirming the card is still present.
void cb_reprobe_load(int sync_result) {
  if (sync_result != 0) {
    mc[p1].state = MemoryCardState::UNKNOWN;
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::BAD_HANDLE;
    return;
  }
  p2 = 0;
  if (sceMcOpen(p1, 0, filename[op.param2 * 2 + (4 + p2)], 1) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_openedload;
  }
}

// Read a complete bank, including its header and footer.
void cb_openedload(int sync_result) {
  if (cb_check(sync_result, McStatusCode::READ_ERROR)) {
    return;
  }
  p3 = sync_result;
  if (sceMcRead(p3, (unsigned char*)op.data_ptr + p2 * (BANK_SIZE + 2 * sizeof(McHeader)),
                (BANK_SIZE + 2 * sizeof(McHeader))) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_readload;
  }
}

// Close the bank before inspecting it or opening the next one.
void cb_readload(int sync_result) {
  if (cb_check(sync_result, McStatusCode::READ_ERROR)) {
    return;
  }
  if (sceMcClose(p3) != 0) {
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::INTERNAL_ERROR;
  } else {
    callback = cb_closedload;
  }
}

// Validate both banks and select the newest intact save.
void cb_closedload(int sync_result) {
  if (cb_check(sync_result, McStatusCode::READ_ERROR)) {
    return;
  }
  p2++;
  if (p2 < 2) {
    if (sceMcOpen(p1, 0, filename[op.param2 * 2 + (4 + p2)], 1) != 0) {
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::INTERNAL_ERROR;
    } else {
      callback = cb_openedload;
    }
    return;
  }
  {
    int ok[2];
    McHeader* headers[2];
    McHeader* footers[2];

    headers[0] = (McHeader*)((unsigned char*)op.data_ptr);
    footers[0] = (McHeader*)((unsigned char*)op.data_ptr + sizeof(McHeader) + BANK_SIZE);
    headers[1] = (McHeader*)((unsigned char*)op.data_ptr + sizeof(McHeader) * 2 + BANK_SIZE);
    footers[1] = (McHeader*)((unsigned char*)op.data_ptr + sizeof(McHeader) * 3 + BANK_SIZE * 2);
    ok[0] = true;
    ok[1] = true;

    for (int idx = 0; idx < 2; idx++) {
      unsigned int expected_save_count = headers[idx]->save_count;
      if (headers[idx]->save_count2 != expected_save_count ||
          footers[idx]->save_count != expected_save_count ||
          footers[idx]->save_count2 != expected_save_count) {
        ok[idx] = 0;
        continue;
      }
      if (headers[idx]->magic != MEM_CARD_MAGIC || footers[idx]->magic != MEM_CARD_MAGIC) {
        ok[idx] = 0;
        continue;
      }
      if (headers[idx]->checksum != footers[idx]->checksum) {
        ok[idx] = 0;
        continue;
      }
      // Checksum save data one word at a time.
      if (headers[idx]->checksum != mc_checksum(headers[idx] + 1, BANK_SIZE)) {
        ok[idx] = 0;
        continue;
      }
    }

    if (!ok[0] && !ok[1]) {
      if (headers[0]->save_count == 0 && headers[0]->checksum == 0 && headers[0]->magic == 0 &&
          headers[0]->save_count2 == 0 && headers[1]->save_count == 0 &&
          headers[1]->checksum == 0 && headers[1]->magic == 0 && headers[1]->save_count2 == 0) {
        op.operation = MemoryCardOperationKind::NO_OP;
        op.result = McStatusCode::NEW_GAME;
        mc[p1].last_file = op.param2;
#if REVISION < 20010920
        mc[p1].most_recent_save_count = 0;
        mc[p1].last_saved_bank = 1;
#endif
        return;
      }
      op.operation = MemoryCardOperationKind::NO_OP;
      op.result = McStatusCode::READ_ERROR;

      return;
    }

    int bank = 0;

    if (ok[0] && ok[1]) {
      int first_count = headers[0]->save_count;
      int second_count = headers[1]->save_count;
      if (first_count - second_count > 0) {
        bank = 0;
      } else {
        bank = 1;
      }
    } else if (ok[1]) {
      bank = 1;
    }

    unsigned int current_save_count = headers[bank]->save_count;
    memcpy((unsigned char*)op.data_ptr,
           (unsigned char*)op.data_ptr +
               (bank * (BANK_SIZE + 2 * sizeof(McHeader)) + sizeof(McHeader)),
           BANK_SIZE);
    mc[p1].last_file = op.param2;
#if REVISION < 20010920
    mc[p1].most_recent_save_count = current_save_count;
    mc[p1].last_saved_bank = bank;
#else
    mc[p1].files[op.param2].most_recent_save_count = current_save_count;
    mc[p1].files[op.param2].last_saved_bank = bank;
#endif
    op.operation = MemoryCardOperationKind::NO_OP;
    op.result = McStatusCode::OK;
  }
}

// Punctuation is irregular in Shift-JIS
struct AsciiEntry {
  unsigned short sjis, ascii;
};

AsciiEntry ascii_special[] = {
    {0x8140, ' '}, {0x8149, '!'}, {0x8168, '"'}, {0x8194, '#'}, {0x8190, '$'}, {0x8193, '%'},
    {0x8195, '&'}, {0x8166, 39},  {0x8169, '('}, {0x816a, ')'}, {0x8196, '*'}, {0x817b, '+'},
    {0x8143, ','}, {0x817c, '-'}, {0x8144, '.'}, {0x815e, '/'}, {0x8146, ':'}, {0x8147, ';'},
    {0x8171, '<'}, {0x8181, '='}, {0x8172, '>'}, {0x8148, '?'}, {0x8197, '@'}, {0x816d, '['},
    {0x818f, 92},  {0x816e, ']'}, {0x814f, '^'}, {0x8151, '_'}, {0x8165, '`'}, {0x816f, '{'},
    {0x8162, '|'}, {0x8170, '}'}, {0x8150, '~'}};
static AsciiEntry ascii_table[] = {{0x824f, '0'}, {0x8260, 'A'}, {0x8281, 'a'}};

// Convert one printable ASCII character to a full-width Shift-JIS character.
static unsigned short Ascii2Sjis(unsigned char c) {
  unsigned short result = 0;
  unsigned char range;
  unsigned char special = 0;
  if (c >= 0x20 && c <= 0x2f) {
    special = 1;
  } else if (c >= 0x30 && c <= 0x39) {
    range = 0;
  } else if (c >= 0x3a && c <= 0x40) {
    special = 11;
  } else if (c >= 0x41 && c <= 0x5a) {
    range = 1;
  } else if (c >= 0x5b && c <= 0x60) {
    special = 37;
  } else if (c >= 0x61 && c <= 0x7a) {
    range = 2;
  } else if (c >= 0x7b && c <= 0x7e) {
    special = 63;
  } else {
    return 0;
  }
  if (special) {
    result = ascii_special[(c - 0x20) - (special - 1)].sjis;
  } else {
    result = ascii_table[range].sjis + c - ascii_table[range].ascii;
  }
  return result;
}

// Write the browser title as big-endian Shift-JIS, with a two-byte terminator.
static void ASCII2SJIS(char* dest, char* source) {
  unsigned short c;
  while (*source) {
    c = Ascii2Sjis(*source++);
    *dest++ = c >> 8;
    *dest++ = c;
  }
  *dest++ = 0;
  *dest++ = 0;
}

McHeader header = {0};
