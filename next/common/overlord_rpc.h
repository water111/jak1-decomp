#ifndef OVERLORD_RPC_H
#define OVERLORD_RPC_H

#define SPLASH_SCREEN_ADDR 0x1000000

typedef struct Vec3w {
  int x, y, z;
} Vec3w;

// RAMDISK (aka Server)
// See overlord/ramdisk.c
#define RAMDISK_RPC_ID 0xdeb3

// fnos
// Clear the entire ramdisk and load this file to IOP with given ID
#define FNO_RAMDISK_RESET_AND_LOAD 1
// Get data from the file by ID. Contents returned via RPC buffer
#define FNO_RAMDISK_GET_DATA 0
// Load an entire file to EE via ramdisk. Loaded to the given EE address.
#define FNO_RAMDISK_BYPASS_LOAD 4

// Load a file from DVD into the IOP Ramdisk
typedef struct RamdiskCommand {
  int unused;
  unsigned int file_id_or_ee_addr;
  int offset;
  int size;
  char name[16];
} RamdiskCommand;

// DGO
#define DGO_RPC_ID 0xdeb4
#define DGO_RPC_LOAD_FNO 0
#define DGO_RPC_LOAD_NEXT_FNO 1
#define DGO_RPC_CANCEL_FNO 2
#define DGO_RPC_RESULT_DONE 0
#define DGO_RPC_RESULT_ERROR 1
#define DGO_RPC_RESULT_MORE 2
#define DGO_RPC_RESULT_ABORTED 3

typedef struct DgoRpcCommand {
  unsigned short reserved;
  unsigned short result;
  unsigned int buffer1;
  unsigned int buffer2;
  unsigned int buffer_heap_top;
  char name[16];
} DgoRpcCommand;

// STR
#define STR_RPC_ID 0xdeb5
// Request aninmation chunk loads.
typedef struct StrCommand {
  unsigned short reserved;
  unsigned short result;
  unsigned int ee_addr;
  int chunk_id;  // -1 to load file directly
  unsigned int length;
  char name[48];  // anim name for anims, or file name for chunk = -1
} StrCommand;

// Play
#define PLAY_RPC_ID 0xdeb6
typedef struct PlayCommand {
  unsigned short reserved;
  unsigned short result;
  unsigned int address;
  unsigned int section;
  unsigned int maxlen;
  char name[48];
} PlayCommand;

enum SoundCommand {
  LOAD_BANK,
  LOAD_MUSIC_CMD,
  UNLOAD_BANK,
  PLAY_SOUND,
  PAUSE_SOUND,
  STOP_SOUND,
  CONTINUE_SOUND,
  SET_PARAM,
  SET_MASTER_VOLUME,
  PAUSE_GROUP,
  STOP_GROUP,
  CONTINUE_GROUP,
  GET_IRX_VERSION,
  SET_FALLOFF_CURVE,
  SET_SOUND_FALLOFF,
  RELOAD_INFO,
  SET_LANGUAGE,
  SET_FLAVA,
  SET_REVERB,
  SET_EAR_TRANS,
  SHUTDOWN,
  LIST_SOUNDS,
  UNLOAD_MUSIC,
#if REVISION >= 20011104 && !defined(DEMO) && !defined(REGION_KR)
  SET_FPS
#endif
};

typedef struct SoundParams {
  unsigned short mask;
  short pitch_mod, bend, fo_min, fo_max;
  signed char fo_curve, priority;
  int volume;
  Vec3w position;
  unsigned char group, reg[3];
} SoundParams;

typedef struct SoundRpcCommand {
  unsigned short reserved, command;
  union {
    struct {
      unsigned int major, minor, ee_addr;
    } version;
    struct {
      unsigned int pad[3];
      char name[16];
    } bank;
    struct {
      unsigned int id;
    } language;
    struct {
      unsigned int id, pad[2];
      char name[16];
      SoundParams params;
    } play;
    struct {
      unsigned int id;
      SoundParams params;
      unsigned char automatic[8];
    } param;
    struct {
      unsigned char group;
      int volume;
    } master;
    struct {
      unsigned char group;
    } group;
    struct {
      unsigned char value;
    } flava;
    struct {
      unsigned char core;
      int type, left, right;
    } reverb;
#if REVISION >= 20011104 && !defined(DEMO) && !defined(REGION_KR)
    struct {
      unsigned char fps;
    } fps;
#endif
    struct {
      Vec3w ear, camera;
      int angle;
    } ear;
    struct {
      int curve, falloff, ease;
    } curve;
    struct {
      unsigned int pad[3];
      char name[16];
      int curve, min, max;
    } falloff;
    unsigned char payload[76];
  } args;
} SoundRpcCommand;

typedef struct SoundIopInfo {
  unsigned int frame;
  int strpos, stream_id;
  unsigned int freemem;
  unsigned char chinfo[48];
#if REVISION <= 20011002
  unsigned int freemem2;
  unsigned int times[41], times_seq;
#else
  unsigned int freemem2, nocd, dirtycd, diskspeed[2], lastspeed;
  int dupseg;
  unsigned int times[41], times_seq;
#endif
} SoundIopInfo;

#endif
