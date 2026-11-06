#ifndef NEXT_KERNEL_KLINK_H
#define NEXT_KERNEL_KLINK_H

#include "next/kernel/kmalloc.h"

struct link_segment {
  unsigned char* relocs;
  char* data;
  int size;
  int reserved;
};

struct link_block_v3 {
  int length;
  int version;
  int segment_count;
  char name[64];
  link_segment segments[3];
};

struct link_header_v4 {
  unsigned int type_tag;
  int length;
  int version;
  int code_size;
};

#define LINK_FLAG_OUTPUT_LOAD 1
#define LINK_FLAG_OUTPUT_TRUE 2
#define LINK_FLAG_EXECUTE 4
#define LINK_FLAG_PRINT_LOGIN 8
#define LINK_FLAG_FORCE_DEBUG 0x10
#define LINK_FLAG_FORCE_FAST_LINK 0x20

struct link_control {
  char* object_data;
  char object_name[64];
  int object_size;
  kheapinfo* heap;
  unsigned int flags;
  void* entry;
  char* heap_top;
  int keep_debug;
  link_block_v3* link_block;
  int code_size;
  int state;
  int segment_process;
  char* original_object_location;
  int heap_gap;
  unsigned char* reloc_ptr;
  unsigned int base_ptr;
  unsigned int* loc_ptr;
  int table_toggle;
  link_segment* segments;
  void* code_start;

  void begin(void*, char*, int, kheapinfo*, unsigned int);
  unsigned int work();
  unsigned int work_v3();
  unsigned int work_v2();
  void finish();
};

void* link_and_exec(void*, char*, int, kheapinfo*, unsigned int);
unsigned int link_begin(void*, char*, int, kheapinfo*, unsigned int);
unsigned int link_resume();
void ultimate_memcpy(void*, void*, unsigned int);
unsigned char* c_rellink3(void*, link_segment*, unsigned char*);
unsigned char* c_symlink2(void*, unsigned int, unsigned char*);
unsigned char* c_symlink3(void*, unsigned int, unsigned char*);
extern unsigned char* (*symlink2)(void*, unsigned int, unsigned char*);
extern unsigned char* (*symlink3)(void*, unsigned int, unsigned char*);
#endif
