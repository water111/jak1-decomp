#ifndef SCE_IOP_989SND_SRAM_H
#define SCE_IOP_989SND_SRAM_H

#include "sce/iop/types.h"

struct SRAMNode;

u_int snd_SRAMMallocRev(u_int size);
void snd_SRAMFree(u_int loc, u_int size);
u_int snd_SRAMMarkUsed(u_int loc, u_int size);
u_int snd_SRAMMalloc(u_int size);
int snd_SRAMGetFreeMem(void);
int snd_SRAMMaxFreeBlockSize(void);
void snd_SRAMInitMalloc(void);
void snd_SRAMDumpFreeBlocks(void);
void snd_SRAMRemoveNode(struct SRAMNode* node);
void snd_SRAMAddNode(struct SRAMNode* node);
struct SRAMNode* snd_SRAMSearch(struct SRAMNode* root, u_int size);

struct SRAMNode {
  u_int loc;
  u_int size;
  u_int in_use;
  struct SRAMNode* root;
  struct SRAMNode* smaller;
  struct SRAMNode* bigger;
};

#endif
