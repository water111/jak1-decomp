#include "sce/iop/mcman/block.h"
#include "sce/iop/mcman/buffers.h"
#include "sce/iop/mcman/cache.h"
#include "sce/iop/mcman/device.h"
#include "sce/iop/mcman/directory.h"
#include "sce/iop/mcman/filesystem.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/transport.h"
#include "sce/iop/sysclib/string.h"

int mcman_probe_port = -1;
int mcman_probe_slot = -1;
int mcman_probe_block = -1;
int mcman_probe_result = -10;
int mcman_directory_cluster = -1;

int mcman_setPS2geometry(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int card_size, blocks;
  if (McGetCardSpec(port, slot, &device->page_size, &device->pages_per_block, &card_size,
                    &device->card_flags) != 0)
    return -3;
  device->pages_per_cluster = 1024 / device->page_size;
  blocks = card_size / device->pages_per_block;
  device->cluster_size = 1024;
  device->unknown[0] = 0;
  device->unknown[1] = 0;
  *(u_short*)&device->reserved = 0xff00;
  device->fat_entries_per_cluster = 256;
  device->free_clusters = -1;
  device->unknown[5] = -1;
  device->root_cluster_copy = device->root_cluster;
  card_size = blocks;
  device->clusters_per_block = device->pages_per_block / device->pages_per_cluster;
  device->clusters_per_card = card_size * device->clusters_per_block;
  return 0;
}

int mcman_reportBadBlocks(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int bad_blocks;
  int block, page, index, count, result, threshold;
  int marker;
  mcman_wmemset(device->bad_blocks, 128, -1);
  bad_blocks = 0;
  if (!(device->card_flags & 8))
    return 0;
  marker = 0;
  threshold = device->page_size / 2;
  if (device->card_flags & 16)
    marker = 255;
  for (block = 0; block < (int)device->clusters_per_card / device->clusters_per_block; block++) {
    if (bad_blocks >= 16)
      return 0;
    count = 0;
    for (page = 0; page < 2; page++) {
      result = McReadPage(port, slot, block * device->pages_per_block + page, mcman_pagebytes);
      if (result == -2) {
        device->bad_blocks[bad_blocks++] = block;
        break;
      }
      if (result != 0)
        return result;
      if (!(device->card_flags & 1)) {
        for (index = 0; index < device->page_size; index++) {
          if (mcman_pagebytes[index] == marker)
            count++;
          if (count >= threshold) {
            device->bad_blocks[bad_blocks++] = block;
            goto next_block;
          }
        }
      }
    }
  next_block:;
  }
  return 0;
}

int mcman_setPS2devinfos(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McFsEntry* entry;
  int result, major, minor, limit, count, cluster, index;
  mcman_wmemset(device, sizeof(*device), 0);
  result = mcman_setPS2geometry(port, slot);
  if (result != 0)
    return -49;
  result = McReadPage(port, slot, 0, &mcman_pagebytes);
  if (result == -2)
    return -2;
  if (result != 0)
    return -48;
  if (strncmp("Sony PS2 Memory Card Format ", (char*)mcman_pagebytes, 28) != 0)
    return -2;
  major = mcman_pagebytes[28] - '0';
  minor = mcman_pagebytes[30] - '0';
  if (major == 1 && minor == 0)
    return -2;
  memcpy(device, &mcman_pagebytes, 340);
  if (mcman_checkBackupBlocks(port, slot) != 0)
    return -47;
  if (McReadDirEntry(port, slot, 0, 0, &entry) != 0)
    return -46;
  if (strcmp(entry->name, ".") != 0)
    return -2;
  if (McReadDirEntry(port, slot, 0, 1, &entry) != 0)
    return -45;
  if (strcmp(entry->name, "..") != 0)
    return -2;
  device->formatted = 1;
  device->card_type = 2;
  if (major == 1 && minor == 1 &&
      device->backup_block2 * device->clusters_per_block == device->allocation_end)
    device->allocation_end =
        device->backup_block2 * device->clusters_per_block - device->allocation_offset;
  limit = ((int)device->clusters_per_card / 1000) * 1000 + 1;
  count = 0;
  cluster = device->allocation_offset;
  while (count < limit) {
    if (cluster >= (int)(device->allocation_end + device->allocation_offset))
      break;
    if (cluster % device->clusters_per_block == 0 || cluster == device->allocation_offset) {
      result = 1;
      for (index = 0; index < 16; index++) {
        if (device->bad_blocks[index] == cluster / device->clusters_per_block)
          result = 0;
      }
    }
    if (result == 1)
      count++;
    cluster++;
  }
  device->unknown[2] = cluster - device->allocation_offset;
  return 0;
}

int mcman_format2(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry* cache;
  int reserved;
  int result, cluster, fat_clusters, indirect_clusters, index, valid, limit, value, end;

  if (device->formatted > 0) {
    int major = device->format_version[0] - '0';
    int minor = device->format_version[2] - '0';
    if (major < 2 && minor < 2) {
      result = mcman_reportBadBlocks(port, slot);
      if (result != 0 && result != -2)
        return -1;
    }
  } else {
    result = mcman_reportBadBlocks(port, slot);
    if (result != 0 && result != -2)
      return -1;
  }
  memset(device, 0, 40);
  strcpy((char*)device, "Sony PS2 Memory Card Format ");
  strcat((char*)device, "1.2.0.0");
  reserved = 8192 / device->cluster_size;
  device->formatted = -1;
  mcman_probe_port = -1;
  mcman_probe_slot = -1;
  mcman_probe_block = -1;
  mcman_probe_result = -10;
  if (reserved < 1)
    reserved = 1;
  for (cluster = 0; cluster < reserved; cluster++) {
    result = mcman_writecluster(port, slot, cluster, 1);
    if (result == 0)
      return -2;
    if (result != 1)
      return -40;
  }
  fat_clusters = (device->clusters_per_card * 4 - 1) / (u_int)device->cluster_size + 1;
  indirect_clusters = ((u_int)fat_clusters * 4 - 1) / (u_int)device->cluster_size + 1;
  if (indirect_clusters > 32) {
    indirect_clusters = 32;
    fat_clusters = device->fat_entries_per_cluster * 32;
  }
  for (index = 0; index < indirect_clusters; index++) {
    if (cluster >= (int)device->clusters_per_card)
      return -2;
    do {
      result = mcman_writecluster(port, slot, cluster, 1);
      if (result != 0)
        break;
      cluster++;
    } while (cluster < (int)device->clusters_per_card);
    if (cluster >= (int)device->clusters_per_card)
      return -2;
    device->indirect_fat[index] = cluster++;
  }
  for (index = 0; index < fat_clusters; index++) {
    if (index % device->fat_entries_per_cluster == 0) {
      result = McReadCluster(port, slot,
                             device->indirect_fat[index / device->fat_entries_per_cluster], &cache);
      if (result != 0)
        return -42;
      cache->dirty = 1;
    }
    if (cluster >= (int)device->clusters_per_card)
      return -2;
    do {
      result = mcman_writecluster(port, slot, cluster, 1);
      if (result == 1)
        break;
      cluster++;
      if (result < 0)
        return -43;
    } while (cluster < (int)device->clusters_per_card);
    if (cluster >= (int)device->clusters_per_card)
      return -2;
    ((int*)cache->data)[index % device->fat_entries_per_cluster] = cluster++;
  }
  result = (int)device->clusters_per_card / device->clusters_per_block;
  device->backup_block1 = 0;
  device->backup_block2 = 0;
  for (index = result - 1; index != 0; index--) {
    result = mcman_writecluster(port, slot, index * device->clusters_per_block, 1);
    if (result < 0)
      return -44;
    if (result != 0 && device->backup_block1 == 0)
      device->backup_block1 = index;
    else if (result != 0 && device->backup_block2 == 0) {
      device->backup_block2 = index;
      break;
    }
  }
  if (mcman_eraseblock(port, slot, device->backup_block2, 0, 0) != 0)
    return -45;
  limit = ((int)device->clusters_per_card / 1000) * 1000 + 1;
  valid = 0;
  end = index * device->clusters_per_block;
  for (; cluster < end; cluster++) {
    result = mcman_writecluster(port, slot, cluster, 0);
    if (result == 1) {
      if (valid == 0) {
        device->allocation_offset = cluster;
        device->root_cluster = 0;
        value = -1;
      } else {
        value = 0x7fffffff;
      }
      valid++;
      if (valid == limit)
        device->unknown[2] = cluster - device->allocation_offset + 1;
    } else {
      value = -3;
      if (result != 0)
        return -45;
    }
    if (McSetFATentry(port, slot, cluster - device->allocation_offset, value) != 0)
      return -46;
  }
  device->allocation_end = index * device->clusters_per_block - device->allocation_offset;
  if (device->unknown[2] == 0)
    device->unknown[2] = end;
  if (valid < device->clusters_per_block)
    return -2;
  index = 0;
  do {
    if (McReadCluster(port, slot, (int)index / 1024, &cache) != 0)
      return -48;
    cache->dirty = 1;
    memcpy(cache->data, (u_char*)device + index,
           sizeof(*device) - (u_int)index > 1024 ? 1024 : sizeof(*device) - (u_int)index);
    index += 1024;
  } while ((u_int)index < sizeof(*device));
  device->unknown[0] = 0;
  device->unknown[1] = 0;
  device->free_clusters = -1;
  device->unknown[5] = -1;
  device->root_cluster_copy = device->root_cluster;
  if (McCreateDirentry(port, slot, 0, 0, 0, 0) != 0)
    return -49;
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  device->formatted = 1;
  return 0;
}

struct McDateBytes {
  u_char bytes[8];
};

int McCreateDirentry(int port,
                     int slot,
                     int parent_cluster,
                     int parent_index,
                     int cluster,
                     const void* time) {
  struct McCacheEntry* cache;
  struct McFsEntry *entry, *parent;
  int result;
  result = McReadCluster(port, slot, cluster + mcman_devices[port][slot].allocation_offset, &cache);
  if (result != 0)
    return result;
  mcman_wmemset(cache->data, 1024, 0);
  entry = (struct McFsEntry*)cache->data;
  entry[0].mode = 0x8427;
  if (time != 0)
    memcpy(&entry[0].created, time, 8);
  else
    mcman_getmcrtime(&entry[0].created);
  entry[0].modified = entry[0].created;
  entry[0].length = 0;
  entry[0].parent_entry = parent_index;
  entry[0].cluster = parent_cluster;
  strcpy(entry[0].name, ".");
  if (parent_cluster == 0 && parent_index == 0) {
    *(struct McDateBytes*)&entry[1].created = *(struct McDateBytes*)&entry[0].created;
    entry[0].length = 2;
    entry++;
    entry->mode = 0xa426;
    entry->parent_entry = 0;
    entry->cluster = 0;
  } else {
    McReadDirEntry(port, slot, parent_cluster, 0, &parent);
    *(struct McDateBytes*)&entry[1].created = *(struct McDateBytes*)&parent->created;
    entry++;
    entry->mode = 0x8427;
    entry->parent_entry = parent->parent_entry;
    entry->cluster = parent->cluster;
  }
  entry->modified = entry->created;
  entry->length = 0;
  strcpy(entry->name, "..");
  cache->dirty = 1;
  return 0;
}

int mcman_fatRseek(int fd) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = handle->port, slot = handle->slot;
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  int remaining = (int)handle->position / device->cluster_size;
  int cluster, entry;
  if (remaining >= (int)handle->cluster_offset) {
    cluster = handle->link;
    remaining -= handle->cluster_offset;
  } else
    cluster = handle->free_link;
  if (cluster < 0)
    return -3;
  while (remaining != 0) {
    int result = McGetFATentry(port, slot, cluster, &entry);
    if (result != 0)
      return result;
    remaining--;
    if (entry >= -1)
      return -3;
    {
      int offset = (int)handle->position / device->cluster_size - remaining;
      cluster = entry & 0x7fffffff;
      handle->link = cluster;
      handle->cluster_offset = offset;
    }
  }
  return cluster + device->allocation_offset;
}

int mcman_fatWseek(int fd) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = handle->port, slot = handle->slot;
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  int remaining = (int)handle->position / device->cluster_size;
  int cluster, entry, result;
  struct McCacheEntry* cache;
  if ((int)handle->free_link >= 0 && handle->cluster_offset != 0 &&
      remaining >= (int)handle->cluster_offset) {
    cluster = handle->link;
    remaining -= handle->cluster_offset;
  } else {
    cluster = handle->free_link;
    if (cluster < 0) {
      cluster = mcman_findfree2(port, slot, 1);
      if (cluster < 0)
        return cluster;
      cache = mcman_get1stcacheEntp();
      handle->free_link = cluster;
      result = mcman_close2(fd);
      if (result != 0)
        return result;
      mcman_addcacheentry(cache);
      McFlushCache(port, slot);
    }
  }
  while (remaining != 0) {
    result = McGetFATentry(port, slot, cluster, &entry);
    if (result != 0)
      return result;
    if (entry >= -1) {
      entry = mcman_findfree2(port, slot, 1);
      if (entry < 0)
        return entry;
      entry |= 0x80000000;
      cache = mcman_get1stcacheEntp();
      result = McSetFATentry(port, slot, cluster, entry);
      if (result != 0)
        return result;
      mcman_addcacheentry(cache);
    }
    remaining--;
    cluster = entry & 0x7fffffff;
  }
  {
    int offset = (int)handle->position / device->cluster_size;
    handle->link = cluster;
    handle->cluster_offset = offset;
  }
  return 0;
}

extern int mcman_badblock;

int mcman_findfree2(int port, int slot, int reserve) {
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry *indirect, *fat, *cache;
  int index, count = 0, group, result;
  int* entry;
  for (index = device->unknown[1]; index < (int)device->unknown[2]; index++) {
    group = index / device->fat_entries_per_cluster;
    if (index % device->fat_entries_per_cluster == 0 || index == (int)device->unknown[1]) {
      result = McReadCluster(
          port, slot, device->indirect_fat[group / device->fat_entries_per_cluster], &indirect);
      if (result != 0)
        return result;
    }
    if (index % device->fat_entries_per_cluster == 0 || index == (int)device->unknown[1]) {
      result = McReadCluster(port, slot,
                             ((int*)indirect->data)[group % device->fat_entries_per_cluster], &fat);
      if (result != 0)
        return result;
    }
    cache = fat;
    entry = (int*)((index % device->fat_entries_per_cluster) * sizeof(int) + (u_int)cache->data);
    if (*entry >= 0) {
      int block = (index + (int)device->allocation_offset) / device->clusters_per_block;
      if (block != mcman_badblock) {
        if (reserve != 0) {
          *entry = -1;
          cache->dirty = 1;
          device->unknown[1] = index;
          device->free_clusters--;
          return index;
        }
        count++;
      }
    }
  }
  if (reserve == 0) {
    device->free_clusters = count;
    if (count != 0)
      return count;
  }
  return -3;
}

struct McDirectoryCache;

#define COPY_MODE_BIT(output, entry, bit) \
  if ((entry)->mode & (bit))              \
  (output)->mode |= (bit)

int mcman_dread2(int fd, struct McIoDirEntry* output) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = mcman_handles[fd].port, slot = mcman_handles[fd].slot;
  struct McFsEntry* entry;
  int result;
  while ((int)handle->position < (int)handle->size) {
    result = McReadDirEntry(port, slot, handle->free_link, handle->position, &entry);
    if (result != 0)
      return result;
    if (entry->mode & 0x8000)
      break;
    handle->position++;
  }
  if ((int)handle->position >= (int)handle->size)
    return 0;
  handle->position++;
  mcman_wmemset(output, sizeof(*output), 0);
  strcpy(output->name, entry->name);
  output->name[32] = 0;
  COPY_MODE_BIT(&output->stat, entry, 1);
  COPY_MODE_BIT(&output->stat, entry, 2);
  COPY_MODE_BIT(&output->stat, entry, 4);
  COPY_MODE_BIT(&output->stat, entry, 0x1000);
  COPY_MODE_BIT(&output->stat, entry, 0x800);
  COPY_MODE_BIT(&output->stat, entry, 8);
  if (entry->mode & 0x20)
    output->stat.mode |= 0x20;
  else
    output->stat.mode |= 0x10;
  output->stat.attributes = entry->attributes;
  output->stat.size = entry->length;
  output->stat.created = entry->created;
  output->stat.modified = entry->modified;
  return 1;
}

int mcman_getstat2(int port, int slot, const char* name, struct McIoStat* output) {
  struct McFsEntry* entry;
  int result = mcman_cachedirentry(port, slot, name, 0, &entry, 1);
  if (result != 0)
    return result;
  mcman_wmemset(output, sizeof(*output), 0);
  COPY_MODE_BIT(output, entry, 1);
  COPY_MODE_BIT(output, entry, 2);
  COPY_MODE_BIT(output, entry, 4);
  COPY_MODE_BIT(output, entry, 0x1000);
  COPY_MODE_BIT(output, entry, 0x800);
  COPY_MODE_BIT(output, entry, 8);
  if (entry->mode & 0x20)
    output->mode |= 0x20;
  else
    output->mode |= 0x10;
  output->attributes = entry->attributes;
  if (!(entry->mode & 0x20))
    output->size = entry->length;
  output->created = entry->created;
  output->modified = entry->modified;
  return 0;
}

int mcman_setinfo2(int port, int slot, const char* path, struct McDirectoryEntry* info, int flags) {
  struct McFsEntry directory;
  struct McDirectoryCache cache;
  struct McFsEntry* entry;
  int result;
  short mask;
  result = mcman_cachedirentry(port, slot, path, &cache, &entry, 1);
  if (result == 3) {
  denied:
    return -5;
  }
  if (result > 0)
    return -4;
  if (result != 0)
    return result;
  if (flags & 16) {
    if (strcmp(".", info->name) == 0 || strcmp("..", info->name) == 0)
      return -4;
    if ((u_char)info->name[0] == 0)
      return -4;
    if (mcman_chrpos(info->name, '/') >= 0)
      goto invalid;
    if (cache.entry_index < 2)
      return -4;
    if (!mcman_checkpath(info->name))
      return -4;
    result = McReadDirEntry(port, slot, cache.cluster, 0, &entry);
    if (result != 0)
      return result;
    result = McReadDirEntry(port, slot, entry->cluster, entry->parent_entry, &entry);
    if (result != 0)
      return result;
    memcpy(&directory, entry, sizeof(directory));
    result = mcman_getdirinfo(port, slot, &directory, info->name, 0, 1);
    if (result != 1) {
      if (result < 2) {
        if (result == 0)
          goto invalid;
        return result;
      } else {
        if (result == 2)
          goto denied;
        return result;
      }
    }
    goto update;
  invalid:
    return -4;
  }
update:
  result = McReadDirEntry(port, slot, cache.cluster, cache.entry_index, &entry);
  if (result != 0)
    return result;
  Mc1stCacheEntSetWrFlagOff();
  if (flags & 8)
    entry->attributes = info->application_data;
  if ((flags & 4) && (entry->mode & 32)) {
    mask = -1;
    if (!mcman_allow_ps1)
      mask = 0x180f;
    entry->mode = (entry->mode & ~mask) | (info->attributes & mask);
  }
  if ((flags & 512) && (entry->mode & 32)) {
    mask = 0x180f;
    if (mcman_allow_ps1)
      mask = -1;
    entry->mode = (entry->mode & ~mask) | (info->attributes & mask);
  }
  if (flags & 1)
    entry->created = info->created;
  if (flags & 2)
    entry->modified = info->modified;
  if (flags & 16) {
    strncpy(entry->name, info->name, 32);
    entry->name[31] = 0;
  }
  return 0;
}

int mcman_read2(int fd, void* buffer, int count) {
  struct McFileHandle* handle;
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry* cache;
  int done;
  handle = &mcman_handles[fd];
  if ((int)handle->position >= (int)handle->size)
    return 0;
  {
    int remaining = handle->size - handle->position;
    if (count < remaining)
      remaining = count;
    count = remaining;
  }
  done = 0;
  if (count != 0) {
    do {
      int result, size, offset, available;
      offset = (int)handle->position % device->cluster_size;
      available = device->cluster_size - offset;
      if (available < count)
        size = available;
      else
        size = count;
      result = mcman_fatRseek(fd);
      if (result <= 0)
        return result;
      result = McReadCluster(port, slot, result, &cache);
      if (result != 0)
        return result;
      memcpy((u_char*)buffer + done, cache->data + offset, size);
      cache->read_flag = 1;
      done += size;
      count -= size;
      handle->position += size;
    } while (count != 0);
  }
  return done;
}

int mcman_write2(int fd, void* buffer, int count) {
  struct McFileHandle* handle;
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry* cache;
  int result, done;
  handle = &mcman_handles[fd];
  if (count != 0 && handle->unknown2 == 0) {
    handle->unknown2 = 1;
    result = mcman_close2(fd);
    if (result != 0)
      return result;
    result = McFlushCache(handle->port, handle->slot);
    if (result != 0)
      return result;
  }
  done = 0;
  if (count != 0) {
    do {
      int size, offset, available;
      int cluster;
      cluster = mcman_fatRseek(fd);
      if (cluster == -3) {
        result = mcman_fatWseek(fd);
        if (result == cluster) {
          McFlushCache(port, slot);
          return cluster;
        }
        if (result != 0)
          return result;
        cluster = mcman_fatRseek(fd);
      } else if (cluster < 0)
        return cluster;
      result = McReadCluster(port, slot, cluster, &cache);
      if (result != 0)
        return result;
      cache->read_flag = 1;
      offset = (int)handle->position % device->cluster_size;
      available = device->cluster_size - offset;
      if (available < count)
        size = available;
      else
        size = count;
      memcpy(cache->data + offset, (u_char*)buffer + done, size);
      cache->dirty = 1;
      handle->position += size;
      {
        int length = handle->position;
        if (length < (int)handle->size)
          length = handle->size;
        handle->size = length;
      }
      count -= size;
      done += size;
    } while (count != 0);
  }
  result = mcman_close2(fd);
  if (result != 0)
    return result;
  return done;
}

extern struct McDateTime mcman_modified_time;

int mcman_close2(int fd) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = mcman_handles[fd].port, slot = mcman_handles[fd].slot;
  int result;
  struct McFsEntry *entry, *parent;
  result = McReadDirEntry(port, slot, handle->cluster, handle->entry_index, &entry);
  if (result != 0)
    return -31;
  if (handle->unknown2 == 0)
    entry->mode |= 0x80;
  else
    entry->mode &= ~0x80;
  mcman_getmcrtime(&entry->modified);
  entry->cluster = handle->free_link;
  entry->length = handle->size;
  Mc1stCacheEntSetWrFlagOff();
  mcman_modified_time = entry->modified;
  result = McReadDirEntry(port, slot, handle->parent_cluster, handle->parent_entry_index, &parent);
  if (result != 0)
    return result;
  parent->modified = mcman_modified_time;
  Mc1stCacheEntSetWrFlagOff();
  return 0;
}

int mcman_getentspace(int port, int slot, const char* path) {
  struct McFsEntry directory;
  struct McDirectoryCache cache;
  struct McFsEntry* entry;
  int result, index, count;
  result = mcman_cachedirentry(port, slot, path, &cache, &entry, 1);
  if (result > 0)
    return -4;
  if (result < 0)
    return result;
  memcpy(&directory, entry, sizeof(directory));
  count = directory.length & 1;
  for (index = 0; index < (int)directory.length; index++) {
    result = McReadDirEntry(port, slot, directory.cluster, index, &entry);
    if (result != 0)
      return result;
    if (!(entry->mode & 0x8000))
      count++;
  }
  return count;
}

extern struct McFsEntry mcman_directory_buffer;

int mcman_cachedirentry(int port,
                        int slot,
                        const char* path,
                        struct McDirectoryCache* cache,
                        struct McFsEntry** output,
                        int mode) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McDirectoryCache position, local;
  struct McFsEntry* entry;
  int result, slash;
  if (cache == 0) {
    cache = &local;
    local.maximum_entries = -1;
  }
  if (*(const u_char*)path == '/') {
    path++;
    position.cluster = 0;
    position.entry_index = 0;
  } else {
    position.cluster = device->root_cluster_copy;
    position.entry_index = device->unknown[0];
  }
  result = McReadDirEntry(port, slot, position.cluster, position.entry_index, &entry);
  if (result != 0)
    return result;
  if (*(const u_char*)path == 0) {
    if (!(entry->mode & 0x8000))
      return 2;
    if ((entry->mode & 0x2000) && !mcman_allow_ps1)
      return 3;
    if (cache != 0) {
      memcpy(&mcman_directory_buffer, entry, sizeof(*entry));
      result = mcman_getdirinfo(port, slot, &mcman_directory_buffer, ".", cache, mode);
      if (result < 0)
        return result;
      McReadDirEntry(port, slot, cache->cluster, cache->entry_index, output);
      if (result > 0)
        return 2;
      return result;
    }
    *output = entry;
    return 0;
  }
  while (*(const u_char*)path) {
    if ((entry->mode & 5) != 5 && !mcman_allow_ps1)
      return -5;
    slash = mcman_chrpos((const char*)path, '/');
    if (slash < 0)
      strlen((const char*)path);
    memcpy(&mcman_directory_buffer, entry, sizeof(*entry));
    result = mcman_getdirinfo(port, slot, &mcman_directory_buffer, (const char*)path, cache, mode);
    if (result < 0)
      return result;
    if (result > 0) {
      if (mcman_chrpos((const char*)path, '/') >= 0)
        return 2;
      cache->cluster = position.cluster;
      cache->entry_index = position.entry_index;
      if (result == 1)
        return 1;
      return 3;
    }
    slash = mcman_chrpos((const char*)path, '/');
    if (slash >= 0 && (u_char)((const u_char*)path)[slash + 1] != 0) {
      slash = mcman_chrpos((const char*)path, '/');
      path += slash + 1;
      position.cluster = cache->cluster;
      position.entry_index = cache->entry_index;
      McReadDirEntry(port, slot, cache->cluster, cache->entry_index, &entry);
      if (!(entry->mode & 32))
        return 2;
    } else {
      McReadDirEntry(port, slot, cache->cluster, cache->entry_index, output);
      return 0;
    }
  }
  return 0;
}

int mcman_getdirinfo(int port,
                     int slot,
                     struct McFsEntry* directory,
                     const char* name,
                     struct McDirectoryCache* cache,
                     int mode) {
  int index, result, found, length, component;
  struct McFsEntry* entry;
  component = mcman_chrpos(name, '/');
  if (component < 0)
    component = strlen(name);
  found = 0;
  if (component == 2 && !strncmp(name, "..", 2)) {
    result = McReadDirEntry(port, slot, directory->cluster, 0, &entry);
    if (result != 0)
      return result;
    result = McReadDirEntry(port, slot, entry->cluster, 0, &entry);
    if (result != 0)
      return result;
    if (cache) {
      cache->cluster = entry->cluster;
      cache->entry_index = entry->parent_entry;
    }
    result = McReadDirEntry(port, slot, entry->cluster, entry->parent_entry, &entry);
    if (result != 0)
      return result;
    memcpy(directory, entry, sizeof(*entry));
    if ((entry->mode & 0x2000) && !mcman_allow_ps1) {
      found = 2;
      if (!cache || cache->maximum_entries < 0)
        return 3;
    } else {
      found = 1;
      if (!cache || cache->maximum_entries < 0)
        return 0;
    }
  } else if (component == 1 && !strncmp(name, ".", 1)) {
    result = McReadDirEntry(port, slot, directory->cluster, 0, &entry);
    if (result != 0)
      return result;
    if (cache) {
      cache->cluster = entry->cluster;
      cache->entry_index = entry->parent_entry;
    }
    if ((entry->mode & 0x2000) && !mcman_allow_ps1) {
      found = 2;
      if (!cache || cache->maximum_entries < 0)
        return 3;
    } else {
      found = 1;
      if (!cache || cache->maximum_entries < 0)
        return 0;
    }
  }
  if (cache && cache->maximum_entries >= 0)
    cache->maximum_entries = directory->length;
  for (index = 0; index < (int)directory->length; index++) {
    result = McReadDirEntry(port, slot, directory->cluster, index, &entry);
    if (result != 0)
      return result;
    if (!(entry->mode & 0x8000) && cache && index < cache->maximum_entries)
      cache->maximum_entries = index;
    if (mode) {
      if (!(entry->mode & 0x8000))
        continue;
    } else {
      if (entry->mode & 0x8000)
        continue;
    }
    if (found)
      continue;
    if (component >= 11 && !strncmp(name + 10, entry->name + 10, component - 10)) {
      if (strlen(entry->name) >= (u_int)component)
        length = strlen(entry->name);
      else
        length = component;
      if (!strncmp(name, entry->name, length))
        goto matched;
    }
    if (strlen(entry->name) >= (u_int)component)
      length = strlen(entry->name);
    else
      length = component;
    if (strncmp(name, entry->name, length))
      continue;
  matched:
    found = 1;
    if ((entry->mode & 0x2000) && !mcman_allow_ps1)
      found = 2;
    if (!cache)
      break;
    {
      int cluster = directory->cluster;
      cache->entry_index = index;
      cache->cluster = cluster;
    }
    if (cache->maximum_entries < 0)
      break;
  }
  if (found == 2)
    return 2;
  return found == 0;
}

extern struct McFsEntry mcman_open_entry, mcman_open_parent;

#define MC_OPEN_READ 1
#define MC_OPEN_WRITE 2
#define MC_OPEN_CREATE 0x200
#define MC_OPEN_MKDIR 0x40
#define MC_ATTR_EXISTS 0x8000
#define MC_ATTR_DIRECTORY 0x20

int mcman_open2(int port, int slot, const char* filename, int flags) {
  int fd, i, r, read, write, mcfree;
  struct McFileHandle* fh;
  McDeviceInfo* mcdi;
  struct McDirectoryCache cacheDir;
  struct McFsEntry *fse1, *fse2;
  struct McCacheEntry* mce;

  int fat_entry;
  int first_character;

  if ((flags & MC_OPEN_CREATE) != 0)
    flags |= MC_OPEN_WRITE;

  first_character = *(const u_char*)filename;
  if (!first_character || !mcman_checkpath(filename)) {
    if (!first_character)
      return -4;
    goto no_entry;
  }

  for (fd = 0; fd < MCMAN_HANDLES; fd++) {
    if (mcman_handles[fd].status == 0)
      break;
  }

  if (fd == MCMAN_HANDLES)
    return -7;

  fh = (struct McFileHandle*)&mcman_handles[fd];

  mcdi = &mcman_devices[port][slot];
  mcman_wmemset(fh, sizeof(*fh), 0);

  cacheDir.maximum_entries = 0;
  if ((flags & (MC_OPEN_CREATE | MC_OPEN_MKDIR)) == 0)
    cacheDir.maximum_entries = -1;

  fse1 = 0;
  r = mcman_cachedirentry(port, slot, filename, &cacheDir, &fse1, 1);
  if (r < 0)
    return r;

  if (fse1) {
    memcpy(&mcman_open_entry, fse1, sizeof(*fse1));
  }
  if ((flags == 0) && ((fse1->mode & MC_ATTR_EXISTS) == 0))
    r = 1;

  if (r == 2) {
  no_entry: {
    asm("");
    return -4;
  }
  }

  if (r == 3)
    return -5;

  if (r == 0) {
    if (flags & MC_OPEN_MKDIR)
      return -4;
  } else if (flags & MC_OPEN_MKDIR) {
    if (cacheDir.cluster != mcdi->root_cluster || cacheDir.entry_index != 0)
      return -5;
  }
  if ((flags & (MC_OPEN_CREATE | MC_OPEN_MKDIR)) && r != 0 && cacheDir.maximum_entries > 19 &&
      cacheDir.cluster != mcdi->root_cluster)
    return -5;

  if ((r == 1) && ((flags & (MC_OPEN_CREATE | MC_OPEN_MKDIR)) == 0))
    return -4;

  read = flags & MC_OPEN_READ;
  write = flags & MC_OPEN_WRITE;
  fh->free_link = -1;
  fh->link = -1;
  fh->cluster_offset = 0;
  fh->size = 0;
  fh->position = 0;
  do {
    fh->port = port;
    fh->slot = slot;
    fh->unknown2 = 0;
    fh->read_flag = read;
    fh->write_flag = write;
    fh->unknown1 = 0;
    fh->cluster = cacheDir.cluster;
  } while (0);
  fh->entry_index = cacheDir.entry_index;

  if (r == 0) {
    if (!mcman_allow_ps1 && write && !(mcman_open_entry.mode & MC_OPEN_WRITE))
      return -5;
    if (!mcman_allow_ps1 && (flags & MC_OPEN_READ) && !(mcman_open_entry.mode & MC_OPEN_READ))
      return -5;
    r = McReadDirEntry(port, slot, cacheDir.cluster, 0, &fse2);
    if (r != 0)
      return r;

    fh->parent_cluster = fse2->cluster;
    fh->parent_entry_index = fse2->parent_entry;

    if ((mcman_open_entry.mode & MC_ATTR_DIRECTORY) != 0 && !mcman_allow_ps1) {
      if ((*(u_int*)&mcman_open_entry & 5) != 5)
        return -5;

      fh->free_link = mcman_open_entry.cluster;
      fh->size = mcman_open_entry.length;
      fh->link = fh->free_link;
      fh->read_flag = 0;
      fh->write_flag = 0;
      fh->unknown1 = 0;
      fh->directory_flag = 1;
      fh->status = 1;

      return fd;
    }

    if ((flags & MC_OPEN_WRITE) != 0) {
      i = 0;
      do {
        if ((mcman_handles[i].status == 0) || (mcman_handles[i].port != port) ||
            (mcman_handles[i].slot != slot) ||
            (mcman_handles[i].cluster != (u_int)(cacheDir.cluster)) ||
            (mcman_handles[i].entry_index != (u_int)(cacheDir.entry_index)))
          continue;

        if (mcman_handles[i].write_flag != 0)
          return -5;

      } while (++i < MCMAN_HANDLES);
    }

    if ((flags & MC_OPEN_CREATE) != 0) {
      r = McSetDirEntryState(port, slot, cacheDir.cluster, cacheDir.entry_index, 0);
      McFlushCache(port, slot);

      if (r != 0)
        return -43;

      if (cacheDir.maximum_entries > cacheDir.entry_index)
        cacheDir.maximum_entries = cacheDir.entry_index;
    } else {
      fh->free_link = mcman_open_entry.cluster;
      fh->size = mcman_open_entry.length;
      fh->link = fh->free_link;

      {
        int read, write;
        read = 0;
        if (fh->read_flag != 0)
          read = *((u_char*)&mcman_open_entry.mode) & MC_OPEN_READ;
        fh->read_flag = read;
        write = 0;
        if (fh->write_flag != 0)
          write = (mcman_open_entry.mode >> 1) & MC_OPEN_READ;
        fh->write_flag = write;
      }

      fh->status = 1;

      return fd;
    }
  } else {
    fh->parent_cluster = cacheDir.cluster;
    fh->parent_entry_index = cacheDir.entry_index;
  }

  r = McReadDirEntry(port, slot, fh->parent_cluster, fh->parent_entry_index, &fse1);
  if (r != 0)
    return r;

  memcpy(&mcman_open_parent, fse1, sizeof(*fse1));

  i = -1;
  if (mcman_open_parent.length == (u_int)(cacheDir.maximum_entries)) {
    int entry_index, fsoffset;

    entry_index = mcman_open_parent.length / ((u_int)mcdi->cluster_size >> 9);
    fsoffset = mcman_open_parent.length % ((u_int)mcdi->cluster_size >> 9);

    if (fsoffset == 0) {
      int fat_index;

      fat_index = mcman_open_parent.cluster;
      i = entry_index;

      if ((mcman_open_parent.cluster == 0) && (i >= 2)) {
        if (mcman_getFATindex(port, slot, i - 1) >= 0) {
          fat_index = mcman_getFATindex(port, slot, i - 1);
          i = 1;
        }
      }
      i--;

      if (i != -1) {
        do {
          r = McGetFATentry(port, slot, fat_index, &fat_entry);
          if (r != 0)
            return r;

          if (fat_entry >= -1) {
            fat_entry = mcman_findfree2(port, slot, 1);
            if (fat_entry < 0)
              return fat_entry;
            mce = mcman_get1stcacheEntp();

            fat_entry |= 0x80000000;

            r = McSetFATentry(port, slot, fat_index, fat_entry);
            if (r != 0)
              return r;

            mcman_addcacheentry(mce);
          }
          i--;
          fat_index = fat_entry & ~0x80000000;

        } while (i != -1);
      }
    }

    r = McFlushCache(port, slot);
    if (r != 0)
      return r;

    i = -1;

    mcman_open_parent.length++;
  }

  for (;;) {
    r = mcman_chrpos(filename + i + 1, '/');
    if (r < 0)
      break;
    i += r + 1;
  }
  filename += i + 1;
  mcfree = 0;

  if ((flags & MC_OPEN_MKDIR) != 0) {
    mcfree = mcman_findfree2(port, slot, 1);
    if (mcfree < 0)
      return mcfree;
  }

  do {
    mce = mcman_get1stcacheEntp();

    mcman_getmcrtime(&mcman_open_parent.modified);

    r = McReadDirEntry(port, slot, mcman_open_parent.cluster, cacheDir.maximum_entries, &fse2);
    if (r != 0)
      return r;

  } while (0);
  mcman_wmemset((void*)fse2, sizeof(struct McFsEntry), 0);

  strncpy(fse2->name, filename, 32);

  fse2->created = mcman_open_parent.modified;
  fse2->modified = mcman_open_parent.modified;

  Mc1stCacheEntSetWrFlagOff();

  mcman_addcacheentry(mce);

  if (!mcman_allow_ps1)
    flags &= 0xffffdfff;

  if ((flags & MC_OPEN_MKDIR) != 0) {
    fse2->mode = flags & 0x2000;
    fse2->mode |= 0x8427;
    fse2->mode |= flags & 0x1800;
    fse2->length = 2;
    fse2->cluster = mcfree;

    r = McCreateDirentry(port, slot, mcman_open_parent.cluster, cacheDir.maximum_entries, mcfree,
                         (struct McDateTime*)&fse2->created);
    if (r != 0)
      return -46;

    r = McReadDirEntry(port, slot, fh->parent_cluster, fh->parent_entry_index, &fse1);
    if (r != 0)
      return r;

    memcpy(fse1, &mcman_open_parent, sizeof(*fse1));

    Mc1stCacheEntSetWrFlagOff();

    r = McFlushCache(port, slot);
    if (r != 0)
      return r;

    return 0;
  } else {
    fse2->mode = flags & 0x2000;
    fse2->mode |= 0x8417;
    fse2->mode |= flags & 0x1800;

    fse2->cluster = -1;
    fh->cluster = mcman_open_parent.cluster;
    fh->entry_index = cacheDir.maximum_entries;

    r = McReadDirEntry(port, slot, fh->parent_cluster, fh->parent_entry_index, &fse1);
    if (r != 0)
      return r;

    memcpy(fse1, &mcman_open_parent, sizeof(*fse1));

    Mc1stCacheEntSetWrFlagOff();

    r = McFlushCache(port, slot);
    if (r != 0)
      return r;
  }

  fh->unknown2 = 1;
  fh->status = 1;
  return fd;
}

int mcman_chdir(int port, int slot, const char* path, char* current) {
  struct McDirectoryCache cache;
  struct McFsEntry *entry, *parent;
  McDeviceInfo* device;
  int result, length, index, cluster;
  if (!mcman_checkpath(path))
    return -4;
  cache.maximum_entries = -1;
  device = &mcman_devices[port][slot];
  result = mcman_cachedirentry(port, slot, path, &cache, &entry, 1);
  if (result < 0)
    return result;
  if (result == 1 || result == 2)
    return -4;
  device->root_cluster_copy = cache.cluster;
  device->unknown[0] = cache.entry_index;
  cluster = cache.cluster;
  if (!strcmp(entry->name, "..")) {
    result = McReadDirEntry(port, slot, entry->cluster, 0, &entry);
    if (result != 0)
      return result;
  }
  if (!strcmp(entry->name, ".")) {
    device->root_cluster_copy = entry->cluster;
    device->unknown[0] = entry->parent_entry;
    cluster = entry->cluster;
    result = McReadDirEntry(port, slot, cluster, entry->parent_entry, &entry);
    if (result != 0)
      return result;
  }
  result = McReadDirEntry(port, slot, cluster, device->unknown[0], &parent);
  if (result != 0)
    return result;
  result = McReadDirEntry(port, slot, parent->cluster, 0, &parent);
  if (result != 0)
    return result;
  if (device->root_cluster_copy != parent->cluster || device->unknown[0] != parent->parent_entry)
    return -6;
  current[0] = 0;
  while (strcmp(entry->name, ".") != 0) {
    if (strlen(entry->name) < 32)
      length = strlen(entry->name);
    else
      length = 32;
    if (strlen(current) != 0) {
      for (index = strlen(current); index >= 0; index--)
        current[index + length + 1] = current[index];
      current[length] = '/';
      strncpy(current, entry->name, length);
    } else {
      strncpy(current, entry->name, 32);
      current[32] = 0;
    }
    result = McReadDirEntry(port, slot, cluster, 0, &entry);
    if (result != 0)
      return result;
    cluster = entry->cluster;
    result = McReadDirEntry(port, slot, cluster, entry->parent_entry, &entry);
    if (result != 0)
      return result;
  }
  strlen(entry->name);
  for (index = strlen(current); index >= 0; index--)
    current[index + 1] = current[index];
  current[0] = '/';
  return 0;
}

int mcman_writecluster(int port, int slot, int cluster, int test) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int block = cluster / device->clusters_per_block;
  int i, j, result, first_page, bytes;
  int words;
  u_int fill;
  u_int* data;
  if (mcman_probe_port == port && mcman_probe_slot == slot && mcman_probe_block == block)
    return mcman_probe_result;
  mcman_probe_port = port;
  mcman_probe_slot = slot;
  mcman_probe_block = block;
  mcman_probe_result = -9;
  for (j = 0; j < 16; j++) {
    if (device->bad_blocks[j] < 0)
      break;
    if (device->bad_blocks[j] == block) {
      mcman_probe_result = 0;
      return 0;
    }
  }
  if (test) {
    for (i = 1; i < device->pages_per_block; i++)
      mcman_pagedata[i] = 0;
    mcman_pagedata[0] = mcman_pagebuf.word;
    first_page = block * device->pages_per_block;
    bytes = device->page_size;
    words = (u_int)bytes >> 2;
    fill = -1;
    if (!(device->card_flags & 16))
      fill = 0;
    {
      u_int* data = mcman_pagebuf.word;
      for (j = 0; j < words; j++) {
        *data++ = fill;
      }
    }
    result = mcman_eraseblock(port, slot, block, mcman_pagedata, mcman_eccdata);
    if (result == -8)
      return 0;
    if (result != 0)
      return -1;
    for (i = 1; i < device->pages_per_block; i++) {
      result = McWritePage(port, slot, i + first_page, mcman_pagebuf.word, mcman_eccdata);
      if (result == -8)
        return 0;
      if (result != 0)
        return -2;
    }
    for (i = 1; i < device->pages_per_block; i++) {
      result = McReadPage(port, slot, i + first_page, mcman_pagebuf.word);
      if (result == -2)
        return 0;
      if (result != 0)
        return -3;
      for (j = 0; j < words; j++) {
        if (*(u_int*)&mcman_pagebuf.byte[j * 4] != fill) {
          mcman_probe_result = 0;
          return 0;
        }
      }
    }
    result = mcman_eraseblock(port, slot, block, 0, 0);
    if (result == -8)
      return 0;
    if (result != 0)
      return -1;
    result = McWritePage(port, slot, first_page, mcman_pagebuf.word, mcman_eccdata);
    if (result == -8)
      return 0;
    if (result != 0)
      return -2;
    result = McReadPage(port, slot, first_page, mcman_pagebuf.word);
    if (result == -2)
      return 0;
    if (result != 0)
      return -3;
    j = 0;
    if (words) {
      u_int* data = mcman_pagebuf.word;
      do {
        if (*data != fill) {
          mcman_probe_result = 0;
          return 0;
        }
        data++;
      } while (++j < words);
    }
    result = mcman_eraseblock(port, slot, block, 0, 0);
    if (result == -8)
      return 0;
    if (result != 0)
      return -4;
    for (i = 0; i < device->pages_per_block; i++) {
      result = McReadPage(port, slot, i + first_page, mcman_pagebuf.word);
      if (result != 0)
        return -5;
      for (j = 0; j < words; j++) {
        if (*(u_int*)&mcman_pagebuf.byte[j * 4] != ~fill) {
          mcman_probe_result = 0;
          return 0;
        }
      }
    }
  }
  mcman_probe_result = 1;
  return mcman_probe_result;
}

int McSetDirEntryState(int port, int slot, int cluster, int index, int exists) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McFsEntry* entry;
  int next, current, fd, result;
  result = McReadDirEntry(port, slot, cluster, index, &entry);
  if (result != 0)
    return result;
  if ((u_char)entry->name[0] == '.' &&
      ((u_char)entry->name[1] == 0 || (u_char)entry->name[1] == '.'))
    return -4;
  for (fd = 0; fd < MCMAN_HANDLES; fd++) {
    if (mcman_handles[fd].status && mcman_handles[fd].port == port &&
        mcman_handles[fd].slot == slot && mcman_handles[fd].cluster == cluster &&
        mcman_handles[fd].entry_index == index)
      return -5;
  }
  if (exists == 0)
    entry->mode &= ~0x8000;
  else
    entry->mode |= 0x8000;
  Mc1stCacheEntSetWrFlagOff();
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  current = entry->cluster;
  if (current < 0)
    return 0;
  if (current < (int)device->unknown[1])
    device->unknown[1] = current;
  device->unknown[5] = -1;
  do {
    result = McGetFATentry(port, slot, current, &next);
    if (result != 0)
      return result;
    if (exists == 0) {
      next &= 0x7fffffff;
      device->free_clusters++;
      if (current < (int)device->unknown[1])
        device->unknown[1] = next;
    } else {
      next |= 0x80000000;
      device->free_clusters--;
    }
    result = McSetFATentry(port, slot, current, next);
    if (result != 0)
      return result;
    current = next & 0x7fffffff;
  } while (current != 0x7fffffff);
  return 0;
}

extern int mcman_directory_index, mcman_directory_length;
extern char mcman_directory_path[1024];
extern const char* mcman_directory_pattern;
extern int mcman_directory_cluster;

int mcman_getdir2(int port,
                  int slot,
                  const char* path,
                  int flags,
                  int count,
                  struct McDirectoryEntry* output) {
  struct McFsEntry* entry;
  int result, position, done = 0;
  if ((flags & 0xffff) == 0) {
    strncpy(mcman_directory_path, path, 1023);
    mcman_directory_path[1023] = 0;
    position = -1;
    while ((result = mcman_chrpos(mcman_directory_path + position + 1, '/')) >= 0)
      position += result + 1;
    if (position > 0)
      mcman_directory_path[position] = 0;
    else if (position == 0)
      mcman_directory_path[1] = 0;
    else
      mcman_directory_path[0] = 0;
    mcman_directory_pattern = path + position + 1;
    result = mcman_cachedirentry(port, slot, mcman_directory_path, 0, &entry, 1);
    if (result > 0)
      return -4;
    if (result < 0)
      return result;
    if (!(entry->mode & 32)) {
      mcman_directory_cluster = -1;
      return -4;
    }
    mcman_directory_cluster = entry->cluster;
    mcman_directory_length = entry->length;
    if (entry->cluster == mcman_devices[port][slot].root_cluster && entry->parent_entry == 0)
      mcman_directory_index = 2;
    else
      mcman_directory_index = 0;
  } else if (mcman_directory_cluster < 0) {
    return -4;
  }
  while (count != 0) {
    if (mcman_directory_index >= mcman_directory_length)
      return done;
    result = McReadDirEntry(port, slot, mcman_directory_cluster, mcman_directory_index, &entry);
    if (result != 0)
      return result;
    mcman_directory_index++;
    if (!(entry->mode & 0x8000) || ((entry->mode & 0x2000) && !mcman_allow_ps1) ||
        !mcman_checkdirpath(entry->name, mcman_directory_pattern))
      continue;
    mcman_wmemset(&output[done], sizeof(*output), 0);
    if (mcman_directory_index == 2) {
      result = McReadDirEntry(port, slot, mcman_directory_cluster, 0, &entry);
      if (result != 0)
        return result;
      result = McReadDirEntry(port, slot, entry->cluster, 0, &entry);
      if (result != 0)
        return result;
      result = McReadDirEntry(port, slot, entry->cluster, entry->parent_entry, &entry);
      if (result != 0)
        return result;
      strcpy(output[done].name, "..");
    } else if (mcman_directory_index == 1) {
      result = McReadDirEntry(port, slot, mcman_directory_cluster, 0, &entry);
      if (result != 0)
        return result;
      result = McReadDirEntry(port, slot, entry->cluster, entry->parent_entry, &entry);
      if (result != 0)
        return result;
      strcpy(output[done].name, ".");
    } else {
      strncpy(output[done].name, entry->name, 32);
    }
    output[done].attributes = entry->mode;
    output[done].reserved = entry->reserved;
    output[done].application_data = entry->attributes;
    output[done].created = entry->created;
    output[done].modified = entry->modified;
    if (!(entry->mode & 32))
      output[done].size = entry->length;
    done++;
    count--;
  }
  return done;
}

int mcman_delete2(int port, int slot, const char* path, int flags) {
  struct McDirectoryCache cache;
  struct McFsEntry* entry;
  int result, index, length, cluster;
  if (!mcman_checkpath(path))
    goto no_entry;
  {
    result = mcman_cachedirentry(port, slot, path, &cache, &entry, flags == 0);
    if (result == 3)
      goto denied;
    if (result > 0)
      return -4;
    if (result < 0)
      return result;
    if (flags == 0) {
      if (!(entry->mode & 0x8000))
        goto invalid;
    } else {
      if (entry->mode & 0x8000)
        goto invalid;
    }
    if (!mcman_allow_ps1 && flags == 0 && !(entry->mode & 2)) {
    denied:
      return -5;
    }
    if (entry->cluster == 0 && entry->parent_entry == 0) {
    no_entry:
    invalid:
      return -4;
    }
    {
      if (flags == 0 && (entry->mode & 32)) {
        length = entry->length;
        cluster = entry->cluster;
        for (index = 2; index < length; index++) {
          result = McReadDirEntry(port, slot, cluster, index, &entry);
          if (result != 0)
            return result;
          if (entry->mode & 0x8000)
            return -6;
        }
      }
      result = McSetDirEntryState(port, slot, cache.cluster, cache.entry_index, flags);
      if (result != 0)
        return result;
      result = McFlushCache(port, slot);
      if (result != 0)
        return result;
      return 0;
    }
  }
}

int mcman_checkBackupBlocks(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  struct McCacheEntry* cache;
  int second_result, result, cluster, page;
  u_int first, second;
  result =
      McReadPage(port, slot, device->backup_block2 * device->pages_per_block, mcman_pagebuf.word);
  first = mcman_pagebuf.word[0];
  if ((device->card_flags & 16) && first == 0)
    first = -1;
  if (first != -1)
    first &= 0x7fffffff;
  second_result = McReadPage(port, slot, device->backup_block2 * device->pages_per_block + 1,
                             mcman_pagebuf.word);
  second = mcman_pagebuf.word[0];
  if ((device->card_flags & 16) && second == 0)
    second = -1;
  if (second != -1)
    second &= 0x7fffffff;
  if ((first == -1 || second != -1) && result >= 0 && second_result >= 0) {
    if (first == -1 && second == -1)
      return 0;
    for (cluster = 0; cluster < device->clusters_per_block; cluster++) {
      result = McReadCluster(port, slot,
                             device->backup_block1 * device->clusters_per_block + cluster, &cache);
      if (result != 0)
        return result;
      cache->read_flag = 1;
      {
        int page_test;
        asm("" : "=r"(result) : "0"(result));
        page_test = result;
        for (page = 0; page_test < device->pages_per_cluster; page_test = ++page)
          mcman_pagedata[cluster * device->pages_per_cluster + page] =
              cache->data + page * device->page_size;
      }
    }
    result = mcman_eraseblock(port, slot, first, mcman_pagedata, mcman_eccdata);
    if (result != 0)
      return result;
    {
      u_short pages_per_block;
      asm("" : "=r"(result) : "0"(result));
      page = 0;
      pages_per_block = device->pages_per_block;
      if (result < device->pages_per_block) {
        do {
          result =
              McWritePage(port, slot, first * (short)pages_per_block + page, mcman_pagedata[page],
                          mcman_eccdata + (device->page_size / 32) * page);
          if (result != 0)
            return result;
          pages_per_block = device->pages_per_block;
        } while (++page < device->pages_per_block);
      }
    }
    for (cluster = 0; cluster < device->clusters_per_block; cluster++)
      mcman_freecluster(port, slot, device->backup_block1 * device->clusters_per_block + cluster);
  }
  return mcman_eraseblock(port, slot, device->backup_block2, 0, 0);
}

int mcman_unformat2(int port, int slot) {
  McDeviceInfo* device = &mcman_devices[port][slot];
  int blocks = (int)device->clusters_per_card / device->clusters_per_block;
  int bytes = device->page_size;
  u_int words = (u_int)bytes >> 2;
  u_int fill;
  int block, page, index, first_page, status, count, result, threshold;
  u_int* data;

  fill = -1;
  if (!(device->card_flags & 16))
    fill = 0;
  index = 0;
  if (words) {
    data = mcman_pagebuf.word;
    do {
      *data++ = fill;
    } while (++index < (int)words);
  }
  index = 0;
  data = (u_int*)mcman_eccdata;
  for (; (u_int)index < 128; index++)
    *data++ = fill;
  threshold = device->clusters_per_block * 64;
  for (block = 1; block < blocks; block++) {
    first_page = block * device->pages_per_block;
    if (device->formatted > 0) {
      for (status = 0; status < 16; status++) {
        if (device->bad_blocks[status] < 1) {
          status = 16;
          break;
        }
        if (device->bad_blocks[status] == block)
          break;
      }
    } else {
      count = 0;
      status = -1;
      for (page = 0; page < device->pages_per_block; page++) {
        result = McReadPage(port, slot, first_page + page, mcman_pagebuf.word);
        if (result == -2) {
          status = -2;
          break;
        }
        if (result != 0)
          return -42;
        if (!(device->card_flags & 1)) {
          for (index = 0; index < device->page_size; index++) {
            if (((u_char*)mcman_pagebuf.word)[index] != (fill & 255))
              count++;
            if (count >= threshold) {
              status = 16;
              break;
            }
          }
          if (status != -1)
            break;
        }
      }
      if ((device->card_flags & 1) && status == -1)
        status = 16;
    }
    if (status == 16) {
      result = mcman_eraseblock(port, slot, block, 0, 0);
      if (result != 0)
        return -43;
    } else {
      index = 0;
      if (words) {
        data = mcman_pagebuf.word;
        do {
          *data++ = fill;
        } while (++index < (int)words);
      }
      page = 0;
      if (page < device->pages_per_block) {
        void* ecc = mcman_eccdata;
        status = -8;
        do {
          result = McWritePage(port, slot, page + first_page, mcman_pagebuf.word, ecc);
          if (result != 0 && result != status)
            return -44;
        } while (++page < device->pages_per_block);
      }
    }
  }
  if (mcman_eraseblock(port, slot, 0, 0, 0) != 0)
    return -45;
  return 0;
}

int McCheckBlock(int port, int slot, int block) {
  int r, i, j, ecc_count, page, words, flag, fill;
  McDeviceInfo* device = &mcman_devices[port][slot];
  u_char *p_page, *p_ecc;

  int page_size = device->page_size;
  page = block * device->pages_per_block;
  words = (u_int)page_size >> 2;

  ecc_count = page_size / 128;

  if (device->formatted > 0) {
    for (flag = 0; flag < 16; flag++) {
      if (device->bad_blocks[flag] <= 0)
        break;
      if (device->bad_blocks[flag] == block)
        goto finish;
    }
  }

  flag = 16;

  for (i = 0; i < device->pages_per_block; i++) {
    r = mcman_readpage(port, slot, i + page, &mcman_pagebuf, mcman_eccdata);
    if (r != 0)
      return -45;

    if ((mcman_devices[port][slot].card_flags & 1) != 0) {
      if (mcman_eccdata[mcman_spare_size(port, slot) - 1] != 0xff) {
        j = 0;
        if (ecc_count > 0) {
          p_ecc = (void*)mcman_eccdata;
          p_page = mcman_pagebuf.byte;
          do {
            r = mcman_correctECC(p_page, p_ecc);
            if (r != 0) {
              flag = -1;
              goto finish;
            }
            p_ecc = (void*)((u_char*)p_ecc + 3);
            p_page = (void*)((u_char*)p_page + 128);
          } while (++j < ecc_count);
        }
      }
    }
  }

  for (j = 0; j < words; j++)
    *(u_int*)&mcman_pagebuf.byte[j * 4] = 0x5a5aa5a5;

  for (j = 0; (u_int)j < 128; j++)
    *((u_int*)&mcman_eccdata + j) = 0x5a5aa5a5;

  r = mcman_eraseblock(port, slot, block, 0, 0);
  if (r != 0) {
    r = mcman_probePS2Card2(port, slot);
    if (r != 0)
      return -41;
    flag = -1;
    goto finish;
  }

  for (i = 0; i < device->pages_per_block; i++) {
    r = McWritePage(port, slot, i + page, &mcman_pagebuf, mcman_eccdata);
    if (r != 0) {
      r = mcman_probePS2Card2(port, slot);
      if (r != 0)
        return -44;
      flag = -1;
      goto finish;
    }
  }

  for (i = 0; i < device->pages_per_block; i++) {
    r = mcman_readpage(port, slot, i + page, &mcman_pagebuf, mcman_eccdata);
    if (r != 0)
      return -45;

    j = 0;
    if (words)
      do {
        if (*(u_int*)&mcman_pagebuf.byte[j * 4] != 0x5a5aa5a5) {
          flag = -1;
          goto finish;
        }
      } while (++j < words);

    for (j = 0; (u_int)j < 128; j++) {
      if (*((u_int*)&mcman_eccdata + j) != 0x5a5aa5a5) {
        flag = -1;
        goto finish;
      }
    }
  }

  for (j = 0; j < words; j++)
    *(u_int*)&mcman_pagebuf.byte[j * 4] = 0xa5a55a5a;

  for (j = 0; (u_int)j < 128; j++)
    *((u_int*)&mcman_eccdata + j) = 0xa5a55a5a;

  r = mcman_eraseblock(port, slot, block, 0, 0);
  if (r != 0) {
    r = mcman_probePS2Card2(port, slot);
    if (r != 0)
      return -42;
    flag = -1;
    goto finish;
  }

  for (i = 0; i < device->pages_per_block; i++) {
    r = McWritePage(port, slot, i + page, &mcman_pagebuf, mcman_eccdata);
    if (r != 0) {
      r = mcman_probePS2Card2(port, slot);
      if (r != 0)
        return -46;
      flag = -1;
      goto finish;
    }
  }

  for (i = 0; i < device->pages_per_block; i++) {
    r = mcman_readpage(port, slot, i + page, &mcman_pagebuf, mcman_eccdata);
    if (r != 0)
      return -47;

    j = 0;
    if (words)
      do {
        if (*(u_int*)&mcman_pagebuf.byte[j * 4] != 0xa5a55a5a) {
          flag = -1;
          goto finish;
        }
      } while (++j < words);

    for (j = 0; (u_int)j < 128; j++) {
      if (*((u_int*)&mcman_eccdata + j) != 0xa5a55a5a) {
        flag = -1;
        goto finish;
      }
    }
  }

finish:
  if (flag == 16) {
    mcman_eraseblock(port, slot, block, 0, 0);
    return 0;
  }

  fill = -1;
  if (!(device->card_flags & 16))
    fill = 0;

  for (j = 0; j < words; j++)
    *(u_int*)&mcman_pagebuf.byte[j * 4] = fill;

  for (j = 0; (u_int)j < 128; j++)
    *((u_int*)&mcman_eccdata + j) = fill;

  for (i = 0; i < device->pages_per_block; i++) {
    r = McWritePage(port, slot, i + page, &mcman_pagebuf, mcman_eccdata);
    if (r != 0) {
      r = mcman_probePS2Card2(port, slot);
      if (r != 0)
        return -48;
    }
  }

  return 1;
}

int mcman_remove_tree(int port, int slot, int cluster, int index, int count) {
  struct McFsEntry* entry;
  int next, result, length, position;
  int original = cluster;
  int original_index = index;
  if (count == 0) {
    result = McReadDirEntry(port, slot, cluster, index, &entry);
    if (result != 0)
      return result;
    if (!(entry->mode & 32))
      goto remove_entry;
    next = entry->cluster;
    length = entry->length;
  } else {
    next = cluster;
    length = count;
  }
  result = McGetFATentry(port, slot, next, &next);
  if (result != 0)
    return result;
  position = 2;
  while (next < 0 && position < length) {
    result = McReadDirEntry(port, slot, next, 0, &entry);
    if (result != 0)
      return result;
    if (entry->mode & 0x8000)
      mcman_remove_tree(port, slot, next, 0, 0);
    position++;
    if (position >= length)
      break;
    result = McReadDirEntry(port, slot, next, 1, &entry);
    if (result != 0)
      return result;
    if (entry->mode & 0x8000)
      mcman_remove_tree(port, slot, next, 1, 0);
    result = McGetFATentry(port, slot, next, &next);
    if (result != 0)
      return result;
  }
  if (count != 0)
    return 0;
remove_entry:
  return McSetDirEntryState(port, slot, original, original_index, 0);
}

extern struct McFsEntry* mcman_rename_entry;
extern struct McFsEntry mcman_rename_source, mcman_rename_target;

int mcman_rename2(int port, int slot, const char* source, const char* target) {
  McDeviceInfo* device;
  struct McDirectoryCache from, to;
  struct McDirectoryEntry info;
  int source_result, target_result, result, cluster, slash, index;
  int fat_entry;
  if (*(const u_char*)source == 0 || *(const u_char*)target == 0)
    return -4;
  from.maximum_entries = -1;
  to.maximum_entries = -1;
  mcman_rename_entry = 0;
  device = &mcman_devices[port][slot];
  source_result = mcman_cachedirentry(port, slot, source, &from, &mcman_rename_entry, 1);
  if (source_result < 0)
    return source_result;
  if (mcman_rename_entry)
    memcpy(&mcman_rename_source, mcman_rename_entry, sizeof(mcman_rename_source));
  mcman_rename_entry = 0;
  target_result = mcman_cachedirentry(port, slot, target, &to, &mcman_rename_entry, 1);
  if (target_result < 0)
    return target_result;
  if (mcman_rename_entry)
    memcpy(&mcman_rename_target, mcman_rename_entry, sizeof(mcman_rename_target));
  if ((u_int)(source_result - 1) < 2 || target_result == 2)
    return -4;
  if (source_result == 3 || target_result == 3)
    return -5;
  if (target_result == 0) {
    if (from.cluster != to.cluster)
      return -4;
  } else {
    result = McReadDirEntry(port, slot, to.cluster, to.entry_index, &mcman_rename_entry);
    if (result != 0)
      return result;
    if (from.cluster != mcman_rename_entry->cluster)
      return -4;
  }
  if (target_result == 1) {
    index = -1;
    for (;;) {
      slash = mcman_chrpos(target + index + 1, '/');
      if (slash < 0)
        break;
      index += slash + 1;
    }
    strcpy(info.name, target + index + 1);
    result = mcman_setinfo2(port, slot, source, &info, 16);
    if (result != 0)
      return result;
    return McFlushCache(port, slot);
  }
  if (from.entry_index == to.entry_index)
    return -4;
  if ((mcman_rename_source.mode & 32) != (mcman_rename_target.mode & 32))
    return -6;
  if (mcman_rename_source.mode & 32) {
    for (index = 2; index < (int)mcman_rename_target.length; index++) {
      result = McReadDirEntry(port, slot, mcman_rename_target.cluster, index, &mcman_rename_entry);
      if (result != 0)
        return result;
      if (mcman_rename_entry->mode & 0x8000)
        return -6;
    }
  }
  for (index = 0; index < 3; index++) {
    if (mcman_handles[index].status && mcman_handles[index].port == port &&
        mcman_handles[index].slot == slot && mcman_handles[index].cluster == (u_int)to.cluster &&
        mcman_handles[index].entry_index == (u_int)to.entry_index)
      return -5;
  }
  if (!mcman_allow_ps1 && !(mcman_rename_target.mode & 2))
    return -5;
  McReadDirEntry(port, slot, from.cluster, from.entry_index, &mcman_rename_entry);
  if (source_result < 0)
    return source_result;
  if (source_result > 0)
    return -4;
  mcman_rename_entry->mode &= 0x7fff;
  Mc1stCacheEntSetWrFlagOff();
  target_result = McReadDirEntry(port, slot, to.cluster, to.entry_index, &mcman_rename_entry);
  if (target_result < 0)
    return target_result;
  if (target_result > 0)
    return -4;
  memcpy(mcman_rename_entry, &mcman_rename_source, sizeof(mcman_rename_source));
  memcpy(mcman_rename_entry->name, mcman_rename_target.name, sizeof(mcman_rename_target.name));
  Mc1stCacheEntSetWrFlagOff();
  if (mcman_rename_source.mode & 32) {
    source_result = McReadDirEntry(port, slot, mcman_rename_source.cluster, 0, &mcman_rename_entry);
    if (source_result < 0)
      return source_result;
    mcman_rename_entry->parent_entry = to.entry_index;
    Mc1stCacheEntSetWrFlagOff();
  }
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  cluster = mcman_rename_target.cluster;
  if (cluster < 0)
    return 0;
  if (cluster < (int)device->unknown[1])
    device->unknown[1] = cluster;
  device->unknown[5] = -1;
  do {
    result = McGetFATentry(port, slot, cluster, &fat_entry);
    if (result != 0)
      return result;
    fat_entry &= 0x7fffffff;
    if (cluster < (int)device->unknown[1])
      device->unknown[1] = fat_entry;
    result = McSetFATentry(port, slot, cluster, fat_entry);
    if (result != 0)
      return result;
    cluster = fat_entry & 0x7fffffff;
    device->free_clusters++;
  } while (cluster != 0x7fffffff);
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  return 0;
}
