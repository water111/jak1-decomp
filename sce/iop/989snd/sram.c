#include "sce/iop/989snd/sram.h"

#include "sce/iop/989snd/error.h"
#include "sce/iop/stdio/stdio.h"

#define IS_ALIGNED(value, alignment) (((value) & ((alignment) - 1)) == 0)

enum {
  NUM_SRAM_NODE = 129,

  SRAM_BASE_ADDR = 0x5040,
  SRAM_MAX_FREE = 0x1FAFBF,
};

struct SRAMNode* gSRAMFreeListHead = 0;
int gSRAMFreeNodesInUse = 0;
extern struct SRAMNode gSRAMFreeNodeStorage[NUM_SRAM_NODE];

#if SND989_VERSION == 0x020604
static const char sSRAMMallocError[] = "snd_SRAMMalloc: Couldn't allocate %d bytes of sound ram.\n";
#endif

void snd_SRAMInitMalloc() {
  int i;
  gSRAMFreeListHead = 0;
  for (i = 0; i < NUM_SRAM_NODE; i++) {
    gSRAMFreeNodeStorage[i].in_use = 0;
  }
  snd_SRAMFree(SRAM_BASE_ADDR, SRAM_MAX_FREE);
}

u_int snd_SRAMMalloc(u_int size) {
  struct SRAMNode* p = gSRAMFreeListHead;
  struct SRAMNode* candidate;
  u_int ret_loc;

  if (!IS_ALIGNED(size, 64)) {
    size += 64 - (size & 63);
  }

  candidate = snd_SRAMSearch(p, size);
  if (candidate == 0) {
#if SND989_VERSION == 0x020604
    printf(sSRAMMallocError, size);
#else
    snd_ShowError(32, size, 0, 0, 0);
#endif
    return 0;
  }

  while (1) {
    p = snd_SRAMSearch(candidate->smaller, size);
    if (p == 0) {
      break;
    }
    candidate = p;
  }

  ret_loc = candidate->loc;
  snd_SRAMRemoveNode(candidate);
  candidate->size -= size;
  if (candidate->size != 0) {
    candidate->loc += size;
    snd_SRAMAddNode(candidate);
  }

  return ret_loc;
}

u_int snd_SRAMMallocRev(u_int size) {
  int i;
  struct SRAMNode* candidate = 0;
  struct SRAMNode* storage = gSRAMFreeNodeStorage;
  u_int loc;
  u_int end;
#if SND989_VERSION == 0x020604
  u_int ret_loc;
#endif

  if (!IS_ALIGNED(size, 64)) {
    size += 64 - (size & 63);
  }

  for (i = 0; i < NUM_SRAM_NODE; i++) {
    storage = &gSRAMFreeNodeStorage[i];
    if (storage->in_use == 0) {
      continue;
    }

    if (storage->size < size) {
      continue;
    }

    loc = storage->loc;
    end = storage->loc + storage->size;
    if (
#if SND989_VERSION == 0x020608
        loc < (end - (end & 0x1ffff)) &&
#endif
        (end - (end & 0x1ffff) - loc) >= size) {
      if (candidate == 0) {
        candidate = storage;
        continue;
      }
      if (storage->size < candidate->size) {
        candidate = storage;
      }
    }
  }

#if SND989_VERSION == 0x020604
  if (candidate) {
    end = candidate->loc + candidate->size;
    ret_loc = snd_SRAMMarkUsed(end - (end & 0x1ffff) - size, size);
  } else {
    ret_loc = 0;
  }
  return ret_loc;
#else
  if (candidate == 0) {
    if (snd_SRAMGetFreeMem() >= size) {
      snd_ShowError(88, 0, 0, 0, 0);
    }
    return 0;
  }
  end = candidate->loc + candidate->size;
  return snd_SRAMMarkUsed(end - (end & 0x1ffff) - size, size);
#endif
}

void snd_SRAMFree(u_int loc, u_int size) {
  int i;
  int free_node = NUM_SRAM_NODE;
  struct SRAMNode* storage;
  struct SRAMNode* base = gSRAMFreeNodeStorage;

  if (!IS_ALIGNED(size, 64)) {
    size += 64 - (size & 63);
  }

  for (i = 0; i < NUM_SRAM_NODE; i++) {
    storage = &base[i];
    if (storage->in_use) {
      if (storage->loc + storage->size == loc) {
        snd_SRAMRemoveNode(storage);
        size += storage->size;
        loc = storage->loc;
        if (i < free_node)
          free_node = i;
      } else if (storage->loc == loc + size) {
        snd_SRAMRemoveNode(storage);
        size += storage->size;
        if (i < free_node)
          free_node = i;
      } else {
        continue;
      }
    } else if (i < free_node) {
      free_node = i;
    }
  }

  if (free_node < NUM_SRAM_NODE) {
    storage = (struct SRAMNode*)(free_node * sizeof(struct SRAMNode) + (u_int)base);
    storage->size = size;
    storage->loc = loc;
    storage->root = 0;
    storage->smaller = 0;
    storage->bigger = 0;
    snd_SRAMAddNode(storage);
  } else {
#if SND989_VERSION == 0x020604
    printf("snd_SRAMFree: Lost free sound RAM!!!! No FREELIST nodes available!\n");
#else
    snd_ShowError(33, 0, 0, 0, 0);
#endif
  }
}

#if SND989_VERSION == 0x020604
inline
#endif
    u_int
    snd_SRAMMarkUsed(u_int loc, u_int size) {
  int i;
  int nn_size = 0;
  int nn_loc = 0;
  int free_node = NUM_SRAM_NODE;
  struct SRAMNode* storage;
  struct SRAMNode* new_node;
  struct SRAMNode* base = gSRAMFreeNodeStorage;

  if (!IS_ALIGNED(size, 64)) {
    size += 64 - (size & 63);
  }

  for (i = 0; i < NUM_SRAM_NODE; i++) {
    storage = &base[i];
    if (storage->in_use) {
      if (loc >= storage->loc && loc < storage->loc + storage->size) {
        snd_SRAMRemoveNode(storage);
        if (loc == storage->loc) {
          storage->loc = loc + size;
          storage->size -= size;
          if (storage->size != 0) {
            snd_SRAMAddNode(storage);
          }
          goto allocated;
        }

        if (loc + size == storage->loc + storage->size) {
          storage->size -= size;
          if (storage->size != 0) {
            snd_SRAMAddNode(storage);
          }
          goto allocated;
        }

        nn_size = storage->size;
        storage->size = loc - storage->loc;
        snd_SRAMAddNode(storage);
        nn_size -= storage->size;
        nn_size -= size;
        nn_loc = loc + size;
      }
    } else if (i < free_node) {
      free_node = i;
    }
  }

  if (nn_size != 0 && free_node < NUM_SRAM_NODE) {
    if (nn_size < 0) {
#if SND989_VERSION == 0x020604
      printf("snd_SRAMMarkUsed: Possible sound ram curruption!!!\n");
#else
      snd_ShowError(34, 0, 0, 0, 0);
#endif
      goto allocated;
    }
    if (nn_size > 0) {
      new_node = (struct SRAMNode*)(free_node * sizeof(struct SRAMNode) + (u_int)base);
      new_node->size = nn_size;
      new_node->loc = nn_loc;
      new_node->root = 0;
      new_node->smaller = 0;
      new_node->bigger = 0;
      snd_SRAMAddNode(new_node);
    }
  allocated:
    return loc;
  }
#if SND989_VERSION == 0x020604
  printf("snd_SRAMMarkUsed: requested RAM is in the middle of an allocated block!\n");
#else
  snd_ShowError(35, 0, 0, 0, 0);
#endif
  return 0;
}

void snd_SRAMRemoveNode(struct SRAMNode* node) {
  if (node == 0) {
    return;
  }

  if (node == gSRAMFreeListHead) {
    gSRAMFreeListHead = node->bigger;
  } else {
    if (node->root->smaller == node) {
      node->root->smaller = node->bigger;
    } else {
      node->root->bigger = node->bigger;
    }
  }

  if (node->bigger != 0) {
    node->bigger->root = node->root;
  }
  snd_SRAMAddNode(node->smaller);
  node->in_use = 0;
  node->smaller = 0;
  node->bigger = 0;
  gSRAMFreeNodesInUse--;
}

void snd_SRAMAddNode(struct SRAMNode* node) {
  struct SRAMNode* work;
  if (node == 0) {
    return;
  }

  node->in_use = 1;
  gSRAMFreeNodesInUse++;
  work = gSRAMFreeListHead;
  if (work == 0) {
    gSRAMFreeListHead = node;
    node->root = 0;

    return;
  }

  while (1) {
    if (node->size < work->size) {
      if (work->smaller == 0) {
        work->smaller = node;
        node->root = work;
        return;
      }
      work = work->smaller;
    } else {
      if (work->bigger == 0) {
        work->bigger = node;
        node->root = work;
        return;
      }
      work = work->bigger;
    }
  }
}

struct SRAMNode* snd_SRAMSearch(struct SRAMNode* root, u_int size) {
  if (root == 0) {
    return 0;
  }
  if (root->size < size) {
    return snd_SRAMSearch(root->bigger, size);
  }

  return root;
}

int snd_SRAMGetFreeMem() {
  int i;
  struct SRAMNode* storage = gSRAMFreeNodeStorage;
  int free_size = 0;
  for (i = 0; i < NUM_SRAM_NODE; i++) {
    storage = &gSRAMFreeNodeStorage[i];
    if (storage->in_use) {
      free_size += (int)storage->size;
    }
  }

  return free_size;
}

int snd_SRAMMaxFreeBlockSize() {
  int i;
  struct SRAMNode* storage = gSRAMFreeNodeStorage;
  int largest = 0;
  for (i = 0; i < NUM_SRAM_NODE; i++) {
    storage = &gSRAMFreeNodeStorage[i];
    if (storage->in_use != 0) {
      if (largest < storage->size) {
        largest = (int)storage->size;
      }
    }
  }

  return largest;
}

void snd_SRAMDumpFreeBlocks() {
  int i;
  struct SRAMNode* storage = gSRAMFreeNodeStorage;

  for (i = 0; i < NUM_SRAM_NODE; i++) {
    storage = &gSRAMFreeNodeStorage[i];
    if (storage->in_use != 0) {
#if SND989_VERSION == 0x020604
      printf("Node #%d (0x%x): Block at -> %u (0x%x) size -> %d s -> 0x%x b -> 0x%x\n", i, storage,
             storage->loc, storage->loc, storage->size, storage->smaller, storage->bigger);
#else
      printf("\nFree SPU Block at -> %u (0x%x) size -> %d\n\n", storage->loc, storage->loc,
             storage->size);
#endif
    }
  }
}
