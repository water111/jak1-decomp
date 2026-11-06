// GOAL language helpers.

#include "next/kernel/kscheme.h"
#include "next/kernel/fileio.h"
#include "next/kernel/kboot.h"
#include "next/kernel/kdgo.h"
#include "next/kernel/kdsnetm.h"
#include "next/kernel/klink.h"
#include "next/kernel/klisten.h"
#include "next/kernel/kmachine.h"
#include "next/kernel/kmalloc.h"
#include "next/kernel/kmemcard.h"
#include "next/kernel/kprint.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Code to read the EE clock, oddly enough non-constant data instead of
// assembly.
static unsigned int read_clock_code[4] = {0x40024800, 0x03e00008, 0,
                                          0};  // mfc0 v0, Count; jr ra; nop
static unsigned int (*read_clock)() = (unsigned int (*)())read_clock_code;

// Build CRC32 table used for symbol table hashing.
void init_crc() {
  int i, j;
  unsigned int n;
  for (i = 0; i < 256; i++) {
    n = i << 24;
    for (j = 0; j < 8; j++) {
      if (n & 0x80000000)
        n = (n << 1) ^ 0x04c11db7;
      else
        n <<= 1;
    }
    crc_table[i] = n;
  }
}

void* LinkBlockBuf = 0;        // unused?, link block storage
unsigned int FastLink = 0;     // If set, use faster method setting that only works on in-order defs
unsigned int symbol_slot = 0;  // Symbol table probe slot (either match or next open slot)

// Hash a byte string, used for symbol table hash.
unsigned int crc32(unsigned char* data, int size) {
  unsigned int crc = 0;
  while (size) {
    crc = ((crc << 8) | *data++) ^ crc_table[crc >> 24];
    size--;
  }
  return ~crc;
}

// Default new function which rejects allocation and returns #f.
unsigned int new_illegal(unsigned int heap, type* object_type) {
  DECLARE_SYMBOL_TABLE();
  MsgErr("dkernel: illegal attempt to call new method of static object type %s\n",
         SYMBOL_NAME(object_type->name)->data);
  return FIX_SYM(FIX_SYM_FALSE);
}

// Default delete which prints an error.
void delete_illegal(unsigned int object) {
  MsgErr(
      "dkernel: illegal attempt to call delete method of static object @ "
      "#x%X\n",
      object);
}

// kheap allocator called from GOAL.
unsigned int goal_malloc(kheapinfo* heap, int size, unsigned int flags, gstring* name) {
  return (unsigned int)kmalloc(heap, size, flags, name->data);
}

// Allocate from a kernel heap, the current process, scratchpad, or a supplied
// stack address.
unsigned int alloc_from_heap(unsigned int heap, unsigned int object_type, int size) {
  DECLARE_SYMBOL_TABLE();
  register unsigned int pp asm("$22");
  unsigned int result;
  int aligned_size = ((size + 15) / 16) * 16;
  symbol* name;
  gstring* string;

  // The process heap starts at byte 0x4c of its payload.
  struct process_heap {
    int base;
    int top;
    int current;
  };

  process_heap* process;

  // kernel heap
  if (heap == FIX_SYM(FIX_SYM_GLOBAL_HEAP) || heap == FIX_SYM(FIX_SYM_DEBUG_HEAP) ||
      heap == FIX_SYM(FIX_SYM_LOADING_LEVEL) || heap == FIX_SYM(FIX_SYM_PROCESS_LEVEL_HEAP)) {
    result = (unsigned int)kmalloc(
        *(kheapinfo**)heap, size, KMALLOC_MEMSET,
        object_type && (name = ((type*)object_type)->name) && (string = SYMBOL_NAME(name))
            ? string->data
            : "global-object");
  } else if (heap == FIX_SYM(FIX_SYM_PROCESS_TYPE)) {
    // process heap
    process = (process_heap*)(pp + 0x4c);
    if (process->current + aligned_size >= process->top) {
      MsgErr("kmalloc: !alloc mem in heap for #<process @ #x%x> (%d bytes)\n", pp, aligned_size);
      return 0;
    }
    result = process->current;
    process->current += aligned_size;
    memset((void*)result, 0, aligned_size);
  } else if (heap == FIX_SYM(FIX_SYM_SCRATCH)) {
    // scratchpad (unused)
    result = FIX_SYM_VALUE(FIX_SYM_SCRATCH_TOP);
    FIX_SYM_VALUE(FIX_SYM_SCRATCH_TOP) += aligned_size;
    memset((void*)result, 0, aligned_size);
  } else {
    // other: treat as stack address and just set to 0.
    result = heap;
    memset((void*)result, 0, aligned_size);
  }
  return result;
}

// Allocate untyped memory on a GOAL heap.
unsigned int alloc_heap_memory(unsigned int heap, int size) {
  return alloc_from_heap(heap, 0, size);
}

// Allocate a basic object and set its type tag.
unsigned int alloc_heap_object(unsigned int heap, unsigned int object_type, int size) {
  unsigned int memory = alloc_from_heap(heap, object_type, size);
  if (memory) {
    *(unsigned int*)memory = object_type;
    return memory + GOAL_BASIC_TAG;
  } else {
    return 0;
  }
}

// Allocate an untagged structure using its type's fixed size.
unsigned int new_structure(unsigned int heap, type* object_type) {
  unsigned int memory =
      alloc_from_heap(heap, (unsigned int)object_type, object_type->allocated_size);
  return memory;
}

// Allocate an untagged structure with a caller-supplied size.
unsigned int new_dynamic_structure(unsigned int heap, type* object_type, int size) {
  unsigned int memory = alloc_from_heap(heap, (unsigned int)object_type, size);
  return memory;
}

// Delete a structure (kfree is a no-op).
void delete_structure(unsigned int* object) {
  kfree(object);
}

// Allocate a fixed-size tagged object.
unsigned int new_basic(unsigned int heap, type* object_type) {
  return alloc_heap_object(heap, (unsigned int)object_type, object_type->allocated_size);
}

// Delete a basic (does nothing, and has pointer math bug).
void delete_basic(unsigned int* object) {
  kfree(object - 4);
}

// Allocate a pair and set its car and cdr.
unsigned int new_pair(unsigned int heap, type* object_type, unsigned int car, unsigned int cdr) {
  unsigned int memory =
      alloc_from_heap(heap, (unsigned int)object_type, object_type->allocated_size);
  if (memory) {
    ((unsigned int*)memory)[0] = car;
    ((unsigned int*)memory)[1] = cdr;
    return memory + GOAL_PAIR_TAG;
  } else {
    return 0;
  }
}

// Another pointer math bug, but kfree does nothing so there's no harm.
void delete_pair(unsigned long long* object) {
  kfree(object - 1);
}

// Allocate a string with room for its terminator and at least eight character
// bytes.
unsigned int make_string(int size) {
  DECLARE_SYMBOL_TABLE();
  gstring* string = (gstring*)alloc_heap_object(
      FIX_SYM(FIX_SYM_GLOBAL_HEAP), FIX_SYM_VALUE(FIX_SYM_STRING_TYPE), MAX(size + 1, 8) + 8);
  if (string)
    string->length = size;
  return (unsigned int)string;
}

// Copy a C string into a new GOAL string.
unsigned int make_string_from_c(char* source) {
  DECLARE_SYMBOL_TABLE();
  int size = strlen(source);
  gstring* string = (gstring*)alloc_heap_object(
      FIX_SYM(FIX_SYM_GLOBAL_HEAP), FIX_SYM_VALUE(FIX_SYM_STRING_TYPE), MAX(size + 1, 8) + 8);
  string->length = size;
  kstrcpy(string->data, source);
  return (unsigned int)string;
}

// C code access data relative to "gp". Normally, C runtime sets gp register to
// the appropriate value _gp determined by the linker, but when leaving GOAL
// code we have to restore it.
extern char _gp[];

// Emit a GOAL-to-C trampoline, preserving the GOAL return address and global
// pointer.
unsigned int make_function_from_c(unsigned int (*function)()) {
  DECLARE_SYMBOL_TABLE();
  unsigned int* code = (unsigned int*)alloc_heap_object(FIX_SYM(FIX_SYM_GLOBAL_HEAP),
                                                        FIX_SYM_VALUE(FIX_SYM_FUNCTION_TYPE), 64);
  // This function executes from GOAL, restores gp, and calls a C function.
  code[0] = 0x67bdffe0;                                      // daddiu sp, sp, -32
  code[1] = 0xffbf0000;                                      // sd ra, 0(sp)
  code[2] = 0xffbc0010;                                      // sd gp, 16(sp)
  code[3] = 0x3c1c0000 | ((unsigned int)_gp >> 16);          // lui gp, hi(_gp)
  code[4] = 0x379c0000 | ((unsigned int)_gp & 0xffff);       // ori gp, gp, lo(_gp)
  code[5] = 0x3c190000 | ((unsigned int)function >> 16);     // lui t9, hi(function)
  code[6] = 0x37390000 | ((unsigned int)function & 0xffff);  // ori t9, t9, lo(function)
  code[7] = 0x0320f809;                                      // jalr t9
  code[8] = 0;                                               // nop
  code[9] = 0xdfbf0000;                                      // ld ra, 0(sp)
  code[10] = 0xdfbc0010;                                     // ld gp, 16(sp)
  code[11] = 0x03e00008;                                     // jr ra
  code[12] = 0x67bd0020;                                     // daddiu sp, sp, 32
  code[13] = 0;
  code[14] = 0;
  CacheFlush(code, 52);
  return (unsigned int)code;
}

// Build a GOAL function that returns immediately.
unsigned int make_nothing_func() {
  DECLARE_SYMBOL_TABLE();
  unsigned int* code = (unsigned int*)alloc_heap_object(FIX_SYM(FIX_SYM_GLOBAL_HEAP),
                                                        FIX_SYM_VALUE(FIX_SYM_FUNCTION_TYPE), 20);
  code[0] = 0x03e00008;  // jr ra
  code[1] = 0;           // nop
  code[2] = 0;
  code[3] = 0;
  CacheFlush(code, 8);
  return (unsigned int)code;
}

// Build a GOAL function that returns zero.
unsigned int make_zero_func() {
  DECLARE_SYMBOL_TABLE();
  unsigned int* code = (unsigned int*)alloc_heap_object(FIX_SYM(FIX_SYM_GLOBAL_HEAP),
                                                        FIX_SYM_VALUE(FIX_SYM_FUNCTION_TYPE), 20);
  code[0] = 0x03e00008;  // jr ra
  code[1] = 0x24020000;  // addiu v0, zero, 0
  code[2] = 0;
  code[3] = 0;
  CacheFlush(code, 8);
  return (unsigned int)code;
}

// Export a C function through a GOAL trampoline.
unsigned int make_function_symbol_from_c(char* name, unsigned int (*function)()) {
  symbol* sym = intern_from_c(name);
  sym->value = make_function_from_c(function);
  return sym->value;
}

// Export a function already using the GOAL calling convention.
unsigned int make_raw_function_symbol_from_c(char* name, unsigned int (*function)()) {
  symbol* sym = intern_from_c(name);
  sym->value = (unsigned int)function;
  return sym->value;
}

// Initialize a symbol whose slot is compile-time fixed.
// I believe this exists purely as an optimization for C Kernel code to avoid
// doing hash-table lookups? Nothing seems to rely on these exact addresses.
symbol* set_fixed_symbol(int offset, char* name, unsigned int value) {
  DECLARE_SYMBOL_TABLE();
  symbol* sym = (symbol*)FIX_SYM(offset);
  OBJECT_TYPE(sym) = (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE);
  sym->value = value;
  SYMBOL_NAME(sym) = (gstring*)make_string_from_c(name);
  SYMBOL_HASH(sym) = crc32((unsigned char*)name, strlen(name));
  NumSymbols++;
  return sym;
}

// Look for a symbol by name in the fixed symbol area after a hash-table probe
// misses.
static symbol* find_symbol_in_fixed_area(unsigned int hash, char* name) {
  DECLARE_SYMBOL_TABLE();
  unsigned int slot = symbol_table;
  unsigned int end = slot + FIX_FIXED_SYM_END_OFFSET;
  for (; slot < end; slot += 8) {
    if (SYMBOL_HASH(slot) == hash) {
      if (!strcmp(SYMBOL_NAME(slot)->data, name))
        return (symbol*)slot;
    }
  }
  return 0;
}

// Linear probe for a symbol in the hash table. Returns 1 when it reaches the
// end, indicating the caller it should wrap to the other side.
static symbol* find_symbol_in_area(unsigned int hash, char* name, char* slot, char* end) {
  for (; slot < end; slot += 8) {
    unsigned int slot_hash = SYMBOL_HASH(slot);
    if (slot_hash == hash && !strcmp(SYMBOL_NAME(slot)->data, name)) {
      return (symbol*)slot;
    } else if (!slot_hash) {
      symbol_slot = (unsigned int)slot;
      symbol* result = find_symbol_in_fixed_area(hash, name);
      return result;
    }
  }
  return (symbol*)1;
}

// Find a symbol. If it fails, returns the empty slot for the symbol in
// symbol_slot. Also finds the empty pair (not a symbol) if the name is
// "_empty_"
symbol* find_symbol_from_c(char* name) {
  DECLARE_SYMBOL_TABLE();
  symbol_slot = 0;
  unsigned int hash = crc32((unsigned char*)name, strlen(name));
  int offset;
  symbol* result;
  if (hash == 0x8454b6e6 && !strcmp(name, "_empty_")) {
    return (symbol*)FIX_SYM(FIX_SYM_EMPTY_PAIR);
  }
  offset = (int)(hash << 19) >> 16;
  if (offset > 0) {
    result = find_symbol_in_area(hash, name, (char*)(symbol_table + offset), (char*)LastSymbol);
    if (result != (symbol*)1)
      return result;
    result = find_symbol_in_area(hash, name, (char*)SymbolTable2, (char*)(symbol_table - 16));
    if (result != (symbol*)1)
      return result;
  } else {
    result =
        find_symbol_in_area(hash, name, (char*)(symbol_table + offset), (char*)(symbol_table - 16));
    if (result != (symbol*)1)
      return result;
    result = find_symbol_in_area(hash, name, (char*)(symbol_table + FIX_FIXED_SYM_END_OFFSET),
                                 (char*)LastSymbol);
    if (result != (symbol*)1)
      return result;
  }
  result = find_symbol_in_fixed_area(hash, name);
  return result;
}

// Intern a name by C string. The name string is copied to a new GOAL string if
// the symbol isn't found.
symbol* intern_from_c(char* name) {
  DECLARE_SYMBOL_TABLE();
  symbol* sym = find_symbol_from_c(name);
  if (!sym) {
    sym = (symbol*)symbol_slot;
    OBJECT_TYPE(sym) = (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE);
    SYMBOL_NAME(sym) = (gstring*)make_string_from_c(name);
    SYMBOL_HASH(sym) = crc32((unsigned char*)name, strlen(name));
    NumSymbols++;
  }
  return sym;
}

// Intern a name by GOAL string. The string is copied to a new GOAL string if
// the symbol isn't found.
symbol* intern(gstring* name) {
  DECLARE_SYMBOL_TABLE();
  symbol* sym = find_symbol_from_c(name->data);
  if (!sym) {
    sym = (symbol*)symbol_slot;
    OBJECT_TYPE(sym) = (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE);
    SYMBOL_NAME(sym) = (gstring*)make_string_from_c(name->data);
    SYMBOL_HASH(sym) = crc32((unsigned char*)name->data, strlen(name->data));
    NumSymbols++;
  }
  return sym;
}

// Type object size, including method table.
#define TYPE_SIZE(methods) ((4 * (methods) + 0x23) & 0xfffffff0)

// Allocate a type and set its symbol.
type* alloc_and_init_type(symbol* sym, int methods) {
  DECLARE_SYMBOL_TABLE();
  type* result = (type*)alloc_heap_object(FIX_SYM(FIX_SYM_GLOBAL_HEAP),
                                          FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE), TYPE_SIZE(methods));
  sym->value = (unsigned int)result;
  return result;
}

// Get a type, allocating space for its method table if it doesn't exist.
// If methods is 0, set to 12, if method is 1, set to 44. This is a bit of
// a hack workaround for GOAL object producers that don't know the correct
// number of methods.
unsigned int intern_type_from_c(char* name, long long methods) {
  symbol* sym = intern_from_c(name);
  type* result = (type*)sym->value;
  if (!result) {
    switch (methods) {
      case 0:
        methods = 12;
        break;
      case 1:
        methods = 44;
        break;
    }
    result = alloc_and_init_type(sym, methods);
    result->name = sym;
    result->num_methods = methods;
  } else if (TYPE_SIZE(methods) > TYPE_SIZE(result->num_methods)) {
    MsgErr(
        "dkernel: trying to redefine a type '%s' with %d methods when it had "
        "%d, try restarting\n",
        name, methods, result->num_methods);
  }
  return (unsigned int)result;
}

// Get a type named by a GOAL string, allocating it if it doesn't exist.
// If methods is 0, set to 12, if method is 1, set to 44. This is a bit of
// a hack workaround for GOAL object producers that don't know the correct
// number of methods.
unsigned int intern_type(gstring* name, long long methods) {
  return intern_type_from_c(name->data, methods);
}

unsigned int set_type_values(type*,
                             type*,
                             long long,
                             unsigned int (*)(unsigned int),
                             unsigned int (*)(unsigned int));

// Set up a fixed type, inheriting its parent's standard methods.
unsigned int set_fixed_type(int offset,
                            char* name,
                            unsigned int parent_symbol,
                            long long flags,
                            unsigned int* print,
                            unsigned int* inspect) {
  DECLARE_SYMBOL_TABLE();
  symbol* sym = (symbol*)FIX_SYM(offset);
  type* result = (type*)sym->value;
  OBJECT_TYPE(sym) = (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE);
  SYMBOL_NAME(sym) = (gstring*)make_string_from_c(name);
  SYMBOL_HASH(sym) = crc32((unsigned char*)name, strlen(name));
  NumSymbols++;
  if (!result)
    result = alloc_and_init_type(sym, (flags >> 32) & 0xffff);
  OBJECT_TYPE(result) = (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE);
  result->name = sym;
  type* parent = *(type**)parent_symbol;
  set_type_values(result, parent, flags, 0, 0);
  result->methods[0] = parent->methods[0];
  result->methods[1] = parent->methods[1];
  result->methods[2] = print ? (unsigned int)print : parent->methods[2];
  result->methods[3] = inspect ? (unsigned int)inspect : parent->methods[3];
  result->methods[4] = FIX_SYM_VALUE(FIX_SYM_ZERO_FUNC);
  result->methods[5] = parent->methods[5];
  result->methods[6] = parent->methods[6];
  return (unsigned int)result;
}

// Create a new type, inherit methods, and set flag values.
// This is the runtime type creation function called from GOAL.
unsigned int new_type(symbol* name, type* parent, long long flags) {
  int i;
  long long methods = (flags >> 32) & 0xffff;
  if (!methods)
    methods = 12;
  type* result = (type*)intern_type(SYMBOL_NAME(name), methods);
  for (i = 0; i < methods; i++)
    result->methods[i] = parent->methods[i];
  return set_type_values(result, parent, flags, print_object, inspect_object);
}

// Decode type flags emitted by GOAL compiler.
// The two printer arguments are unused.
unsigned int set_type_values(type* object_type,
                             type* parent,
                             long long flags,
                             unsigned int (*print)(unsigned int),
                             unsigned int (*inspect)(unsigned int)) {
  object_type->parent = parent;
  object_type->allocated_size = flags & 0xffff;
  object_type->heap_base = (flags >> 16) & 0xffff;
  object_type->padded_size = (unsigned short)(object_type->allocated_size + 15) & 0xfff0;
  // clang-format off
  // Couldn't get this to match without the >?
  object_type->num_methods = object_type->num_methods >? ((flags >> 32) & 0xffff);
  // clang-format on
  return (unsigned int)object_type;
}

// Test whether a type is equal to, or descended from, another type.
symbol* type_typep(type* child, type* parent) {
  DECLARE_SYMBOL_TABLE();
  type* root = (type*)FIX_SYM_VALUE(FIX_SYM_OBJECT_TYPE);
  if (child == parent)
    return (symbol*)FIX_SYM(FIX_SYM_TRUE);
  do {
    child = child->parent;
    if (child == parent)
      return (symbol*)FIX_SYM(FIX_SYM_TRUE);
  } while (child != root && child);
  return (symbol*)FIX_SYM(FIX_SYM_FALSE);
}

// Runtime object pointers may refer to the GOAL heap or the some unused space
// in the EE Kernel. GOAL code reuses this unused kernel space for extra
// storage.
#define INVALID_GOAL_ADDRESS(object)                                                     \
  ((((unsigned int)(object) < SymbolTable2) || ((unsigned int)(object) > 0x07ffffff)) && \
   (((unsigned int)(object) < 0x84000) || ((unsigned int)(object) >= 0x100000)))
#define INVALID_BASIC(object) \
  (INVALID_GOAL_ADDRESS(object) || (((unsigned int)(object) & GOAL_TAG_MASK) != GOAL_BASIC_TAG))

// If methods aren't defined in hierarchy order, we need to scan all types to
// check if we need to propagate an override down. This happens when
// EnableMethodSet is true, or if we're debugging. If debugging, and not doing
// EnableMethodSet, you can still force the faster method set by setting
// FastLink.
#if REVISION < 20011019
#define METHOD_SET_CHECK_NEEDS_CHILD_PROP() (*EnableMethodSet || (!FastLink && MasterDebug))
#else
#define METHOD_SET_CHECK_NEEDS_CHILD_PROP() \
  (*EnableMethodSet || (!FastLink && MasterDebug && !DiskBoot))
#endif

// Replace a method and propagate it to descendant types that still inherit the
// old implementation. If method is null, do nothing. If method is 1, set the
// method to nothing. If method is 2, set the method to the parent's method.
unsigned int method_set(type* object_type, int method_id, unsigned int* method) {
  DECLARE_SYMBOL_TABLE();
  unsigned int old_method = object_type->methods[method_id];
  unsigned int slot = symbol_table;
  type* child;
  switch ((int)method) {
    case 0:
      return (unsigned int)method;
    case 1:
      method = 0;
      break;
    case 2:
      method = (unsigned int*)object_type->parent->methods[method_id];
      break;
  }
  object_type->methods[method_id] = (unsigned int)method;
  if (!METHOD_SET_CHECK_NEEDS_CHILD_PROP())
    return (unsigned int)method;
  while (slot < LastSymbol) {
    child = *(type**)slot;
    if (!INVALID_GOAL_ADDRESS(child) && ((unsigned int)child & GOAL_TAG_MASK) == GOAL_BASIC_TAG &&
        OBJECT_TYPE(child) == (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE) &&
        method_id < child->num_methods && child->methods[method_id] == old_method &&
        type_typep(child, object_type) != (symbol*)FIX_SYM(FIX_SYM_FALSE)) {
      if (FastLink) {
        printf("************ WARNING **************\n");
        printf(
            "method %d of %s redefined - you must define class heirarchies in "
            "order now\n",
            method_id, SYMBOL_NAME(slot)->data);
        printf("***********************************\n");
      }
      child->methods[method_id] = (unsigned int)method;
    }
    slot += 8;
  }
  slot = SymbolTable2;
  while (slot < symbol_table) {
    child = *(type**)slot;
    if (!INVALID_GOAL_ADDRESS(child) && ((unsigned int)child & GOAL_TAG_MASK) == GOAL_BASIC_TAG &&
        OBJECT_TYPE(child) == (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE) &&
        method_id < child->num_methods && child->methods[method_id] == old_method &&
        type_typep(child, object_type) != (symbol*)FIX_SYM(FIX_SYM_FALSE)) {
      if (FastLink) {
        printf("************ WARNING **************\n");
        printf(
            "method %d of %s redefined - you must define class heirarchies in "
            "order now\n",
            method_id, SYMBOL_NAME(slot)->data);
        printf("***********************************\n");
      }
      child->methods[method_id] = (unsigned int)method;
    }
    slot += 8;
  }
  return (unsigned int)method;
}

// Validate a runtime type and call its selected GOAL method.
unsigned int call_method_of_type(unsigned int object, type* object_type, int method_id) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object_type)) {
    cprintf("#<#x%x has invalid type ptr #x%x>\n", object, object_type);
    return object;
  }
  if (OBJECT_TYPE(object_type) != (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE)) {
    // Retail omits one argument from this diagnostic's three placeholders.
    cprintf("#<#x%x has invalid type ptr #x%x, bad type #x%x>\n", object, OBJECT_TYPE(object_type));
    return object;
  }
  unsigned int (*function)(unsigned int) =
      (unsigned int (*)(unsigned int))object_type->methods[method_id];

  // TODO: probably a macro here.
  register unsigned int (*target)(unsigned int) asm("$25");
  asm("or %0,%1,$0" : "=r"(target) : "r"(function));
  unsigned int result = target(object);
  return result;
}

// Call a GOAL function from C.
extern "C" unsigned int call_goal_function(unsigned long long (*function)()) {
  // TODO: probably a macro here.
  register unsigned long long (*target)() asm("$25");
  asm("or %0,%1,$0" : "=r"(target) : "r"(function));
  unsigned int result = target();
  return result;
}

// Look up and call a global GOAL function.
extern "C" unsigned int call_goal_function_by_name(char* name) {
  symbol* sym = intern_from_c(name);
  return call_goal_function((unsigned long long (*)())sym->value);
}

// Validate a runtime type and call its selected GOAL method with two arguments.
unsigned int call_method_of_type_arg2(unsigned int object,
                                      type* object_type,
                                      int method_id,
                                      unsigned int arg1,
                                      unsigned int arg2) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object_type)) {
    cprintf("#<#x%x has invalid type ptr #x%x>\n", object, object_type);
    return object;
  }
  if (OBJECT_TYPE(object_type) != (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE)) {
    // BUG: missing an argument here.
    cprintf("#<#x%x has invalid type ptr #x%x, bad type #x%x>\n", object, OBJECT_TYPE(object_type));
    return object;
  }
  unsigned int (*function)(unsigned int, unsigned int, unsigned int) =
      (unsigned int (*)(unsigned int, unsigned int, unsigned int))object_type->methods[method_id];
  register unsigned int (*target)(unsigned int, unsigned int, unsigned int) asm("$25");
  asm("or %0,%1,$0" : "=r"(target) : "r"(function));
  unsigned int result = target(object, arg1, arg2);
  return result;
}

unsigned long long print_binteger(unsigned long long);
unsigned int print_pair(unsigned int);
unsigned long long inspect_binteger(unsigned long long);
unsigned int inspect_pair(unsigned int);

// Print an object followed by a newline.
unsigned int sprint(unsigned int object) {
  unsigned int result = print_object(object);
  cprintf("\n");
  return result;
}

// Relatively "safe" print function for a boxed GOAL object.
unsigned int print_object(unsigned int object) {
  // lower 3 bits are 0, it's binteger.
  if (!(object & GOAL_TAG_MASK))
    return print_binteger(object);

  // other types are all pointers, validate first.
  if (INVALID_GOAL_ADDRESS(object)) {
    cprintf("#<invalid object #x%x>", object);
    return object;
  }
  if ((object & GOAL_TAG_MASK) == GOAL_PAIR_TAG)
    return print_pair(object);
  if ((object & GOAL_TAG_MASK) == GOAL_BASIC_TAG) {
    return call_method_of_type(object, OBJECT_TYPE(object), GOAL_PRINT_METHOD);
  }
  cprintf("#<unknown type %d @ #x%x>", object & GOAL_TAG_MASK, object);
  return object;
}

// Print structure (just its address)
unsigned int print_structure(unsigned int object) {
  cprintf("#<structure @ #x%x>", (unsigned long long)object);
  return object;
}

// Print a basic as the type name and address.
unsigned int print_basic(unsigned int object) {
  if (INVALID_BASIC(object)) {
    cprintf("#<invalid basic #x%x>", object);
    return object;
  }
  cprintf("#<%s @ #x%x>", SYMBOL_NAME(OBJECT_TYPE(object)->name)->data, (unsigned long long)object);
  return object;
}

// Print proper and dotted lists. Circular lists are not detected.
unsigned int print_pair(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  unsigned int current;
  if (object == FIX_SYM(FIX_SYM_EMPTY_PAIR)) {
    cprintf("()");
  } else {
    current = object;
    cprintf("(");
    while ((current & GOAL_TAG_MASK) == GOAL_PAIR_TAG) {
      print_object(*(unsigned int*)(current + (unsigned int)-2));
      current = *(unsigned int*)(current + 2);
      if (current == FIX_SYM(FIX_SYM_EMPTY_PAIR)) {
        cprintf(")");
        return object;
      }
      cprintf(" ");
    }
    cprintf(". ");
    print_object(current);
    cprintf(")");
  }
  return object;
}

// Default integer print as decimal.
unsigned long long print_integer(unsigned long long object) {
  PrintPending = strend(PrintPending ? PrintPending : PrintBufArea + sizeof(ListenerMessageHeader));
  kitoa(PrintPending, object, 10, -1, '0', 0);
  return object;
}

// Default print boxed integer as decimal.
unsigned long long print_binteger(unsigned long long object) {
  PrintPending = strend(PrintPending ? PrintPending : PrintBufArea + sizeof(ListenerMessageHeader));
  kitoa(PrintPending, (long long)object >> 3, 10, -1, '0', 0);
  return object;
}

// Print float
unsigned int print_float(unsigned int object) {
  PrintPending = strend(PrintPending ? PrintPending : PrintBufArea + sizeof(ListenerMessageHeader));
  ftoa(PrintPending, *(float*)&object, -1, ' ', 4, 0);
  return object;
}

// Print a symbol as its name.
unsigned int print_symbol(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object) || OBJECT_TYPE(object) != (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE)) {
    cprintf("#<invalid symbol #x%x>", object);
    return object;
  }
  cprintf("%s", SYMBOL_NAME(object)->data);
  return object;
}

// Print a type as its name.
unsigned int print_type(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object) || OBJECT_TYPE(object) != (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE)) {
    cprintf("#<invalid type #x%x>", object);
    return object;
  }
  cprintf("%s", SYMBOL_NAME(((type*)object)->name)->data);
  return object;
}

// Print string as quoted string.
unsigned int print_string(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object) || OBJECT_TYPE(object) != (type*)FIX_SYM_VALUE(FIX_SYM_STRING_TYPE)) {
    cprintf("#<invalid string #x%x>", object);
    return object;
  }
  cprintf("\"%s\"", ((gstring*)object)->data);
  return object;
}

// Print the function (just its address)
unsigned int print_function(unsigned int object) {
  cprintf("#<compiled %s @ #x%x>", SYMBOL_NAME(OBJECT_TYPE(object)->name)->data, object);
  return object;
}

// Print the vu-function (just its address)
unsigned int print_vu_function(unsigned int object) {
  cprintf("#<compiled vu-function @ #x%x>", object);
  return object;
}

// Return a basic object's fixed allocation size from its type.
unsigned int asize_of_basic(unsigned int object) {
  return OBJECT_TYPE(object)->allocated_size;
}

// Default copy, does nothing.
unsigned int copy_fixed(unsigned int object, unsigned int heap) {
  return object;
}

// Default copy, does nothing.
unsigned int copy_structure(unsigned int object, unsigned int heap) {
  return object;
}

// Copy a basic object to a new heap allocation or to a supplied destination
// address.
unsigned int copy_basic(unsigned int object, unsigned int heap) {
  DECLARE_SYMBOL_TABLE();
  unsigned int size = call_method_of_type(object, OBJECT_TYPE(object), GOAL_ASIZE_METHOD);
  unsigned int result;
  if (OBJECT_TYPE(heap) == (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE)) {
    result = alloc_heap_object(heap, (unsigned int)OBJECT_TYPE(object), size);
    memcpy((void*)result, (void*)object, size - GOAL_BASIC_TAG);
  } else {
    result = heap;
    memcpy((void*)(result - GOAL_BASIC_TAG), (void*)(object - GOAL_BASIC_TAG), size);
  }
  return result;
}

// Relatively "safe" inspect function for a boxed GOAL object.
unsigned int inspect_object(unsigned int object) {
  if (!(object & GOAL_TAG_MASK))
    return inspect_binteger(object);
  if (INVALID_GOAL_ADDRESS(object)) {
    cprintf("#<invalid object #x%x>\n", object);
    return object;
  }
  if ((object & GOAL_TAG_MASK) == GOAL_PAIR_TAG)
    return inspect_pair(object);
  if ((object & GOAL_TAG_MASK) == GOAL_BASIC_TAG) {
    return call_method_of_type(object, OBJECT_TYPE(object), GOAL_INSPECT_METHOD);
  }
  cprintf("#<unknown type %d @ #x%x>", object & GOAL_TAG_MASK, object);
  return object;
}

// Inspect a pair as its address and contents.
unsigned int inspect_pair(unsigned int object) {
  cprintf("[%8x] pair ", object);
  print_pair(object);
  cprintf("\n");
  return object;
}

// Inspect a integer as its value.
unsigned long long inspect_integer(unsigned long long object) {
  cprintf("[%16lx] fixnum %ld\n", object, object);
  return object;
}

// Inspect a boxed integer as its value.
unsigned long long inspect_binteger(unsigned long long object) {
  cprintf("[%16lx] boxed-fixnum %ld\n", object, (long long)object >> 3);
  return object;
}

// Inspect a float as its value.
unsigned int inspect_float(unsigned int object) {
  cprintf("[%8x] float ", object);
  PrintPending = strend(PrintPending ? PrintPending : PrintBufArea + sizeof(ListenerMessageHeader));
  ftoa(PrintPending, *(float*)&object, -1, ' ', 4, 0);
  cprintf("\n");
  return object;
}

// Inspect a string as its length and data.
unsigned int inspect_string(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object) || OBJECT_TYPE(object) != (type*)FIX_SYM_VALUE(FIX_SYM_STRING_TYPE)) {
    cprintf("#<invalid string #x%x>\n", object);
    return object;
  }
  cprintf("[%8x] string\n\tallocated_length: %d\n\tdata: \"%s\"\n", object,
          ((gstring*)object)->length, ((gstring*)object)->data);
  return object;
}

// Inspect a symbol as its name, hash, and value.
unsigned int inspect_symbol(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object) || OBJECT_TYPE(object) != (type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE)) {
    cprintf("#<invalid symbol #x%x>\n", object);
    return object;
  }
  cprintf("[%8x] symbol\n\tname: %s\n\thash: #x%x\n\tvalue: ", object, SYMBOL_NAME(object)->data,
          SYMBOL_HASH(object));
  print_object(((symbol*)object)->value);
  cprintf("\n");
  return object;
}

// Inspect a type as its name, parent, and other metadata.
unsigned int inspect_type(unsigned int object) {
  DECLARE_SYMBOL_TABLE();
  if (INVALID_BASIC(object) || OBJECT_TYPE(object) != (type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE)) {
    cprintf("#<invalid type #x%x>\n", object);
    return object;
  }
  cprintf("[%8x] type\n\tname: %s\n\tparent: ", object, SYMBOL_NAME(((type*)object)->name)->data);
  print_object((unsigned int)((type*)object)->parent);
  cprintf("\n\tsize: %d/%d\n\theap-base: %d\n\tallocated_length: %d\n\tprint: ",
          ((type*)object)->allocated_size, ((type*)object)->padded_size, ((type*)object)->heap_base,
          ((type*)object)->num_methods);
  print_object(((type*)object)->methods[2]);
  cprintf("\n\tinspect: ");
  print_object(((type*)object)->methods[3]);
  cprintf("\n");
  return object;
}

// Inspect a structure as its address.
unsigned int inspect_structure(unsigned int object) {
  cprintf("[%8x] structure\n", object);
  return object;
}

// Inspect a basic
unsigned int inspect_basic(unsigned int object) {
  register unsigned int s7 asm("$23");
  if (INVALID_BASIC(object)) {
    cprintf("#<invalid basic #x%x>\n", object);
    return object;
  }
  cprintf("[%8x] ", object);
  print_object((unsigned int)OBJECT_TYPE(object));
  cprintf("\n");
  return object;
}

// This link block layout is not used in the linker.
unsigned int inspect_link_block(unsigned int object) {
  struct link_block {
    unsigned int length, version;
  };

  cprintf("[%8x] link-block\n\tallocated_length: %d\n\tversion: %d\n\tfunction: ", object,
          ((link_block*)object)->length, ((link_block*)object)->version);
  print_object(object + ((link_block*)object)->length);
  cprintf("\n");
  return object;
}

// This vu-function layout is not used.
unsigned int inspect_vu_function(unsigned int object) {
  struct vu_function {
    unsigned int length, origin, qlength;
  };

  cprintf("[%8x] vu-function\n\tlength: %d\n\torigin: #x%x\n\tqlength: %d\n", object,
          ((vu_function*)object)->length, ((vu_function*)object)->origin,
          ((vu_function*)object)->qlength);
  return object;
}

// Pack the type flags passed by the GOAL compiler.
#define TYPE_FLAGS(methods, heap_base, size) \
  (((long long)(methods) << 32) | ((long long)(heap_base) << 16) | (size))
#define SET_FIXED_TYPE(offset, name, parent, flags, print, inspect) \
  set_fixed_type(offset, name, parent, flags, (unsigned int*)(print), (unsigned int*)(inspect))

unsigned int load(gstring*, kheapinfo*);
unsigned int loado(gstring*, kheapinfo*);
void unload(gstring*);
int load_and_link(char*, char*, kheapinfo*, unsigned int);

// Allocate the symbol table, set up built-in types, and start the GOAL kernel.
int InitHeapAndSymbol() {
  DECLARE_SYMBOL_TABLE();
  char* table = kmalloc(kglobalheap, 0x20000, KMALLOC_MEMSET, "symbol-table");
  symbol_table = (unsigned int)(table + 0x8004);
  SymbolTable2 = (unsigned int)(table + 4);
  LastSymbol = (unsigned int)(table + 0xff00);
  NumSymbols = 0;
  reset_output();
  FIX_SYM_VALUE(FIX_SYM_EMPTY_CAR) = FIX_SYM(FIX_SYM_EMPTY_PAIR);
  FIX_SYM_VALUE(FIX_SYM_EMPTY_CDR) = FIX_SYM(FIX_SYM_EMPTY_PAIR);
  FIX_SYM_VALUE(FIX_SYM_GLOBAL_HEAP) = (unsigned int)kglobalheap;
  alloc_and_init_type((symbol*)FIX_SYM(FIX_SYM_TYPE_TYPE), 9);
  alloc_and_init_type((symbol*)FIX_SYM(FIX_SYM_SYMBOL_TYPE), 9);
  alloc_and_init_type((symbol*)FIX_SYM(FIX_SYM_STRING_TYPE), 9);
  alloc_and_init_type((symbol*)FIX_SYM(FIX_SYM_FUNCTION_TYPE), 9);

  // booleans
  set_fixed_symbol(FIX_SYM_FALSE, "#f", FIX_SYM(FIX_SYM_FALSE));
  set_fixed_symbol(FIX_SYM_TRUE, "#t", FIX_SYM(FIX_SYM_TRUE));

  // functions
  set_fixed_symbol(FIX_SYM_NOTHING_FUNC, "nothing", make_nothing_func());
  set_fixed_symbol(FIX_SYM_ZERO_FUNC, "zero-func", make_zero_func());
  set_fixed_symbol(FIX_SYM_ASIZE_OF_BASIC_FUNC, "asize-of-basic-func",
                   make_function_from_c((unsigned int (*)())asize_of_basic));
  // NOTE: this is a typo in the game too.
  set_fixed_symbol(FIX_SYM_COPY_BASIC_FUNC, "asize-of-basic-func",
                   make_function_from_c((unsigned int (*)())copy_basic));
  set_fixed_symbol(FIX_SYM_DEL_BASIC_FUNC, "delete-basic",
                   make_function_from_c((unsigned int (*)())delete_basic));

  // heap symbols
  set_fixed_symbol(FIX_SYM_GLOBAL_HEAP, "global", (unsigned int)kglobalheap);
  set_fixed_symbol(FIX_SYM_DEBUG_HEAP, "debug", (unsigned int)kdebugheap);
  set_fixed_symbol(FIX_SYM_STATIC, "static", FIX_SYM(FIX_SYM_STATIC));
  set_fixed_symbol(FIX_SYM_LOADING_LEVEL, "loading-level", (unsigned int)kglobalheap);
  set_fixed_symbol(FIX_SYM_LOADING_PACKAGE, "loading-package", (unsigned int)kglobalheap);
  set_fixed_symbol(FIX_SYM_PROCESS_LEVEL_HEAP, "process-level-heap", (unsigned int)kglobalheap);
  set_fixed_symbol(FIX_SYM_STACK, "stack", FIX_SYM(FIX_SYM_STACK));
  set_fixed_symbol(FIX_SYM_SCRATCH, "scratch", FIX_SYM(FIX_SYM_SCRATCH));
  set_fixed_symbol(FIX_SYM_SCRATCH_TOP, "*scratch-top*", 0x70000000);

  // level stuff
  set_fixed_symbol(FIX_SYM_LEVEL, "level", 0);
  set_fixed_symbol(FIX_SYM_ART_GROUP, "art-group", 0);
  set_fixed_symbol(FIX_SYM_TX_PAGE_DIR, "texture-page-dir", 0);
  set_fixed_symbol(FIX_SYM_TX_PAGE, "texture-page", 0);
  set_fixed_symbol(FIX_SYM_SOUND, "sound", 0);
  set_fixed_symbol(FIX_SYM_DGO, "dgo", 0);
  set_fixed_symbol(FIX_SYM_TOP_LEVEL, "top-level", FIX_SYM_VALUE(FIX_SYM_NOTHING_FUNC));

  // OBJECT type
  unsigned int new_illegal_func = make_function_from_c((unsigned int (*)())new_illegal);
  {
    unsigned int delete_illegal_func = make_function_from_c((unsigned int (*)())delete_illegal);
    SET_FIXED_TYPE(FIX_SYM_OBJECT_TYPE, "object", FIX_SYM(FIX_SYM_OBJECT_TYPE), TYPE_FLAGS(9, 0, 4),
                   make_function_from_c((unsigned int (*)())print_object),
                   make_function_from_c((unsigned int (*)())inspect_object));
    ((type*)FIX_SYM_VALUE(FIX_SYM_OBJECT_TYPE))->methods[0] = FIX_SYM_VALUE(FIX_SYM_NOTHING_FUNC);
    ((type*)FIX_SYM_VALUE(FIX_SYM_OBJECT_TYPE))->methods[1] = delete_illegal_func;
    ((type*)FIX_SYM_VALUE(FIX_SYM_OBJECT_TYPE))->methods[5] = FIX_SYM_VALUE(FIX_SYM_ZERO_FUNC);
    ((type*)FIX_SYM_VALUE(FIX_SYM_OBJECT_TYPE))->methods[6] =
        make_function_from_c((unsigned int (*)())copy_fixed);

    // STRUCTURE type
    SET_FIXED_TYPE(FIX_SYM_STRUCTURE_TYPE, "structure", FIX_SYM(FIX_SYM_OBJECT_TYPE),
                   TYPE_FLAGS(9, 0, 4), make_function_from_c((unsigned int (*)())print_structure),
                   make_function_from_c((unsigned int (*)())inspect_structure));
    ((type*)FIX_SYM_VALUE(FIX_SYM_STRUCTURE_TYPE))->methods[0] =
        make_function_from_c((unsigned int (*)())new_structure);
    ((type*)FIX_SYM_VALUE(FIX_SYM_STRUCTURE_TYPE))->methods[1] =
        make_function_from_c((unsigned int (*)())delete_structure);

    // BASIC type
    SET_FIXED_TYPE(FIX_SYM_BASIC_TYPE, "basic", FIX_SYM(FIX_SYM_STRUCTURE_TYPE),
                   TYPE_FLAGS(9, 0, 4), make_function_from_c((unsigned int (*)())print_basic),
                   make_function_from_c((unsigned int (*)())inspect_basic));
    ((type*)FIX_SYM_VALUE(FIX_SYM_BASIC_TYPE))->methods[0] =
        make_function_from_c((unsigned int (*)())new_basic);
    ((type*)FIX_SYM_VALUE(FIX_SYM_BASIC_TYPE))->methods[1] = FIX_SYM_VALUE(FIX_SYM_DEL_BASIC_FUNC);
    ((type*)FIX_SYM_VALUE(FIX_SYM_BASIC_TYPE))->methods[5] =
        FIX_SYM_VALUE(FIX_SYM_ASIZE_OF_BASIC_FUNC);
    ((type*)FIX_SYM_VALUE(FIX_SYM_BASIC_TYPE))->methods[6] = FIX_SYM_VALUE(FIX_SYM_COPY_BASIC_FUNC);

    // SYMBOL type
    SET_FIXED_TYPE(FIX_SYM_SYMBOL_TYPE, "symbol", FIX_SYM(FIX_SYM_BASIC_TYPE), TYPE_FLAGS(9, 0, 8),
                   make_function_from_c((unsigned int (*)())print_symbol),
                   make_function_from_c((unsigned int (*)())inspect_symbol));
    ((type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE))->methods[0] = new_illegal_func;
    ((type*)FIX_SYM_VALUE(FIX_SYM_SYMBOL_TYPE))->methods[1] = delete_illegal_func;

    // TYPE type
    SET_FIXED_TYPE(FIX_SYM_TYPE_TYPE, "type", FIX_SYM(FIX_SYM_BASIC_TYPE), TYPE_FLAGS(9, 0, 0x38),
                   make_function_from_c((unsigned int (*)())print_type),
                   make_function_from_c((unsigned int (*)())inspect_type));
    ((type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE))->methods[0] =
        make_function_from_c((unsigned int (*)())new_type);
    ((type*)FIX_SYM_VALUE(FIX_SYM_TYPE_TYPE))->methods[1] = delete_illegal_func;

    // STRING type
    SET_FIXED_TYPE(FIX_SYM_STRING_TYPE, "string", FIX_SYM(FIX_SYM_BASIC_TYPE), TYPE_FLAGS(9, 0, 8),
                   make_function_from_c((unsigned int (*)())print_string),
                   make_function_from_c((unsigned int (*)())inspect_string));

    // FUNCTION type
    SET_FIXED_TYPE(FIX_SYM_FUNCTION_TYPE, "function", FIX_SYM(FIX_SYM_BASIC_TYPE),
                   TYPE_FLAGS(9, 0, 4), make_function_from_c((unsigned int (*)())print_function),
                   0);

    ((type*)FIX_SYM_VALUE(FIX_SYM_FUNCTION_TYPE))->methods[0] = new_illegal_func;
    ((type*)FIX_SYM_VALUE(FIX_SYM_FUNCTION_TYPE))->methods[1] = delete_illegal_func;

    // VU FUNCTION type
    SET_FIXED_TYPE(FIX_SYM_VU_FUNCTION_TYPE, "vu-function", FIX_SYM(FIX_SYM_STRUCTURE_TYPE),
                   TYPE_FLAGS(9, 0, 0x10),
                   make_function_from_c((unsigned int (*)())print_vu_function),
                   make_function_from_c((unsigned int (*)())inspect_vu_function));

    ((type*)FIX_SYM_VALUE(FIX_SYM_VU_FUNCTION_TYPE))->methods[1] = delete_illegal_func;

    // LINK BLOCK type
    SET_FIXED_TYPE(FIX_SYM_LINK_BLOCK, "link-block", FIX_SYM(FIX_SYM_BASIC_TYPE),
                   TYPE_FLAGS(9, 0, 0xc), 0,
                   make_function_from_c((unsigned int (*)())inspect_link_block));
    ((type*)FIX_SYM_VALUE(FIX_SYM_LINK_BLOCK))->methods[0] = new_illegal_func;
    ((type*)FIX_SYM_VALUE(FIX_SYM_LINK_BLOCK))->methods[1] = delete_illegal_func;

    // KHEAP
    SET_FIXED_TYPE(FIX_SYM_KHEAP, "kheap", FIX_SYM(FIX_SYM_STRUCTURE_TYPE), TYPE_FLAGS(9, 0, 0x10),
                   0, make_function_from_c((unsigned int (*)())kheapstatus));

    // ARRAY
    SET_FIXED_TYPE(FIX_SYM_ARRAY_TYPE, "array", FIX_SYM(FIX_SYM_BASIC_TYPE), TYPE_FLAGS(9, 0, 0x10),
                   0, 0);

    // PAIR
    SET_FIXED_TYPE(FIX_SYM_PAIR_TYPE, "pair", FIX_SYM(FIX_SYM_OBJECT_TYPE), TYPE_FLAGS(9, 0, 8),
                   make_function_from_c((unsigned int (*)())print_pair),
                   make_function_from_c((unsigned int (*)())inspect_pair));
    ((type*)FIX_SYM_VALUE(FIX_SYM_PAIR_TYPE))->methods[0] =
        make_function_from_c((unsigned int (*)())new_pair);
    ((type*)FIX_SYM_VALUE(FIX_SYM_PAIR_TYPE))->methods[1] =
        make_function_from_c((unsigned int (*)())delete_pair);

    // KERNEL TYPES
    SET_FIXED_TYPE(FIX_SYM_PROCESS_TREE_TYPE, "process-tree", FIX_SYM(FIX_SYM_BASIC_TYPE),
                   TYPE_FLAGS(0xe, 0, 0x20), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_PROCESS_TYPE, "process", FIX_SYM(FIX_SYM_PROCESS_TREE_TYPE),
                   TYPE_FLAGS(0xe, 0, 0x70), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_THREAD_TYPE, "thread", FIX_SYM(FIX_SYM_BASIC_TYPE),
                   TYPE_FLAGS(0xc, 0, 0x28), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_CONNECTABLE_TYPE, "connectable", FIX_SYM(FIX_SYM_STRUCTURE_TYPE),
                   TYPE_FLAGS(9, 0, 0x10), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_STACK_FRAME_TYPE, "stack-frame", FIX_SYM(FIX_SYM_BASIC_TYPE),
                   TYPE_FLAGS(9, 0, 0xc), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_FILE_STREAM_TYPE, "file-stream", FIX_SYM(FIX_SYM_BASIC_TYPE),
                   TYPE_FLAGS(9, 0, 0x14), 0, 0);

    // NUMERIC TYPES
    SET_FIXED_TYPE(FIX_SYM_POINTER_TYPE, "pointer", FIX_SYM(FIX_SYM_OBJECT_TYPE),
                   TYPE_FLAGS(9, 0, 4), 0, 0);

    ((type*)FIX_SYM_VALUE(FIX_SYM_POINTER_TYPE))->methods[0] = new_illegal_func;

    // NUMERIC TYPES
    SET_FIXED_TYPE(FIX_SYM_NUMBER_TYPE, "number", FIX_SYM(FIX_SYM_OBJECT_TYPE), TYPE_FLAGS(9, 0, 8),
                   make_function_from_c((unsigned int (*)())print_integer),
                   make_function_from_c((unsigned int (*)())inspect_integer));

    ((type*)FIX_SYM_VALUE(FIX_SYM_NUMBER_TYPE))->methods[0] = new_illegal_func;

    SET_FIXED_TYPE(FIX_SYM_FLOAT_TYPE, "float", FIX_SYM(FIX_SYM_NUMBER_TYPE), TYPE_FLAGS(9, 0, 4),
                   make_function_from_c((unsigned int (*)())print_float),
                   make_function_from_c((unsigned int (*)())inspect_float));

    SET_FIXED_TYPE(FIX_SYM_INTEGER_TYPE, "integer", FIX_SYM(FIX_SYM_NUMBER_TYPE),
                   TYPE_FLAGS(9, 0, 8), 0, 0);

    SET_FIXED_TYPE(FIX_SYM_BINTEGER_TYPE, "binteger", FIX_SYM(FIX_SYM_INTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 8), make_function_from_c((unsigned int (*)())print_binteger),
                   make_function_from_c((unsigned int (*)())inspect_binteger));

    SET_FIXED_TYPE(FIX_SYM_SINTEGER_TYPE, "sinteger", FIX_SYM(FIX_SYM_INTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 8), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_INT8_TYPE, "int8", FIX_SYM(FIX_SYM_SINTEGER_TYPE), TYPE_FLAGS(9, 0, 1),
                   0, 0);
    SET_FIXED_TYPE(FIX_SYM_INT16_TYPE, "int16", FIX_SYM(FIX_SYM_SINTEGER_TYPE), TYPE_FLAGS(9, 0, 2),
                   0, 0);
    SET_FIXED_TYPE(FIX_SYM_INT32_TYPE, "int32", FIX_SYM(FIX_SYM_SINTEGER_TYPE), TYPE_FLAGS(9, 0, 4),
                   0, 0);
    SET_FIXED_TYPE(FIX_SYM_INT64_TYPE, "int64", FIX_SYM(FIX_SYM_SINTEGER_TYPE), TYPE_FLAGS(9, 0, 8),
                   0, 0);
    SET_FIXED_TYPE(FIX_SYM_INT128_TYPE, "int128", FIX_SYM(FIX_SYM_SINTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 0x10), 0, 0);

    SET_FIXED_TYPE(FIX_SYM_UINTEGER_TYPE, "uinteger", FIX_SYM(FIX_SYM_INTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 8), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_UINT8_TYPE, "uint8", FIX_SYM(FIX_SYM_UINTEGER_TYPE), TYPE_FLAGS(9, 0, 1),
                   0, 0);
    SET_FIXED_TYPE(FIX_SYM_UINT16_TYPE, "uint16", FIX_SYM(FIX_SYM_UINTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 2), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_UINT32_TYPE, "uint32", FIX_SYM(FIX_SYM_UINTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 4), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_UINT64_TYPE, "uint64", FIX_SYM(FIX_SYM_UINTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 8), 0, 0);
    SET_FIXED_TYPE(FIX_SYM_UINT128_TYPE, "uint128", FIX_SYM(FIX_SYM_UINTEGER_TYPE),
                   TYPE_FLAGS(9, 0, 0x10), 0, 0);

    // Object new macro
    ((type*)FIX_SYM_VALUE(FIX_SYM_OBJECT_TYPE))->methods[0] =
        make_function_from_c((unsigned int (*)())alloc_heap_object);
  }

  // Stuff that isn't in a fixed spot:
  make_function_symbol_from_c("string->symbol", (unsigned int (*)())intern);
  make_function_symbol_from_c("print", (unsigned int (*)())sprint);
  make_function_symbol_from_c("inspect", (unsigned int (*)())inspect_object);

  // loads
  make_function_symbol_from_c("load", (unsigned int (*)())load);
  make_function_symbol_from_c("loado", (unsigned int (*)())loado);
  make_function_symbol_from_c("unload", (unsigned int (*)())unload);
  make_function_symbol_from_c("_format", (unsigned int (*)())format);

  // allocations
  make_function_symbol_from_c("malloc", (unsigned int (*)())alloc_heap_memory);
  make_function_symbol_from_c("kmalloc", (unsigned int (*)())goal_malloc);
  make_function_symbol_from_c("new-dynamic-structure", (unsigned int (*)())new_dynamic_structure);

  // type system
  make_function_symbol_from_c("method-set!", (unsigned int (*)())method_set);

  // dgo
  make_function_symbol_from_c("link", (unsigned int (*)())link_and_exec);
  make_function_symbol_from_c("dgo-load", (unsigned int (*)())load_and_link_dgo);

  // forward declare
  make_raw_function_symbol_from_c("ultimate-memcpy", 0);
  make_raw_function_symbol_from_c("memcpy-and-rellink", 0);  // never used
  make_raw_function_symbol_from_c("symlink2", 0);
  make_raw_function_symbol_from_c("symlink3", 0);

  // game stuff
  make_function_symbol_from_c("link-begin", (unsigned int (*)())link_begin);
  make_function_symbol_from_c("link-resume", (unsigned int (*)())link_resume);
  make_function_symbol_from_c("mc-run", (unsigned int (*)())MC_run);
  make_function_symbol_from_c("mc-format", (unsigned int (*)())MC_format);
  make_function_symbol_from_c("mc-unformat", (unsigned int (*)())MC_unformat);
  make_function_symbol_from_c("mc-create-file", (unsigned int (*)())MC_createfile);
  make_function_symbol_from_c("mc-save", (unsigned int (*)())MC_save);
  make_function_symbol_from_c("mc-load", (unsigned int (*)())MC_load);
  make_function_symbol_from_c("mc-check-result", (unsigned int (*)())MC_check_result);
  make_function_symbol_from_c("mc-get-slot-info", (unsigned int (*)())MC_get_status);
#if REVISION >= 20010920
  make_function_symbol_from_c("mc-makefile", (unsigned int (*)())MC_makefile);
#endif
#if REVISION >= 20011019
  make_function_symbol_from_c("kset-language", (unsigned int (*)())MC_set_language);
#endif

  {
    symbol* sym = intern_from_c("*debug-segment*");
    sym->value = (unsigned int)(DebugSegment ? (symbol*)((char*)symbol_table + FIX_SYM_TRUE)
                                             : (symbol*)((char*)symbol_table + FIX_SYM_FALSE));
    sym = intern_from_c("*enable-method-set*");
    sym->value = 0;
    EnableMethodSet = &sym->value;
#if REVISION >= 20010920
    sym = intern_from_c("*boot-video-mode*");
#ifdef VIDEO_PAL
    sym->value = 1;
#else
    sym->value = 0;
#endif
#endif
    (*EnableMethodSet)++;
    load_and_link_dgo_from_c("kernel", kglobalheap,
                             LINK_FLAG_OUTPUT_LOAD | LINK_FLAG_EXECUTE | LINK_FLAG_PRINT_LOGIN,
                             0x400000);
    (*EnableMethodSet)--;
  }
  unsigned int version = intern_from_c("*kernel-version*")->value;
  if (!version || ((version >> 19) & 0xffff) != 20) {
    MsgErr("\n");
    MsgErr(
        "dkernel: compiled C kernel version is %d.%d but the goal kernel is "
        "%d.%d!\n"
        "\tfrom the goal> prompt (:mch) then mkee your kernel in linux.\n",
        20, 0, (version >> 19) & 0xffff, (version >> 3) & 0xffff);
    return -1;
  }
  GoalProtoBlock.deci2count = (int*)intern_from_c("*deci-count*");
  InitListener();
  InitMachineScheme();
  return 0;
}

// Load code or data through the development filesystem to the given heap.
// During load, the package heap is temporary set to this heap, then restored.
unsigned int load(gstring* name, kheapinfo* heap) {
  DECLARE_SYMBOL_TABLE();
  char decoded[256];
  int result;
  unsigned int previous = FIX_SYM_VALUE(FIX_SYM_LOADING_PACKAGE);
  FIX_SYM_VALUE(FIX_SYM_LOADING_PACKAGE) = (unsigned int)heap;
  kstrcpy(decoded, DecodeFileName(name->data));
  result = load_and_link(
      name->data, decoded, heap,
      LINK_FLAG_OUTPUT_TRUE | LINK_FLAG_OUTPUT_LOAD | LINK_FLAG_EXECUTE | LINK_FLAG_PRINT_LOGIN);
  FIX_SYM_VALUE(FIX_SYM_LOADING_PACKAGE) = previous;
  return result < 0 ? symbol_table : result;
}

// C-string entry point for loading code with explicit link flags.
unsigned int loadc(char* name, kheapinfo* heap, unsigned int flags) {
  DECLARE_SYMBOL_TABLE();
  char decoded[256];
  int result;
  unsigned int previous = FIX_SYM_VALUE(FIX_SYM_LOADING_PACKAGE);
  FIX_SYM_VALUE(FIX_SYM_LOADING_PACKAGE) = (unsigned int)heap;
  printf("****** CALL TO loadc() ******\n");
  kstrcpy(decoded, MakeFileName(CODE_FILE_TYPE, name, 0, 0, 0));
  result = load_and_link(name, decoded, heap, flags);
  FIX_SYM_VALUE(FIX_SYM_LOADING_PACKAGE) = previous;
  return result < 0 ? symbol_table : result;
}

// Load an object file without executing its top-level code.
unsigned int loado(gstring* name, kheapinfo* heap) {
  DECLARE_SYMBOL_TABLE();
  char decoded[256];
  int result;
  printf("****** CALL TO loado(%s) ******\n", name->data);
  kstrcpy(decoded, MakeFileName(DATA_FILE_TYPE, name->data, 0, 0, 0));
  result = load_and_link(name->data, decoded, heap, LINK_FLAG_PRINT_LOGIN);
  return result < 0 ? symbol_table : result;
}

// Tell the listener that an object was unloaded; its allocation is not
// reclaimed.
void unload(gstring* name) {
  output_unload(name->data);
}

// Read an object file and pass its buffer to the linker.
int load_and_link(char* name, char* decoded, kheapinfo* heap, unsigned int flags) {
  int size;
  unsigned char* memory = FileLoad(decoded, heap, 0, KMALLOC_ALIGN_64, &size);
  if ((int)memory < 0)
    return (int)memory;
  int unused;  // needed to match.
  int result = (int)link_and_exec(memory, decoded, size, heap, flags);
  return result;
}

// kscheme and klink are compiled together in one file for unknown reasons.
#include "next/kernel/klink.cpp"

unsigned int crc_table[256] = {0};

unsigned int SymbolTable2 = 0;
unsigned int LastSymbol = 0;
int NumSymbols = 0;
unsigned int* EnableMethodSet = 0;
unsigned int symbol_time = 0;
unsigned int symbol_cnt = 0;
unsigned int symbol_fail = 0;
unsigned int symbol_fixed = 0;
