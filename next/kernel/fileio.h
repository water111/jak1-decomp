#ifndef NEXT_KERNEL_FILEIO_H
#define NEXT_KERNEL_FILEIO_H

struct symbol;
struct gstring;

// GOAL file-stream object.
struct file_stream {
  unsigned int flags;
  symbol* mode;
  gstring* name;
  unsigned int descriptor;
};

file_stream* kopen(file_stream*, gstring*, symbol*);
int klength(file_stream*);
int kseek(file_stream*, int offset, int where);
int kread(file_stream*, void* buffer, int size);
int kwrite(file_stream*, void* buffer, int size);
file_stream* kclose(file_stream*);

void kstrcpy(char* destination, char* source);
void kstrcpyup(char* destination, char* source);
char* strend(char* string);
void kstrcat(char* destination, char* source);
void kstrncat(char* destination, char* source, int length);
char* kstrinsert(char* destination, char character, int count);

enum GoalFileType {
  LISTENER_TO_KERNEL_FILE_TYPE = 1,
  KERNEL_TO_LISTENER_FILE_TYPE = 2,
  CODE_FILE_TYPE = 3,
  GAMEPAD_FILE_TYPE = 4,
  LISTENER_TO_KERNEL_LOCK_FILE_TYPE = 5,
  KERNEL_TO_LISTENER_LOCK_FILE_TYPE = 6,
  IOP_MODULE_FILE_TYPE = 8,
  DATA_FILE_TYPE = 0x20,
  TX_PAGE_FILE_TYPE = 0x21,
  JA_FILE_TYPE = 0x22,
  JG_FILE_TYPE = 0x23,
  MA_FILE_TYPE = 0x24,
  MG_FILE_TYPE = 0x25,
  TG_FILE_TYPE = 0x26,
  LEVEL_FILE_TYPE = 0x27,
  ART_GROUP_FILE_TYPE = 0x30,
  VS_FILE_TYPE = 0x31,
  TX_FILE_TYPE = 0x32,
  VS_BIN_FILE_TYPE = 0x33,
  DGO_TXT_FILE_TYPE = 0x34,
  LEVEL_WITH_EXTENSION_FILE_TYPE = 0x35,
  DATA_DGO_FILE_TYPE = 0x36,
  GAME_DGO_FILE_TYPE = 0x37,
  DATA_CGO_FILE_TYPE = 0x38,
  GAME_CGO_FILE_TYPE = 0x39,
  CNT_FILE_TYPE = 0x3a,
  RES_FILE_TYPE = 0x3b,
  SND_BNK_FILE_TYPE = 0x3c,
  MUSIC_BNK_FILE_TYPE = 0x3d,
  VAG_FILE_TYPE = 0x3e,
  REFPLANT_FILE_TYPE = 0x301,
};

struct kheapinfo;
struct MakeFileNameInfo;
char* DecodeFileName(char*);
char* MakeFileName(int, const char*, int, const char*, MakeFileNameInfo*);
unsigned char* FileLoad(const char*, kheapinfo*, unsigned char*, unsigned int, int*);

char* basename(char*);
unsigned int ReadHufWord(unsigned char** cursor);
unsigned int FileExists(const char* name);
void FileDelete(const char* name);
int FileCopy(const char* source, const char* destination);
int FileLength(const char* name);
int FileSave(const char* name, char* data, int size);

#endif
