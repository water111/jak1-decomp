#include "sce/iop/ioman/ioman.h"
#include "sce/iop/mcman/device.h"
#include "sce/iop/mcman/internal.h"
#include "sce/iop/mcman/mcman.h"
#include "sce/iop/modload/modload.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thsemap/thsemap.h"

static const char mcman_driver_description[] = "Memory Card";
static const char mcman_driver_name[] = "mc";

extern struct IopDevice mcman_driver;

int mcman_ionop(void) {
  return 0;
}

int mcman_iounsupported(void) {
  return -38;
}

int McGetSemaphore(void) {
  return mcman_io_semaphore;
}

int mcman_ioerrcode(int result) {
  if (result < 0) {
    switch (result) {
      case -2:
        result = -31;
        break;
      case -3:
        result = -28;
        break;
      case -4:
        result = -2;
        break;
      case -5:
        result = -13;
        break;
      case -6:
        result = -17;
        break;
      case -7:
        result = -24;
        break;
      case -8:
        result = -5;
        break;
      default:
        result = -6;
        break;
    }
  }
  return result;
}

int mcman_parse_device(const char* name, int* port, int* slot) {
  int index, scale;
  while (*name == ' ')
    name++;
  if ((char)(name[0] | 0x20) != mcman_driver.name[0] ||
      (char)(name[1] | 0x20) != mcman_driver.name[1])
    return 0;
  if (strlen((const char*)name) <= 1)
    return 0;
  index = mcman_chrpos((const char*)name, ':');
  if (index < 0)
    index = strlen((const char*)name);
  index--;
  if (port) {
    if ((u_int)((u_char)name[index] - '0') < 10) {
      *port = name[index] - '0';
      index--;
    } else {
      *port = 0;
    }
    *port += 2;
  }
  if (slot) {
    *slot = 0;
    scale = 1;
    while ((u_int)((u_char)name[index] - '0') < 10) {
      *slot += scale * (name[index] - '0');
      index--;
      scale *= 10;
    }
  }
  return 1;
}

void mcman_unit2card(u_int unit) {
  u_int mask = 0xf0000000;
  while (!(unit & mask)) {
    mask >>= 4;
    if (mask < 0x10)
      break;
  }
  mcman_io_card.port = unit & 15;
  if (mask >= 15) {
    mcman_io_card.slot = 0;
    return;
  }
  mcman_io_card.slot = 0;
  while (mask) {
    mcman_io_card.slot = mcman_io_card.slot * 10 + (unit & mask) / (mask & (mask >> 3));
    mask >>= 4;
  }
}

int mcman_initdev(void) {
  struct SemaParam sema;
  int result;
  SetCheckKelfPathCallback(mcman_parse_device);
  DelDrv(mcman_driver.name);
  result = AddDrv(&mcman_driver);
  if (result != 0) {
    McCloseAll();
    return 1;
  }
  {
    sema.attr = 1;
    sema.initCount = 1;
    sema.maxCount = 1;
    sema.option = 0;
    mcman_io_semaphore = CreateSema(&sema);
    return 0;
  }
}

int mcman_iodeinit(void) {
  DeleteSema(mcman_io_semaphore);
  McCloseAll();
  return 0;
}

int mcman_ioopen(struct McIoFile* file, const char* path, int flags) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result >= -1) {
    result = McOpen(mcman_io_card.port, mcman_io_card.slot, path, flags);
    if (result >= 0)
      file->card_fd = result;
  }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_ioclose(struct McIoFile* file) {
  int result;
  WaitSema(mcman_io_semaphore);
  result = McClose(file->card_fd);
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_ioseek(struct McIoFile* file, int offset, int origin) {
  int result;
  WaitSema(mcman_io_semaphore);
  result = McSeek(file->card_fd, offset, origin);
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_ioread(struct McIoFile* file, void* buffer, int size) {
  int result;
  WaitSema(mcman_io_semaphore);
  result = McRead(file->card_fd, buffer, size);
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iowrite(struct McIoFile* file, void* buffer, int size) {
  int result;
  WaitSema(mcman_io_semaphore);
  result = McWrite(file->card_fd, buffer, size);
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_ioformat(struct McIoFile* file) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result < -2) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  { result = McFormat(mcman_io_card.port, mcman_io_card.slot); }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_ioremove(struct McIoFile* file, const char* path) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result < -1) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  { result = McDelete(mcman_io_card.port, mcman_io_card.slot, path, 0); }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iomkdir(struct McIoFile* file, const char* path) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result < -1) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  { result = McOpen(mcman_io_card.port, mcman_io_card.slot, path, 0x40); }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iormdir(struct McIoFile* file, const char* path) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result < -1) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  { result = McDelete(mcman_io_card.port, mcman_io_card.slot, path, 0); }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iodopen(struct McIoFile* file, const char* path) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result >= -1) {
    result = McOpen(mcman_io_card.port, mcman_io_card.slot, path, 0);
    if (result >= 0)
      file->card_fd = result;
  }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iodclose(struct McIoFile* file) {
  int result;
  WaitSema(mcman_io_semaphore);
  result = McClose(file->card_fd);
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iodread(struct McIoFile* file, struct McIoDirEntry* entry) {
  int result;
  WaitSema(mcman_io_semaphore);
  result = mcman_dread(file->card_fd, entry);
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iogetstat(struct McIoFile* file, const char* path, struct McIoStat* info) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result < -1) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  { result = mcman_getstat(mcman_io_card.port, mcman_io_card.slot, path, info); }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

#define COPY_ATTRIBUTE(bit)   \
  if (stat->mode & (bit))     \
    info.attributes |= (bit); \
  else                        \
    info.attributes &= ~(bit)

int mcman_iochstat(struct McIoFile* file, const char* path, struct McIoStat* stat, int flags) {
  struct McDirectoryEntry info;
  int result, mode;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result <= -2) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  {
    mode = 0;
    if (flags & 2) {
      info.application_data = stat->attributes;
      mode = 8;
    }
    if (flags & 1) {
      mode |= 512;
      COPY_ATTRIBUTE(1);
      COPY_ATTRIBUTE(2);
      COPY_ATTRIBUTE(4);
      COPY_ATTRIBUTE(8);
      COPY_ATTRIBUTE(0x1000);
      COPY_ATTRIBUTE(0x800);
    }
    if (flags & 8) {
      info.created = stat->created;
      mode |= 1;
    }
    if (flags & 32) {
      info.modified = stat->modified;
      mode |= 2;
    }
    result = McSetFileInfo(mcman_io_card.port, mcman_io_card.slot, path, &info, mode);
  }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iorename(struct McIoFile* file, const char* old_path, const char* new_path) {
  int result;
  WaitSema(mcman_io_semaphore);
  mcman_unit2card(file->unit);
  result = McDetectCard2(mcman_io_card.port, mcman_io_card.slot);
  if (result < -1) {
    SignalSema(mcman_io_semaphore);
    return mcman_ioerrcode(result);
  }
  { result = mcman_rename(mcman_io_card.port, mcman_io_card.slot, old_path, new_path); }
  SignalSema(mcman_io_semaphore);
  return mcman_ioerrcode(result);
}

int mcman_iolock(void) {
  WaitSema(mcman_io_semaphore);
  return 0;
}

static void* mcman_driver_operations[] = {
    mcman_ionop,         mcman_iodeinit,      mcman_ioformat,      mcman_ioopen,
    mcman_ioclose,       mcman_ioread,        mcman_iowrite,       mcman_ioseek,
    mcman_iolock,        mcman_ioremove,      mcman_iomkdir,       mcman_iormdir,
    mcman_iodopen,       mcman_iodclose,      mcman_iodread,       mcman_iogetstat,
    mcman_iochstat,      mcman_iorename,      mcman_iounsupported, mcman_iounsupported,
    mcman_iounsupported, mcman_iounsupported, mcman_iounsupported, mcman_iounsupported,
    mcman_iounsupported, mcman_iounsupported, mcman_iounsupported,
};
struct IopDevice mcman_driver = {mcman_driver_name, 0x10000010, 1, mcman_driver_description,
                                 mcman_driver_operations};
