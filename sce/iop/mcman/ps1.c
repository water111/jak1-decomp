#include "sce/iop/mcman/cache.h"
#include "sce/iop/mcman/device.h"
#include "sce/iop/mcman/directory.h"
#include "sce/iop/mcman/filesystem.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/transport.h"
#include "sce/iop/sio2man/sio2man.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thsemap/thsemap.h"
#include "sce/iop/types.h"

#if MCMAN_VERSION < 0x0220
static u_char mcman_ps1_commands[][2] = {{0x53, 10}, {0x52, 140}, {0x57, 138}, {0x58, 5}};
#endif

extern struct Sio2Transfer mcman_ps1_packet;
extern u_char mcman_ps1_tx[144], mcman_ps1_rx[144];
extern int mcman_ps1_semaphore;
extern struct SysClock mcman_ps1_delay;

#if MCMAN_VERSION >= 0x0220
void mcman_begin_ps1_transfer(int port, int slot) {
  mcman_begin_transfer(port, slot);
  DelayThread(300);
}
#else
#define mcman_begin_ps1_transfer mcman_begin_transfer
#endif

void mcman_initPS1PDAcom(void) {
  int port;
  struct SemaParam semaphore;
  memset(&mcman_ps1_packet, 0, sizeof(mcman_ps1_packet));
  for (port = 0; port < 4; port++) {
    struct McSend1 {
      u_char byte0, byte1, byte2, byte3;
    }* send1 = (void*)&mcman_ps1_packet.send1[port];

    union McSend2 {
      u_int word;

      struct {
        u_short half0;
        u_char byte2, byte3;
      } fields;
    }* send2 = (void*)&mcman_ps1_packet.send2[port];

    send1->byte0 = 5;
    send1->byte1 = 5;
    send1->byte2 = 0xc0;
    send1->byte3 = 0xff;
    send2->fields.byte2 = 2;
    send2->fields.half0 = 500;
    send2->word &= ~0x03000000;
  }
  mcman_ps1_packet.input = mcman_ps1_tx;
  mcman_ps1_packet.output = mcman_ps1_rx;
  semaphore.attr = SA_THPRI;
  semaphore.initCount = 1;
  semaphore.maxCount = 1;
  semaphore.option = 0;
  mcman_ps1_semaphore = CreateSema(&semaphore);
}

#if MCMAN_VERSION >= 0x0220
int mcman_shutdownPS1com(void) {
  int result = DeleteSema(mcman_ps1_semaphore);
  return result == 0 || result == -408;
}
#endif

unsigned int mcman_ps1_alarm(void* argument) {
  iSignalSema(mcman_ps1_semaphore);
  return 0;
}

int McWritePS1PDACard(int port, int slot, int page, const void* buffer) {
  int i;
  int command = 0x02288a40;
  command |= (((port & 1) + 2) & 3);
  *(volatile u_int*)&mcman_ps1_packet.send3[0] = 0;
  mcman_ps1_packet.send3[0] = command;
  mcman_ps1_packet.send3[1] = 0;
  mcman_ps1_packet.input_size = 138;
  mcman_ps1_packet.output_size = 138;
  for (i = 0; i < 20; i++) {
    if (mcman_devices[port][slot].bad_blocks[i] == page)
      page = i + 36;
  }
  mcman_ps1_tx[0] = 0x81;
  mcman_ps1_tx[1] = 0x57;
  mcman_ps1_tx[4] = page >> 8;
  mcman_ps1_tx[5] = page;
  for (i = 0; i < 128; i++)
    mcman_ps1_tx[6 + i] = ((const u_char*)buffer)[i];
  mcman_ps1_tx[134] = mcman_calcEDC(mcman_ps1_tx + 4, 130);
  for (i = 0; i < 5; i++) {
    WaitSema(mcman_ps1_semaphore);
    mcman_begin_ps1_transfer(port, slot);
    sio2_transfer_legacy(&mcman_ps1_packet);
    mcman_end_transfer();
    USec2SysClock(20000, &mcman_ps1_delay);
    SetAlarm(&mcman_ps1_delay, mcman_ps1_alarm, 0);
    if ((mcman_ps1_packet.recv1 & 0xf000) == 0x1000 && mcman_ps1_rx[2] == 0x5a &&
        mcman_ps1_rx[3] == 0x5d && mcman_ps1_rx[137] == 0x47)
      break;
  }
  if (i == 5)
    return -4;
  if (mcman_ps1_rx[1] == 0 || mcman_ps1_rx[1] == 8)
    return 0;
  return -8;
}

int McReadPS1PDACard(int port, int slot, int page, void* buffer) {
  int i;
  int command = 0x02308c40;
  command |= (((port & 1) + 2) & 3);
  *(volatile u_int*)&mcman_ps1_packet.send3[0] = 0;
  mcman_ps1_packet.send3[0] = command;
  mcman_ps1_packet.send3[1] = 0;
  mcman_ps1_packet.input_size = 140;
  mcman_ps1_packet.output_size = 140;
  for (i = 0; i < 20; i++) {
    if (mcman_devices[port][slot].bad_blocks[i] == page)
      page = i + 36;
  }
  mcman_ps1_tx[0] = 0x81;
  mcman_ps1_tx[1] = 0x52;
  mcman_ps1_tx[4] = page >> 8;
  mcman_ps1_tx[5] = page;
  for (i = 0; i < 5; i++) {
    WaitSema(mcman_ps1_semaphore);
    mcman_begin_ps1_transfer(port, slot);
    sio2_transfer_legacy(&mcman_ps1_packet);
    mcman_end_transfer();
    USec2SysClock(10000, &mcman_ps1_delay);
    SetAlarm(&mcman_ps1_delay, mcman_ps1_alarm, 0);
    if ((mcman_ps1_packet.recv1 & 0xf000) == 0x1000 && mcman_ps1_rx[2] == 0x5a &&
        mcman_ps1_rx[3] == 0x5d && mcman_ps1_rx[4] == 0 && mcman_ps1_rx[6] == 0x5c &&
        mcman_ps1_rx[7] == 0x5d && mcman_ps1_rx[139] == 0x47 &&
        mcman_ps1_rx[138] == (u_char)mcman_calcEDC(mcman_ps1_rx + 8, 130))
      break;
  }
  if (i == 5)
    return -4;
  for (i = 0; i < 128; i++)
    ((u_char*)buffer)[i] = mcman_ps1_rx[10 + i];
  if (mcman_ps1_rx[1] == 0 || mcman_ps1_rx[1] == 8)
    return 0;
  return -5;
}

int mcman_probePS1Card2(int port, int slot) {
  int retries;
  int command = 0xc0340;
  command |= (((port & 1) + 2) & 3);
  *(volatile u_int*)&mcman_ps1_packet.send3[0] = 0;
  mcman_ps1_packet.send3[1] = 0;
  mcman_ps1_packet.send3[0] = command;
  mcman_ps1_packet.input_size = 3;
  mcman_ps1_packet.output_size = 3;
  mcman_ps1_tx[0] = 0x81;
  mcman_ps1_tx[1] = 0x52;
  for (retries = 0; retries < 5; retries++) {
    WaitSema(mcman_ps1_semaphore);
    mcman_begin_ps1_transfer(port, slot);
    sio2_transfer_legacy(&mcman_ps1_packet);
    mcman_end_transfer();
    USec2SysClock(100, &mcman_ps1_delay);
    SetAlarm(&mcman_ps1_delay, mcman_ps1_alarm, 0);
    if ((mcman_ps1_packet.recv1 & 0xf000) == 0x1000 && mcman_ps1_rx[2] == 0x5a)
      break;
  }
  if (retries == 5)
    return -12;
  if (mcman_ps1_rx[1] == 0) {
    if (mcman_devices[port][slot].formatted > 0)
      return 0;
    if (mcman_devices[port][slot].formatted < 0)
      return -2;
  } else if (mcman_ps1_rx[1] != 8)
    return -14;
  return -13;
}

int mcman_probePS1Card(int port, int slot) {
  int i, result;
  u_int* p;
  int command = 0xc0340;
  command |= (((port & 1) + 2) & 3);
  *(volatile u_int*)&mcman_ps1_packet.send3[0] = 0;
  mcman_ps1_packet.send3[1] = 0;
  mcman_ps1_packet.send3[0] = command;
  mcman_ps1_packet.input_size = 3;
  mcman_ps1_packet.output_size = 3;
  mcman_ps1_tx[0] = 0x81;
  mcman_ps1_tx[1] = 0x52;
  for (i = 0; i < 5; i++) {
    WaitSema(mcman_ps1_semaphore);
    mcman_begin_ps1_transfer(port, slot);
    sio2_transfer_legacy(&mcman_ps1_packet);
    mcman_end_transfer();
    USec2SysClock(100, &mcman_ps1_delay);
    SetAlarm(&mcman_ps1_delay, mcman_ps1_alarm, 0);
    if ((mcman_ps1_packet.recv1 & 0xf000) == 0x1000 && mcman_ps1_rx[2] == 0x5a)
      break;
  }
  if (i == 5)
    return -11;
  if (mcman_ps1_rx[1] == 0) {
    if (mcman_devices[port][slot].formatted != 0)
      return 0;
  } else if (mcman_ps1_rx[1] != 8)
    return -15;
  mcman_clearcache(port, slot);
  i = 31;
  p = (u_int*)&mcman_ps1_rx[124];
  for (; i >= 0; i--)
    *p-- = 0;
  result = McWritePS1PDACard(port, slot, 63, mcman_ps1_rx);
  if (result < 0)
    return -13;
  result = mcman_setPS1devinfos(port, slot);
  if (result == 0)
    return -1;
  if (result == -2)
    mcman_devices[port][slot].formatted = -2;
  return result;
}

int mcman_probePDACard(int port, int slot) {
  int retries;
  int command = 0x140540;
  command |= (((port & 1) + 2) & 3);
  *(volatile u_int*)&mcman_ps1_packet.send3[0] = 0;
  mcman_ps1_packet.send3[0] = command;
  mcman_ps1_packet.input_size = 5;
  mcman_ps1_packet.output_size = 5;
  mcman_ps1_packet.send3[1] = 0;
  mcman_ps1_tx[0] = 0x81;
  mcman_ps1_tx[1] = 0x58;
  for (retries = 0; retries < 5; retries++) {
    mcman_begin_ps1_transfer(port, slot);
    sio2_transfer_legacy(&mcman_ps1_packet);
    mcman_end_transfer();
    if ((mcman_ps1_packet.recv1 & 0xf000) == 0x1000)
      break;
  }
  if (retries == 5)
    return -11;
  return 0;
}

extern u_char mcman_ps1_rx[144];
extern u_char mcman_ps1_page[128];

int mcman_setPS1devinfos(int port, int slot) {
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  int i, result;
  memset(device, 0, sizeof(*device));
  memset(device->bad_blocks, -1, sizeof(device->bad_blocks));
  device->page_size = 128;
  device->pages_per_block = 128;
  device->pages_per_cluster = 64;
  *(u_short*)&device->reserved = 0xff00;
  device->cluster_size = 8192;
  device->fat_entries_per_cluster = 2048;
  device->clusters_per_card = 16;
  device->card_type = 1;
  device->clusters_per_block = 0;
  device->formatted = 0;
  device->free_clusters = -1;
  result = McReadPS1PDACard(port, slot, 0, mcman_ps1_page);
  if (result < 0)
    return -14;
  if (mcman_ps1_rx[1] != 0)
    return -15;
  if (mcman_ps1_page[0] != 'M' || mcman_ps1_page[1] != 'C')
    return -2;
  for (i = 0; i < 20; i++) {
    result = McReadPS1PDACard(port, slot, i + 16, mcman_ps1_page);
    if (result != 0)
      return -43;
    if (mcman_ps1_page[127] == (u_char)mcman_calcEDC(mcman_ps1_page, 127))
      device->bad_blocks[i] = *(int*)mcman_ps1_page;
  }
  result = mcman_cachePS1dirs(port, slot);
  if (result != 0)
    return result;
  device->formatted = 1;
  return 0;
}

extern u_char mcman_ps1_page[128];

int mcman_format1(int port, int slot) {
  int i, result;
  u_char* buffer;
  struct McDeviceInfo* device;
  mcman_invhandles(port, slot);
  device = &mcman_devices[port][slot];
  for (i = 0; i < 56; i++) {
    result = mcman_PS1pagetest(port, slot, i);
    if (result == 0)
      return -2;
    if (result != 1)
      return -41;
  }
  buffer = mcman_ps1_page;
  memset(buffer, 0, 128);
  ((struct McPs1Entry*)buffer)->mode = 0xa0;
  ((struct McPs1Entry*)buffer)->next_block = -1;
  buffer[127] = mcman_calcEDC(buffer, 127);
  i = 0;
  do {
    int next = i + 1;
    result = McWritePS1PDACard(port, slot, next, buffer);
    i = next;
    if (result != 0)
      return -42;
  } while (i < 15);
  memset(mcman_ps1_page, 0, 128);
  for (i = 0; i < 20; i++) {
    *(int*)mcman_ps1_page = device->bad_blocks[i];
    mcman_ps1_page[127] = mcman_calcEDC(mcman_ps1_page, 127);
    result = McWritePS1PDACard(port, slot, i + 16, mcman_ps1_page);
    if (result != 0)
      return -43;
  }
  mcman_wmemset(mcman_ps1_page, 128, 0);
  mcman_ps1_page[0] = 'M';
  mcman_ps1_page[1] = 'C';
  mcman_ps1_page[127] = 14;
  result = McWritePS1PDACard(port, slot, 0, mcman_ps1_page);
  if (result != 0)
    return -44;
  device->formatted = 1;
#if MCMAN_VERSION >= 0x0220
  device->free_clusters = -1;
#endif
  return 0;
}

#define MC_OPEN_READ 1
#define MC_OPEN_WRITE 2
#define MC_OPEN_CREATE 0x200
#define MC_OPEN_PDA_EXEC 0x800

int mcman_open1(int port, int slot, const char* name, int flags) {
  int result, existing, i, fd, cluster, index;
  struct McFileHandle* handle;
  struct McPs1Entry* entry;
  struct McCacheEntry* cache;
  if (flags & MC_OPEN_CREATE)
    flags |= MC_OPEN_WRITE;
  for (fd = 0; fd < MCMAN_HANDLES; fd++) {
    if (mcman_handles[fd].status == 0)
      break;
  }
  if (fd == MCMAN_HANDLES)
    return -7;
  handle = &mcman_handles[fd];
  mcman_wmemset(handle, sizeof(*handle), 0);
  handle->port = port;
  handle->slot = slot;
  if (*(const u_char*)name == '/')
    name++;
  if (*(const u_char*)name == 0 || (*(const u_char*)name == '.' && ((const u_char*)name)[1] == 0)) {
    handle->size = 15;
    handle->directory_flag = 1;
    handle->status = 1;
    return fd;
  }
  existing = mcman_lookup1(port, slot, name, &entry, 1);
  if ((entry->mode & 0xf0) == 0xa0)
    existing = -4;
  handle->free_link = existing;
  if (existing >= 0) {
    if (entry->transient != 0)
      handle->size = entry->unknown4;
    else
      handle->size = entry->length;
  } else
    handle->size = 0;
  handle->read_flag = flags & MC_OPEN_READ;
  handle->write_flag = flags & MC_OPEN_WRITE;
  handle->unknown1 = 0;
  if (!(flags & MC_OPEN_CREATE)) {
    if (existing < 0)
      return -4;
    handle->status = 1;
    return fd;
  }
  index = 0;
  if (flags & MC_OPEN_WRITE) {
    for (; index < MCMAN_HANDLES; index++) {
      if (mcman_handles[index].status && mcman_handles[index].port == port &&
          mcman_handles[index].slot == slot && mcman_handles[index].free_link == (u_int)existing &&
          mcman_handles[index].write_flag)
        return -5;
    }
  }
  if (existing >= 0) {
    result = mcman_setPS1FileState(port, slot, existing, 0);
    if (result != 0)
      return result;
  }
  cluster = -1;
  for (i = 0; i < 15; i++) {
    result = mcman_readdirentryPS1(port, slot, i, &entry);
    if (result != 0)
      return result;
    if ((entry->mode & 0xf0) == 0xa0) {
      if (cluster < 0)
        cluster = i;
      if (entry->mode != 0xa0) {
        mcman_wmemset(entry, sizeof(*entry), 0);
        entry->mode = 0xa0;
        entry->next_block = -1;
        entry->checksum = mcman_calcEDC(entry, 127);
        cache = mcman_get1stcacheEntp();
        cache->dirty |= 1 << ((i + 1) % 8);
      }
    }
  }
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  if (cluster < 0)
    return -3;
  mcman_devices[port][slot].free_clusters--;
  result = mcman_readdirentryPS1(port, slot, cluster, &entry);
  if (result != 0)
    return result;
  mcman_wmemset(entry, sizeof(*entry), 0);
  entry->mode = 0x51;
  entry->length = 0;
  entry->next_block = -1;
  strncpy(entry->name, name, 20);
  if (flags & MC_OPEN_PDA_EXEC)
    entry->unknown5 = 1;
  entry->checksum = mcman_calcEDC(entry, 127);
  handle->free_link = cluster;
  cache = mcman_get1stcacheEntp();
  cache->dirty |= 1 << ((cluster + 1) % 8);
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  handle->unknown2 = 1;
  handle->status = 1;
  return fd;
}

int mcman_read1(int fd, void* buffer, int count) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  struct McCacheEntry* cache;
  int done;
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
      offset = (int)handle->position % 1024;
      available = 1024 - offset;
      if (available < count)
        size = available;
      else
        size = count;
      result = mcman_fatRseekPS1(fd);
      if (result < 0)
        return result;
      result = mcman_readclusterPS1(port, slot, result, &cache);
      if (result != 0)
        return result;
      memcpy((u_char*)buffer + done, cache->data + offset, size);
      done += size;
      count -= size;
      handle->position += size;
    } while (count != 0);
  }
  return done;
}

int mcman_write1(int fd, void* buffer, int count) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  struct McCacheEntry* cache;
  int result, done;
  if (count != 0 && handle->unknown2 == 0) {
    handle->unknown2 = 1;
    result = mcman_close1(fd);
    if (result != 0)
      return result;
    result = McFlushCache(port, slot);
    if (result != 0)
      return result;
  }
  done = 0;
  if (count != 0) {
    do {
      int size, offset, available;
      int cluster;
      cluster = mcman_fatRseekPS1(fd);
      if (cluster == -3) {
        int next = mcman_fatWseekPS1(fd);
        if (next == cluster)
          return cluster;
        if (next != 0)
          return next;
        cluster = mcman_fatRseekPS1(fd);
      } else if (cluster < 0)
        return cluster;
      result = mcman_readclusterPS1(port, slot, cluster, &cache);
      if (result != 0)
        return result;
      offset = (int)handle->position % 1024;
      available = 1024 - offset;
      if (available < count)
        size = available;
      else
        size = count;
      memcpy(cache->data + offset, (u_char*)buffer + done, size);
      cache->dirty = -1;
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
  mcman_close1(fd);
  return done;
}

int mcman_lookup1(int port, int slot, const char* name, struct McPs1Entry** entry, int mode) {
  int i, result;
  if (*(const u_char*)name == '/')
    name++;
  if (mcman_chrpos(name, '/') >= 0)
    return -4;
  if (!mcman_checkpath(name))
    return -4;
  for (i = 0; i < 15; i++) {
    result = mcman_readdirentryPS1(port, slot, i, entry);
    if (result != 0)
      return result;
    if (mode != 0) {
      if ((*entry)->mode != 0x51)
        continue;
    } else if ((*entry)->mode != 0xa1)
      continue;
    if (strcmp(name, (*entry)->name) == 0)
      return i;
  }
  return -4;
}

extern int mcman_ps1_directory_cluster;
extern char mcman_ps1_directory_name[64];

int mcman_dread1(int fd, struct McIoDirEntry* output) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = mcman_handles[fd].port, slot = mcman_handles[fd].slot;
  struct McPs1Entry* entry;
  int result;
  while ((int)handle->position < (int)handle->size) {
    result = mcman_readdirentryPS1(port, slot, handle->position, &entry);
    if (result != 0)
      return result;
    if (entry->mode == 0x51)
      break;
    handle->position++;
  }
  if ((int)handle->position >= (int)handle->size)
    return 0;
  handle->position++;
  mcman_wmemset(output, sizeof(*output), 0);
  strncpy(output->name, entry->name, 20);
  output->name[20] = 0;
  output->stat.mode = 0x101f;
  if (entry->unknown5 != 0)
    output->stat.mode = 0x181f;
  if (entry->transient != 0) {
    output->stat.created = entry->created;
    output->stat.modified = entry->modified;
    output->stat.size = entry->unknown4;
    output->stat.attributes = entry->unknown2;
  } else
    output->stat.size = entry->length;
  return 1;
}

int mcman_getstat1(int port, int slot, const char* name, struct McIoStat* output) {
  struct McPs1Entry* entry;
  int result = mcman_lookup1(port, slot, name, &entry, 1);
  if ((entry->mode & 0xf0) == 0xa0)
    result = -4;
  if (result < 0)
    return result;
  mcman_wmemset(output, sizeof(*output), 0);
  output->mode = 0x1f;
  if (entry->transient != 0) {
    if (entry->unknown3 & 0x80)
      output->mode |= 0x80;
    output->created = entry->created;
    output->modified = entry->modified;
    output->size = entry->unknown4;
    output->attributes = entry->unknown2;
  } else
    output->size = entry->length;
  return 0;
}

int mcman_setinfo1(int port, int slot, const char* name, struct McDirectoryEntry* info, int flags) {
  int result, status;
  struct McPs1Entry *other, *entry;
  struct McCacheEntry* cache;
  status = 0;
  if (flags & 0x10) {
    result = mcman_lookup1(port, slot, info->name, &other, 1);
    if (result < 0) {
      if (result != -4)
        status = result;
      else if (strcmp(".", info->name) == 0 || strcmp("..", info->name) == 0 ||
               *(u_char*)info->name == 0 || mcman_chrpos(info->name, '/') >= 0 ||
               !mcman_checkpath(info->name))
        status = -4;
    } else
      status = -4;
  }
  result = mcman_lookup1(port, slot, name, &entry, 1);
  if ((entry->mode & 0xf0) == 0xa0)
    result = -4;
  if (result < 0)
    return result;
  if (status != 0)
    return status;
  cache = mcman_get1stcacheEntp();
  cache->dirty |= 1 << ((result + 1) % 8);
  entry->transient = 0;
  entry->unknown3 = 0;
  flags &= -12;
  if (flags & 4) {
    if (info->attributes & 0x800)
      entry->unknown5 = 1;
    else
      entry->unknown5 = 0;
  }
  if (flags & 8)
    entry->unknown2 = info->application_data;
  if (flags & 1)
    entry->created = info->created;
  if (flags & 2)
    entry->modified = info->modified;
  if (flags & 0x10) {
    strncpy(entry->name, info->name, 20);
    entry->unknown1[0] = 0;
  }
  entry->checksum = mcman_calcEDC(entry, 127);
  return McFlushCache(port, slot);
}

extern int mcman_ps1_directory_cluster;
extern char mcman_ps1_directory_name[64];

int mcman_getdir1(int port,
                  int slot,
                  const char* name,
                  int flags,
                  int count,
                  struct McDirectoryEntry* output) {
  int result;
  int done;
  struct McPs1Entry* entry;
  flags &= 0xffff;
  done = 0;
  if (flags == 0) {
    mcman_ps1_directory_cluster = 0;
    while (mcman_chrpos(name, '/') >= 0)
      name += mcman_chrpos(name, '/') + 1;
    strncpy(mcman_ps1_directory_name, name, 63);
    mcman_ps1_directory_name[63] = 0;
  }
  while (count != 0) {
    if (mcman_ps1_directory_cluster >= 15)
      break;
    result = mcman_readdirentryPS1(port, slot, mcman_ps1_directory_cluster, &entry);
    if (result != 0)
      return result;
    mcman_ps1_directory_cluster++;
    if (entry->mode != 0x51)
      continue;
    if (!mcman_checkdirpath(entry->name, mcman_ps1_directory_name))
      continue;
#if MCMAN_VERSION >= 0x0220
    if (count < 0) {
      done++;
      continue;
    }
#endif
    memset(&output[done], 0, sizeof(struct McDirectoryEntry));
    output[done].attributes = 0x9417;
    if (entry->unknown5 != 0)
      output[done].attributes = 0x9c17;
    if (entry->transient != 0) {
      if (entry->unknown3 & 0x80)
        output[done].attributes |= 0x80;
      output[done].reserved = entry->reserved2;
      output[done].created = entry->created;
      output[done].modified = entry->modified;
      output[done].size = entry->unknown4;
      output[done].application_data = entry->unknown2;
      output[done].entry_index = mcman_ps1_directory_cluster;
    } else
      output[done].size = entry->length;
    strncpy(output[done].name, entry->name, 20);
    output[done].name[20] = 0;
    output[done].entry_index = mcman_ps1_directory_cluster;
    count--;
    done++;
  }
  return done;
}

int mcman_setPS1FileState(int port, int slot, int cluster, int flags) {
  int i, result;
  struct McPs1Entry* entry;
  struct McCacheEntry* cache;
  result = mcman_readdirentryPS1(port, slot, cluster, &entry);
  if (result != 0)
    return result;
  for (i = 0; i < MCMAN_HANDLES; i++) {
    if (mcman_handles[i].status != 0 && mcman_handles[i].port == port &&
        mcman_handles[i].slot == slot && mcman_handles[i].free_link == (u_int)cluster)
      return -5;
  }
  if (flags == 0) {
    if (entry->mode != 0x51)
      return -4;
    // needed for matching...
    asm("");
    goto valid;
  }
  if (entry->mode != 0xa1)
    return -4;
valid:
  do {
  next:
    cache = mcman_get1stcacheEntp();
    cache->dirty |= 1 << ((cluster + 1) % 8);
    {
      struct McPs1Entry* record = entry;
      int mode = record->mode & 15;
      if (flags != 0)
        mode |= 0x50;
      else
        mode |= 0xa0;
      *(u_int*)record = mode;
    }
    entry->checksum = mcman_calcEDC(entry, 127);
    cluster = entry->next_block;
    if (flags != 0)
      mcman_devices[port][slot].free_clusters--;
    else
      mcman_devices[port][slot].free_clusters++;
    if (cluster < 0)
      goto finished;
    if (mcman_readdirentryPS1(port, slot, cluster, &entry) != 0)
      return result;
    if (flags == 0) {
      if ((entry->mode & 0xf0) != 0x50)
        break;
      goto next;
    }
  } while ((entry->mode & 0xf0) == 0xa0);
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  return -4;
finished:
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  return 0;
}

int mcman_delete1(int port, int slot, const char* name, int flags) {
  struct McPs1Entry* entry;
  int cluster = mcman_lookup1(port, slot, name, &entry, flags == 0);
  if (cluster < 0)
    return cluster;
  return mcman_setPS1FileState(port, slot, cluster, flags);
}

int mcman_close1(int fd) {
  struct McFileHandle* handle = &mcman_handles[fd];
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  int result;
  struct McPs1Entry* entry;
  struct McCacheEntry* cache;
  result = mcman_readdirentryPS1(port, slot, handle->free_link, &entry);
  if (result != 0)
    return result;
  cache = mcman_get1stcacheEntp();
  cache->dirty |= 1 << (((int)handle->free_link + 1) % 8);
  if (handle->size == 0)
    entry->length = 0;
  else
    entry->length = (((int)handle->size - 1) / 8192 + 1) * 8192;
  {
    struct McPs1Entry* record = entry;
    record->transient = 0;
    record->unknown3 = 0;
  }
  entry->unknown4 = 0;
  memset(&entry->created, 0, sizeof(entry->created));
  memset(&entry->modified, 0, sizeof(entry->modified));
  entry->checksum = mcman_calcEDC(entry, 127);
  result = McFlushCache(port, slot);
  if (result != 0)
    return result;
  return 0;
}

int mcman_findfree1(int port, int slot, int reserve) {
  int i, count = 0, result;
  struct McPs1Entry* entry;
  for (i = 0; i < 15; i++) {
    result = mcman_readdirentryPS1(port, slot, i, &entry);
    if (result != 0)
      return result;
    if ((entry->mode & 0xf0) == 0xa0) {
      if (reserve != 0)
        return i;
      count++;
    }
  }
  if (reserve == 0) {
    mcman_devices[port][slot].free_clusters = count;
    if (count != 0)
      return count;
  }
  return -3;
}

int mcman_fatRseekPS1(int fd) {
  struct McFileHandle* handle;
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  struct McPs1Entry* entry;
  int links;
  int cluster;
  int result;
  handle = &mcman_handles[fd];
  links = (int)handle->position / device->cluster_size;
  cluster = handle->free_link;
  while (--links != -1) {
    if (cluster < 0)
      break;
    result = mcman_readdirentryPS1(port, slot, cluster, &entry);
    if (result != 0)
      return result;
    cluster = entry->next_block;
  }
  if (cluster < 0)
    return -3;
  return (cluster + 1) * 64 +
         ((int)handle->position % device->cluster_size) / 1024 * (1024 / device->page_size);
}

int mcman_fatWseekPS1(int fd) {
  struct McFileHandle* handle;
  int port = mcman_handles[fd].port;
  int slot = mcman_handles[fd].slot;
  struct McPs1Entry* entry;
  struct McDeviceInfo* device = &mcman_devices[port][slot];
  int links, cluster, result;
  handle = &mcman_handles[fd];
  links = (int)handle->position / device->cluster_size;
  cluster = handle->free_link;
  while (links != 0) {
    result = mcman_readdirentryPS1(port, slot, cluster, &entry);
    if (result != 0)
      return result;
    cluster = entry->next_block;
    if (cluster < 0) {
      cluster = mcman_allocPS1cluster(port, slot, handle);
      if (cluster < 0)
        return cluster;
    }
    links--;
  }
  return McFlushCache(port, slot);
}

int mcman_allocPS1cluster(int port, int slot, struct McFileHandle* handle) {
  int result, current, previous, free_cluster, cluster_size, pages_per_cluster;
  struct McPs1Entry *source, *destination, *prior;
  struct McCacheEntry* cache;
  cluster_size = mcman_devices[port][slot].cluster_size / 1024;
  pages_per_cluster = 1024 / mcman_devices[port][slot].page_size;
  free_cluster = mcman_findfree1(port, slot, 1);
  if (free_cluster < 0)
    return free_cluster;
  current = handle->free_link;
  previous = -1;
  while (current >= 0) {
    if (free_cluster < current) {
      int i;
      for (i = 0; i < cluster_size; i++) {
        mcman_freecluster(port, slot, (free_cluster * cluster_size + i) * pages_per_cluster);
        result = mcman_readclusterPS1(port, slot, (current * cluster_size + i) * pages_per_cluster,
                                      &cache);
        if (result != 0)
          return result;
        cache->dirty = -1;
        cache->cluster -= (current - free_cluster) * cluster_size;
      }
      result = mcman_readdirentryPS1(port, slot, current, &source);
      if (result != 0)
        return result;
      result = mcman_readdirentryPS1(port, slot, free_cluster, &destination);
      if (result != 0)
        return result;
      cache = mcman_get1stcacheEntp();
      cache->dirty |= 1 << ((free_cluster + 1) % 8);
      *destination = *source;
      if (previous >= 0) {
        result = mcman_readdirentryPS1(port, slot, previous, &prior);
        if (result != 0)
          return result;
        cache = mcman_get1stcacheEntp();
        cache->dirty |= 1 << ((previous + 1) % 8);
        prior->next_block = free_cluster;
        prior->checksum = mcman_calcEDC(prior, 127);
      } else {
        handle->free_link = free_cluster;
      }
      previous = free_cluster;
      free_cluster = current;
    } else {
      previous = current;
      result = mcman_readdirentryPS1(port, slot, previous, &source);
      if (result != 0)
        return result;
    }
    current = source->next_block;
  }
  result = mcman_readdirentryPS1(port, slot, free_cluster, &destination);
  if (result != 0)
    return result;
  cache = mcman_get1stcacheEntp();
  cache->dirty |= 1 << ((free_cluster + 1) % 8);
  mcman_wmemset(destination, sizeof(*destination), 0);
  destination->mode = 0x53;
  destination->next_block = -1;
  destination->checksum = mcman_calcEDC(destination, 127);
  mcman_devices[port][slot].free_clusters--;
  result = mcman_readdirentryPS1(port, slot, previous, &prior);
  if (result != 0)
    return result;
  cache = mcman_get1stcacheEntp();
  cache->dirty |= 1 << ((previous + 1) % 8);
  if (prior->mode == 0x53)
    prior->mode = 0x52;
  prior->next_block = free_cluster;
  prior->checksum = mcman_calcEDC(prior, 127);
  return free_cluster;
}

extern u_char mcman_ps1_page[128];

int mcman_PS1pagetest(int port, int slot, int page) {
  unsigned int i;
  int result;
  for (i = 0; i < 32; i++)
    ((u_int*)mcman_ps1_page)[i] = 0xffffffff;
  result = McWritePS1PDACard(port, slot, page, mcman_ps1_page);
  if (result != 0)
    return -5;
  result = McReadPS1PDACard(port, slot, page, mcman_ps1_page);
  if (result != 0)
    return -6;
  for (i = 0; i < 32; i++) {
    if (((u_int*)mcman_ps1_page)[i] != 0xffffffff)
      return 0;
  }
  for (i = 0; i < 32; i++)
    ((u_int*)mcman_ps1_page)[i] = 0;
  result = McWritePS1PDACard(port, slot, page, mcman_ps1_page);
  if (result != 0)
    return -5;
  result = McReadPS1PDACard(port, slot, page, mcman_ps1_page);
  if (result != 0)
    return -6;
  for (i = 0; i < 32; i++) {
    if (((u_int*)mcman_ps1_page)[i] != 0)
      return 0;
  }
  return 1;
}

int mcman_unformat1(int port, int slot) {
  int i, result;
  for (i = 0; (u_int)i < 32; i++)
    ((u_int*)mcman_ps1_page)[i] = 0;
  for (i = 0; i < 1024; i++) {
    result = McWritePS1PDACard(port, slot, i, mcman_ps1_page);
    if (result != 0)
      return -41;
  }
  return 0;
}

int mcman_cachePS1dirs(int port, int slot) {
  int i, j, mode, bit;
  struct McPs1Entry* entries[15];
  struct McCacheEntry* cache[2];
  int clusters[15];
  i = 0;
  do {
    int result = mcman_readdirentryPS1(port, slot, i, &entries[i]);
    if (result != 0)
      return result;
    if (i == 0)
      cache[0] = mcman_get1stcacheEntp();
    else if (i == 7)
      cache[1] = mcman_get1stcacheEntp();
  } while (++i < 15);
  memset(clusters, -1, sizeof(clusters));
  i = 0;
  do {
    if ((entries[i]->mode & 15) != 1)
      continue;
    mode = entries[i]->mode & 0xf0;
    j = entries[i]->next_block;
    clusters[i] = i;
    while (j >= 0) {
      if ((entries[j]->mode & 0xf0) != mode)
        mode = 0;
      if (entries[j]->mode == 0xa0)
        break;
      if (clusters[j] != -1)
        break;
      clusters[j] = i;
      j = entries[j]->next_block;
    }
    if (j < 0 && mode != 0)
      continue;
    j = 0;
    do {
      if (clusters[j] != i)
        continue;
      memset(entries[j], 0, sizeof(*entries[j]));
      entries[j]->mode = 0xa0;
      entries[j]->next_block = -1;
      entries[j]->checksum = mcman_calcEDC(entries[j], 127);
      mode = j + 1;
      cache[j < 7 ? 0 : 1]->dirty |= 1 << (mode % 8);
    } while (++j < 15);
  } while (++i < 15);
  i = 0;
  do {
    if (clusters[i] != -1 || entries[i]->mode == 0xa0)
      continue;
    memset(entries[i], 0, sizeof(*entries[i]));
    entries[i]->mode = 0xa0;
    entries[i]->next_block = -1;
    entries[i]->checksum = mcman_calcEDC(entries[i], 127);
    bit = i + 1;
    cache[i < 7 ? 0 : 1]->dirty |= 1 << (bit % 8);
  } while (++i < 15);
  return 0;
}

extern u_char mcman_ps1_page[128];

int mcman_fillPS1backuparea(int port, int slot, int block) {
  int result, i;
  memset(mcman_ps1_page, 0, 128);
  i = 0;
  do {
    if (mcman_devices[port][slot].bad_blocks[i] == block) {
      mcman_devices[port][slot].bad_blocks[i] = block | 0x01000000;
      *(int*)mcman_ps1_page = block | 0x01000000;
      mcman_ps1_page[127] = mcman_calcEDC(mcman_ps1_page, 127);
      result = McWritePS1PDACard(port, slot, i + 16, mcman_ps1_page);
      if (result != 0)
        return -43;
    } else if (mcman_devices[port][slot].bad_blocks[i] < 0) {
      *(int*)mcman_ps1_page = block;
      mcman_ps1_page[127] = mcman_calcEDC(mcman_ps1_page, 127);
      result = McWritePS1PDACard(port, slot, i + 16, mcman_ps1_page);
      if (result != 0)
        return -43;
      mcman_devices[port][slot].bad_blocks[i] = block;
      return 0;
    }
  } while (++i < 20);
  return -8;
}
