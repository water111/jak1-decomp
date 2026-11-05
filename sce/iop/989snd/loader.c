#include "sce/iop/989snd/loader.h"

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/blocksound.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/grain.h"
#include "sce/iop/989snd/init.h"
#include "sce/iop/989snd/loader_callbacks.h"
#include "sce/iop/989snd/loader_cd.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/loader_file.h"
#include "sce/iop/989snd/loader_memory.h"
#include "sce/iop/989snd/loader_midi.h"
#include "sce/iop/989snd/loader_source.h"
#include "sce/iop/989snd/loader_state.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/989snd/stream_cd.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/cdvdman/libcdvd.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/ioman/ioman.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/sifcmd/sifcmd.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/sysmem/sysmem.h"


#if SND989_VERSION == 0x020604

// TODO hack
static const char bank_load_by_loc_message_1[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Done... ERROR reading!\n";
static const char bank_load_by_loc_message_2[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Done... ERROR couldn't allocate %d bytes of IOP RAM!\n";
static const char bank_load_by_loc_message_3[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Done... ERROR couldn't allocate IOP RAM!\n";
static const char bank_load_by_loc_message_4[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Done... ERROR transfering data to SPU memory!\n";
static const char bank_load_by_loc_message_5[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Done... ERROR loading imbedded MMD!\n";
static const char bank_load_by_loc_message_6[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Wow unrecognized file type (0x%x)!\n";
static const char bank_load_by_loc_message_7[] __attribute__((aligned(4))) =
    "snd_BankLoadByLoc: Are you SURE you sent in the correct location and offset?\n";
static const char unload_bank_message_1[] __attribute__((aligned(4))) =
    "snd_UnloadBank: Tried to unload a null bank!\n";
static const char unload_bank_message_2[] __attribute__((aligned(4))) =
    "snd_UnloadBank: Tried to unload a bank that isn't loaded!!!\n";
static const char unload_block_message_1[] __attribute__((aligned(4))) =
    "snd_UnloadBlock: Tried to unload a bank that isn't loaded!!!\n";
static const char mmd_load_by_loc_message_1[] __attribute__((aligned(4))) =
    "snd_MMDLoadByLoc: Done... error reading.\n";
static const char mmd_load_by_loc_message_2[] __attribute__((aligned(4))) =
    "snd_MMDLoadByLoc: Done... error couldn't allocate IOP RAM!\n";
static const char mmd_load_by_loc_message_3[] __attribute__((aligned(4))) =
    "snd_MMDLoadByLoc: Done... error reading!\n";
static const char bank_transfer_message_1[] __attribute__((aligned(4))) =
    "snd_BankTransfer: ERROR... Sound System isn't running!\n";
static const char bank_transfer_message_2[] __attribute__((aligned(4))) =
    "snd_BankTransfer: ERROR... There is already a transfer in progress!\n";
static const char bank_transfer_message_3[] __attribute__((aligned(4))) =
    "snd_BankTransfer: ERROR... couldn't allocate %d bytes of sound ram!\n";
static const char bank_transfer_message_4[] __attribute__((aligned(4))) =
    "snd_BankTransfer: ERROR... couldn't allocate %d bytes at 0x%x in sound ram!\n";
static const char bank_transfer_message_5[] __attribute__((aligned(4))) =
    "snd_BankTransfer: Waiting for a free DMA channel!\n";
static const char bank_transfer_message_6[] __attribute__((aligned(4))) =
    "snd_BankTransfer: Got free DMA channel... continueing.\n";
static const char block_transfer_message_1[] __attribute__((aligned(4))) =
    "snd_BlockTransfer: ERROR... Sound System isn't running!\n";
static const char block_transfer_message_2[] __attribute__((aligned(4))) =
    "snd_BlockTransfer: ERROR... There is already a transfer in progress!\n";
static const char block_transfer_message_3[] __attribute__((aligned(4))) =
    "snd_BlockTransfer: ERROR... couldn't allocate %d bytes of sound ram!\n";
static const char block_transfer_message_4[] __attribute__((aligned(4))) =
    "snd_BlockTransfer: ERROR... couldn't allocate %d bytes at 0x%x in sound ram!\n";
static const char block_transfer_message_5[] __attribute__((aligned(4))) =
    "snd_BlockTransfer: Waiting for a free DMA channel!\n";
static const char block_transfer_message_6[] __attribute__((aligned(4))) =
    "snd_BlockTransfer: Got free DMA channel... continueing.\n";

#else
int gFileHandle = -1;
int gFileStartSector = 0;
int gFileStartOffset = 0;
int gFileNextReadSector = 0;
int gFileNextReadOffset = 0;
int gFileMarkSector = 0;
int gFileMarkOffset = 0;
int gLastLoadError = 0;
#endif

void* (*gAllocProc)(int size, int use, int* actual_size) = snd_IOPMemAlloc;
void (*gFreeProc)(void* memory) = snd_IOPMemFree;

#if SND989_VERSION == 0x020604
int gTransfering = 0;
void (*gTransferDoneCallback)(void) = 0;
#endif
struct SoundBank* gBankListHead = 0;
struct SFXBlock2* gBlockListHead = 0;
struct MIDIBlockHeader* gMIDIListHead = 0;
#if SND989_VERSION == 0x020608
int gReadBufferHasSector = 0;
#endif

#if SND989_VERSION == 0x020604
static inline SoundBank* find_midi_bank(u_int id) {
  SoundBank* walk;
  for (walk = gBankListHead; walk; walk = walk->next) {
    if (walk->id == id)
      return walk;
  }
  return (SoundBank*)snd_FindBlockByID(id);
}
#else
#define find_midi_bank snd_FindBankByID
#endif

void snd_StopAllHandlersForSound(Sound* sound, int silence);

#if SND989_VERSION == 0x020604
static inline void unload_midi(MIDIBlockHeader* midi) {
  snd_UnregisterMIDI(midi);
  gFreeProc(midi);
}
#else
#define unload_midi snd_UnloadMMD
#endif

#if SND989_VERSION == 0x020604
static inline void unload_bank(SoundBank* bank) {
  if (!bank) {
    printf(unload_bank_message_1);
    return;
  }
  if (bank->data_id == 0x6b6c4253) {
    snd_UnloadBlock((SFXBlock2*)bank);
    return;
  }
  if (!snd_BankLoaded(bank)) {
    printf(unload_bank_message_2);
    return;
  }
  snd_RemoveBank(bank);
  if (bank->flags & 8) {
    bank = (SoundBank*)((char*)bank - 4);
    snd_UnloadMMD(*(MIDIBlockHeader**)bank);
  }
  gFreeProc(bank);
}

static inline void unload_block(SFXBlock2* block) {
  if (!snd_BlockLoaded(block)) {
    printf(unload_block_message_1);
    return;
  }
  snd_RemoveBlock(block);
  gFreeProc(block);
}
#else
#define unload_bank snd_UnloadBank
#define unload_block snd_UnloadBlock
#endif

extern int gReadBufferHasSector;
extern char gFileLoadBuffer[SND_CD_SECTOR_SIZE];

#if SND989_VERSION == 0x020604
#define FILE_SET_REG(reg_number, reg_value)                                                       \
  do {                                                                                            \
    gCDSifRegPacket.reg = (reg_number);                                                           \
    gCDSifRegPacket.value = (reg_value);                                                          \
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, \
                       0, 0);                                                                     \
    while (sceSifDmaStat(id) >= 0) {                                                              \
    }                                                                                             \
  } while (0)
#define FILE_READ_STATUS(error) \
  do {                          \
    FILE_SET_REG(9, error);     \
    FILE_SET_REG(8, 0);         \
  } while (0)
#define FILE_COPY memmove
#else
#define FILE_READ_STATUS(error) snd_SetCDSifReg(0, error)
#define FILE_COPY memmove
#endif

#define SOUND_XREF_RESOLVED 1
#define BANK_XREF_RESOLVED 2

extern struct SndCDSifRegPacket gCDSifRegPacket;

void snd_InitLoader(void) {
  gTransfering = 0;
  gTransferDoneCallback = 0;
  gBankListHead = 0;
  gBlockListHead = 0;
  gMIDIListHead = 0;
  gAllocProc = snd_IOPMemAlloc;
  gFreeProc = snd_IOPMemFree;
}

void snd_RegisterIOPMemAllocator(void* (*allocate)(int, int, int*), void (*release)(void*)) {
  if (allocate)
    gAllocProc = allocate;
  else
    gAllocProc = snd_IOPMemAlloc;
  if (release)
    gFreeProc = release;
  else
    gFreeProc = snd_IOPMemFree;
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
int snd_OpenDataSourceByName(const char* name, int offset) {
  gFileStartOffset = offset;
  gFileHandle = open(name, IOP_O_RDONLY);
  if (gFileHandle < 0)
    return 0;
  lseek(gFileHandle, gFileStartOffset, IOP_SEEK_SET);
  return 1;
}
#endif

#if SND989_VERSION == 0x020608
int snd_OpenDataSourceByLoc(int sector, int offset) {
  gFileStartSector = sector;
  gFileStartOffset = offset;
  gFileNextReadSector = sector;
  gFileNextReadOffset = offset;
  gFileMarkSector = sector;
  gFileMarkOffset = offset;
  gFileHandle = 1;
  return 1;
}
#endif

#if SND989_VERSION == 0x020608
void snd_CloseDataSource(void) {
  if (gFileHandle == -1)
    return;
  if (!gFileStartSector) {
    close(gFileHandle);
  } else {
    gFileStartSector = 0;
    gFileStartOffset = 0;
    gFileNextReadSector = 0;
    gFileNextReadOffset = 0;
  }
  gFileHandle = -1;
}
#endif

#if SND989_VERSION == 0x020608
int snd_SeekDataSource(int where, int from_where) {
  if (gFileHandle == -1)
    return 0;
  if (!gFileStartSector) {
    if (!from_where)
      return lseek(gFileHandle, where + gFileStartOffset, IOP_SEEK_SET);
    else
      return lseek(gFileHandle, where + gFileMarkOffset, IOP_SEEK_SET);
  }
  if (!from_where) {
    gFileNextReadSector = gFileStartSector;
    gFileNextReadOffset = gFileStartOffset + where;
  } else {
    gFileNextReadSector = gFileMarkSector;
    gFileNextReadOffset = gFileMarkOffset + where;
  }
  if (gFileNextReadOffset >= SND_CD_SECTOR_SIZE) {
    gFileNextReadSector += gFileNextReadOffset / SND_CD_SECTOR_SIZE;
    gFileNextReadOffset %= SND_CD_SECTOR_SIZE;
  }
  return where;
}
#endif

#if SND989_VERSION == 0x020608
void snd_SetDataSourceMark(void) {
  if (gFileHandle == -1)
    return;
  if (!gFileStartSector) {
    gFileMarkOffset = lseek(gFileHandle, 0, IOP_SEEK_CUR);
  } else {
    gFileMarkSector = gFileNextReadSector;
    gFileMarkOffset = gFileNextReadOffset;
  }
}
#endif

#if SND989_VERSION == 0x020608
int snd_GetLastLoadError(void) {
  return gLastLoadError;
}
#endif

#if SND989_VERSION == 0x020608
int snd_ReadBytes(void* buffer, int count) {
  int result;
  if (gFileHandle == -1)
    return 0;
  if (!gFileStartSector) {
    result = read(gFileHandle, buffer, count);
    if (result != count)
      gLastLoadError = 257;
    return result;
  }
  if (!snd_FileRead(gFileNextReadSector, gFileNextReadOffset, count, buffer))
    return 0;
  gFileNextReadOffset += count;
  if (gFileNextReadOffset >= SND_CD_SECTOR_SIZE) {
    gFileNextReadSector += gFileNextReadOffset / SND_CD_SECTOR_SIZE;
    gFileNextReadOffset %= SND_CD_SECTOR_SIZE;
  }
  return count;
}
#endif

#endif
#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
struct SoundBank* snd_BankLoad(char* name, int offset) {
  struct SoundBank* bank;
  if (!snd_OpenDataSourceByName(name, offset)) {
    gLastLoadError = 256;
    return 0;
  }
  gLastLoadError = 0;
  bank = snd_ReadBank();
  snd_CloseDataSource();
  return bank;
}
#endif

#if SND989_VERSION == 0x020608
struct SoundBank* snd_ReadBank(void) {
  // TODO hack
  register struct SoundBank* bank __asm__("$17") = 0;
  struct SndFileHeader* header;
  char* allocation;
  void* samples;
  char buffer[32];
  int size, maximum, block, offset, result;
  if (snd_ReadBytes(buffer, sizeof(buffer)) != sizeof(buffer)) {
    snd_ShowError(14, 0, 0, 0, 0);
    goto done;
  }
  header = (struct SndFileHeader*)buffer;
  if (header->type == 1 || header->type == 3) {
    size = header->chunks[0].size;
    if (header->chunk_count >= 3)
      size += 4;
    allocation = gAllocProc(size, 1, 0);
    if (!allocation) {
      snd_ShowError(15, size, 0, 0, 0);
      gLastLoadError = 258;
      return 0;
    }
    bank = (struct SoundBank*)allocation;
    if (header->chunk_count == 3)
      bank = (struct SoundBank*)((char*)bank + 4);
    samples = gAllocProc(header->chunks[1].size, 8, &maximum);
    if ((u_int)maximum > (u_int)header->chunks[1].size)
      maximum = header->chunks[1].size;
    if (!samples) {
      snd_ShowError(16, 0, 0, 0, 0);
      gFreeProc(allocation);
      gLastLoadError = 258;
      return 0;
    }
    snd_SeekDataSource(header->chunks[0].offset, 0);
    if (snd_ReadBytes(bank, header->chunks[0].size) != header->chunks[0].size) {
      snd_ShowError(17, 0, 0, 0, 0);
      gFreeProc(allocation);
      gFreeProc(samples);
      return 0;
    }
    snd_SeekDataSource(header->chunks[1].offset, 0);
    block = 0;
    offset = 0;
    while (header->chunks[1].size) {
      result = snd_ReadBytes(samples, maximum);
      if (result != maximum) {
        snd_ShowError(14, 0, 0, 0, 0);
        gFreeProc(allocation);
        gFreeProc(samples);
        return 0;
      }
      if (!block)
        result = snd_BankTransfer(bank, samples, result, 0, 0, 0);
      else
        result = snd_BankTransfer(bank, samples, result, offset, 1, 0);
      if (result < 0) {
        snd_ShowError(18, 0, 0, 0, 0);
        gFreeProc(allocation);
        gFreeProc(samples);
        return 0;
      }
      while (!snd_IsCurrentTransferComplete()) {
      }
      ++block;
      offset += maximum;
      header->chunks[1].size -= maximum;
      if ((u_int)maximum > (u_int)header->chunks[1].size)
        maximum = header->chunks[1].size;
    }
    snd_EndBankTransfer(bank);
    gFreeProc(samples);
    if (header->chunk_count == 3) {
      snd_SeekDataSource(header->chunks[2].offset, 0);
      *(MultiMIDIBlockHeader**)allocation = snd_MMDLoadFromDataSource();
      if (!*(MultiMIDIBlockHeader**)allocation) {
        snd_RemoveBank(bank);
        gFreeProc(allocation);
        snd_ShowError(19, 0, 0, 0, 0);
        return 0;
      }
      bank->flags |= 8;
    }
  } else {
    snd_ShowError(84, 0, 0, 0, 0);
  }
done:
  return bank;
}
#endif

#endif

int snd_FileRead(int sector, int offset, int size, void* destination) {
  sceCdRMode mode;
  int remaining, moved, sectors, result, id;
  char* buffer = destination;
#if SND989_VERSION == 0x020608
  moved = 0;
#endif
  snd_StreamSafeCdSync(0);
  snd_StreamSafeCdSync(16);
#if SND989_VERSION == 0x020604
  {
    // TODO hack
    register u_int command __asm__("$4") = SIF_CMD_SET_SYSTEM_REGISTER;
    asm("" : "+r"(command));
    gCDSifRegPacket.reg = 8;
    gCDSifRegPacket.value = 1;
    id = sceSifSendCmd(command, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0, 0);
    while (sceSifDmaStat(id) >= 0) {
    }
  }
  FILE_SET_REG(9, 0);
#else
  snd_SetCDSifReg(1, 0);
#endif
  remaining = size;
  mode.trycount = 30;
  mode.spindlctrl = 1;
  mode.datapattern = 0;
  sector += offset / SND_CD_SECTOR_SIZE;
#if SND989_VERSION == 0x020608
  offset %= SND_CD_SECTOR_SIZE;
  if (remaining < SND_CD_SECTOR_SIZE || offset) {
    if (sector != gReadBufferHasSector) {
#endif
      result = snd_StreamSafeCdRead(sector, 1, gFileLoadBuffer);
      snd_StreamSafeCdSync(0);
      if (!result || (result = snd_StreamSafeCdGetError()) != 0) {
#if SND989_VERSION == 0x020608
        gReadBufferHasSector = 0;
#endif
        if (!result)
          result = SCECdErREAD;
        FILE_READ_STATUS(result);
#if SND989_VERSION == 0x020608
        gLastLoadError = result;
#endif
        return 0;
      }
#if SND989_VERSION == 0x020608
      gReadBufferHasSector = sector;
    }
#endif
    moved = SND_CD_SECTOR_SIZE - offset % SND_CD_SECTOR_SIZE;
    if (remaining < moved)
      moved = remaining;
    FILE_COPY(buffer, gFileLoadBuffer + offset % SND_CD_SECTOR_SIZE, moved);
    remaining -= moved;
    if (!remaining) {
      FILE_READ_STATUS(0);
      return 1;
    }
    ++sector;
#if SND989_VERSION == 0x020608
  }
#endif
  sectors = remaining / SND_CD_SECTOR_SIZE;
  if (sectors) {
    result = snd_StreamSafeCdRead(sector, sectors, buffer + moved);
    snd_StreamSafeCdSync(0);
    if (!result || (result = snd_StreamSafeCdGetError()) != 0) {
      if (!result)
        result = SCECdErREAD;
      FILE_READ_STATUS(result);
#if SND989_VERSION == 0x020608
      gLastLoadError = result;
#endif
      return 0;
    }
  }
  sector += sectors;
  remaining -= sectors * SND_CD_SECTOR_SIZE;
  if (remaining) {
#if SND989_VERSION == 0x020608
    if (sector != gReadBufferHasSector) {
#endif
      result = snd_StreamSafeCdRead(sector, 1, gFileLoadBuffer);
      snd_StreamSafeCdSync(0);
      if (!result || (result = snd_StreamSafeCdGetError()) != 0) {
#if SND989_VERSION == 0x020608
        gReadBufferHasSector = 0;
#endif
        if (!result)
          result = SCECdErREAD;
        FILE_READ_STATUS(result);
#if SND989_VERSION == 0x020608
        gLastLoadError = result;
#endif
        return 0;
      }
#if SND989_VERSION == 0x020608
      gReadBufferHasSector = sector;
    }
#endif
    FILE_COPY(buffer + (size - remaining), gFileLoadBuffer, remaining);
  }
  FILE_READ_STATUS(0);
  return 1;
}

#if SND989_VERSION == 0x020604
struct SoundBank* snd_BankLoadByLoc(int sector, int file_offset) {
  char buffer[32];
  struct SndFileHeader* header = (struct SndFileHeader*)buffer;
#else
struct SoundBank* snd_BankLoadByLoc(int sector, int offset) {
#endif
  struct SoundBank* bank;
#if SND989_VERSION == 0x020604
  char* allocation;
  void* samples;
  int size, maximum, block, offset, result;
  if (!snd_FileRead(sector, file_offset, sizeof(buffer), buffer)) {
    printf(bank_load_by_loc_message_1);
#else
  if (!snd_OpenDataSourceByLoc(sector, offset))
#endif
    return 0;
#if SND989_VERSION == 0x020604
  }
  if (header->type == 1) {
    size = header->chunks[0].size;
    if (header->chunk_count >= 3)
      size += 4;
    allocation = gAllocProc(size, 1, 0);
    if (!allocation) {
      printf(bank_load_by_loc_message_2, size);
      return 0;
    }
    if (header->chunk_count == 3)
      bank = (struct SoundBank*)(allocation + 4);
    else
      bank = (struct SoundBank*)allocation;
    samples = gAllocProc(header->chunks[1].size, 8, &maximum);
    if (!samples) {
      printf(bank_load_by_loc_message_3);
      gFreeProc(allocation);
      return 0;
    }
    if (!snd_FileRead(sector, file_offset + header->chunks[0].offset, header->chunks[0].size,
                      bank)) {
      printf(bank_load_by_loc_message_1);
      gFreeProc(allocation);
      gFreeProc(samples);
      return 0;
    }
    block = 0;
    offset = 0;
    while (header->chunks[1].size) {
      if (!snd_FileRead(sector, file_offset + header->chunks[1].offset, maximum, samples)) {
        printf(bank_load_by_loc_message_1);
        gFreeProc(allocation);
        gFreeProc(samples);
        return 0;
      }
      if (!block)
        result = snd_BankTransfer(bank, samples, maximum, 0, 0, 0);
      else
        result = snd_BankTransfer(bank, samples, maximum, offset, 1, 0);
      if (result < 0) {
        printf(bank_load_by_loc_message_4);
        gFreeProc(allocation);
        gFreeProc(samples);
        return 0;
      }
      while (!snd_IsCurrentTransferComplete()) {
      }
      ++block;
      offset += maximum;
      header->chunks[1].offset += maximum;
      header->chunks[1].size -= maximum;
      if ((u_int)header->chunks[1].size < (u_int)maximum)
        maximum = header->chunks[1].size;
    }
    snd_EndBankTransfer(bank);
    gFreeProc(samples);
    if (header->chunk_count == 3) {
      u_int midi_offset = file_offset + header->chunks[2].offset;
      *(MultiMIDIBlockHeader**)allocation =
          snd_MMDLoadByLoc(sector + midi_offset / 2048, midi_offset & 2047);
      if (!*(MultiMIDIBlockHeader**)allocation) {
        printf(bank_load_by_loc_message_5);
        snd_RemoveBank(bank);
        gFreeProc(allocation);
        return 0;
      }
      bank->flags |= 8;
    }
  } else if (header->type == 3) {
    struct SoundBank* block_bank;
    block_bank = gAllocProc(header->chunks[0].size, 1, 0);
    if (!block_bank) {
      printf(bank_load_by_loc_message_2, 0);
      return 0;
    }
    samples = gAllocProc(header->chunks[1].size, 8, &maximum);
    if (!samples) {
      printf(bank_load_by_loc_message_3);
      gFreeProc(block_bank);
      return 0;
    }
    if (!snd_FileRead(sector, file_offset + header->chunks[0].offset, header->chunks[0].size,
                      block_bank)) {
      printf(bank_load_by_loc_message_1);
      gFreeProc(block_bank);
      gFreeProc(samples);
      return 0;
    }
    block = 0;
    offset = 0;
    while (header->chunks[1].size) {
      if (!snd_FileRead(sector, file_offset + header->chunks[1].offset, maximum, samples)) {
        printf(bank_load_by_loc_message_1);
        gFreeProc(block_bank);
        gFreeProc(samples);
        return 0;
      }
      if (!block)
        result = snd_BlockTransfer((struct SFXBlock2*)block_bank, samples, maximum, 0, 0, 0);
      else
        result = snd_BlockTransfer((struct SFXBlock2*)block_bank, samples, maximum, offset, 1, 0);
      if (result < 0) {
        printf(bank_load_by_loc_message_4);
        gFreeProc(block_bank);
        gFreeProc(samples);
        return 0;
      }
      while (!snd_IsCurrentTransferComplete()) {
      }
      ++block;
      offset += maximum;
      header->chunks[1].offset += maximum;
      header->chunks[1].size -= maximum;
      if ((u_int)header->chunks[1].size < (u_int)maximum)
        maximum = header->chunks[1].size;
    }
    snd_EndBlockTransfer((struct SFXBlock2*)block_bank);
    gFreeProc(samples);
    return block_bank;
  } else {
    bank = 0;
    printf(bank_load_by_loc_message_6, header->type);
    printf(bank_load_by_loc_message_7);
  }
#else
  gLastLoadError = 0;
  bank = snd_ReadBank();
  snd_CloseDataSource();
#endif
  return bank;
}

void snd_UnloadBank(SoundBank* bank) {
#if SND989_VERSION == 0x020604
  unload_bank(bank);
#else
  if (!bank) {
    snd_ShowError(20, 0, 0, 0, 0);
    return;
  }
  if (bank->data_id == 0x6b6c4253) {
    snd_UnloadBlock((SFXBlock2*)bank);
    return;
  }
  if (!snd_BankLoaded(bank)) {
    snd_ShowError(21, 0, 0, 0, 0);
    return;
  }
  snd_RemoveBank(bank);
  if (bank->flags & 8) {
    bank = (SoundBank*)((char*)bank - 4);
    snd_UnloadMMD(*(MIDIBlockHeader**)bank);
  }
  gFreeProc(bank);
#endif
}

int snd_BankLoaded(SoundBank* bank) {
  SoundBank* walk = gBankListHead;
  if (!bank)
    return 0;
  while (walk) {
    if (bank == walk)
      return 1;
    walk = walk->next;
  }
  return 0;
}

void snd_UnloadBlock(SFXBlock2* block) {
#if SND989_VERSION == 0x020604
  unload_block(block);
#else
  if (!snd_BlockLoaded(block)) {
    snd_ShowError(22, 0, 0, 0, 0);
    return;
  }
  snd_RemoveBlock(block);
  gFreeProc(block);
#endif
}

int snd_BlockLoaded(SFXBlock2* block) {
  SFXBlock2* walk = gBlockListHead;
  if (!block)
    return 0;
  while (walk) {
    if (block == walk)
      return 1;
    walk = walk->next;
  }
  return 0;
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
MultiMIDIBlockHeader* snd_MMDLoad(char* name, int offset) {
  MultiMIDIBlockHeader* midi;
  if (!snd_OpenDataSourceByName(name, offset))
    return 0;
  midi = snd_MMDLoadFromDataSource();
  snd_CloseDataSource();
  return midi;
}
#endif

#if SND989_VERSION == 0x020608
MultiMIDIBlockHeader* snd_MMDLoadFromDataSource(void) {
  int length;
  struct SndFileHeader* header = (struct SndFileHeader*)gFileLoadBuffer;
  MultiMIDIBlockHeader* midi;
  snd_SetDataSourceMark();
  if (snd_ReadBytes(header, 16) != 16)
    return 0;
  length = header->chunks[0].size;
  snd_SeekDataSource(header->chunks[0].offset, 1);
  midi = gAllocProc(length, 3, 0);
  if (!midi) {
    snd_ShowError(23, length, 0, 0, 0);
    gLastLoadError = 258;
    return 0;
  }
  if (snd_ReadBytes(midi, length) != length) {
    gFreeProc(midi);
    return 0;
  }
  snd_RegisterMIDI((MIDIBlockHeader*)midi);
  return midi;
}
#endif

#endif

MultiMIDIBlockHeader* snd_MMDLoadByLoc(int sector, int offset) {
#if SND989_VERSION == 0x020604
  char buffer[16];
  struct SndFileHeader* header = (struct SndFileHeader*)buffer;
#endif
  MultiMIDIBlockHeader* midi;
#if SND989_VERSION == 0x020604
  if (!snd_FileRead(sector, offset, 16, buffer)) {
    printf(mmd_load_by_loc_message_1);
#else
  if (!snd_OpenDataSourceByLoc(sector, offset))
#endif
    return 0;
#if SND989_VERSION == 0x020604
  }
  midi = gAllocProc(header->chunks[0].size, 3, 0);
  if (!midi) {
    printf(mmd_load_by_loc_message_2);
    return 0;
  }
  if (!snd_FileRead(sector, offset + header->chunks[0].offset, header->chunks[0].size, midi)) {
    gFreeProc(midi);
    printf(mmd_load_by_loc_message_3);
    return 0;
  }
  snd_RegisterMIDI((MIDIBlockHeader*)midi);
#else
  midi = snd_MMDLoadFromDataSource();
  snd_CloseDataSource();
#endif
  return midi;
}

void snd_UnloadMMD(MIDIBlockHeader* midi) {
  snd_UnregisterMIDI(midi);
  gFreeProc(midi);
}

void snd_UnloadAllMMD(void) {
  MIDIBlockHeader* walk;
  for (walk = gMIDIListHead; walk; walk = walk->next) {
    if (walk->data_id == SND_MULTI_MIDI_ID)
      unload_midi(walk);
  }
  while (gMIDIListHead)
    unload_midi(gMIDIListHead);
}

int snd_BankTransfer(struct SoundBank* bank,
                     void* samples,
                     int size,
                     int offset,
                     int continuing,
                     void (*done)(void)) {
#if SND989_VERSION == 0x020608
  u_int allocation_size, location;
#endif
  int channel;
  int waited = 0;
#if SND989_VERSION == 0x020604
#else
  struct SFXBlock2* block = (struct SFXBlock2*)bank;
#endif
  // BUG forgot to actually call the function.
  if (!snd_SystemRunning) {
#if SND989_VERSION == 0x020604
    printf(bank_transfer_message_1);
#else
    snd_ShowError(24, 0, 0, 0, 0);
#endif
    return -2;
  }
  if (gTransfering) {
#if SND989_VERSION == 0x020604
    printf(bank_transfer_message_2);
#else
    snd_ShowError(25, 0, 0, 0, 0);
    gLastLoadError = 260;
#endif
    return -10;
#if SND989_VERSION == 0x020608
  }
  if (bank->data_id == SOUND_BANK_V2_ID) {
    allocation_size = bank->sample_size;
    location = (u_int)bank->samples;
  } else {
    allocation_size = block->allocation_size;
    location = (u_int)block->samples;
#endif
  }
  if (!continuing) {
    if (bank->flags & 4) {
      int disabled = CpuDisableIntr();
#if SND989_VERSION == 0x020604
      bank->samples = (void*)snd_SRAMMalloc(bank->sample_size);
#else
      location = snd_SRAMMalloc(allocation_size);
#endif
      if (!disabled)
        CpuEnableIntr();
#if SND989_VERSION == 0x020604
      if (!bank->samples) {
        printf(bank_transfer_message_3, bank->sample_size);
#else
      if (!location) {
        snd_ShowError(26, bank->sample_size, 0, 0, 0);
        gLastLoadError = 259;
#endif
        return -11;
      }
#if SND989_VERSION == 0x020608
      if (bank->data_id == SOUND_BANK_V2_ID)
        bank->samples = (void*)location;
      else
        block->samples = (void*)location;
#endif
    } else {
      int disabled = CpuDisableIntr();
#if SND989_VERSION == 0x020604
      if (!snd_SRAMMarkUsed((u_int)bank->samples, bank->sample_size)) {
#else
      if (!snd_SRAMMarkUsed(location, allocation_size)) {
#endif
        if (!disabled)
          CpuEnableIntr();
#if SND989_VERSION == 0x020604
        printf(bank_transfer_message_4, bank->sample_size, bank->samples);
#else
        snd_ShowError(27, bank->sample_size, (u_int)bank->samples, 0, 0);
        gLastLoadError = 259;
#endif
        return -11;
      }
      if (!disabled)
        CpuEnableIntr();
    }
    if (!(bank->flags & 1)) {
#if SND989_VERSION == 0x020604
      bank->sounds = (struct Sound*)((u_int)bank->sounds + (u_int)bank);
      bank->programs = (struct Prog*)((u_int)bank->programs + (u_int)bank);
      bank->tones = (struct Tone*)((u_int)bank->tones + (u_int)bank);
      bank->flags |= 1;
#else
      if (bank->data_id == SOUND_BANK_V2_ID) {
        bank->sounds = (struct Sound*)((u_int)bank->sounds + (u_int)bank);
        bank->programs = (struct Prog*)((u_int)bank->programs + (u_int)bank);
        bank->tones = (struct Tone*)((u_int)bank->tones + (u_int)bank);
        bank->flags |= 1;
      } else {
        block->sounds = (struct SFX2*)((u_int)block->sounds + (u_int)block);
        block->grains = (struct SFXGrain2*)((u_int)block->grains + (u_int)block);
        block->flags |= 1;
      }
#endif
    }
  }
  gTransferDoneCallback = done;
  while (1) {
    channel = snd_GetFreeSPUDMA();
    if (channel != -1)
      break;
#if SND989_VERSION == 0x020604
    if (!waited) {
      printf(bank_transfer_message_5);
#else
    if (!waited)
#endif
      waited = 1;
#if SND989_VERSION == 0x020604
    }
#endif
    while (channel < 1000)
      ++channel;
  }
#if SND989_VERSION == 0x020604
  if (waited == 1)
    printf(bank_transfer_message_6);
#endif
  gTransfering = channel + 1;
  sceSdSetTransIntrHandler(channel, snd_TransCallback, 0);
#if SND989_VERSION == 0x020604
  if ((u_int)sceSdVoiceTrans(channel, 0, samples, (u_int)bank->samples + offset, size) <
      (u_int)size) {
#else
  if ((u_int)sceSdVoiceTrans(channel, 0, samples, location + offset, size) < (u_int)size) {
#endif
    snd_FreeSPUDMA(gTransfering - 1);
    gTransfering = 0;
#if SND989_VERSION == 0x020608
    gLastLoadError = 261;
#endif
    return -12;
  }
  return 0;
}

int snd_IsCurrentTransferComplete(void) {
  return gTransfering == 0;
}

int snd_EndBankTransfer(struct SoundBank* bank) {
  int index;
  struct SoundBank* bank_walk;
#if SND989_VERSION == 0x020608
  struct SFXBlock2 *block = (struct SFXBlock2*)bank, *block_walk;
  if (bank->data_id == SOUND_BANK_V2_ID) {
#endif
    for (index = 0; index < bank->tone_count; ++index)
      bank->tones[index].sample_address += (u_int)bank->samples;
    for (index = 0; index < bank->program_count; ++index)
      bank->programs[index].tones =
          (struct Tone*)((u_int)bank + (u_int)bank->programs[index].tones);
    if (!gBankListHead) {
      gBankListHead = bank;
    } else {
      for (bank_walk = gBankListHead; bank_walk->next; bank_walk = bank_walk->next) {
      }
      bank_walk->next = bank;
    }

#if SND989_VERSION == 0x020608
  } else {
    for (index = 0; index < block->grain_count; ++index) {
      if (block->grains[index].type == 1)
        block->grains[index].data.tone.sample_address += (u_int)block->samples;
    }
    for (index = 0; index < block->sound_count; ++index)
      block->sounds[index].FirstGrain =
          (struct SFXGrain2*)((u_int)block->grains + (u_int)block->sounds[index].FirstGrain);
    if (!gBlockListHead) {
      gBlockListHead = block;
    } else {
      for (block_walk = gBlockListHead; block_walk->next; block_walk = block_walk->next) {
      }
      block_walk->next = block;
    }
  }
#endif
  return 0;
}

void snd_RemoveBank(SoundBank* bank) {
  SoundBank* walk = gBankListHead;
  SoundBank* previous = 0;
  int disabled;
  if (bank->data_id == 0x6b6c4253) {
    snd_RemoveBlock((SFXBlock2*)bank);
    return;
  }
  if (!bank)
    return;
  while (walk && walk != bank) {
    previous = walk;
    walk = walk->next;
  }
  if (walk == bank) {
    snd_StopAllSoundsInBank(bank, 1);
    if (previous)
      previous->next = walk->next;
    else
      gBankListHead = walk->next;
    disabled = CpuDisableIntr();
    snd_SRAMFree((u_int)bank->samples, bank->sample_size);
    if (!disabled)
      CpuEnableIntr();
  }
}

void snd_RemoveBlock(SFXBlock2* block) {
  SFXBlock2* walk = gBlockListHead;
  SFXBlock2* previous = 0;
  int disabled;
  if (!block)
    return;
  while (walk && walk != block) {
    previous = walk;
    walk = walk->next;
  }
  if (walk == block) {
    snd_StopAllSoundsInBank((SoundBank*)block, 1);
    if (previous)
      previous->next = walk->next;
    else
      gBlockListHead = walk->next;
    disabled = CpuDisableIntr();
    snd_SRAMFree((u_int)block->samples, block->allocation_size);
    if (!disabled)
      CpuEnableIntr();
  }
}

void snd_UnloadAllBanks(void) {
  while (gBankListHead)
    unload_bank(gBankListHead);
  while (gBlockListHead)
    unload_block(gBlockListHead);
}

#if SND989_VERSION == 0x020604

#if SND989_VERSION == 0x020604
int snd_BlockTransfer(struct SFXBlock2* bank,
                      void* samples,
                      int size,
                      int offset,
                      int continuing,
                      void (*done)(void)) {
  int channel;
  int waited = 0;
  // BUG forgot to call function
  if (!snd_SystemRunning) {
    printf(block_transfer_message_1);
    return -2;
  }
  if (gTransfering) {
    printf(block_transfer_message_2);
    return -10;
  }
  if (!continuing) {
    if (bank->flags & 4) {
      int disabled = CpuDisableIntr();
      bank->samples = (void*)snd_SRAMMalloc(bank->allocation_size);
      if (!disabled)
        CpuEnableIntr();
      if (!bank->samples) {
        printf(block_transfer_message_3, bank->allocation_size);
        return -11;
      }
    } else {
      int disabled = CpuDisableIntr();
      if (!snd_SRAMMarkUsed((u_int)bank->samples, bank->allocation_size)) {
        if (!disabled)
          CpuEnableIntr();
        printf(block_transfer_message_4, bank->allocation_size, bank->samples);
        return -11;
      }
      if (!disabled)
        CpuEnableIntr();
    }
    if (!(bank->flags & 1)) {
      bank->sounds = (struct SFX2*)((u_int)bank->sounds + (u_int)bank);
      bank->grains = (struct SFXGrain2*)((u_int)bank->grains + (u_int)bank);
      bank->flags |= 1;
    }
  }
  gTransferDoneCallback = done;
  while (1) {
    channel = snd_GetFreeSPUDMA();
    if (channel != -1)
      break;
    if (!waited) {
      printf(block_transfer_message_5);
      waited = 1;
    }
    while (channel < 1000)
      ++channel;
  }
  if (waited == 1)
    printf(block_transfer_message_6);
  gTransfering = channel + 1;
  sceSdSetTransIntrHandler(channel, snd_TransCallback, 0);
  if ((u_int)sceSdVoiceTrans(channel, 0, samples, (u_int)bank->samples + offset, size) <
      (u_int)size) {
    snd_FreeSPUDMA(gTransfering - 1);
    gTransfering = 0;
    return -12;
  }
  return 0;
}
#endif

#if SND989_VERSION == 0x020604
int snd_EndBlockTransfer(struct SFXBlock2* block) {
  int index;
  struct SFXBlock2* block_walk;
  for (index = 0; index < block->grain_count; ++index) {
    if (block->grains[index].type == 1)
      block->grains[index].data.tone.sample_address += (u_int)block->samples;
  }
  for (index = 0; index < block->sound_count; ++index)
    block->sounds[index].FirstGrain =
        (struct SFXGrain2*)((u_int)block->grains + (u_int)block->sounds[index].FirstGrain);
  if (!gBlockListHead) {
    gBlockListHead = block;
  } else {
    for (block_walk = gBlockListHead; block_walk->next; block_walk = block_walk->next) {
    }
    block_walk->next = block;
  }

  return 0;
}
#endif

#endif

void snd_ResolveBankXREFS(void) {
  struct SoundBank *bank, *reference;
  struct SFXBlock2* block;
  struct Sound *sound, *source;
  struct MIDISound* midi;
  int index, is_reference;
  for (bank = gBankListHead; bank; bank = bank->next) {
    for (index = 0; index < bank->sound_count; ++index) {
      if (bank->sounds[index].flags & SOUND_XREF_RESOLVED) {
        bank->sounds[index].type = bank->sounds[index].original_type;
        bank->sounds[index].bank = bank->sounds[index].original_bank;
      }
      reference = 0;
      is_reference = 0;
      if (bank->sounds[index].type == 6 || bank->sounds[index].type == 7) {
        reference = snd_FindBankByID((u_int)bank->sounds[index].bank);
        is_reference = 1;
      }
      if (bank->sounds[index].type >= 8 && bank->sounds[index].type <= 12) {
        reference = snd_FindBankByNum((signed char)(u_int)bank->sounds[index].bank);
        is_reference = 1;
      }
      if (reference) {
        sound = (struct Sound*)(index * sizeof(struct Sound) + (u_int)bank->sounds);
        if (sound->type >= 6 && sound->type <= 9) {
          if (sound->xref_sound < reference->sound_count) {
            source = &reference->sounds[sound->xref_sound];
            sound->type = source->type;
            sound->bank = reference;
            sound->program = source->program;
            if (bank->sounds[index].original_type == 6 || bank->sounds[index].original_type == 8) {
              sound->note = source->note;
              sound->fine = source->fine;
              sound->volume = source->volume;
              sound->pan = source->pan;
              sound->volume_group = source->volume_group;
            }
          } else {
            sound->bank = 0;
          }
        } else {
          sound->type -= 10;
          bank->sounds[index].bank = reference;
        }
        bank->sounds[index].flags |= SOUND_XREF_RESOLVED;
      } else {
        if (is_reference)
          bank->sounds[index].bank = 0;
        else
          bank->sounds[index].bank = bank;
        if (bank->sounds[index].type == 4 || bank->sounds[index].type == 5) {
          midi = (struct MIDISound*)&bank->sounds[index];
          midi->midi = snd_FindMIDIBlock(midi->midi_id);
#if SND989_VERSION == 0x020608
          if (!midi->midi)
            snd_ShowError(90, midi->midi_id, 0, 0, 0);
#endif
        }
      }
    }
    bank->flags |= BANK_XREF_RESOLVED;
  }
  for (block = gBlockListHead; block; block = block->next)
    block->flags |= BANK_XREF_RESOLVED;
}

SoundBank* snd_FindBankByID(u_int id) {
  SoundBank* bank;
  for (bank = gBankListHead; bank; bank = bank->next) {
    if (bank->id == id)
      return bank;
  }
  return (SoundBank*)snd_FindBlockByID(id);
}

SFXBlock2* snd_FindBlockByID(u_int id) {
  SFXBlock2* block;
  for (block = gBlockListHead; block; block = block->next) {
    if (block->id == id)
      return block;
  }
  return block;
}

SoundBank* snd_FindBankByNum(signed char number) {
  SoundBank* bank = gBankListHead;
  if (bank) {
    for (; bank; bank = bank->next) {
      if (bank->number == number)
        return bank;
    }
    return (SoundBank*)snd_FindBlockByNum(number);
    // TODO hack??
    while (0) {
    }
  }
  return (SoundBank*)snd_FindBlockByNum(number);
}

SFXBlock2* snd_FindBlockByNum(signed char number) {
  SFXBlock2* block;
  for (block = gBlockListHead; block; block = block->next) {
    if (block->number == number)
      return block;
  }
  return block;
}

int snd_RegisterMIDI(MIDIBlockHeader* midi) {
  MultiMIDIBlockHeader* multi;
  MIDIBlockHeader* child;
  int i;
  switch (midi->data_id) {
    case SND_MIDI_ID:
      midi->bank = find_midi_bank(midi->bank_id);
#if SND989_VERSION == 0x020608
      if (!midi->bank)
        snd_ShowError(91, midi->bank_id, 0, 0, 0);
#endif
      midi->data_start += (u_int)midi;
      midi->next = 0;
      midi->parent = 0;
      snd_InsertMIDIBlockInList(midi);
      break;
    case SND_MULTI_MIDI_ID:
      multi = (MultiMIDIBlockHeader*)midi;
      multi->next = 0;
      snd_InsertMIDIBlockInList((MIDIBlockHeader*)multi);
      for (i = 0; i < multi->block_count; i++) {
        multi->blocks[i] = (MIDIBlockHeader*)((u_int)multi->blocks[i] + (u_int)multi);
        child = multi->blocks[i];
        snd_RegisterMIDI(child);
        child->parent = multi;
      }
      break;
  }
  return 0;
}

void snd_UnregisterMIDI(MIDIBlockHeader* midi) {
  MultiMIDIBlockHeader* multi;
  int i;
  MIDIBlockHeader* walk;
  static int working_mmd = 0;
  if (!working_mmd)
    snd_MIDIStreamGone(midi);
  if (midi->data_id == SND_MULTI_MIDI_ID) {
    multi = (MultiMIDIBlockHeader*)midi;
    working_mmd = 1;
    for (i = 0; i < multi->block_count; i++) {
      snd_UnregisterMIDI(multi->blocks[i]);
    }
    working_mmd = 0;
  }
  if (!gMIDIListHead)
    return;
  if (gMIDIListHead == midi) {
    gMIDIListHead = midi->next;
  } else {
    walk = gMIDIListHead;
    while (walk->next != midi)
      walk = walk->next;
    walk->next = midi->next;
  }
}

MIDIBlockHeader* snd_FindMIDIBlock(u_int id) {
  MIDIBlockHeader* walk;
  for (walk = gMIDIListHead; walk; walk = walk->next) {
    if (walk->id == id)
      break;
  }
  return walk;
}

void snd_InsertMIDIBlockInList(MIDIBlockHeader* midi) {
  MIDIBlockHeader* walk;
  if (!gMIDIListHead) {
    gMIDIListHead = midi;
  } else {
    walk = gMIDIListHead;
    while (walk->next)
      walk = walk->next;
    walk->next = midi;
  }
}

void snd_MIDIStreamGone(MIDIBlockHeader* midi) {
  SoundBank* bank;
  int i;
  MIDISound* sound;
  for (bank = gBankListHead; bank; bank = bank->next) {
    for (i = 0; i < bank->sound_count; i++) {
      sound = (MIDISound*)&bank->sounds[i];
      if (sound->midi == midi) {
        snd_StopAllHandlersForSound((Sound*)sound, 0);
        sound->midi = 0;
      }
    }
  }
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
void snd_SetCDSifReg(int reg8, int reg9) {
  int id;
  int disabled;
  int intr_state;
  if (reg8) {
    gCDSifRegPacket.reg = 8;
    gCDSifRegPacket.value = reg8;
    disabled = CpuSuspendIntr(&intr_state);
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                       0);
    if (!disabled)
      CpuResumeIntr(intr_state);
    while (sceSifDmaStat(id) >= 0) {
    }
  }
  gCDSifRegPacket.reg = 9;
  gCDSifRegPacket.value = reg9;
  disabled = CpuSuspendIntr(&intr_state);
  id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                     0);
  if (!disabled)
    CpuResumeIntr(intr_state);
  while (sceSifDmaStat(id) >= 0) {
  }
  if (!reg8) {
    gCDSifRegPacket.reg = 8;
    gCDSifRegPacket.value = 0;
    disabled = CpuSuspendIntr(&intr_state);
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                       0);
    if (!disabled)
      CpuResumeIntr(intr_state);
    while (sceSifDmaStat(id) >= 0) {
    }
  }
}
#endif

#endif
#if SND989_VERSION == 0x020604

#if SND989_VERSION == 0x020604

int snd_TransCallback(int channel, void* data) {
  if (!gTransfering)
    return 1;
  snd_FreeSPUDMA(gTransfering - 1);
  gTransfering = 0;
  if (gTransferDoneCallback)
    gTransferDoneCallback();
  return 0;
}

#endif

#endif

void* snd_IOPMemAlloc(int size, int use, int* actual_size) {
  void* memory;
  int max_available;
  int from_where;
  int disabled = CpuDisableIntr();
  if (use == 8) {
    max_available = QueryMaxFreeMemSize();
    if (max_available & 15)
      max_available = 16 * (max_available / 16);
    if (max_available < size)
      size = max_available;
    if (!size) {
      if (!disabled)
        CpuEnableIntr();
      return 0;
    }
  }
  switch (use) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
      from_where = 1;
      break;
    case 7:
      from_where = 1;
      break;
    case 8:
      from_where = 0;
      break;
    default:
      from_where = 0;
      break;
  }
  memory = AllocSysMemory(from_where, size, 0);
  if (memory && actual_size)
    *actual_size = size;
  if (!disabled)
    CpuEnableIntr();
  return memory;
}

void snd_IOPMemFree(void* memory) {
  int disabled = CpuDisableIntr();
  FreeSysMemory(memory);
  if (!disabled)
    CpuEnableIntr();
}
