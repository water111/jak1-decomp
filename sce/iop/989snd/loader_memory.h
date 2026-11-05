#ifndef SCE_IOP_989SND_LOADER_MEMORY_H
#define SCE_IOP_989SND_LOADER_MEMORY_H
void snd_DumpIOPMemoryUsage(void);
void* snd_IOPMemAlloc(int size, int use, int* actual_size);
void snd_IOPMemFree(void* memory);
extern void* (*gAllocProc)(int size, int use, int* actual_size);
extern void (*gFreeProc)(void* memory);
void snd_RegisterIOPMemAllocator(void* (*allocate)(int, int, int*), void (*release)(void*));
#endif
