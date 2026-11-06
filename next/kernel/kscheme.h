#ifndef NEXT_KERNEL_KSCHEME_H
#define NEXT_KERNEL_KSCHEME_H

struct gstring {
  unsigned int length;
  char data[1];
};

#define GOAL_TAG_MASK 7U
#define GOAL_BASIC_TAG 4U
#define GOAL_PRINT_METHOD 2
#define GOAL_INSPECT_METHOD 3

struct symbol {
  unsigned int value;
};

struct type {
  symbol* name;
  type* parent;
  unsigned short allocated_size;
  unsigned short padded_size;
  unsigned short heap_base;
  unsigned short num_methods;
  unsigned int methods[1];
};

#define DECLARE_SYMBOL_TABLE() register unsigned int symbol_table asm("$23")
#define FIX_SYM_VALUE(offset) *(unsigned int*)(symbol_table + (offset))
#define FIX_SYM(offset) (unsigned int)(symbol_table + (offset))

extern unsigned int* print_column;
unsigned int print_object(unsigned int object);
unsigned int inspect_object(unsigned int object);
symbol* find_symbol_from_c(char* name);
unsigned int call_method_of_type(unsigned int object, type* object_type, int method);
unsigned int make_string_from_c(char* string);

void init_crc();

#define EE_T9 "$25"
#define EE_ZERO "$0"

// Enter GOAL through t9
#define GOAL_CALL(function)                                                               \
  asm volatile("or " EE_T9 ",%0," EE_ZERO "\n\tjalr " EE_T9 "\n\tor " EE_ZERO "," EE_ZERO \
               "," EE_ZERO                                                                \
               :                                                                          \
               : "r"(function)                                                            \
               : "t9", "ra", "memory")

// parallel table for names/hash.
#define SYMBOL_INFO_OFFSET 0xff38

struct symbol_info {
  gstring* name;
};

int InitHeapAndSymbol();
symbol* intern_from_c(char*);
unsigned int make_function_symbol_from_c(char*, unsigned int (*)());
extern unsigned int* EnableMethodSet;
extern symbol* kernel_packages;
unsigned int new_pair(unsigned int, type*, unsigned int, unsigned int);
extern "C" unsigned int call_goal_function_by_name(char*);

#define FIX_SYM_EMPTY_CAR -0xc
#define FIX_SYM_EMPTY_PAIR -0xa
#define FIX_SYM_EMPTY_CDR -0x8
#define FIX_SYM_FALSE 0x0
#define FIX_SYM_TRUE 8
#define FIX_SYM_FUNCTION_TYPE 0x10
#define FIX_SYM_BASIC_TYPE 0x18
#define FIX_SYM_STRING_TYPE 0x20
#define FIX_SYM_SYMBOL_TYPE 0x28
#define FIX_SYM_TYPE_TYPE 0x30
#define FIX_SYM_OBJECT_TYPE 0x38
#define FIX_SYM_LINK_BLOCK 0x40
#define FIX_SYM_INTEGER_TYPE 0x48
#define FIX_SYM_SINTEGER_TYPE 0x50
#define FIX_SYM_UINTEGER_TYPE 0x58
#define FIX_SYM_BINTEGER_TYPE 0x60
#define FIX_SYM_INT8_TYPE 0x68
#define FIX_SYM_INT16_TYPE 0x70
#define FIX_SYM_INT32_TYPE 0x78
#define FIX_SYM_INT64_TYPE 0x80
#define FIX_SYM_INT128_TYPE 0x88
#define FIX_SYM_UINT8_TYPE 0x90
#define FIX_SYM_UINT16_TYPE 0x98
#define FIX_SYM_UINT32_TYPE 0xA0
#define FIX_SYM_UINT64_TYPE 0xA8
#define FIX_SYM_UINT128_TYPE 0xB0
#define FIX_SYM_FLOAT_TYPE 0xB8
#define FIX_SYM_PROCESS_TREE_TYPE 0xC0
#define FIX_SYM_PROCESS_TYPE 0xC8
#define FIX_SYM_THREAD_TYPE 0xD0
#define FIX_SYM_STRUCTURE_TYPE 0xD8
#define FIX_SYM_PAIR_TYPE 0xE0
#define FIX_SYM_POINTER_TYPE 0xE8
#define FIX_SYM_NUMBER_TYPE 0xF0
#define FIX_SYM_ARRAY_TYPE 0xF8
#define FIX_SYM_VU_FUNCTION_TYPE 0x100
#define FIX_SYM_CONNECTABLE_TYPE 0x108
#define FIX_SYM_STACK_FRAME_TYPE 0x110
#define FIX_SYM_FILE_STREAM_TYPE 0x118
#define FIX_SYM_KHEAP 0x120
#define FIX_SYM_NOTHING_FUNC 0x128
#define FIX_SYM_DEL_BASIC_FUNC 0x130
#define FIX_SYM_STATIC 0x138
#define FIX_SYM_GLOBAL_HEAP 0x140
#define FIX_SYM_DEBUG_HEAP 0x148
#define FIX_SYM_LOADING_LEVEL 0x150
#define FIX_SYM_LOADING_PACKAGE 0x158
#define FIX_SYM_PROCESS_LEVEL_HEAP 0x160
#define FIX_SYM_STACK 0x168
#define FIX_SYM_SCRATCH 0x170
#define FIX_SYM_SCRATCH_TOP 0x178
#define FIX_SYM_ZERO_FUNC 0x180
#define FIX_SYM_ASIZE_OF_BASIC_FUNC 0x188
#define FIX_SYM_COPY_BASIC_FUNC 0x190
#define FIX_SYM_LEVEL 0x198
#define FIX_SYM_ART_GROUP 0x1a0
#define FIX_SYM_TX_PAGE_DIR 0x1a8
#define FIX_SYM_TX_PAGE 0x1b0
#define FIX_SYM_SOUND 0x1b8
#define FIX_SYM_DGO 0x1c0
#define FIX_SYM_TOP_LEVEL 0x1c8
#define FIX_FIXED_SYM_END_OFFSET 0x1d0
#define SYMBOL_HASH_OFFSET (SYMBOL_INFO_OFFSET - 4)
#define SYMBOL_NAME(sym) (((symbol_info*)((char*)(sym) + SYMBOL_INFO_OFFSET))->name)
#define SYMBOL_HASH(sym) (*(unsigned int*)((char*)(sym) + SYMBOL_HASH_OFFSET))
#define OBJECT_TYPE(obj) (((type**)(obj))[-1U])
#define GOAL_PAIR_TAG 2U
#define GOAL_ASIZE_METHOD 5

extern unsigned int crc_table[256];
extern unsigned int SymbolTable2;
extern unsigned int LastSymbol;
extern int NumSymbols;
extern unsigned int FastLink;
extern unsigned int symbol_slot;

#endif
