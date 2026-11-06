#ifndef NEXT_KERNEL_KMEMCARD_H
#define NEXT_KERNEL_KMEMCARD_H

namespace MemoryCardState {
enum Value {
  UNKNOWN = 0,  //
  KNOWN = 1,
  OPEN_BUT_UNFORMATTED = 2,
  FORMATTED = 3
};
}  // namespace MemoryCardState

struct MemoryCardFile {
  int present;
#if REVISION >= 20010920
  int most_recent_save_count;
  int last_saved_bank;
  int data[16];
#endif
};

struct MemoryCard {
  int state;
  int handle;
  int countdown_to_check;
  int inited;
#if REVISION < 20010920
  MemoryCardFile files[4];
  int last_file;
  int most_recent_save_count;
  int last_saved_bank;
  int mem_size;
#else
  int last_file;
  int mem_size;
  MemoryCardFile files[4];
#endif
};

namespace MemoryCardOperationKind {
enum Value {
  NO_OP = 0,
  FORMAT = 1,       // Erase and format a card.
  UNFORMAT = 2,     // Remove a card's filesystem.
  CREATE_FILE = 3,  // Create the save directory and its files.
  SAVE = 4,
  LOAD = 5,
};
}  // namespace MemoryCardOperationKind

namespace McStatusCode {
enum Value {
  BUSY = 0,
  OK = 1,
  BAD_HANDLE = 2,
  FORMAT_FAILED = 3,
  INTERNAL_ERROR = 4,
  WRITE_ERROR = 5,
  READ_ERROR = 6,
  NEW_GAME = 7,
  NO_MEMORY = 8,
  NO_CARD = 9,
  NO_LAST = 10,
  NO_FORMAT = 11,
  NO_FILE = 12,
  NO_SAVE = 13,
  NO_SPACE = 14,
  BAD_VERSION = 15,
  NO_PROCESS = 16,
  NO_AUTO_SAVE = 17
};
}  // namespace McStatusCode

struct MemoryCardOperation {
  int operation;
  int param;
  int param2;
  int result;
  int retry_count;
  void* data_ptr;
#if REVISION >= 20010920
  int* data_ptr2;
#endif
};

struct mc_file_info {
#if REVISION < 20010920
  unsigned char present;
#else
  int present;
  int data[16];
#endif
};

struct mc_slot_info {
  int handle;
  int known;
  int formatted;
#if REVISION < 20010920
  mc_file_info files[4];
  int last_file;
  int mem_required;
  int mem_actual;
#else
#if REVISION > 20011019
  int initted;
#endif
  int last_file;
  int mem_required;
  int mem_actual;
  mc_file_info files[4];
#endif
};

void MC_run();
unsigned int MC_format(int);
unsigned int MC_unformat(int);
unsigned int MC_createfile(int, void*);
#if REVISION < 20010920
unsigned int MC_save(int, int, void*);
#else
unsigned int MC_save(int, int, void*, int*);
#endif
unsigned int MC_load(int, int, void*);
unsigned int MC_check_result();
void MC_get_status(int, mc_slot_info*);
#if REVISION >= 20010920
void MC_makefile(int, int);
#endif
#if REVISION >= 20011019
void MC_set_language(int);
#endif

#endif
