#include "next/kernel/kmalloc.h"
#include "next/kernel/kprint.h"
#include "next/kernel/kscheme.h"
#include "next/kernel/memory_layout.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/newlib/stdlib.h"
#include "sce/ee/newlib/string.h"

// Kernel global, debug, and link heap variables.
static kheapinfo _globalheap;
static kheapinfo _debugheap;
static kheapinfo _linkheap;
kheapinfo* kglobalheap = &_globalheap;
kheapinfo* kdebugheap = &_debugheap;
// linkheap and systemheap aren't used.
kheapinfo* klinkheap = &_linkheap;
kheapinfo* ksystemheap = 0;

// Allocate through libc.
char* ksmalloc(void* heap, int size, unsigned int flags, const char* name) {
  unsigned int alignment = flags & 0xfff;
  unsigned int memory = (unsigned int)((flags & KMALLOC_MEMSET) ? calloc(1, size + alignment)
                                                                : malloc(size + alignment));
  switch (alignment) {
    case KMALLOC_ALIGN_64:
      memory = (memory + 63) & 0xffffffc0;
      break;
    case KMALLOC_ALIGN_256:
      memory = (memory + 255) & 0xffffff00;
      break;
  }
  return (char*)memory;
}

// Print heap usage and, for the global heap, the space remaining before the stack.
kheapinfo* kheapstatus(kheapinfo* heap) {
  Msg(6, "[%8x] kheap\n\tbase: #x%x\n\ttop-base: #x%x\n\tcur: #x%x\n\ttop: #x%x\n", heap,
      heap->base, heap->top_base, heap->current, heap->top);
  Msg(6, "\t used bot: %d of %d bytes\n\t used top: %d of %d bytes\n\t symbols: %d of %d\n",
      heap->current - heap->base, heap->top_base - heap->base, heap->top_base - heap->top,
      heap->top_base - heap->base, NumSymbols, 8192);
  if (heap == kglobalheap) {
    Msg(6, "\t %d bytes before stack\n", GLOBAL_HEAP_END - (unsigned int)heap->current);
  }
  return heap;
}

// Initialize heap
kheapinfo* kinitheap(kheapinfo* heap, void* memory, int size) {
  heap->base = (char*)memory;
  heap->current = (char*)memory;
  heap->top = (char*)memory + size;
  heap->top_base = heap->top;
  memset(memory, 0, size);
  return heap;
}

// Return the number of bytes consumed by bottom (non-temp) allocations.
unsigned int kheapused(kheapinfo* heap) {
  return heap->current - heap->base;
}

// Allocate from either end of a heap.
char* kmalloc(kheapinfo* heap, int size, unsigned int flags, const char* name) {
  unsigned int alignment = flags & 0xfff;
  char* memory;
  char* end;
  if (!heap) {
    Msg(6, "--------------------> kmalloc: alloc %s mem %s #x%x (a:%d  %d bytes)\n", "DEBUG", name,
        memory, alignment, size);
    heap = kglobalheap;
  }
  if (flags & KMALLOC_TOP) {
    memory = heap->top - size;
    if (!alignment)
      alignment = KMALLOC_ALIGN_16;
    memory = (char*)((unsigned int)memory & -alignment);
    if (!size)
      return memory;
    end = memory;
    if (end <= heap->current) {
      Msg(6, "kmalloc: !alloc mem from top %s (%d bytes) heap %x\n", name, size, heap);
      kheapstatus(heap);
      return 0;
    }
    heap->top = end;
  } else {
    memory = heap->current;
    switch (alignment) {
      case KMALLOC_ALIGN_64:
        memory = (char*)(((unsigned int)memory + 63) & 0xffffffc0);
        break;
      case KMALLOC_ALIGN_256:
        memory = (char*)(((unsigned int)memory + 255) & 0xffffff00);
        break;
      default:
        memory = (char*)(((unsigned int)memory + 15) & 0xfffffff0);
        break;
    }
    if (!size)
      return memory;
    end = memory + size;
    if (end >= heap->top) {
      Msg(6, "kmalloc: !alloc mem %s (%d bytes) heap %x\n", name, size, heap);
      kheapstatus(heap);
      return 0;
    }
    heap->current = end;
  }
  if (flags & KMALLOC_MEMSET)
    memset(memory, 0, size);
  return memory;
}

// can't free memory.
void kfree(void* memory) {
}
