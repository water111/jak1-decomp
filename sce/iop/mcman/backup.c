#include "sce/iop/mcman/block.h"
#include "sce/iop/mcman/buffers.h"
#include "sce/iop/mcman/cache.h"
#include "sce/iop/mcman/directory.h"
#include "sce/iop/mcman/filesystem.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/transport.h"
#include "sce/iop/sysclib/string.h"

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
