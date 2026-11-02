#include "989snd/989snd.h"
#include "overlord/iso.h"
#include "overlord/isocommon.h"
#include "overlord/overlord.h"
#include "overlord/ramdisk.h"
#include "overlord/soundcommon.h"
#include "overlord/srpc.h"
#include "sce/iop/libcdvd.h"
#include "sce/iop/string.h"
#include "sce/iop/thread.h"

// Number of files on disc.
static int sNumFiles = 0;

#if REVISION <= 20010903
#define MAX_ISO_FILES 2048
#else
#define MAX_ISO_FILES 350
#endif

#define MAX_OPEN_FILES 16

#if REVISION > 20011002
// There's a partially implemented system for storing two copies of the game data, back to back.
// The first copy starts at sArea1, the second starts at sArea1 + sAreaDiff.
static int sArea1 = 0;
static int sAreaDiff = 0;

#if REVISION >= 20011031
// When piracy is detected, set pirated = 1, triggering less-than-subtle data corruption.
// Piracy is detected by having less than 1 GB of files, likely
// their thought is that pirates would try to get the game to run on a
// CD instead of a DVD, which would be possible if the pirates removed
// some of the game's languages. (in ~2001 it was harder to copy DVDs I guess)
static int pirated = 0;
// Indirection to call sceCdMmode, which locks out the drive if its running on the
// wrong media type (DVD vs. CD).
static int (*cdmmode)(int) = 0;
#endif

// Timing measurements, used to determine which of the two
// areas is reading faster, and prefer that.
int _timesix = 0;
unsigned int _tkps[2] = {0, 0};
unsigned int _tsamps[2] = {0, 0};

// If set, load from second copy of all data.
int _dupseg = 0;
#endif

static int FS_Init(unsigned char* buffer);
static FileRecord* FS_Find(const char* name);
static FileRecord* FS_FindIN(const char* name);
static int FS_GetLength(FileRecord* file);
static LoadStackEntry* FS_Open(FileRecord* file, int offset);
#if REVISION > 20010903
static LoadStackEntry* FS_OpenWad(FileRecord* file, int offset);
#endif
static void FS_Close(void* fd);
static int FS_BeginRead(void* fd, void* buffer, int length);
static int FS_SyncRead(void);
static int FS_LoadSoundBank(char* name, SoundBank* bank);
static int FS_LoadMusic(char* name, void** handle);
#if REVISION > 20011002
static void FS_PollDrive(void);
static void CD_WaitReturn(void);
#endif

// Create the FileSystem API
IsoFs iso_cd = {FS_Init,     FS_Find,      FS_FindIN,   FS_GetLength,     FS_Open,
#if REVISION > 20010903
                FS_OpenWad,
#endif
                FS_Close,    FS_BeginRead, FS_SyncRead, FS_LoadSoundBank, FS_LoadMusic,
#if REVISION >= 20011031
                FS_PollDrive
#endif
};

// Drive settings. sNominalMode is likely the default.
// sNominalMode is unused, possibly the default
// sStreamMode sets 15 retries (not the default 0 -> 256 retries),
// possibly to avoid hanging on failing reads, and turns on
// CLV (constant linear velocity) which probably gives more consistent,
// but slower average read speeds. Unclear why these were chosen.
#if REVISION <= 20011002
static sceCdRMode sNominalMode = {15, SCECdSpinNom, 0, 0};
static sceCdRMode sStreamMode = {15, SCECdSpinStm, 0, 0};
static sceCdRMode* sMode = &sNominalMode;
#else
static sceCdRMode sNominalMode = {0, SCECdSpinNom, 0, 0};
static sceCdRMode sStreamMode = {15, SCECdSpinStm, 0, 0};
static sceCdRMode* sMode = &sStreamMode;
#endif

// In-progress read info.
static LoadStackEntry* sReadInfo = 0;
#if REVISION <= 20011002
unsigned int speeds_seqno = 0;
#endif

// Table of all files on DVD. Avoids the need to read filesystem
// data off the DVD on each read.
static FileRecord sFiles[MAX_ISO_FILES];

// Contents and location of the ID file. The game reads this to detect if you've
// swapped the disc to a different one. This contains "random" 32-bit integers,
// just the straight output of glibc rand().
#if REVISION > 20011002
static unsigned int CD_ID_Sector[SECTOR_SIZE / 4];
static int CD_ID_SectorNum;
static unsigned int CD_ID_SectorSum;
#endif

// Sector buffers used for initialization.
static unsigned char* sSecBuffer[3];

// List of files currently "open" by the filesystem.
static LoadStackEntry sLoadStack[MAX_OPEN_FILES];

// If set, recursive directory traversal adds files.
// This is used to avoid adding files outside Jak for
// shared demos
#if REVISION > 20010903
static int add_files;
#endif

#if REVISION <= 20011002
void* _buffer;
int _sectors;
int _sector;
unsigned int speeds[41];
#else
// Current/previous read's real DVD sector.
int _real_sector;
// Retry count during normal reading
int _retries;
// Current/previous read's destination
void* _buffer;
// Current/previous read's start time
struct SysClock _starttime;
// Current/previous read is continuous with previous
int _continuous;
// Current/previous read sector count
int _sectors;
// Current/previous read's sector, relative to area
int _sector;
// Current/previous read's end time
struct SysClock _endtime;
// Read durations
unsigned int _times[16];
#endif

// Read a possibly misaligned 32-bit little endian integer.
static unsigned int ReadU32(unsigned char* p) {
  return *p + (*(p + 1) << 8) + (*(p + 2) << 16) + (*(p + 3) << 24);
}

// Read from the drive immediately.
// This is only for metadata loading and doesn't work with
// the normal read tracking system.
// Still sets gDirtyCD if it fails.
#if REVISION <= 20011002
static int ReadSectorsNow(int sector, int count, void* buffer) {
  int error;
  if (!sceCdRead(sector, count, buffer, sMode)) {
    return 0;
  }
  sceCdSync(0);
  error = sceCdGetError();
  if (!error) {
    return 1;
  }
  return 0;
}
#else
static int ReadSectorsNow(int sector, int count, void* buffer) {
  int error;
  // invalidate read tracking.
  _sector = 0;
  for (;;) {
    if (!sceCdRead(sector, count, buffer, sMode)) {
      return 0;
    }
    sceCdSync(0);  // 0 = block
    error = sceCdGetError();
    if (!error) {
      gDirtyCD = 0;
      return 1;
    }
    gDirtyCD = 1;
  }
}
#endif

// Recursively read directory structure and add files to
// the file table. If add_files is on, it adds files.
// Otherwise, it looks for NAUGHTY.DOG folder and adds
// files under there only.
static int ReadDirectory(int sector, int length, int depth) {
  unsigned char* buffer = sSecBuffer[depth];
  unsigned char* entry;
  int record_length;
  int location;
  int file_length;
  int name_length;
  int directory;
  // BUG: used unitialized if global add_files is false.
  int name_offset;

  if (depth >= 3) {
    // overflow.
    return 1;
  }
  while (length > 0) {
    if (!ReadSectorsNow(sector, 1, buffer)) {
      return 0;
    }
    entry = buffer;
    while (*entry && entry < buffer + SECTOR_SIZE) {
      record_length = *entry;
      if (*(entry + 33) != 0 && *(entry + 33) != 1) {
        location = ReadU32(entry + 2);
        file_length = ReadU32(entry + 10);
        name_length = *(entry + 32);
        directory = 1;
#if REVISION <= 20011002
        name_offset = 0;
        while (name_offset < depth) {
          name_offset++;
        }
#endif
        if (entry[name_length + 31] == ';' && entry[name_length + 32] == '1') {
          directory = 0;
        }
        if (directory) {
#if REVISION <= 20010903
          if (!ReadDirectory(location, file_length, depth + 1)) {
            return 0;
          }
#else
          if (add_files) {
            if (!ReadDirectory(location, file_length, depth + 1)) {
              return 0;
            }
          } else if (!memcmp(entry + name_offset + 33, "NAUGHTY.DOG", 11)) {
            add_files = 1;
            if (!ReadDirectory(location, file_length, depth + 1)) {
              return 0;
            }
            add_files = 0;
          }
#endif
        } else if (sNumFiles == MAX_ISO_FILES) {
          return 0;
#if REVISION <= 20010903
        } else {
#else
        } else if (add_files) {
#endif
          entry[name_length + 31] = 0;
          MakeISOName(sFiles[sNumFiles].name.chars, entry + 33);
          sFiles[sNumFiles].location = location;
          sFiles[sNumFiles].size = file_length;
          sNumFiles++;
        }
      }
      entry += record_length;
    }
    sector++;
    length -= SECTOR_SIZE;
  }
  return 1;
}

#if REVISION <= 20011002
int TimedCDRead(int sector, int count, void *buffer, sceCdRMode *mode) {
  struct SysClock start;
  struct SysClock end;
  GetSystemTime(&start);
  if (!sceCdRead(sector, count, buffer, mode)) {
    return 0;
  }
  sceCdSync(0);
  GetSystemTime(&end);
  return end.low - start.low;
}

void TimeDVD(void) {
  int i;
  int total;
  int sector;
  for (i = 0; i < 41; i++) {
    total = 0;
    sector = i * 0x10000 + 0x400;
    TimedCDRead(sector, 0x100, gRamdiskRAM, &sStreamMode);
    total += TimedCDRead(sector + 0x100, 0x100, gRamdiskRAM, &sNominalMode);
    total += TimedCDRead(sector + 0x200, 0x100, gRamdiskRAM, &sNominalMode);
    total += TimedCDRead(sector + 0x300, 0x100, gRamdiskRAM, &sNominalMode);
    total += TimedCDRead(sector + 0x400, 0x100, gRamdiskRAM, &sNominalMode);
    speeds[i] = total;
    speeds_seqno++;
  }
}
#else
#if REVISION < 20011031
#define DUP_INDEX "1_INDEX.DUP"
#define DUP_AREA1 "3_AREA1.DUP"
#define DUP_AREA2 "5_AREA2.DUP"
#else
#define DUP_INDEX "Z1INDEX.DUP"
#define DUP_AREA1 "Z3AREA1.DUP"
#define DUP_AREA2 "Z5AREA2.DUP"
#endif

// Set up duplicate area reading
// In this case, file records are stored in the index,
// and the DVD wouldn't have file locations stored
// in standard ISO metadata.
// If it runs, needs 4 sector buffers.
static void DecodeDUP(void* buffer) {
  char name[16];
  FileRecord* index;
  FileRecord* area1;
  FileRecord* area2;
  struct DupIndex {
    int count;
    FileRecord files[1];
  }* dup;
  int i;
  // disable area in case this fails
  sArea1 = 0x7fffffff;

  // find the index and area markers
  MakeISOName(name, DUP_INDEX);
  index = FS_FindIN(name);
  MakeISOName(name, DUP_AREA1);
  area1 = FS_FindIN(name);
  MakeISOName(name, DUP_AREA2);
  area2 = FS_FindIN(name);
  if (!index || !area1 || !area2) {
    return;
  }

  // Read the index file.
  if (!ReadSectorsNow(index->location, 4, buffer)) {
    return;
  }

  // Set up offsets
  dup = buffer;
  sArea1 = area1->location;
  sAreaDiff = area2->location - area1->location;
  if (sNumFiles + dup->count > MAX_ISO_FILES) {
    return;
  }

  // Copy file records to our table.
  for (i = 0; i < dup->count; i++) {
    FileRecord* src = &dup->files[i];
    FileRecord* dest = &sFiles[sNumFiles++];
    dest->name.words[0] = src->name.words[0];
    dest->name.words[1] = src->name.words[1];
    dest->name.words[2] = src->name.words[2];
    dest->size = src->size;
    dest->location = src->location + sArea1;
  }
}

// Load music tweak values.
static void LoadMusicTweaks(void* buffer) {
  char name[16];
  FileRecord* file;
  MakeISOName(name, "TWEAKVAL.MUS");
  file = FS_FindIN(name);
  if (!file || !ReadSectorsNow(file->location, 1, buffer)) {
    gMusicTweakInfo.count = 0;
  } else {
    memcpy(&gMusicTweakInfo, buffer, sizeof(gMusicTweakInfo));
  }
}

// Load Disc ID file, compute the sum of its random values.
static void LoadDiscID(void) {
  char name[16];
  FileRecord* file;
  int i;
  MakeISOName(name, "DISK_ID.DIZ");
  file = FS_FindIN(name);
  if (file) {
    CD_ID_SectorNum = file->location;
  } else {
    CD_ID_SectorNum = 1024;
  }
  ReadSectorsNow(CD_ID_SectorNum, 1, CD_ID_Sector);
  CD_ID_SectorSum = 0;
  for (i = 0; i < SECTOR_SIZE / 4; i++) {
    CD_ID_SectorSum += CD_ID_Sector[i];
  }
}

// Re-read the Disc ID file and confirm the disc is still the original.
static int CheckDiscID(void) {
  unsigned int sum;
  int i;
  if (!ReadSectorsNow(CD_ID_SectorNum, 1, CD_ID_Sector)) {
    return 0;
  }
  sum = 0;
  for (i = 0; i < SECTOR_SIZE / 4; i++) {
    sum += CD_ID_Sector[i];
  }
  return sum == CD_ID_SectorSum;
}

// Set _real_sector based on _sector and area selection.
// If pirated, adjust sector to prevent proper loading.
static void SetRealSector(void) {
  if (_sector >= sArea1 && _dupseg) {
    _real_sector = _sector + sAreaDiff;
  } else {
    _real_sector = _sector;
  }

#if REVISION >= 20011031 && defined(MEDIA_DVD)
  if (pirated) {
    _real_sector += 3;
  }
#endif
}

// Initialize the filesystem.
#endif

#if REVISION <= 20011002
static int FS_Init(unsigned char* buffer) {
  int i;
  int disk_type;
  int sector;
  int length;
  for (;;) {
    disk_type = sceCdGetDiskType();
    if (disk_type != SCECdDETCT) {
      break;
    }
    SleepThread(10000);
  }
  switch (disk_type) {
    case SCECdIllgalMedia:
      return 1;
    case SCECdPS2CD:
      break;
    case SCECdPS2CDDA:
      break;
    case SCECdPS2DVD:
      break;
    case SCECdPSCD:
      break;
    case SCECdPSCDDA:
      break;
    case SCECdDVDV:
      break;
    case SCECdCDDA:
      break;
    case SCECdNODISC:
      break;
    default:
      break;
  }
  for (i = 0; i < 3; i++) {
    sSecBuffer[i] = buffer + i * SECTOR_SIZE;
  }
  if (!ReadSectorsNow(16, 1, sSecBuffer[0])) {
    return 1;
  }
  if (memcmp(sSecBuffer[0] + 1, "CD001", 5)) {
    return 1;
  }
  sector = ReadU32(sSecBuffer[0] + 140);
  if (!ReadSectorsNow(sector, 1, sSecBuffer[0])) {
    return 1;
  }
  sector = ReadU32(sSecBuffer[0] + 2);
  if (!ReadSectorsNow(sector, 1, sSecBuffer[0])) {
    return 1;
  }
  length = ReadU32(sSecBuffer[0] + 10);
#if REVISION > 20010903
#ifdef SHARED_DEMO
  add_files = 0;
#else
  add_files = 1;
#endif
#endif
  if (!ReadDirectory(sector, length, 0)) {
    return 1;
  }
  for (i = 0; i < sNumFiles; i++) {
    FileRecord* file = &sFiles[i];
  }
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    sLoadStack[i].file = 0;
  }
  for (i = 0; i < 3; i++) {
    sSecBuffer[i] = 0;
  }
  return 0;
}
#else
static int FS_Init(unsigned char* buffer) {
  int i;
  int disk_type;
  int sector;
  int length;
  // loop until detection is done.
  for (;;) {
    disk_type = sceCdGetDiskType();
    if (disk_type != SCECdDETCT) {
      break;
    }
    SleepThread(10000);
  }
  // reject illegal media.
  switch (disk_type) {
    case SCECdIllgalMedia:
      return 1;
    case SCECdPS2CD:
      break;
    case SCECdPS2CDDA:
      break;
    case SCECdPS2DVD:
      break;
    case SCECdPSCD:
      break;
    case SCECdPSCDDA:
      break;
    case SCECdDVDV:
      break;
    case SCECdCDDA:
      break;
    case SCECdNODISC:
      break;
    default:
      break;
  }
#if REVISION >= 20011031 && defined(MEDIA_DVD)
  // indirection for future calls, harder for hackers to patch out.
  cdmmode = sceCdMmode;
  // verify we have a DVD.
  sceCdMmode(SCECdDVD);
#endif

  // Split up our scratch data into 3 sector buffers.
  for (i = 0; i < 3; i++) {
    sSecBuffer[i] = buffer + i * SECTOR_SIZE;
  }

  // Read primary volume descriptor
  if (!ReadSectorsNow(16, 1, sSecBuffer[0])) {
    return 1;
  }
  if (memcmp(sSecBuffer[0] + 1, "CD001", 5)) {
    return 1;
  }

  // Read path table and find root directory
  sector = ReadU32(sSecBuffer[0] + 140);
  if (!ReadSectorsNow(sector, 1, sSecBuffer[0])) {
    return 1;
  }
  sector = ReadU32(sSecBuffer[0] + 2);

  // Read root directory
  if (!ReadSectorsNow(sector, 1, sSecBuffer[0])) {
    return 1;
  }

  // Get root directory length
  length = ReadU32(sSecBuffer[0] + 10);

  // Read from the root (probably add_files is cleared in demo?? TODO)
  add_files = 1;
  if (!ReadDirectory(sector, length, 0)) {
    return 1;
  }

  // Read DUP files for hidden and duplicated ISO layout
  DecodeDUP(sSecBuffer[0]);
  // Read constant music metadata
  LoadMusicTweaks(sSecBuffer[0]);
  // Read id used to make sure disc doesn't change.
  LoadDiscID();
  for (i = 0; i < sNumFiles; i++) {
    FileRecord* file = &sFiles[i];
    // Does nothing...
  }
  // Reset load stack and buffers.
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    sLoadStack[i].file = 0;
  }
  for (i = 0; i < 3; i++) {
    sSecBuffer[i] = 0;
  }
  return 0;
}
#endif

// Find by name
static FileRecord* FS_Find(const char* name) {
  char iso_name[16];
  MakeISOName(iso_name, name);
  return FS_FindIN(iso_name);
}

// Find by name, with anti-piracy feature on later revisions with DVD media type.
// If lookup fails, and the sum of files is under 1 GB,
// then seek the name by 12 bytes and keep looking... forever...
static FileRecord* FS_FindIN(const char* name) {
  int i;
#if REVISION >= 20011031
  int total;
#endif
#if REVISION >= 20011031 && defined(MEDIA_DVD)
  for (;;) {
    total = 0;
#endif
    for (i = 0; i < sNumFiles; i++) {
      FileRecord* file = &sFiles[i];
      if (file->name.words[0] == ((const IsoName*)name)->words[0] &&
          file->name.words[1] == ((const IsoName*)name)->words[1] &&
          file->name.words[2] == ((const IsoName*)name)->words[2]) {
        return file;
      }
#if REVISION >= 20011031
      total += file->size;
#endif
    }

#if REVISION >= 20011031 && defined(MEDIA_DVD)
    // anti-piracy check.
    name += 12;
    if (total >= 0x40000000) {
      break;
    }
  }
#endif
  return 0;
}

// Get the length of a file in bytes.
static int FS_GetLength(FileRecord* file) {
  return file->size;
}

// Open a file after looking it up.
static LoadStackEntry* FS_Open(FileRecord* file, int offset) {
  LoadStackEntry* entry = 0;
  int i;
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (!sLoadStack[i].file) {
      entry = &sLoadStack[i];
      break;
    }
  }
  if (!entry) {
    ExitIOP();
  }
  entry->file = file;
  entry->location = file->location;
  if (offset != -1) {
    entry->location += offset;
  }
  return entry;
}

// Open a file within a WAD at the given offset.
#if REVISION > 20010903
static LoadStackEntry* FS_OpenWad(FileRecord* file, int offset) {
  LoadStackEntry* entry = 0;
  int i;
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (!sLoadStack[i].file) {
      entry = &sLoadStack[i];
      break;
    }
  }
  if (!entry) {
    ExitIOP();
  }
  entry->file = file;
  entry->location = file->location + offset;
  return entry;
}
#endif

// Close a file, syncing on CD drive if still in progress.
static void FS_Close(void* fd) {
  LoadStackEntry* entry = fd;
  if (entry == sReadInfo) {
    int busy;
    int count = 0;
    for (;;) {
      busy = sceCdSync(1);  // 1 = poll
      if (!busy) {
        break;
      }
      DelayThread(1000);
      count++;
      if (count == 1000) {
        sceCdBreak();
        break;
      }
    }
    sReadInfo = 0;
  }
  entry->file = 0;
}

// Begin reading a file.
#if REVISION <= 20011002
static int FS_BeginRead(void* fd, void* buffer, int length) {
  LoadStackEntry* entry = fd;
  _sector = entry->location;
  _sectors = length / SECTOR_SIZE;
  _buffer = buffer;
  if (!sceCdRead(entry->location, length / SECTOR_SIZE, buffer, sMode)) {
    return 1;
  }
  entry->location += length / SECTOR_SIZE;
  sReadInfo = entry;
  return CMD_STATUS_IN_PROGRESS;
}
#else
static int FS_BeginRead(void* fd, void* buffer, int length) {
  LoadStackEntry* entry = fd;

  // track if we're reading continuous buffers from disc.
  if (length == BUFFER_PAGE_SIZE && entry->location == _sector + _sectors) {
    _continuous = 1;
  } else {
    _continuous = 0;
  }

  // set up read parameters
  _sector = entry->location;
  _sectors = length / SECTOR_SIZE;
  _buffer = buffer;
  _retries = 0;
  GetSystemTime(&_starttime);

  // update _real_sector based on selected area
  SetRealSector();

  // Issue the read, with retries
  for (;;) {
    if (sceCdRead(_real_sector, _sectors, _buffer, sMode)) {
      break;
    }
    CD_WaitReturn();
    _retries++;

    // on retry, if we're in area1 (implying that DecodeDUP succeeded),
    // retry on the other area.
    if (_sector >= sArea1) {
      _dupseg = 1 - _dupseg;
      _continuous = 0;
      SetRealSector();
    }
  }
  entry->location += length / SECTOR_SIZE;
  sReadInfo = entry;
  return CMD_STATUS_IN_PROGRESS;
}
#endif

// Wait for read completion. If failed, reissue the read and retry until success.
#if REVISION <= 20011002
static int FS_SyncRead(void) {
  int error;
  if (!sReadInfo) {
    return CMD_STATUS_READ_ERR;
  }
  sceCdSync(0);
  sReadInfo = 0;
  error = sceCdGetError();
  if (!error) {
    return CMD_STATUS_IN_PROGRESS;
  }
  return 1;
}
#else
static int FS_SyncRead(void) {
  struct SysClock before_sync;
  int error;
  if (!sReadInfo) {
    return CMD_STATUS_READ_ERR;
  }
  GetSystemTime(&before_sync);

  // block until drive read finishes
  sceCdSync(0);
  GetSystemTime(&_endtime);
  error = sceCdGetError();

  // retry while in error.
  while (error) {
    _retries++;
    // toggle area
    if (_sector >= sArea1) {
      _dupseg = 1 - _dupseg;
#if REVISION >= 20011031
      _continuous = 0;
#endif
      SetRealSector();
    }

    // loop reading
    for (;;) {
      if (sceCdRead(_real_sector, _sectors, _buffer, sMode)) {
        break;
      }
      CD_WaitReturn();
      _retries++;
      if (_sector >= sArea1) {
        _dupseg = 1 - _dupseg;
        _continuous = 0;
        SetRealSector();
      }
    }
    gDirtyCD = 1;
    sceCdSync(0);
    error = sceCdGetError();
  }

  // succesful read! flag CD as non-dirty.
  gDirtyCD = 0;
  // stop tracking this read.
  sReadInfo = 0;

  // update timing stats. Changes to the other area
  // if we haven't benchmarked that side, or if
  // it benchmarks significantly faster than this side.
  if (!_retries && _continuous && _sector >= sArea1) {
    unsigned int elapsed = _endtime.low - _starttime.low;
    unsigned int blocked = _endtime.low - before_sync.low;

    // only check if most of the time is spent blocked on sync,
    // reject samples where the read mostly completed in the background
    // with an uncertain actual completion time.
    if (blocked > elapsed / 2) {
      _times[_timesix] = elapsed;
      _timesix++;

      // once we have 16 records...
      if (_timesix == 16) {
        // compute average.
        unsigned int average = 0;
        unsigned int speed;
        int i;
        for (i = 0; i < 16; i++) {
          average += _times[i];
        }
        average >>= 4;
        speed = 0x69780000 / average;
        gLastSpeed = speed;

        // record speed for this area.
        if (_tsamps[_dupseg] < 0x40000) {
          _tkps[_dupseg] += speed;
          _tsamps[_dupseg] += 1;
        }
        gDiskSpeed[_dupseg] = _tkps[_dupseg] / _tsamps[_dupseg];

        // and reset the stat collection.
        _timesix = 0;
        if (_tsamps[0] < 8) {
          // toggle to dupseg 0 if we haven't benchmarked it yet.
          if (_dupseg != 0) {
            _dupseg = 0;
            _sector = 0;
          }
        } else if (_tsamps[1] < 8) {
          // toggle to dupseg 1 if we haven't benchmarked it yet.
          if (!_dupseg) {
            _dupseg = 1;
            _sector = 0;
          }
        } else {
          // otherwise, compare the two segments...
          int speed0 = _tkps[0] / _tsamps[0];
          int speed1 = _tkps[1] / _tsamps[1];

          // and swap to the faster one if it's more than 50 kbps faster than current.
          if (speed0 > speed1 + 50) {
            if (_dupseg != 0) {
              _dupseg = 0;
              _sector = 0;
            }
          } else if (speed1 > speed0 + 50) {
            if (!_dupseg) {
              _dupseg = 1;
              _sector = 0;
            }
          }
        }
        gDupSeg = _dupseg;
      }
    }
  }
  return CMD_STATUS_IN_PROGRESS;
}
#endif

// Load the soundbank.
#if REVISION <= 20011002
static int FS_LoadSoundBank(char* name, SoundBank* bank) {
  char filename[16];
  FileRecord* file;
  void* handle;
#if REVISION <= 20010903
  int sectors = 4;
#else
  int sectors;
  if (bank->sound_count == 101) {
    sectors = 1;
  } else {
    sectors = 10;
  }
#endif
  strcpy(filename, name);
  filename[8] = 0;
  strcat(filename, ".sbk");
  file = FS_Find(filename);
  if (!file) {
    return CMD_STATUS_FAILED_TO_OPEN;
  }
  if (!ReadSectorsNow(file->location, sectors, bank)) {
    bank->bank_handle = 0;
    return 1;
  }
  handle = snd_BankLoadByLoc(file->location + sectors, 0);
  if (!handle) {
    return 1;
  }
  snd_ResolveBankXREFS();
  PrintBankInfo(bank);
  bank->bank_handle = handle;
  return 0;
}
#else
static int FS_LoadSoundBank(char* name, SoundBank* bank) {
  char filename[16];
  FileRecord* file;
  void* handle;
  int sectors;
#if REVISION >= 20011031
  static int sound_bank_loads;
#endif

  // Soundbanks start with metadata. For level banks, there's up to 101 sounds
  // which fit in 1 sector. For common bank, there's up to 1022 sounds, which fit
  // in 10 sectors. These sizes are constant, and AllocateBank() sets sound_count
  // to the maximum value, it is later overwritten by the actual metadata load.
  if (bank->sound_count == 101) {
    sectors = 1;
  } else {
    sectors = 10;
  }

  // Open file, using empty1.sbk if it doesn't exist.
  strcpy(filename, name);
  filename[8] = 0;
  strcat(filename, ".sbk");
  file = FS_Find(filename);
  if (!file) {
    file = FS_Find("empty1.sbk");
  }

  // Read metadata now into bank, blocking.
  _sector = file->location;
  SetRealSector();
  for (;;) {
    if (ReadSectorsNow(_real_sector, sectors, bank)) {
      break;
    }
    CD_WaitReturn();
    if (_sector >= sArea1) {
      _dupseg = 1 - _dupseg;
      _continuous = 0;
      SetRealSector();
    }
  }

  // Read soundbank data into the 989snd library
  // 989snd issues loads itself.
  handle = snd_BankLoadByLoc(_real_sector + sectors, 0);

  // retry loading the bank data
  while (!handle) {
    int error = snd_GetLastLoadError();
    if (error >= 256) {  // TODO constant
      break;
    }
    CD_WaitReturn();
    if (_sector >= sArea1) {
      _dupseg = 1 - _dupseg;
      _continuous = 0;
      SetRealSector();
    }
    handle = snd_BankLoadByLoc(_real_sector + sectors, 0);
    if (!handle) {
      gDirtyCD = 1;
    }
  }
  gDirtyCD = 0;

  // piracy check
  // every 7th sound load, confirm that files reach past 1 GB on disc,
  // indicating a real DVD, not a partial copy of the game on CD.
  // If not, set pirated = 1.
#if REVISION >= 20011031 && defined(MEDIA_DVD)
  if (!(++sound_bank_loads & 7)) {
    int i;
    pirated = 1;
    for (i = 0; i < sNumFiles; i++) {
      FileRecord* file = &sFiles[i];
      if (file->location + (file->size >> 11) > 0x80000) {
        pirated = 0;
      }
    }
  }
#endif

  // resolve cross-bank references
  snd_ResolveBankXREFS();
  PrintBankInfo(bank);
  _sector = 0;
  bank->bank_handle = handle;
  return 0;
}
#endif

// Load music
#if REVISION <= 20011002
static int FS_LoadMusic(char* name, void* *bank_handle) {
  char filename[16];
  FileRecord* file;
  void* handle;
  strcpy(filename, name);
  filename[8] = 0;
  strcat(filename, ".mus");
  file = FS_Find(filename);
  if (!file) {
    return CMD_STATUS_FAILED_TO_OPEN;
  }
  handle = snd_BankLoadByLoc(file->location, 0);
  if (!handle) {
    return 1;
  }
  snd_ResolveBankXREFS();
  *bank_handle = handle;
  return 0;
}
#else
static int FS_LoadMusic(char* name, void** bank_handle) {
  char filename[16];
  FileRecord* file;
  void* handle;

  // Build filename and open.
  strcpy(filename, name);
  filename[8] = 0;
  strcat(filename, ".mus");
  file = FS_Find(filename);
  if (!file) {
    return CMD_STATUS_FAILED_TO_OPEN;
  }

  // Read file
  _sector = file->location;
  SetRealSector();
#if REVISION >= 20011031 && defined(MEDIA_DVD)
  // piracy check: effectively disables drive if not DVD.
  cdmmode(SCECdDVD);
#endif
  // issue reads through 989snd library
  handle = snd_BankLoadByLoc(_real_sector, 0);
  // handle retries.
  while (!handle) {
    int error = snd_GetLastLoadError();
    if (error >= 256) {
      break;
    }
    CD_WaitReturn();
    if (_sector >= sArea1) {
      _dupseg = 1 - _dupseg;
      _continuous = 0;
      SetRealSector();
    }
    handle = snd_BankLoadByLoc(_real_sector, 0);
    if (!handle) {
      gDirtyCD = 1;
    }
  }
  gDirtyCD = 0;
  snd_ResolveBankXREFS();
  _sector = 0;
  *bank_handle = handle;
  return 0;
}
#endif

#if REVISION > 20011002
#if REVISION >= 20011031
// Poll to see if the CD is in there or not.
// If not, run CD_WaitReturn to wait for it to come back.
static void FS_PollDrive(void) {
  int status = sceCdDiskReady(1);  // poll
  if (status == SCECdNotReady) {
    CD_WaitReturn();
  }
}

#endif

static void CD_WaitReturn(void) {
  int status;
  gNoCD = 1;  // flag CD removed for game to display error
  for (;;) {
    status = sceCdDiskReady(0);  // block

    // wait for it to be inserted
    while (status == SCECdNotReady) {
      status = sceCdDiskReady(0);
    }

    // verify it's the right disc.
    if (CheckDiscID()) {
      gNoCD = 0;
      break;
    }
    // if it's the wrong disc, open the drive. I guess this was removed on a later revision.
    // Possible against TRC or guidelines.
#if REVISION < 20011031
    sceCdTrayReq(0, 0);
#endif
  }
}
#endif
