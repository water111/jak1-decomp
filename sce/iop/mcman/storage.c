#include "sce/iop/mcman/block.h"
#include "sce/iop/mcman/buffers.h"
#include "sce/iop/mcman/cache.h"
#include "sce/iop/mcman/directory.h"
#include "sce/iop/mcman/filesystem.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/transport.h"
#include "sce/iop/sysclib/string.h"

int mcman_cache_count = 36;

#define MCMAN_CACHE_ENTRIES 36
#define MCMAN_CLUSTER_SIZE 1024
extern u_char mcman_cache_buffer[MCMAN_CACHE_ENTRIES * MCMAN_CLUSTER_SIZE];
extern struct McCacheEntry mcman_entry_storage[MCMAN_CACHE_ENTRIES];
extern struct McCacheEntry* mcman_order_storage[MCMAN_CACHE_ENTRIES];

void mcman_initcache(void) {
  int i, last, empty;
  u_char* data;
  struct McCacheEntry* entries;
  i = 0;
  empty = -1;
  last = MCMAN_CACHE_ENTRIES - 1;
  entries = mcman_entry_storage;
  data = mcman_cache_buffer;
  for (; i < MCMAN_CACHE_ENTRIES; i++) {
    mcman_entry_storage[i].data = data;
    mcman_entry_storage[i].cluster = empty;
    mcman_order_storage[i] =
        (struct McCacheEntry*)((last - i) * sizeof(struct McCacheEntry) + (u_int)entries);
    data += MCMAN_CLUSTER_SIZE;
  }
  mcman_cache_entries = mcman_entry_storage;
  mcman_cache_order = mcman_order_storage;
  for (i = 0; i < MCMAN_SLOTS; i++) {
    mcman_devices[0][i].unknown[3] = -1;
    mcman_devices[0][i].unknown[4] = -1;
    mcman_devices[0][i].unknown[5] = -1;
    mcman_devices[0][i].free_clusters = -1;
    mcman_devices[1][i].unknown[3] = -1;
    mcman_devices[1][i].unknown[4] = -1;
    mcman_devices[1][i].unknown[5] = -1;
    mcman_devices[1][i].free_clusters = -1;
  }
  memset(mcman_fat_cache, -1, sizeof(mcman_fat_cache));
  for (i = 0; i < MCMAN_SLOTS; i++) {
    mcman_fat_cache[0][i].entries[0] = 0;
    mcman_fat_cache[1][i].entries[0] = 0;
  }
}

int McInit(int value) {
  return 0;
}

int mcman_clearcache(int port, int slot) {
  int i, j;
  struct McCacheEntry* entry;
  for (i = mcman_cache_count - 1; i >= 0; i--) {
    entry = mcman_cache_order[i];
    if (entry->port == port && entry->slot == slot && entry->cluster >= 0) {
      entry->port = -1;
      entry->slot = -1;
      entry->dirty = 0;
      entry->cluster = -1;
    }
  }
  for (i = 0; i < mcman_cache_count - 1; i++) {
    entry = mcman_cache_order[i];
    if (entry->cluster < 0) {
      for (j = i + 1; j < mcman_cache_count; j++) {
        if (mcman_cache_order[j]->cluster >= 0)
          break;
      }
      if (j == mcman_cache_count)
        break;
      mcman_cache_order[i] = mcman_cache_order[j];
      mcman_cache_order[j] = entry;
    }
  }
  memset(&mcman_fat_cache[port][slot], -1, sizeof(struct McFatCache));
  mcman_fat_cache[port][slot].entries[0] = 0;
  return 0;
}

struct McCacheEntry* mcman_getcacheentry(int port, int slot, int cluster) {
  int i;
  struct McCacheEntry* entry;
  for (i = 0; i < mcman_cache_count; i++) {
    entry = &mcman_cache_entries[i];
    if (entry->port == port && entry->slot == slot && entry->cluster == cluster)
      return entry;
  }
  return 0;
}

void mcman_freecluster(int port, int slot, int cluster) {
  int i;
  for (i = 0; i < mcman_cache_count; i++) {
    struct McCacheEntry* entry =
        (struct McCacheEntry*)(i * sizeof(struct McCacheEntry) + (u_int)mcman_cache_entries);
    if (entry->port == port && entry->slot == slot && entry->cluster == cluster) {
      entry->cluster = -1;
      entry->dirty = 0;
    }
  }
}

int mcman_getFATindex(int port, int slot, int index) {
  return mcman_fat_cache[port][slot].entries[index];
}

void Mc1stCacheEntSetWrFlagOff(void) {
  mcman_cache_order[0]->dirty = -1;
}

struct McCacheEntry* mcman_get1stcacheEntp(void) {
  return mcman_cache_order[0];
}

void mcman_addcacheentry(struct McCacheEntry* entry) {
  int i = mcman_cache_count - 1;
  while (i >= 0) {
    if (mcman_cache_order[i] == entry)
      break;
    i--;
  }
  if (i != 0) {
    do {
      mcman_cache_order[i] = mcman_cache_order[i - 1];
    } while (--i != 0);
  }
  mcman_cache_order[0] = entry;
}

int McFlushCache(int port, int slot) {
  int i;
  for (i = mcman_cache_count - 1; i >= 0; i--) {
    struct McCacheEntry* entry = mcman_cache_order[i];
    if (entry->port == port && entry->slot == slot && entry->dirty) {
      int result = mcman_flushcacheentry(entry);
      if (result)
        return result;
    }
  }
  return 0;
}

extern u_char mcman_backupbuf[16384];
extern int mcman_badblock;

// highly suspicious function
static inline int mcman_count_above(int count, int limit) {
  return count > limit;
}

int mcman_flushcacheentry(struct McCacheEntry* mce) {
  int r, i, j;
  int temp1, temp2, offset, pageindex;
  int block;
  int clusters_per_block, pages_per_block, card_type, page_size, sparesize, flag, cluster;
  int pages_per_cluster;
  struct McCacheEntry* pmce[16];
  McDeviceInfo* mcdi;
  struct McCacheEntry* mcee;
  u_char eccbuf[32];
  void *p_page, *p_ecc;

  if (mce->dirty == 0)
    return 0;

  mcdi = (McDeviceInfo*)&mcman_devices[mce->port][mce->slot];

  page_size = mcdi->page_size;
  card_type = mcdi->card_type;

  if (card_type == 0) {
  clean:
    mce->dirty = 0;
    return 0;
  }
  if (card_type == 1 || card_type == 3) {
    j = mce->cluster;
    i = 0;
    if ((temp2 = 1024 / page_size) > 0) {
    retry_ps1: {
      if ((mce->dirty >> i) & 1) {
        r = McWritePS1PDACard(mce->port, mce->slot, j, mce->data + i * page_size);
        if (r == -8)
          goto ps1_pages_done;
        if (r)
          return -50;
      }
      j++;
    }
      if (++i < (temp2 = 1024 / page_size))
        goto retry_ps1;
    }
  ps1_pages_done:
    if (i == 1024 / page_size) {
      r = mcman_probePS1Card2(mce->port, mce->slot);
      if (r == -14)
        r = -8;
    }
    if (card_type != 1) {
      if (r == -8)
        return r;
      goto clean;
    }
    if (r != -8)
      goto clean;
    if (!(mce->dirty & ~((u_int)-1 << i)))
      goto clean;
    if (!i)
      goto clean;
    do {
      --i;
      --j;
      if ((mce->dirty >> i) & 1) {
        r = mcman_fillPS1backuparea(mce->port, mce->slot, j);
        if (r)
          return -8;
        goto retry_ps1;
      }
    } while (i);
    goto clean;
  }

  clusters_per_block = mcdi->clusters_per_block;
  block = mce->cluster / mcdi->clusters_per_block;
  pages_per_cluster = mcdi->pages_per_cluster;
  pages_per_block = mcdi->pages_per_block;
  sparesize = mcman_spare_size(mce->port, mce->slot);
  i = 0;
  memset((void*)pmce, 0, 64);
  flag = 0;
  if (mcman_cache_count > 0) {
    do {
      mcee = &mcman_cache_entries[i];
      if ((*(u_int*)&mcee->slot & 0xff00ffff) == (*(u_int*)&mce->slot & 0xff00ffff)) {
        temp1 = mcee->cluster / clusters_per_block;
        temp2 = mcee->cluster % clusters_per_block;

        if (temp1 == block) {
          pmce[temp2] = (struct McCacheEntry*)mcee;
          if (mcee->read_flag == 0)
            flag = 1;
        }
      }
    } while (++i < mcman_cache_count);
  }

  i = 0;
  if (clusters_per_block > 0) {
    pageindex = 0;
    cluster = block * clusters_per_block;

    do {
      if (pmce[i] != 0) {
        int cached_index;

        j = 0;
        if (mcman_count_above(pages_per_cluster, 0)) {
          cached_index = pageindex;
          do {
            mcman_pagedata[cached_index + j] = pmce[i]->data + j * page_size;
          } while (++j < pages_per_cluster);
        }

      } else {
        int temp1;
        j = 0;
        if (mcman_count_above(pages_per_cluster, 0)) {
          temp1 = pageindex;
          do {
            offset = (temp1 + j) * page_size;
            mcman_pagedata[temp1 + j] = mcman_backupbuf + offset;
            r = McReadPage(mce->port, mce->slot, (cluster + i) * pages_per_cluster + j,
                           mcman_backupbuf + offset);
            if (r)
              return -51;
          } while (++j < pages_per_cluster);
        }
      }

      pageindex += pages_per_cluster;
    } while (++i < clusters_per_block);
  }

lbl1:
  if ((flag != 0) && (mcman_badblock <= 0)) {
    r = mcman_eraseblock(mce->port, mce->slot, mcdi->backup_block1, (void**)mcman_pagedata,
                         mcman_eccdata);
    if (r == (-8)) {
    lbl2:
      r = mcman_replaceBackupBlock(mce->port, mce->slot, mcdi->backup_block1);
      mcdi->backup_block1 = r;
      goto lbl1;
    }
    if (r != 0)
      return -52;

    mcman_pagebuf.word[0] = block | 0x80000000;
    i = 0;
    p_ecc = (void*)eccbuf;
    p_page = (void*)&mcman_pagebuf;

    for (; i < page_size / 128; i++) {
      McDataChecksum(p_page, p_ecc);
      p_ecc = (u_char*)p_ecc + 3;
      p_page = (u_char*)p_page + 128;
    }

    r = McWritePage(mce->port, mce->slot, mcdi->backup_block2 * pages_per_block, &mcman_pagebuf,
                    eccbuf);
    if (r == (-8))
      goto lbl3;
    if (r != 0)
      return -53;

    j = 0;
    if (r < mcdi->pages_per_block) {
      do {
        r = McWritePage(mce->port, mce->slot, (mcdi->backup_block1 * pages_per_block) + j,
                        mcman_pagedata[j], (u_char*)mcman_eccdata + j * sparesize);
        if (r == (-8))
          goto lbl2;
        if (r != 0)
          return -54;
      } while (++j < mcdi->pages_per_block);
    }

    r = McWritePage(mce->port, mce->slot, (mcdi->backup_block2 * pages_per_block) + 1,
                    &mcman_pagebuf, eccbuf);
    if (r == (-8))
      goto lbl3;
    if (r != 0)
      return -55;
  }

  r = mcman_eraseblock(mce->port, mce->slot, block, (void**)mcman_pagedata, mcman_eccdata);

  if (r == (-8)) {
    r = mcman_fillbackupblock1(mce->port, mce->slot, block, (void**)mcman_pagedata, mcman_eccdata);
    for (i = 0; i < clusters_per_block; i++) {
      if (pmce[i] != 0)
        pmce[i]->dirty = 0;
    }
    if (r == (-8))
      return r;
    return -58;
  }
  if (r != 0)
    return -56;

  if (r < mcdi->pages_per_block) {
    j = 0;

    do {
      if (pmce[j / pages_per_cluster] == 0) {
        r = McWritePage(mce->port, mce->slot, (block * pages_per_block) + j, mcman_pagedata[j],
                        (u_char*)mcman_eccdata + j * sparesize);
        if (r == (-8)) {
          r = mcman_fillbackupblock1(mce->port, mce->slot, block, (void**)mcman_pagedata,
                                     mcman_eccdata);
          for (i = 0; i < clusters_per_block; i++) {
            if (pmce[i] != 0)
              pmce[i]->dirty = 0;
          }
          if (r == (-8))
            return r;
          return -58;
        }
        if (r != 0)
          return -57;
      }
    } while (++j < mcdi->pages_per_block);
  }

  j = 0;
  if (mcdi->pages_per_block > 0) {
    do {
      if (pmce[j / pages_per_cluster] != 0) {
        r = McWritePage(mce->port, mce->slot, (block * pages_per_block) + j, mcman_pagedata[j],
                        (u_char*)mcman_eccdata + j * sparesize);
        if (r == (-8)) {
          r = mcman_fillbackupblock1(mce->port, mce->slot, block, (void**)mcman_pagedata,
                                     mcman_eccdata);
          for (i = 0; i < clusters_per_block; i++) {
            if (pmce[i] != 0)
              pmce[i]->dirty = 0;
          }
          if (r == (-8))
            return r;
          return -58;
        }
        if (r != 0)
          return -57;
      }
    } while (++j < mcdi->pages_per_block);
  }

  i = 0;
  if (clusters_per_block > 0) {
    do {
      if (pmce[i] != 0)
        pmce[i]->dirty = 0;
    } while (++i < clusters_per_block);
  }

  if ((flag != 0) && (mcman_badblock <= 0)) {
    r = mcman_eraseblock(mce->port, mce->slot, mcdi->backup_block2, 0, 0);
    if (r == (-8)) {
      goto lbl3;
    }
    if (r != -8)
      goto final_result;
  } else
    return 0;

lbl3:
  r = mcman_replaceBackupBlock(mce->port, mce->slot, mcdi->backup_block2);
  mcdi->backup_block2 = r;
  goto lbl1;

final_result:
  if (r)
    return -57;
  return 0;
}

int McReadCluster(int port, int slot, int cluster, struct McCacheEntry** output) {
  int i, result, count;
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry* cache;
  if (mcman_badblock_state.block > 0) {
    int block = cluster / device->clusters_per_block;
    int offset = cluster % device->clusters_per_block;
    if (block == mcman_badblock_state.block && mcman_badblock_state.port == port &&
        mcman_badblock_state.slot == slot)
      cluster = device->backup_block1 * device->clusters_per_block + offset;
    else if (mcman_badblock_state.block > 0) {
      int candidate;
      for (candidate = 0; candidate < device->clusters_per_block; candidate++) {
        if (mcman_replacement_clusters[candidate] &&
            mcman_replacement_clusters[candidate] == cluster) {
          int replacement_offset = candidate % device->clusters_per_block;
          cluster = device->backup_block1 * device->clusters_per_block + replacement_offset;
        }
      }
    }
  }
  cache = mcman_getcacheentry(port, slot, cluster);
  if (!cache) {
    cache = mcman_cache_order[mcman_cache_count - 1];
    if (cache->dirty) {
      result = mcman_flushcacheentry(cache);
      if (result)
        return result;
    }
    cache->port = port;
    cache->slot = slot;
    cache->cluster = cluster;
    cache->read_flag = 0;
    count = device->pages_per_cluster;
    for (i = 0; i < count; i++) {
      result = McReadPage(port, slot, cluster * count + i, cache->data + i * device->page_size);
      if (result)
        return -59;
    }
  }
  mcman_addcacheentry(cache);
  *output = cache;
  return 0;
}

int McReadDirEntry(int port, int slot, int cluster, int entry_index, struct McFsEntry** output) {
  int i, limit, index, current;
  McDeviceInfo* device = &mcman_devices[port][slot];
  u_int entries = (u_int)device->cluster_size >> 9;
  struct McCacheEntry* cache;
  limit = 0x402 / entries;
  index = (u_int)entry_index / entries;
  current = cluster;
  i = 0;
  if (cluster == 0 && index != 0) {
    if (index < limit && mcman_fat_cache[port][slot].entries[index] >= 0) {
      current = mcman_fat_cache[port][slot].entries[index];
      i = index;
    } else {
      current = cluster;
      for (i = 0; i < index; i++) {
        if (i >= limit || mcman_fat_cache[port][slot].entries[i] < 0)
          break;
        current = mcman_fat_cache[port][slot].entries[i];
      }
      i--;
    }
  }
  for (; i < index;) {
    if (McGetFATentry(port, slot, current, &current))
      return -70;
    if ((u_int)current == 0xffffffff)
      return -4;
    current &= 0x7fffffff;
    i++;
    if (cluster == 0 && i < limit)
      mcman_fat_cache[port][slot].entries[i] = current;
  }
  if (McReadCluster(port, slot, current + mcman_devices[port][slot].allocation_offset, &cache))
    return -71;
  *output =
      (struct McFsEntry*)(cache->data + (((u_int)entry_index %
                                          ((u_int)mcman_devices[port][slot].cluster_size >> 9))
                                         << 9));
  return 0;
}

int mcman_readdirentryPS1(int port, int slot, int cluster, struct McPs1Entry** output) {
  int count, index, offset;
  struct McCacheEntry* cache;
  if ((u_int)cluster >= 15)
    return -73;
  count = 1024 / mcman_devices[port][slot].page_size;
  index = (cluster + 1) / count;
  offset = (cluster + 1) % count;
  if (mcman_readclusterPS1(port, slot, index * count, &cache))
    return -74;
  *output = (struct McPs1Entry*)(cache->data + (offset << 7));
  (*output)->transient = 0;
  return 0;
}

int McSetFATentry(int port, int slot, int index, int value) {
  int indirect_index, indirect_offset, fat_offset, result, cluster;
  int entries = mcman_devices[port][slot].fat_entries_per_cluster;
  struct McCacheEntry* cache;
  indirect_index = index / entries;
  fat_offset = index % entries;
  result = indirect_index / entries;
  indirect_offset = indirect_index % entries;
  cluster = mcman_devices[port][slot].indirect_fat[result];
  if ((result = McReadCluster(port, slot, cluster, &cache)))
    return -75;
  if ((result = McReadCluster(port, slot, ((int*)cache->data)[indirect_offset], &cache)))
    return -76;
  ((int*)cache->data)[fat_offset] = value;
  cache->dirty = 1;
  return 0;
}

int McGetFATentry(int port, int slot, int index, int* value) {
  int indirect_index, indirect_offset, fat_offset, result, cluster;
  int entries = mcman_devices[port][slot].fat_entries_per_cluster;
  struct McCacheEntry* cache;
  indirect_index = index / entries;
  fat_offset = index % entries;
  result = indirect_index / entries;
  indirect_offset = indirect_index % entries;
  cluster = mcman_devices[port][slot].indirect_fat[result];
  if ((result = McReadCluster(port, slot, cluster, &cache)))
    return -78;
  if ((result = McReadCluster(port, slot, ((int*)cache->data)[indirect_offset], &cache)))
    return -79;
  *value = ((int*)cache->data)[fat_offset];
  return 0;
}

int mcman_readclusterPS1(int port, int slot, int cluster, struct McCacheEntry** output) {
  struct McCacheEntry* cache = mcman_getcacheentry(port, slot, cluster);
  if (!cache) {
    int i, result, count;
    cache = mcman_cache_order[mcman_cache_count - 1];
    if (cache->dirty) {
      result = mcman_flushcacheentry(cache);
      if (result)
        return result;
    }
    cache->port = port;
    cache->slot = slot;
    cache->cluster = cluster;
    count = 1024 / mcman_devices[port][slot].page_size;
    for (i = 0; i < count; i++) {
      result = McReadPS1PDACard(port, slot, cluster + i,
                                cache->data + i * mcman_devices[port][slot].page_size);
      if (result)
        return -59;
    }
  }
  mcman_addcacheentry(cache);
  *output = cache;
  return 0;
}

int mcman_replaceBackupBlock(int port, int slot, int block) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int index;
  if (mcman_badblock_state.block > 0)
    return -8;
  for (index = 0; index < 16; index++) {
    if (device->bad_blocks[index] == -1)
      break;
  }
  if (index >= 16 || (int)(device->allocation_end - device->unknown[2]) < 8)
    return -3;
  {
    device->allocation_end -= 8;
    device->bad_blocks[index] = block;
    mcman_badblock_state.port = port;
    mcman_badblock_state.slot = slot;
    mcman_badblock_state.block = -1;
    return (int)(device->allocation_end + device->allocation_offset) / device->clusters_per_block;
  }
}

int mcman_fillbackupblock1(int port, int slot, int block, void** pages, void* ecc) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int page_count = device->pages_per_block;
  int spare_size = mcman_spare_size(port, slot);
  int result, index, backup, page, offset;
  void** page_data;
  u_char* checksum;
  if (mcman_badblock_state.block != 0) {
    if (mcman_badblock_state.block != block || mcman_badblock_state.port != port ||
        mcman_badblock_state.slot != slot)
      return -8;
  }
  if (block == (int)device->allocation_offset / device->clusters_per_block)
    return -8;
  result = mcman_eraseblock(port, slot, device->backup_block2, 0, 0);
  if (result != 0)
    return result;
  result = mcman_eraseblock(port, slot, device->backup_block1, 0, 0);
  if (result != 0)
    return result;
  for (index = 0; index < 16; index++) {
    if (device->bad_blocks[index] == -1)
      break;
  }
  if (index >= 16)
    return -8;
  backup = device->backup_block1 * page_count;
  page = 0;
  if (page < page_count) {
    page_data = pages;
    checksum = ecc;
    for (; page < page_count; page++) {
      result = McWritePage(port, slot, backup + page, *page_data, checksum);
      if (result != 0)
        return result;
      page_data++;
      checksum += spare_size;
    }
  }
  mcman_badblock_state.port = port;
  mcman_badblock_state.slot = slot;
  mcman_badblock_state.block = block;
  for (index = 15, offset = 60; index >= 0; index--, offset -= 4)
    *(int*)((u_char*)mcman_replacement_clusters + offset) = 0;
  return 0;
}

static inline int mcman_entry_below(int entry, int count) {
  return entry < count;
}

static inline int mcman_same_block(int a, int b) {
  return a == b;
}

int McReplaceBadBlock(void) {
  int port = mcman_badblock_state.port, slot = mcman_badblock_state.slot;
  McDeviceInfo* mcdi = &mcman_devices[port][slot];
  struct McFsEntry** directory_entry;
  int length;
  int root_parent_entry;
  int first_cluster;
  int value2;
  int block_clusters, allocation_offset;
  int r, i, curentry, clust, index, offset, numifc, fat_length, temp;
  int value, cluster, index2, offset2, directory_cluster;

  int fat_entry[16];
  struct McCacheEntry* mce;
  int fat_entry2;
  struct McFsEntry* fse;

  if (mcman_badblock_state.block == 0)
    return 0;

  if (mcman_badblock_state.block < 0) {
    mcman_badblock_state.block = 0;
    goto write_super;
  }
  {
    McFlushCache(port, slot);
    mcman_clearcache(port, slot);

    for (i = 0; i < 16; i++) {
      if (mcdi->bad_blocks[i] == -1)
        break;
    }
    if (i >= 16)
      goto fail;

    if ((int)mcdi->allocation_end >= (int)mcdi->unknown[2] + 8) {
      mcdi->unknown[2] += 8;
      mcdi->bad_blocks[i] = mcman_badblock_state.block;
    }

    fat_length = (((mcdi->clusters_per_card << 2) - 1) / (u_int)mcdi->cluster_size) + 1;
    numifc = ((((u_int)fat_length << 2) - 1) / (u_int)mcdi->cluster_size) + 1;

    if (numifc > 32) {
      numifc = 32;
      fat_length = mcdi->fat_entries_per_cluster << 5;
    }

    block_clusters = mcdi->clusters_per_block;
    allocation_offset = mcdi->allocation_offset;
    first_cluster = mcman_badblock_state.block * block_clusters;

    fat_entry2 = 0;
    value = ~(((u_int)(-1)) << block_clusters);
    if (block_clusters > 0)
      do {
        clust = first_cluster + fat_entry2;
        if (clust < allocation_offset) {
          fat_entry[fat_entry2] = 0;
        } else {
          r = McGetFATentry(port, slot, clust - allocation_offset, &fat_entry[fat_entry2]);
          if (r != 0)
            goto fail;

          if ((fat_entry[fat_entry2] & 0x80000000) == 0)
            goto clearbit;
          if (fat_entry[fat_entry2] >= -1)
            goto keepbit;
          if (fat_entry[fat_entry2] < -9)
            goto keepbit;
        clearbit:
          value &= ~(1 << fat_entry2);
        keepbit:;
        }
      } while (++fat_entry2 < block_clusters);

    fat_entry2 = 0;
    if (block_clusters > 0) {
      do {
        if ((r = 1 << fat_entry2, value & r) != 0) {
          r = mcman_findfree2(port, slot, 1);
          mcman_replacement_clusters[fat_entry2] = r;
          if (r < 0)
            goto fail;
          r += allocation_offset;
          mcman_replacement_clusters[fat_entry2] = r;
        }
      } while (++fat_entry2 < block_clusters);
    }

    fat_entry2 = 0;
    if (block_clusters > 0) {
      do {
        int index, offset;
        if ((r = 1 << fat_entry2, value & r) != 0) {
          if (fat_entry[fat_entry2] != 0) {
            index = ((fat_entry[fat_entry2] & ~0x80000000) + allocation_offset) / block_clusters;
            offset = ((fat_entry[fat_entry2] & ~0x80000000) + allocation_offset) % block_clusters;
            if (index == mcman_badblock_state.block) {
              fat_entry[fat_entry2] =
                  (mcman_replacement_clusters[offset] - allocation_offset) | 0x80000000;
            }
          }
        }
      } while (++fat_entry2 < block_clusters);
    }

    fat_entry2 = 0;
    if (block_clusters > 0) {
      do {
        if ((mcman_replacement_clusters[fat_entry2] != 0) && (fat_entry[fat_entry2] != 0)) {
          r = McSetFATentry(port, slot, mcman_replacement_clusters[fat_entry2] - allocation_offset,
                            fat_entry[fat_entry2]);
          if (r != 0)
            goto fail;
        }
      } while (++fat_entry2 < block_clusters);
    }

    for (i = 0; i < numifc; i++) {
      int index, offset;
      index = (int)mcdi->indirect_fat[i] / block_clusters;
      offset = (int)mcdi->indirect_fat[i] % block_clusters;

      if (index == mcman_badblock_state.block) {
        value &= ~(1 << offset);
        mcdi->indirect_fat[i] = mcman_replacement_clusters[i];
      }
    }

    if (value == 0)
      goto retire_block;

    root_parent_entry = 1;
    value2 = value;
    for (i = 0; i < fat_length; i++) {
      int index, offset;
      {
        int index, offset;
        index = i / mcdi->fat_entries_per_cluster;
        offset = i % mcdi->fat_entries_per_cluster;

        if (offset == 0) {
          r = McReadCluster(port, slot, mcdi->indirect_fat[index], &mce);
          if (r != 0)
            goto fail;
        }
      }
      offset = i % mcdi->fat_entries_per_cluster;
      index2 = ((int*)mce->data)[offset] / block_clusters;
      offset2 = ((int*)mce->data)[offset] % block_clusters;

      if (index2 == mcman_badblock_state.block) {
        value &= ~(1 << offset2);
        ((int*)mce->data)[offset] = mcman_replacement_clusters[offset2];
        mce->dirty = 1;
      }
    }

    if (value != value2) {
      McFlushCache(port, slot);
      mcman_clearcache(port, slot);
    }

    if (value == 0)
      goto retire_block;

    value2 = value;

    for (i = 0; i < (int)mcdi->allocation_end; i++) {
      r = McGetFATentry(port, slot, i, &fat_entry2);
      if (r != 0)
        goto fail;

      index = (u_int)(((fat_entry2 & ~0x80000000) + allocation_offset) / block_clusters);
      offset = (u_int)(((fat_entry2 & ~0x80000000) + allocation_offset) % block_clusters);

      if (index == mcman_badblock_state.block) {
        value &= ~(1 << offset);
        r = McSetFATentry(port, slot, i,
                          (mcman_replacement_clusters[offset] - allocation_offset) | 0x80000000);
        if (r != 0)
          goto fail;
      }
    }

    if (value != value2)
      McFlushCache(port, slot);
    if (value == 0)
      goto retire_block;

    directory_cluster = 0;
    r = McReadDirEntry(port, slot, directory_cluster, directory_cluster, &fse);
    if (r != 0)
      goto fail;

    curentry = 2;
    directory_entry = &fse;
    length = fse->length;

  scan_directory:
    while (mcman_entry_below(curentry, length)) {
      r = McReadDirEntry(port, slot, directory_cluster, curentry, directory_entry);
      if (r != 0)
        goto fail;

      cluster = fse->cluster;
      {
        int index, offset;
        index = ((int)fse->cluster + allocation_offset) / block_clusters;
        offset = ((int)fse->cluster + allocation_offset) % block_clusters;

        temp = fse->length;

        if (index == mcman_badblock_state.block) {
          fse->cluster = mcman_replacement_clusters[offset] - allocation_offset;
          Mc1stCacheEntSetWrFlagOff();
        }
      }

      if ((fse->mode & 32) == 0) {
        curentry++;
        continue;
      }

      r = McReadDirEntry(port, slot, cluster, 0, directory_entry);
      if (r != 0)
        goto fail;

      if ((fse->mode & 32) == 0) {
        curentry++;
        continue;
      }

      if (fse->cluster != (u_int)directory_cluster) {
        curentry++;
        continue;
      }

      if ((int)(fse->parent_entry) != curentry) {
        curentry++;
        continue;
      }

      if (strcmp(fse->name, "."))
        goto next_entry;

      directory_cluster = cluster;
      curentry = 2;
      length = temp;
      continue;
    next_entry:
      curentry++;
      continue;
    }
    r = McReadDirEntry(port, slot, directory_cluster, 1, directory_entry);
    if (r != 0)
      goto fail;

    do {
      {
        int index, offset;
        index = ((int)fse->cluster + allocation_offset) / block_clusters;
        offset = ((int)fse->cluster + allocation_offset) % block_clusters;

        if (mcman_same_block(index, mcman_badblock_state.block)) {
          fse->cluster = mcman_replacement_clusters[offset] - allocation_offset;
          Mc1stCacheEntSetWrFlagOff();
        }
      }

      r = McReadDirEntry(port, slot, fse->cluster, fse->parent_entry, directory_entry);
      if (r != 0)
        goto fail;

      length = fse->length;

      r = McReadDirEntry(port, slot, directory_cluster, 0, directory_entry);
      if (r != 0)
        goto fail;

      directory_cluster = fse->cluster;
      {
        int index, offset;
        index = ((int)fse->cluster + allocation_offset) / block_clusters;
        offset = ((int)fse->cluster + allocation_offset) % block_clusters;
        curentry = fse->parent_entry + 1;

        if (index == mcman_badblock_state.block) {
          fse->cluster = mcman_replacement_clusters[offset] - allocation_offset;
          Mc1stCacheEntSetWrFlagOff();
        }
      }

      if (directory_cluster != 0)
        goto scan_directory;

    } while (0);
    if (curentry != root_parent_entry)
      goto scan_directory;

  retire_block:
    fat_entry2 = 0;
    if (block_clusters > 0) {
      do {
        clust = first_cluster + fat_entry2;
        if (clust >= allocation_offset) {
          r = McSetFATentry(port, slot, clust - allocation_offset, 0xfffffffd);
          if (r != 0)
            goto fail;
        }
      } while (++fat_entry2 < block_clusters);
    }

    McFlushCache(port, slot);
    mcman_clearcache(port, slot);

    mcman_badblock_state.block = 0;

    fat_entry2 = 0;
    if (block_clusters > 0) {
      do {
        if (mcman_replacement_clusters[fat_entry2] != 0) {
          r = McReadCluster(port, slot,
                            (mcdi->backup_block1 * mcdi->clusters_per_block) + fat_entry2, &mce);
          if (r != 0)
            goto fail;

          mce->cluster = mcman_replacement_clusters[fat_entry2];
          mce->dirty = 1;
        }
      } while (++fat_entry2 < block_clusters);
    }

    McFlushCache(port, slot);
  }

write_super:
  r = mcman_write_superblock(port, slot);
  return r;

fail:
  mcman_badblock_state.block = 0;
  return -8;
}

int mcman_write_superblock(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry* cache;
  int result;
  u_int offset;
  memset(device, 0, 40);
  strcpy((char*)device, "Sony PS2 Memory Card Format ");
  strcat((char*)device, "1.2.0.0");
  offset = 0;
  do {
    result = McReadCluster(port, slot, (int)offset / 1024, &cache);
    if (result != 0)
      return -48;
    cache->dirty = 1;
    memcpy(cache->data, (u_char*)device + offset,
           sizeof(*device) - offset > 1024 ? 1024 : sizeof(*device) - offset);
    offset += 1024;
  } while (offset < sizeof(*device));
  return McFlushCache(port, slot);
}
