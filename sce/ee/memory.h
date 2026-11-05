#ifndef SCE_EE_MEMORY_H
#define SCE_EE_MEMORY_H

#define EE_UNCACHED_ADDR(address) ((void*)((unsigned)(address) | 0x20000000))

#endif
