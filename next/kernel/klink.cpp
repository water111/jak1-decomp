// GOAL linker.
#include "next/kernel/klink.h"
#include "next/kernel/fileio.h"
#include "next/kernel/kboot.h"
#include "next/kernel/kmachine.h"
#include "next/kernel/kmalloc.h"
#include "next/kernel/kprint.h"
#include "next/kernel/kscheme.h"
#include "sce/ee/newlib/string.h"

static void update_goal_fns();

// Prepare the file and heaps for linking.
// This kicks out the data from the heap so work can later re-allocate and move
// it.
void link_control::begin(void* object_file,
                         char* name,
                         int size,
                         kheapinfo* target_heap,
                         unsigned int link_flags) {
  object_data = (char*)object_file;
  kstrcpy(object_name, name);
  object_size = size;
  heap = target_heap;
  flags = link_flags;
  entry = 0;
  heap_top = heap->top;
  keep_debug = 0;
  link_block = (link_block_v3*)(object_data + 4);
  code_size = 0;
  code_start = object_data;
  state = 0;
  segment_process = 0;
  if (link_block->version < 4) {
    object_data += link_block->length;
    code_size = object_size - link_block->length;
    if ((int)link_block >= (int)heap->base && (int)link_block < (int)heap->top) {
      // link block is not in temporary allocation, move it there.
      char* new_block = kmalloc(heap, link_block->length, KMALLOC_TOP, "link-block");
      char* old_block = (char*)link_block - 4;
      ultimate_memcpy(new_block, old_block, link_block->length);
      link_block = (link_block_v3*)(new_block + 4);
      // bump heap pointer back, leaving the original link block and object in
      // freed space. WORK functions will handle this.
      if ((int)old_block < (int)heap->current)
        heap->current = old_block;
    } else {
      // Object isn't in the temporary top area, bump heap pointer back to leave
      // it in freed space. Work functions will take care of this.
      if ((int)object_data >= (int)heap->base && (int)object_data < (int)heap->top &&
          (int)object_data < (int)heap->current)
        heap->current = object_data;
    }
    // After this point, the linker needs to allocate space for object.
  } else {
    link_header_v4* header = (link_header_v4*)object_data;
    link_block = (link_block_v3*)(object_data + (header->code_size + 4) + sizeof(link_header_v4));
    object_data += sizeof(link_header_v4);
    code_size = header->code_size;
  }
#if REVISION < 20011019
  if ((flags & LINK_FLAG_FORCE_DEBUG) && MasterDebug)
#else
  if ((flags & LINK_FLAG_FORCE_DEBUG) && MasterDebug && !DiskBoot)
#endif
    keep_debug = 1;
}

// Make progress on linking the file.
unsigned int link_control::work() {
  DECLARE_SYMBOL_TABLE();
  unsigned int old_debug = DebugSegment;
  if (keep_debug)
    DebugSegment = FIX_SYM(FIX_SYM_TRUE);
  OBJECT_TYPE(link_block) = (type*)FIX_SYM_VALUE(FIX_SYM_LINK_BLOCK);
  unsigned int result;
  // both v2 and v4 use the same format for link data
  if (link_block->version == 2 || link_block->version == 4)
    result = work_v2();
  else if (link_block->version == 3)
    result = work_v3();
  DebugSegment = old_debug;
  return result;
}

// Copy the three code segments, then relocate pointers, symbols, and types.
unsigned int link_control::work_v3() {
  unsigned int start = read_clock();
  // Copy segments.
  if (state == 0) {
    link_block_v3* block = link_block;
    int segment;
    int count = block->segment_count;
    segments = block->segments;
    for (segment = count - 1; segment >= 0; segment--) {
      segments[segment].relocs =
          (unsigned char*)block + ((unsigned int)segments[segment].relocs - 4U);
      segments[segment].data = object_data + (unsigned int)segments[segment].data;
      switch (segment) {
        case 0:
          if (segments[segment].size) {
            char* source = segments[segment].data;
            segments[segment].data = kmalloc(heap, segments[segment].size, 0, "main-segment");
            if (!segments[segment].data) {
              MsgErr("dkernel: unable to malloc %d bytes for main-segment~%",
                     segments[segment].size);
              return 1;
            }
            ultimate_memcpy(segments[segment].data, source, segments[segment].size);
          } else
            segments[segment].data = 0;
          break;
        case 1:
          if (DebugSegment) {
            if (segments[segment].size) {
              char* source = segments[segment].data;
              segments[segment].data =
                  kmalloc(kdebugheap, segments[segment].size, 0, "debug-segment");
              if (!segments[segment].data) {
                MsgErr("dkernel: unable to malloc %d bytes for debug-segment~%",
                       segments[segment].size);
                return 1;
              }
              ultimate_memcpy(segments[segment].data, source, segments[segment].size);
            } else
              segments[segment].data = 0;
          } else {
            segments[segment].data = 0;
            segments[segment].size = 0;
          }
          break;
        case 2:
          if (segments[segment].size) {
            char* source = segments[segment].data;
            segments[segment].data =
                kmalloc(heap, segments[segment].size, KMALLOC_TOP, "top-level-segment");
            if (!segments[segment].data) {
              MsgErr("dkernel: unable to malloc %d bytes for top-level-segment~%",
                     segments[segment].size);
              return 1;
            }
            ultimate_memcpy(segments[segment].data, source, segments[segment].size);
          } else
            segments[segment].data = 0;
          break;
      }
    }
    state = 1;
    segment_process = 0;
    if (read_clock() - start > 200000)
      return 0;
  }
next_segment:
  // TODO: probably a big switch over states ??
  if (state < 4 && segment_process == 0) {
    link_block_v3* block = link_block;
    if (segments[state - 1].data && segments[state - 1].size) {
      reloc_ptr = segments[state - 1].relocs;
      object_data = segments[state - 1].data;
      base_ptr = (unsigned int)object_data;
      loc_ptr = (unsigned int*)(object_data - 4);
      table_toggle = 0;
      segment_process = 1;
    } else {
      state++;
      segment_process = 0;
      goto next_segment;
    }
  }
  if (state < 4) {
    if (segment_process == 1) {
      int budget = 0x400;
      if (*reloc_ptr) {
        for (;;) {
          if (table_toggle) {
            for (int i = 0; i < *reloc_ptr; i++) {
              if (*loc_ptr >> 24) {
                int segment = (*loc_ptr >> 8) & 15;
                int low_word = (*loc_ptr >> 12) & 15;
                unsigned int address = (unsigned int)segments[segment].data +
                                       (((*loc_ptr & 255) << 8) | (loc_ptr[low_word] & 0xffff));
                if (!DebugSegment && segment == 1)
                  address = 0;
                *loc_ptr = (*loc_ptr & 0xffff0000) | (address >> 16);
                loc_ptr[low_word] = (loc_ptr[low_word] & 0xffff0000) | (address & 0xffff);
              } else
                *loc_ptr += base_ptr;
              loc_ptr++;
            }
          } else
            loc_ptr += *reloc_ptr;
          if (*reloc_ptr == 255) {
            reloc_ptr++;
            if (!*reloc_ptr) {
              reloc_ptr++;
              table_toggle ^= 1;
            }
            continue;
          } else {
            reloc_ptr++;
            table_toggle ^= 1;
          }
          if (!*reloc_ptr)
            break;
          if (!--budget) {
            if (read_clock() - start > 200000)
              return 0;
            budget = 0x400;
          }
        }
      }
      reloc_ptr++;
      segment_process++;
    }
    if (segment_process == 2) {
      while (*reloc_ptr) {
        int relocation = *reloc_ptr++;
        unsigned int object;
        if (relocation & 128) {
          int methods = relocation & 127;
          object = intern_type_from_c((char*)reloc_ptr, methods);
        } else {
          reloc_ptr--;
          object = (unsigned int)intern_from_c((char*)reloc_ptr);
        }
        reloc_ptr += strlen((char*)reloc_ptr) + 1;
        reloc_ptr = symlink3(object_data, object, reloc_ptr);
        if (read_clock() - start > 200000)
          return 0;
      }
      entry = object_data + 4;
      state++;
      segment_process = 0;
      goto next_segment;
    }
  }
  update_goal_fns();
  return 1;
}

// Copy and link segments.
unsigned int link_control::work_v2() {
  unsigned int start = read_clock();
  if (state == 0) {
    if (segment_process == 0)
      heap_gap = object_data - heap->current;
    // if loaded to the right spot, skip the copy!
    if (heap_gap < 0x90) {
      heap->current = object_data + code_size;
      if (heap->current >= heap->top) {
        MsgErr("dkernel: heap overflow~%");
        return 1;
      }
    } else {
      if (segment_process == 0) {
        original_object_location = object_data;
        object_data = kmalloc(heap, code_size, 0, "data-segment");
        if (!object_data) {
          MsgErr("dkernel: unable to malloc %d bytes for data-segment~%", code_size);
          return 1;
        }
      }
      char* destination = object_data;
      char* source = original_object_location;
      unsigned int size = code_size;
      source += segment_process;
      destination += segment_process;
      size -= segment_process;
      if (size > 0x80000) {
        ultimate_memcpy(destination, source, 0x80000);
        segment_process += 0x80000;
        return 0;
      } else if (size) {
        ultimate_memcpy(destination, source, size);
        if (segment_process > 0) {
          state = 1;
          segment_process = 0;
          return 0;
        }
      }
    }
    state = 1;
    segment_process = 0;
  }
  if (state == 1 && segment_process == 0) {
    reloc_ptr = (unsigned char*)link_block + 8;
    if (*reloc_ptr) {
      base_ptr = (unsigned int)object_data;
      loc_ptr = (unsigned int*)object_data;
      table_toggle = 0;
      segment_process = 1;
    } else {
      reloc_ptr++;
      state = 2;
      segment_process = 0;
    }
  }
  if (state == 1) {
    int budget = 0x400;
    for (;;) {
      int count = *reloc_ptr++;
      if (table_toggle) {
        for (int i = 0; i < count; i++)
          *loc_ptr++ += base_ptr;
      } else
        loc_ptr += count;
      if (count == 255) {
        if (!*reloc_ptr) {
          reloc_ptr++;
          table_toggle ^= 1;
        }
        continue;
      } else
        table_toggle ^= 1;
      if (!*reloc_ptr) {
        reloc_ptr++;
        state = 2;
        segment_process = 0;
        break;
      }
      if (!--budget) {
        if (read_clock() - start > 150000)
          return 0;
        budget = 0x400;
      }
    }
  }
  if (state == 2) {
    if (!*reloc_ptr) {
      state = 3;
      segment_process = 0;
    } else {
      for (;;) {
        unsigned int object;
        int relocation = *reloc_ptr++;
        if (relocation & 128) {
          int methods = relocation & 127;
          if (!methods)
            methods = 1;
          object = intern_type_from_c((char*)reloc_ptr, methods);
        } else {
          if (relocation > 9)
            reloc_ptr--;
          object = (unsigned int)intern_from_c((char*)reloc_ptr);
        }
        reloc_ptr += strlen((char*)reloc_ptr) + 1;
        reloc_ptr = symlink2(object_data, object, reloc_ptr);
        if (!*reloc_ptr) {
          state = 3;
          segment_process = 0;
          break;
        }
        if (read_clock() - start > 150000)
          return 0;
      }
    }
  }
  entry = object_data + 4;
  return 1;
}

// Finish linking by executing the top-level function or running the relocate
// method.
void link_control::finish() {
  DECLARE_SYMBOL_TABLE();
  CacheFlush(code_start, code_size);
  unsigned int old_debug = DebugSegment;
  if (keep_debug)
    DebugSegment = FIX_SYM(FIX_SYM_TRUE);
  if (flags & LINK_FLAG_FORCE_FAST_LINK)
    FastLink = 1;
  *EnableMethodSet = *EnableMethodSet + keep_debug;
  if (link_block->version == 3) {
    link_block_v3* block = link_block;
    if (entry && OBJECT_TYPE(entry) == (type*)FIX_SYM_VALUE(FIX_SYM_FUNCTION_TYPE) &&
        (flags & (LINK_FLAG_EXECUTE | LINK_FLAG_OUTPUT_LOAD))) {
      if (flags & LINK_FLAG_EXECUTE) {
        // preserve C's t9 register here, (could this be different inline asm
        // for GOAL_CALL?)
        unsigned long long saved_t9;
        asm("or %0,$25,$0" : "=r"(saved_t9));
        GOAL_CALL(entry);
        asm("or $25,%0,$0" : : "r"(saved_t9));
      }
    }
    if (flags & LINK_FLAG_OUTPUT_LOAD)
      output_segment_load(block->name, block, flags);
  } else {
    if (flags & LINK_FLAG_EXECUTE)
      call_method_of_type_arg2((unsigned int)entry, OBJECT_TYPE(entry), 7, (unsigned int)heap,
                               (unsigned int)basename(object_name));
  }
  *EnableMethodSet = *EnableMethodSet - keep_debug;
  FastLink = 0;
  heap->top = heap_top;
  DebugSegment = old_debug;
  return;
}

// Link and execute a file now, blocking until it completes.
void* link_and_exec(void* data, char* name, int size, kheapinfo* heap, unsigned int flags) {
  link_control control;
  control.begin(data, name, size, heap, flags);
  for (;;) {
    if (control.work())
      break;
  }
  control.finish();
  return control.entry;
}

static link_control saved_link_control;

// Start async linking which can be resumed later. Returns 1 if done.
unsigned int link_begin(void* data, char* name, int size, kheapinfo* heap, unsigned int flags) {
  saved_link_control.begin(data, name, size, heap, flags);
  if (saved_link_control.work()) {
    saved_link_control.finish();
    return 1;
  }
  return 0;
}

// Resume linking started by link_begin. Return 1 if done.
unsigned int link_resume() {
  if (saved_link_control.work()) {
    saved_link_control.finish();
    return 1;
  }
  return 0;
}

// Fast memcpy. Once GOAL loads its ultimate-memcpy function, prefer this
// version for large and aligned copies.
void ultimate_memcpy(void* destination, void* source, unsigned int size) {
  static void (*gfunc)(void*, void*, unsigned int) __asm__("gfunc.774") = 0;
  if (((unsigned int)source & 15) || ((unsigned int)destination & 15) || (size & 15) ||
      size < 4096) {
    memcpy(destination, source, size);
    return;
  }
  if (!gfunc) {
    symbol* sym = find_symbol_from_c("ultimate-memcpy");
    if (sym->value)
      gfunc = (void (*)(void*, void*, unsigned int))sym->value;
    else {
      memcpy(destination, source, size);
      return;
    }
  }
  // yet another slightly different way to call the function :(
  // this might have been with the exact set of registers used by some version of
  // GOAL's ultimate-memcpy
  register void (*target)(void*, void*, unsigned int) asm("$25");
  asm("or %0,%1,$0"
      : "=r"(target)
      : "r"(gfunc)
      : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8");
  target(destination, source, size);
}

// Unused relocation function. (maybe inlined??)
unsigned char* c_rellink3(void* data, link_segment* segments, unsigned char* reloc) {
  unsigned int base = (unsigned int)data;
  unsigned int* location = (unsigned int*)data;
  int toggle = 0;
  location--;
  if (*reloc) {
    for (;;) {
      int count = *reloc++;
      if (toggle) {
        for (int i = 0; i < count; i++) {
          if (*location & 0xff000000) {
            int segment = (*location >> 8) & 15;
            int low_word = (*location >> 12) & 15;
            unsigned int address = (unsigned int)segments[segment].data + ((*location & 255) << 8) +
                                   (location[low_word] & 0xffff);
            if (!DebugSegment && segment == 1)
              address = 0;
            *location = (*location & 0xffff0000) | (address >> 16);
            location[low_word] = (location[low_word] & 0xffff0000) | (address & 0xffff);
          } else
            *location += base;
          location++;
        }
      } else
        location += count;
      if (count == 255) {
        if (!*reloc) {
          reloc++;
          toggle ^= 1;
        }
        goto next_relocation;
      } else
        toggle ^= 1;
    next_relocation:
      if (!*reloc)
        break;
    }
  }
  reloc++;
  return reloc;
}

// Patch symbol references.
unsigned char* c_symlink2(void* data, unsigned int object, unsigned char* reloc) {
  DECLARE_SYMBOL_TABLE();
  unsigned int* location = (unsigned int*)data;
  for (;;) {
    unsigned int distance = *reloc++;
    if (distance & 3) {
      distance |= *reloc++ << 8;
      if (distance & 2) {
        distance |= *reloc++ << 16;
        if (distance & 1)
          distance |= *reloc++ << 24;
      }
    }
    location += distance >> 2;
    unsigned int value = *location;
    if (value == 0xffffffff)
      *location = object;
    else {
      unsigned int offset = value + object - symbol_table;
      *location = (value & 0xffff0000) | (offset & 0xffff);
    }
    if (!*reloc)
      return reloc + 1;
  }
}

// Patch symbol references.
unsigned char* c_symlink3(void* data, unsigned int object, unsigned char* reloc) {
  DECLARE_SYMBOL_TABLE();
  unsigned int* location = (unsigned int*)data;
  for (;;) {
    for (;;) {
      int distance = *reloc++;
      location += distance;
      if (distance != 255)
        break;
    }
    unsigned int value = *location;
    if (value == 0xffffffff)
      *location = object;
    else {
      unsigned int offset = value + object - symbol_table;
      *location = (value & 0xffff0000) | (offset & 0xffff);
    }
    if (!*reloc)
      return reloc + 1;
  }
}

unsigned char* (*symlink2)(void*, unsigned int, unsigned char*) = c_symlink2;
unsigned char* (*symlink3)(void*, unsigned int, unsigned char*) = c_symlink3;

// Check if GOAL has loaded its optimized linking functions, if so switch to
// those.
static void update_goal_fns() {
  if (symlink2 == c_symlink2) {
    symbol* sym = find_symbol_from_c("symlink2");
    if (sym->value)
      symlink2 = (unsigned char* (*)(void*, unsigned int, unsigned char*))sym->value;
  }
  if (symlink3 == c_symlink3) {
    symbol* sym = find_symbol_from_c("symlink3");
    if (sym->value)
      symlink3 = (unsigned char* (*)(void*, unsigned int, unsigned char*))sym->value;
  }
}
