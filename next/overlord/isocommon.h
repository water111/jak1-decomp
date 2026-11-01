#ifndef ISOCOMMON_H
#define ISOCOMMON_H

#include "sce/iop/thread.h"

// Streaming disc read buffer sizes
#define BUFFER_PAGE_SIZE 0xc000
#define STR_BUFFER_DATA_SIZE 0x6000

#define SECTOR_SIZE 2048

// Command IDs
#define LOAD_TO_EE_CMD_ID 0x100
#define LOAD_TO_IOP_CMD_ID 0x101
#define LOAD_TO_EE_OFFSET_CMD_ID 0x102
#define LOAD_DGO_CMD_ID 0x200
#define LOAD_SOUND_BANK 0x300
#define LOAD_MUSIC 0x380
#define QUEUE_VAG_STREAM 0x400
#define PLAY_VAG_STREAM 0x401
#define STOP_VAG_STREAM 0x402
#define PAUSE_VAG_STREAM 0x403
#define CONTINUE_VAG_STREAM 0x404
#define SET_VAG_VOLUME 0x405
#define SET_DIALOG_VOLUME 0x406
#define SHUTDOWN_ISO_CMD_ID 0x666

// Name of file on ISO, with consistent padding and uppercase 8.3 format
// First 8 bytes are name, uppercase, with trailing spaces
// Next 3 bytes are extension, uppercase, with trailing space
// Final byte is null-terminator.
typedef union IsoName {
  char chars[12];
  unsigned int words[3];
} IsoName;

// Record of where a file is located on DVD and its size.
typedef struct FileRecord {
  IsoName name;
  int location;  // sector number
  int size;      // in bytes
} FileRecord;

// Currently open file in iso.c's state machine.
typedef struct LoadStackEntry {
  FileRecord* file;
  int location;  // sector number
} LoadStackEntry;

// Sound effect information needed for OVERLORD to play it.
typedef struct SoundRecord {
  char name[16];
#if REVISION <= 20010903
  int falloff_min;
  int falloff_max;
  int falloff_curve;
#else
  unsigned int falloff;
#endif
} SoundRecord;

// List of sounds. In earlier revision, this was a fixed size.
// In newer revisions, the sound records just follow.
typedef struct SoundBank {
  char name[16];
  void* bank_handle;
  int sound_count;
#if REVISION <= 20010903
  SoundRecord sound[291];
  unsigned char reserved[20];
#else
  SoundRecord sound[0];
#endif
} SoundBank;

// Per-song adjustment for music. The only tweak is just volume.
typedef struct MusicTweaks {
  int count;
  struct {
    char name[12];
    int volume;
  } tweak[32];
} MusicTweaks;

// Function-pointer interface to CD/DVD drive or other filesystem.
typedef struct IsoFs {
  int (*init)(unsigned char*);
  FileRecord* (*find)(const char*);
  FileRecord* (*find_in)(const char*);
  int (*get_length)(FileRecord*);
  LoadStackEntry* (*open)(FileRecord*, int);
#if REVISION > 20010903
  LoadStackEntry* (*open_wad)(FileRecord*, int);
#endif
  void (*close)(void*);
  int (*begin_read)(void*, void*, int);
  int (*sync_read)(void);
  int (*load_sound_bank)(char*, SoundBank*);
  int (*load_music)(char*, void**);
#if REVISION > 20011019
  void (*poll_drive)(void);
#endif
} IsoFs;

// Header to data buffer used by iso to hold disc data.
typedef struct IsoBufferHeader {
  void* data;
  int data_size;
  unsigned int buffer_size;
  struct IsoBufferHeader* next;
  unsigned char reserved[48];
} IsoBufferHeader;

typedef struct IsoMessage IsoMessage;
typedef unsigned int (*IsoCallback)(IsoMessage*, IsoBufferHeader*);

// Message sent to iso thread requesting a read or similar.
// This itself is a header for more complicated commands.
struct IsoMessage {
  struct MsgPacket packet;           // mailbox data
  int cmd_id;                        // what command to run
  unsigned int status;               // current status
  int messagebox_to_reply;           // if set, where iso should reply when done
  int thread_id;                     // caller's thread
  unsigned int ready_for_data;       // caller-controlled flag to pause/resume data stream
  IsoBufferHeader* callback_buffer;  // buffer to hold data
  IsoCallback callback_function;     // callback to receive buffered data
  LoadStackEntry* fd;                // open file for processing this message.
};

// Message sent to ISO thread to load a single file to EE/IOP.
typedef struct IsoCommandLoadSingle {
  IsoMessage header;
  FileRecord* file_record;
  unsigned char* dest_addr;
  int length;
  int length_to_copy;
  unsigned int offset;
  unsigned char* dst_ptr;
  int bytes_done;
} IsoCommandLoadSingle;

// Message sent to ISO thread to load sound bank.
typedef struct SoundBankLoadCommand {
  IsoMessage header;
  char bank_name[16];
  SoundBank* bank;
} SoundBankLoadCommand;

// Message sent to ISO thread to load music.
typedef struct MusicLoadCommand {
  IsoMessage header;
  char music_name[16];
  void** music_handle;
} MusicLoadCommand;

// The streaming audio files are packed into per-language large "WAD" files.
// Each audio clip's offset is stored in the VAGDIR, containing the name and offset.
typedef struct VagDirEntry {
  char name[8];
  int offset;
} VagDirEntry;

typedef struct VagHeader {
  unsigned int magic;
  unsigned int version;
  unsigned int reserved;
  int data_size;
  int sample_rate;
} VagHeader;

// TODO resolve this difference.
#define VAG_HEADER_SIZE 48
#define ADPCM_BLOCK_SIZE 16
#define VAG_SPU_BUFFER_HALF_SIZE 0x6000
#define VAG_SPU_BUFFER_BOTH_SIZE 0xc000
#define VAG_SPU_BUFFER_LAST_BLOCK 0x5ff0
#define SPU2_SAMPLE_RATE 48000

// ADSR settings to effectively disable envelope:
// starts at maximum volume on key-on, quickly drops to 0 on key-off.
#define VAG_ADSR1 0x000f
#define VAG_ADSR2 0x1fc0

typedef struct VagDir {
  int count;
#if REVISION <= 20011002
  VagDirEntry vag[768];
#else
  VagDirEntry vag[868];
#endif
} VagDir;

typedef struct Vec3w {
  int x, y, z;
} Vec3w;

// Message sent to play streaming audio.
typedef struct VagCommand {
  IsoMessage header;
  FileRecord* file_record;
#if REVISION > 20010903
  VagDirEntry* vag_entry;
#endif
  unsigned int buffer_count;
  int bytes_remaining;
  unsigned int started;
  unsigned int paused;
  int sample_rate;
  unsigned int invalid_header;
  // TODO: maybe rename to last_block
  int end_offset;
  unsigned int end_offset_armed;
  int volume;
  unsigned int sound_id;
  int priority;
#if REVISION > 20011002
  unsigned int is_positioned;
  Vec3w position;
#endif
} VagCommand;

enum DgoState {
  DGO_INIT,
  DGO_READ_HEADER,
  DGO_FINISH_OBJ,
  DGO_READ_LAST_OBJ,
  DGO_READ_OBJ_HEADER,
  DGO_READ_OBJ_DATA,
  DGO_FINISH
};

typedef struct DgoHeader {
  unsigned int object_count;
  char name[60];
} DgoHeader;

typedef struct ObjectHeader {
  unsigned int size;
  char name[60];
} ObjectHeader;

typedef struct DgoCommand {
  IsoMessage header;
  FileRecord* file_record;
  unsigned char* buffer1;
  unsigned char* buffer2;
  unsigned char* buffer_heap_top;
  DgoHeader dgo_header;
  ObjectHeader object_header;
  unsigned char* ee_destination_buffer;
  unsigned int bytes_processed;
  unsigned int objects_loaded;
  enum DgoState state;
  unsigned int finished_first_object;
  unsigned int buffer_index;
  unsigned char* selected_buffer;
  unsigned int abort_requested;
} DgoCommand;

#define CMD_STATUS_IN_PROGRESS (-1)
#define CMD_STATUS_DONE 0
#define CMD_STATUS_UNKNOWN_COMMAND 4
#define CMD_STATUS_FAILED_TO_OPEN 6
#define CMD_STATUS_NULL_CB 7
#define CMD_STATUS_READ_ERR 8

typedef struct VolumePair {
  short left, right;
} VolumePair;

void MakeISOName(char *dest, const char *src);

#endif
