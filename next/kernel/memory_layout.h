#ifndef NEXT_KERNEL_MEMORY_LAYOUT_H
#define NEXT_KERNEL_MEMORY_LAYOUT_H

extern char _stack[];
extern char _stack_size[];
#define EE_MEMORY_MASK 0x07ffffff
#define GLOBAL_HEAP_END 0x01ffc000
#define DEBUG_HEAP_START 0x05000000
#define SPLASH_IMAGE_ADDRESS 0x01000000

#endif
