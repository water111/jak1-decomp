#ifndef NEXT_KERNEL_KPRINT_H
#define NEXT_KERNEL_KPRINT_H

#include "next/kernel/klink.h"
#include "next/kernel/kscheme.h"

typedef int int128 __attribute__((mode(TI)));

#define DEBUG_MESSAGE_BUFFER_SIZE 0x80000
#define DEBUG_OUTPUT_BUFFER_SIZE 0x80000
#define DEBUG_PRINT_BUFFER_SIZE 0x200000
#define PRINT_BUFFER_SIZE 0x2000

extern char* OutputPending;
extern char* PrintPending;
extern char* OutputBufArea;
extern char* PrintBufArea;
extern int MessCount;
extern char* MessBufArea;
extern char AckBufArea[40];

void init_output();
void clear_output();
void clear_print();
void reset_output();
void output_unload(char* name);
void output_segment_load(char* name, link_block_v3* link_block, unsigned int flags);

void cprintf(char* format, ...);
unsigned int format(unsigned int destination, gstring* control, ...);
void ftoa(char* output, float value, int length, char pad, int precision, unsigned int flags);
char* kitoa(char* buffer,
            long long value,
            long long base,
            int length,
            char pad,
            unsigned int flags);
char* kqtoa(char* buffer, int128 value, long long base, int length, char pad, unsigned int flags);
void MsgWarn(const char* format, ...);
void Msg(int category, const char* format, ...);
void MsgErr(const char* format, ...);

#endif
