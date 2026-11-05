#include "sce/iop/989snd/vag_stream.h"

#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/loader_cd.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/loader_memory.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/989snd/stream_callbacks.h"
#include "sce/iop/989snd/stream_cd.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/cdvdman/libcdvd.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sifcmd/sifcmd.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thsemap/thsemap.h"

#if SND989_VERSION == 0x020604

static const char stream_message_1[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: Already Inited!\n";
static const char stream_message_2[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: couldn't create DMA thead.\n";
static const char stream_message_3[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: couldn't create Data Load thead.\n";
static const char stream_message_4[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: couldn't create DMA done thead.\n";
static const char stream_message_5[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: couldn't create Data Load done thead.\n";
static const char stream_message_6[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: Couldn't allocate IOP memory for stream handlers!\n";
static const char stream_message_7[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: Couldn't allocate IOP memory for stream buffers!\n";
static const char stream_message_8[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: Couldn't allocate SPU memory for stream buffers!\n";
static const char stream_message_9[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: Couldn't allocate IOP memory for stream headers!\n";
static const char stream_message_10[] __attribute__((aligned(4))) =
    "snd_InitVAGStreaming: Couldn't allocate IOP memory for stream safe reads!\n";
static const char stream_message_11[] __attribute__((aligned(4))) =
    "sndPlayVAGStreamByLoc: VAG Streaming not inited!\n";
static const char stream_message_12[] __attribute__((aligned(4))) =
    "snd_PlayVAGStreamByLoc: WHAT !?!? There was a stream available but no handler. THIS IS BAD!\n";
static const char stream_message_13[] __attribute__((aligned(4))) =
    "SCEI strikes again! fudging value for NAX!\n";
static const char stream_message_14[] __attribute__((aligned(4))) =
    "STREAM.C: Stream stopped... channel out of sync.\n";
static const char stream_message_15[] __attribute__((aligned(4))) =
    "STREAM.C: Greatest diff -> %d\n";
static const char stream_message_16[] __attribute__((aligned(4))) =
    "STREAM.C: stream read out of sync with play (%d, %d)! Stopping stream!\n";
static const char stream_message_17[] __attribute__((aligned(4))) =
    "STREAM.C: Killing VAG stream!\n";
static const char stream_message_18[] __attribute__((aligned(4))) =
    "STREAM.C: [%d] Different results for NAX... 0x%x, 0x%x, 0x%x\n";
static const char stream_message_19[] __attribute__((aligned(4))) =
    "STREAM.C: Retrying sceSdGetAddr...\n";
static const char stream_message_20[] __attribute__((aligned(4))) =
    "STREAM.C: retry failed. Using last good known position 0x%x.\n";
static const char stream_message_21[] __attribute__((aligned(4))) =
    "STREAM.C: retry success. Using 0x%x.\n";
static const char stream_message_22[] __attribute__((aligned(4))) =
    "snd_CheckVAGStreamProgress: Stream stopped... too many errors!\n";
static const char stream_message_23[] __attribute__((aligned(4))) = "not it any buffer!!!!\n";
static const char stream_message_24[] __attribute__((aligned(4))) =
    "snd_CheckVAGStreamProgress: buffer changed too soon (ErrorAccumulator incremented)!\n";
static const char stream_message_25[] __attribute__((aligned(4))) =
    "989SND Stream error: (0x%x) stream relooped (%d, %d)!!!\n";
static const char stream_message_26[] __attribute__((aligned(4))) =
    "989SND Stream error: Try increasing buffer size!";
static const char stream_message_27[] __attribute__((aligned(4))) =
    "(0x%x) Played %d more bytes then are in the VAG!\n(%d, %d, %d)\n";
static const char stream_message_28[] __attribute__((aligned(4))) =
    "snd_StreamSafeCdReadEE: Streaming not inited! should have been handled by the EE!\n";
static const char stream_message_29[] __attribute__((aligned(4))) =
    "989snd | snd_StreamSafeCdReadEEm: Already a data load in progress!\n";
static const char stream_message_30[] __attribute__((aligned(4))) =
    "snd_StreamSafeCdSync: can't block until CD is finished because there is already a thread "
    "waiting!\n";
static const char stream_message_31[] __attribute__((aligned(4))) =
    "snd_KickDataRead: Error calling sceCdRead()!\n";
static const char stream_message_32[] __attribute__((aligned(4))) =
    "snd_KickVAGRead: Error calling sceCdRead()!\n";
static const char stream_message_33[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Error during VAG stream Read!\n";
static const char stream_message_34[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Error during Data Read!\n";
static const char stream_message_35[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Error DMAing data to EE memory!!!\n";
static const char stream_message_36[] __attribute__((aligned(4))) =
    "STREAM.C: Couldn't add load buffer!\n";
static const char stream_message_37[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Interleaved stream's block sizes don't match the current buffer "
    "sizes!\n";
static const char stream_message_38[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Reinterleave the stream with a block size of %d.\n";
static const char stream_message_39[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Initialize the stream lib with a buffer size of %d.\n";
static const char stream_message_40[] __attribute__((aligned(4))) =
    "snd_VAGStreamLoadDoneThread: Not enough contiguous stream buffers to play interleaved "
    "stream!\n";
static const char stream_message_41[] __attribute__((aligned(4))) =
    "STREAM.C: Invalid VAG at sector %d offset %d! Bad Sector? Wrong CD location?\n";
static const char stream_message_42[] __attribute__((aligned(4))) =
    "[%d] STREAM.C: tried to DMA buffer that isn't ready (stream buffers too small?)!\n";
static const char stream_message_43[] __attribute__((aligned(4))) =
    "[%d] STREAM.C: Tried to add a buffer to the load list and it was already there!\n";
static const char stream_message_44[] __attribute__((aligned(4))) =
    "snd_StreamSafeCheckCDIdle: Streaming not inited!\n";
static const char stream_message_45[] __attribute__((aligned(4))) =
    "snd_StreamSafeCheckCDIdle: sleeping for CD completion (%d,%d,%d)...\n";
static const char stream_message_46[] __attribute__((aligned(4))) =
    "snd_StreamSafeCheckCDIdle: WOKE UP! CD must be done!...\n";
static const char stream_message_47[] __attribute__((aligned(4))) =
    "snd_AddStreamToHandler: Stream already in handler!\n";
static const char stream_message_48[] __attribute__((aligned(4))) =
    "snd_SetVAGStreamVolPan: Holy crap! No stream attached to sound handler (0x%x)!!!\n";
static const char stream_message_49[] __attribute__((aligned(4))) = "\tSCECdErFAIL\n";
static const char stream_message_50[] __attribute__((aligned(4))) = "\tSCECdErNO\n";
static const char stream_message_51[] __attribute__((aligned(4))) = "\tSCECdErEOM\n";
static const char stream_message_52[] __attribute__((aligned(4))) = "\tSCECdErTRMOPN\n";
static const char stream_message_53[] __attribute__((aligned(4))) = "\tSCECdErREAD\n";
static const char stream_message_54[] __attribute__((aligned(4))) = "\tSCECdErPRM\n";
static const char stream_message_55[] __attribute__((aligned(4))) = "\tSCECdErILI\n";
static const char stream_message_56[] __attribute__((aligned(4))) = "\tSCECdErIPI\n";
static const char stream_message_57[] __attribute__((aligned(4))) = "\tSCECdErCUD\n";
static const char stream_message_58[] __attribute__((aligned(4))) = "\tSCECdErNORDY\n";
static const char stream_message_59[] __attribute__((aligned(4))) = "\tSCECdErNODISC\n";
static const char stream_message_60[] __attribute__((aligned(4))) = "\tSCECdErOPENS\n";
static const char stream_message_61[] __attribute__((aligned(4))) = "\tSCECdErCMD\n";
static const char stream_message_62[] __attribute__((aligned(4))) = "\tSCECdErABRT\n";

#else
extern int gDoneDMASema;
extern int gDoneLoadSema;
#endif
extern int gMasterReadBufferSize;
extern int gThreadPriority_STREAM;
extern int gDMAThread, gDataLoadThread, gDMADoneThread, gLoadDoneThread;
extern int gStartDMASema, gStartDataLoadSema;
extern struct VAGBuffer* gVAGReadBusy;
extern struct VAGBuffer* gDMABusy[2];

extern int gStartDataLoadSema;

extern struct VAGStreamQueEntry gVAGStreamQue[VAG_STREAM_QUEUE_CAPACITY];

extern int gStartDataLoadSema;

extern int gNumVAGStreams;
extern int gSTickSema;
struct VAGBuffer;
extern struct VAGBuffer* gVAGReadBusy;
extern struct VAGBuffer* VAGStreamLoadList;
extern int gCDIdleWaiter;
void snd_StopVAGStream(u_int handle);

#define STREAM_HANDLE_ACTIVE 0x80000000u
#define STREAM_STOP_ATTEMPTS 3
#define STREAM_STOP_WAIT_TICKS 480

extern int gNumStreamsPlaying;

extern int gNumVAGStreams;

extern int gNumStreamsPlaying;

#if SND989_VERSION == 0x020608
extern struct VAGBuffer* gVAGReadBusy;
extern struct VAGBuffer* gDMABusy[2];

#endif

#if SND989_VERSION == 0x020604
extern int gNumStreamsPlaying;
#endif
extern int gStartDataLoadSema;

extern int gNumStreamsPlaying;
extern int gStartDMASema;

#define VAG_FRAME_BYTES 16
#define VAG_FRAME_SAMPLES 28
#define STREAM_TIME_UNITS_PER_SECOND 1024

#ifndef SND_STREAM_BUFFER_STATE
#define SND_STREAM_BUFFER_STATE

#endif

#ifndef SND_STREAM_BUFFER_STATE
#define SND_STREAM_BUFFER_STATE

#endif

extern u_int gCdSpinMode;
extern void* gCurrentReadBuffer;
extern u_int gSSReadLoc;
extern int gReadIsEE;
extern int gStartDataLoadSema;

extern void* gMasterReadBuffer;
extern void* gCurrentReadBuffer;
extern u_int gSSReadLoc;
extern int gReadIsEE;
extern int gStartDataLoadSema;

#if SND989_VERSION == 0x020608
static inline void stream_report_read_busy(void) {
  register u_int error asm("$4") = 59;
  register u_int arg1 asm("$5") = 0;
  register u_int arg2 asm("$6") = 0;
  register u_int arg3 asm("$7") = 0;
  // TODO hack
  asm("" : : "r"(error), "r"(arg1), "r"(arg2), "r"(arg3));
  snd_ShowError(error, arg1, arg2, arg3, 0);
}

#endif
extern int gHalfBufferSize;
extern int gMasterReadBufferSize;
extern u_int gCdSpinMode;
extern u_int gSSReadLoc;
extern void* gCurrentReadBuffer;
extern int gReadIsEE;

extern u_int gCdSpinMode;
extern u_int gLastReadSector, gLastReadAmount;
extern void* gLastReadIOPLoc;

extern int gReadIsEE;

extern struct VAGBuffer* gVAGReadBusy;
extern int gStartDataLoadSema;
#ifndef SND_STREAM_IDLE_STATE
#define SND_STREAM_IDLE_STATE
#endif

#define VAG_MAGIC_BE 0x70474156u
#define VAG_MAGIC_LE 0x56414770u
#define VPK_MAGIC 0x56504b20u
#define VAG_HEADER_SIZE 48
#define VAG_SWAP32(x) (((x) >> 24) | (((x) >> 8) & 0xff00) | (((x) << 8) & 0xff0000) | ((x) << 24))

#if SND989_VERSION == 0x020604
#define vag_stream ordinary
#else
#define vag_stream master
#endif

struct VAGFileHeader {
  u_int magic, version, reserved, data_size, sample_rate;
};

struct VPKChannelInfo {
  signed char volume, group;
  short pan;
};

struct VPKFileHeader {
  u_int magic, data_size, reserved, buffer_size;
  int sample_rate;
  u_int channels;
  struct VPKChannelInfo channel[1];
};

extern int gDoneLoadSema, gStartDataLoadSema, gStartDMASema, gCDIdleWaiter;
extern struct VAGBuffer* gVAGReadBusy;
extern int gLastVAGReadError, gReadIsEE;
extern u_int gLastReadSector, gLastReadAmount, gSSReadSectors, gSSReadLoc;
extern void* gLastReadIOPLoc;
extern void* gCurrentReadBuffer;

#ifndef SND_STREAM_BUFFER_STATE
#define SND_STREAM_BUFFER_STATE

#endif

extern struct VAGBuffer* gDMABusy[2];
extern int gStartDMASema;

struct VAGBuffer;
extern struct VAGBuffer* gVAGReadBusy;
#ifndef SND_STREAM_IDLE_STATE
#define SND_STREAM_IDLE_STATE
#endif

struct VAGBuffer;
extern struct VAGBuffer* VAGStreamDMAList;
extern int gStartDMASema;

extern struct VAGBuffer* gDMABusy[2];

extern int gDoneDMASema;
extern int gStartDMASema;

extern int gStartDMASema;
extern int gStartDataLoadSema;

extern int gNumStreamsPlaying;
extern struct VoiceFlags gAwaitingKeyOn;
extern struct VoiceFlags gReverbVoices;

#define ADPCM_FRAME_SIZE 16
#define ADPCM_END 1
#define ADPCM_REPEAT 2
#define ADPCM_LOOP_START 4

extern int gDoneDMASema, gDoneLoadSema;
extern int gLastDMA0Complete, gLastDMA1Complete;
extern int gNumVAGStreams;

#if SND989_VERSION == 0x020604
extern int gDoneLoadSema;
extern int gDoneDMASema;

#endif
extern int gVAGStreamingInited;
extern int gNumVAGStreams;
void snd_StopAllStreams(void);
void snd_StopVAGStream(u_int handle);
void snd_FreeVAGStreamResources(void);

extern int gNumVAGStreams;
#define VAG_STREAM_HANDLE_TYPE 0x04000000u

#if SND989_VERSION == 0x020604
static inline u_int get_stream_location(u_int handle) {
  struct VAGStreamHandler* handler;
  struct VAGStreamHeader* stream;
  u_int samples, tail;
  u_int seconds;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  seconds = 1;
  if (!handler || ((struct VAGStreamHeader*)handler->SH.Sound)->sample_rate == -seconds) {
    snd_UnlockMasterTick();
    return 0;
  }
  stream = handler->SH.Sound;
  samples = (stream->bytes_played / VAG_FRAME_BYTES) * VAG_FRAME_SAMPLES;
  tail = stream->bytes_played % VAG_FRAME_BYTES;
  if (tail > 4)
    samples += (tail - 4) * 2;
  seconds = samples / stream->sample_rate;
  samples -= seconds * stream->sample_rate;
  samples *= STREAM_TIME_UNITS_PER_SECOND;
  samples /= stream->sample_rate;
  seconds = seconds * STREAM_TIME_UNITS_PER_SECOND + samples;
  snd_UnlockMasterTick();
  return seconds;
}

#endif
struct VAGStreamHandler* gVAGStreamHandler = 0;
int gNumVAGStreams = 0;
int gVAGStreamingInited = 0;
u_int gCdSpinMode = 1;
#if SND989_VERSION == 0x020608
int gHalfBufferSize = 0;
#endif
int gMasterReadBufferSize = 0;
#if SND989_VERSION == 0x020608
int gMasterReadBufferFirstSector = 0;
int gMasterReadBufferLastSector = 0;
#endif
void* gMasterReadBuffer = 0;
void* gCurrentReadBuffer = 0;
u_int gSSNeedSectors = 0;
u_int gSSReadSectors = 0;
u_int gSSReadLoc = 0;
u_int gBreakDataRead = 0;
int gLastVAGReadError = 0;
int gLastDATAReadError = 0;
int gReadWaitThread = -1;
int gReadIsEE = 0;
#if SND989_VERSION == 0x020608
u_int gLastReadSector = 0;
u_int gLastReadAmount = 0;
void* gLastReadIOPLoc = 0;
#endif
struct VAGStreamHeader* VAGStreamList = 0;
struct VAGBuffer* VAGStreamLoadList = 0;
struct VAGBuffer* VAGStreamLoadListTail = 0;
struct VAGBuffer* VAGStreamDMAList = 0;
struct VAGBuffer* VAGStreamDMAListTail = 0;
struct VAGBuffer* gVAGReadBusy = 0;
int gDataReadBusy = 0;
struct VAGBuffer* gDMABusy[2] = {0, 0};
#if SND989_VERSION == 0x020604
int gDMAInUse[2] = {0, 0};
int gWaitingDMAComplete = 0;
#endif
int gCDIdleWaiter = 0;
int gStartDataLoadSema = 0;
int gStartDMASema = 0;
#if SND989_VERSION == 0x020604
int gDoneDMASema = 0;
int gDoneLoadSema = 0;
#endif
int gDMAThread = 0;
int gDataLoadThread = 0;
int gDMADoneThread = 0;
int gLoadDoneThread = 0;
#if SND989_VERSION == 0x020604
int gLastDMA0Complete = 0;
int gLastDMA1Complete = 0;
#endif
int gNumStreamsPlaying = 0;
#if SND989_VERSION == 0x020604
int gSettingStreamLoopAddress = 0;
#else
static int in = 0;
static int in_func = 0;
int gSafeCdReadBusy = 0;
#endif

#if SND989_VERSION == 0x020604
void snd_InitVAGStreaming(int channels, int buffer_size, int mode) {
#else
void snd_InitVAGStreaming(int channels, int buffer_size, int mode, int enable_read_buffer) {
#endif
  int index, group;
  struct VAGStreamHeader *header, **list;
  struct ThreadParam thread;
  struct SemaParam sema;
  char* iop_buffer;
  u_int spu_buffer;
  int intr_state, disabled;
  if (VAGStreamList) {
#if SND989_VERSION == 0x020604
    printf(stream_message_1);
#else
    snd_ShowError(37, 0, 0, 0, 0);
#endif
    return;
  }
  gCdSpinMode = mode;
  gNumVAGStreams = channels;
  index = buffer_size / 4096;
  if (index < 2)
    index = 2;
  buffer_size = index * 4096;
#if SND989_VERSION == 0x020608
  gHalfBufferSize = buffer_size >> 1;
#endif

  thread.attr = TH_C;
  thread.entry = snd_VAGStreamDMAThread;
  thread.stackSize = 2048;
  thread.option = 0;
  thread.initPriority = gThreadPriority_STREAM;
  gDMAThread = CreateThread(&thread);
  if (gDMAThread <= 0)
#if SND989_VERSION == 0x020604
    printf(stream_message_2);
#else
    snd_ShowError(38, 0, 0, 0, 0);
#endif
  thread.attr = TH_C;
  thread.entry = snd_VAGStreamDataLoadThread;
  thread.stackSize = 2048;
  thread.option = 0;
  thread.initPriority = gThreadPriority_STREAM + 2;
  gDataLoadThread = CreateThread(&thread);
  if (gDataLoadThread <= 0)
#if SND989_VERSION == 0x020604
    printf(stream_message_3);
#else
    snd_ShowError(39, 0, 0, 0, 0);
#endif
  thread.attr = TH_C;
  thread.entry = snd_VAGStreamDMADoneThread;
  thread.stackSize = 2048;
  thread.option = 0;
  thread.initPriority = gThreadPriority_STREAM + 1;
  gDMADoneThread = CreateThread(&thread);
  if (gDMADoneThread <= 0)
#if SND989_VERSION == 0x020604
    printf(stream_message_4);
#else
    snd_ShowError(40, 0, 0, 0, 0);
#endif
  thread.attr = TH_C;
  thread.entry = snd_VAGStreamLoadDoneThread;
  thread.stackSize = 2048;
  thread.option = 0;
  thread.initPriority = gThreadPriority_STREAM + 3;
  gLoadDoneThread = CreateThread(&thread);
  if (gLoadDoneThread <= 0)
#if SND989_VERSION == 0x020604
    printf(stream_message_5);
#else
    snd_ShowError(41, 0, 0, 0, 0);
#endif
  sema.attr = SA_THPRI;
  sema.initCount = 0;
  sema.maxCount = 1;
  gStartDataLoadSema = CreateSema(&sema);
  gStartDMASema = CreateSema(&sema);
  gDoneDMASema = CreateSema(&sema);
  gDoneLoadSema = CreateSema(&sema);
  VAGStreamLoadList = 0;
  VAGStreamLoadListTail = 0;
  VAGStreamDMAList = 0;
  VAGStreamDMAListTail = 0;
  gVAGReadBusy = 0;
  gDMABusy[0] = 0;
  gDMABusy[1] = 0;
  gVAGStreamHandler = gAllocProc(channels * sizeof(struct VAGStreamHandler), 4, 0);
  if (!gVAGStreamHandler) {
#if SND989_VERSION == 0x020604
    printf(stream_message_6);
#else
    snd_ShowError(42, 0, 0, 0, 0);
#endif
    snd_FreeVAGStreamResources();
    return;
  }
  for (index = 0; index < channels; ++index) {
    struct VAGStreamHandler* handler =
        (struct VAGStreamHandler*)(index * sizeof(struct VAGStreamHandler) +
                                   (u_int)gVAGStreamHandler);
    handler->SH.flags = 0;
    handler->SH.OwnerID = 0x04000001 | (index << 16);
    handler->SH.Sound = 0;
    handler->SH.Effects = 0;
    gVAGStreamHandler[index].SH.VolGroup = 0;
    gVAGStreamHandler[index].SH.Current_PM = 0;
    gVAGStreamHandler[index].SH.Voices[1] = 0;
    gVAGStreamHandler[index].SH.Voices[0] = 0;
    gVAGStreamHandler[index].queue = 0;
    gVAGStreamHandler[index].queued_stream = 0;
    for (group = 0; group < 16; ++group)
      gVAGStreamHandler[index].group_volumes[group] = 127;
  }
  iop_buffer = gAllocProc(buffer_size * channels, 5, 0);
  if (!iop_buffer) {
#if SND989_VERSION == 0x020604
    printf(stream_message_7);
#else
    snd_ShowError(43, 0, 0, 0, 0);
#endif
    snd_FreeVAGStreamResources();
    return;
  }
#if SND989_VERSION == 0x020604
  disabled = CpuDisableIntr();
#else
  disabled = CpuSuspendIntr(&intr_state);
#endif
  spu_buffer = snd_SRAMMalloc(buffer_size * channels);
  if (!disabled) {
#if SND989_VERSION == 0x020604
    CpuEnableIntr();
#else
    CpuResumeIntr(intr_state);
#endif
  }
  if (!spu_buffer) {
#if SND989_VERSION == 0x020604
    printf(stream_message_8);
#else
    snd_ShowError(44, 0, 0, 0, 0);
#endif
    gFreeProc(iop_buffer);
    snd_FreeVAGStreamResources();
    return;
  }
  list = &VAGStreamList;
  for (index = 0; index < channels; ++index) {
    header = gAllocProc(sizeof(struct VAGStreamHeader), 4, 0);
    if (!header) {
#if SND989_VERSION == 0x020604
      printf(stream_message_9);
      disabled = CpuDisableIntr();
#else
      snd_ShowError(45, 0, 0, 0, 0);
      disabled = CpuSuspendIntr(&intr_state);
#endif
      snd_SRAMFree(spu_buffer, buffer_size * channels);
      if (!disabled) {
#if SND989_VERSION == 0x020604
        CpuEnableIntr();
#else
        CpuResumeIntr(intr_state);
#endif
      }
      gFreeProc(iop_buffer);
      snd_FreeVAGStreamResources();
      return;
    }
    header->flags = VAG_STREAM_FREE;
    header->buffer_size = buffer_size;
    header->buffers[0].buffer = iop_buffer + index * (buffer_size / 2);
    header->buffers[1].buffer =
        iop_buffer + (buffer_size / 2) * channels + index * (buffer_size / 2);
    header->buffers[0].spu_buffer = spu_buffer + index * (buffer_size / 2);
    header->buffers[1].spu_buffer =
        spu_buffer + (buffer_size / 2) * channels + index * (buffer_size / 2);
    header->buffers[0].flags = 0;
    header->buffers[1].flags = 0;
    header->buffers[0].owner = header;
    header->buffers[1].owner = header;
    header->buffers[0].next = 0;
    header->buffers[1].next = 0;
    header->buffers[0].is_end = 0;
    header->buffers[1].is_end = 0;
    header->current_buffer = 0;
    header->saved_spu_address = 0;
    header->progress_errors = 0;
    header->last_buffer_tick = 0;
    header->remaining_state[0] = 0;
    header->remaining_state[1] = 0;
    header->voice = 0;
    header->read_state[0] = 0;
    header->read_state[1] = 0;
    header->pitch = 0;
    header->sync_list = 0;
    header->next = 0;
    header->handler = 0;

    *list = header;
    list = &header->next;
  }
#if SND989_VERSION == 0x020608
  if (!enable_read_buffer) {
    gMasterReadBufferSize = 0;
    gMasterReadBuffer = 0;
  } else
#endif
  {
#if SND989_VERSION == 0x020604
    gMasterReadBufferSize = buffer_size >> 1;
#else
    gMasterReadBufferSize = gHalfBufferSize;
#endif
    gMasterReadBuffer = gAllocProc(gMasterReadBufferSize, 5, 0);
    if (!gMasterReadBuffer) {
#if SND989_VERSION == 0x020604
      printf(stream_message_10);
#else
      snd_ShowError(46, 0, 0, 0, 0);
#endif
      snd_FreeVAGStreamResources();
      return;
    }
    gMasterReadBufferSize /= VAG_CD_SECTOR_SIZE;
  }
  {
    int offset;
    index = VAG_STREAM_QUEUE_CAPACITY - 1;
    offset = (VAG_STREAM_QUEUE_CAPACITY - 1) * sizeof(struct VAGStreamQueEntry);
    for (; index >= 0; --index) {
      ((struct VAGStreamQueEntry*)((char*)gVAGStreamQue + offset))->flags = 0;
      offset -= sizeof(struct VAGStreamQueEntry);
    }
  }
  if (gDMAThread > 0)
    StartThread(gDMAThread, 0);
  if (gDataLoadThread > 0)
    StartThread(gDataLoadThread, 0);
  if (gDMADoneThread > 0)
    StartThread(gDMADoneThread, 0);
  if (gLoadDoneThread > 0)
    StartThread(gLoadDoneThread, 0);
  gVAGStreamingInited = 1;
}

int snd_AllocateStreamResources(struct VAGStreamHeader** left,
                                struct VAGStreamHeader** right,
                                int stereo,
                                int get_voices,
                                int volume_group) {
  *left = snd_GetFreeVAGStream();
  if (!*left)
    return 0;
#if SND989_VERSION == 0x020604
  (*left)->flags = VAG_STREAM_ALLOCATED;
#endif
  (*left)->sync_list = 0;
  if (stereo) {
    *right = snd_GetFreeVAGStream();
    if (!*right) {
      (*left)->flags = VAG_STREAM_FREE;
      return 0;
    }
#if SND989_VERSION == 0x020604
    (*right)->flags = VAG_STREAM_ALLOCATED;
#endif
    (*right)->sync_list = 0;
  }
  if (!get_voices) {
    (*left)->voice = -1;
    if (stereo)
      (*right)->voice = -1;
  } else {
#if SND989_VERSION == 0x020608
    snd_LockVoiceAllocator(1);
#endif
    (*left)->voice = snd_AllocateVoice(volume_group, SND_VOICE_PRIORITY_PERMANENT);
    if ((*left)->voice == -1) {
      (*left)->flags = VAG_STREAM_FREE;
      if (stereo)
        (*right)->flags = VAG_STREAM_FREE;
#if SND989_VERSION == 0x020608
      snd_UnlockVoiceAllocator();
#endif
      return 0;
    }
    gChannelStatus[(*left)->voice].status = 4;
    if (stereo) {
      (*right)->voice = snd_AllocateVoice(volume_group, SND_VOICE_PRIORITY_PERMANENT);
      if ((*right)->voice == -1) {
        (*left)->flags = VAG_STREAM_FREE;
        (*right)->flags = VAG_STREAM_FREE;
        snd_MarkVoiceFree(&gChannelStatus[(*left)->voice]);
#if SND989_VERSION == 0x020608
        snd_UnlockVoiceAllocator();
#endif
        return 0;
      }
      gChannelStatus[(*right)->voice].status = 4;
    }
#if SND989_VERSION == 0x020608
    snd_UnlockVoiceAllocator();
#endif
  }
  return 1;
}

u_int snd_PlayVAGStreamByLoc(u_int first_sector,
                             u_int second_sector,
                             int first_offset,
                             int second_offset,
                             int volume,
                             int pan,
                             int volume_group,
                             int queue,
                             u_int flags) {
  struct VAGStreamHandler* handler;
  struct VAGStreamHeader *left, *right;
  struct VAGStreamQueEntry *first, *second;
  int core, voice;
retry:
  left = 0;
  right = 0;
  if (!VAGStreamList) {
#if SND989_VERSION == 0x020604
    printf(stream_message_11);
#else
    snd_ShowError(47, 0, 0, 0, 0);
#endif
    return 0;
  }
  snd_LockMasterTick();
  if (queue) {
    handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(queue);
    if (!handler) {
      queue = 0;
      snd_UnlockMasterTick();
      goto retry;
    }
    left = handler->SH.Sound;
    if (left->sync_list && second_sector) {
      first = snd_GetFreeQueSlot();
      second = snd_GetFreeQueSlot();
      if (!first || !second) {
        if (first)
          first->flags = 0;
        if (second)
          second->flags = 0;
        snd_UnlockMasterTick();
        return 0;
      }
      first->location = first_sector;
      first->offset = first_offset;
      second->location = second_sector;
      second->offset = second_offset;
      if (flags & 2) {
        first->flags |= 2;
        second->flags |= 2;
      }
      snd_AddQueEntryToHandler(handler, first);
      snd_AddQueEntryToHandler(handler, second);
    } else {
      first = snd_GetFreeQueSlot();
      if (!first) {
        snd_UnlockMasterTick();
        return 0;
      }
      first->location = first_sector;
      first->offset = first_offset;
      if (flags & 2)
        first->flags |= 2;
      snd_AddQueEntryToHandler(handler, first);
    }
    snd_UnlockMasterTick();
    return 1;
  }
  if (!snd_AllocateStreamResources(&left, &right, second_sector != 0, 1, volume_group)) {
    snd_UnlockMasterTick();
    return 0;
  }
  handler = snd_GetFreeVAGStreamHandler();
  if (!handler) {
#if SND989_VERSION == 0x020604
    printf(stream_message_12);
#else
    snd_ShowError(48, 0, 0, 0, 0);
    snd_UnlockMasterTick();
#endif
    return 0;
  }
  handler->SH.Original_Vol = handler->SH.Current_Vol = (127 * volume) >> 10;
  if (pan == -1 || pan == -2)
    pan = 0;
  handler->SH.Original_Pan = handler->SH.Current_Pan = pan;
  handler->SH.Current_PM = 0;
  handler->SH.flags = 0;
  if (flags & 1)
    handler->SH.flags |= 2;
  handler->SH.VolGroup = volume_group;
  handler->SH.Voices[0] = 0;
  handler->SH.Voices[1] = 0;
  core = left->voice / SND_VOICES_PER_CORE;
  voice = left->voice % SND_VOICES_PER_CORE;
  handler->SH.Voices[core] |= 1 << voice;
  handler->num_streams = 1;
  handler->queue = 0;
  handler->queued_stream = 0;
  {
    int count;
    signed char value;
    char* cursor;
    value = 127;
    count = 15;
    cursor = (char*)handler + count;
    for (; count >= 0; --count, --cursor)
      ((struct VAGStreamHandler*)cursor)->group_volumes[0] = value;
  }
  if (second_sector) {
    core = right->voice / SND_VOICES_PER_CORE;
    voice = right->voice % SND_VOICES_PER_CORE;
    handler->SH.Voices[core] |= 1 << voice;
    ++handler->num_streams;
  }
  snd_SetupVAGStream(handler, left, first_sector, first_offset, 0);
  if (flags & 1)
    left->flags |= VAG_STREAM_PREBUFFER;
  if (flags & 2)
    left->flags |= 0x100;
  if (second_sector) {
    snd_SetupVAGStream(handler, right, second_sector, second_offset, 0);
    if (flags & 1)
      right->flags |= VAG_STREAM_PREBUFFER;
    if (flags & 2)
      right->flags |= 0x100;
  }
  snd_AddVAGStreamLoadBuffer(&left->buffers[0]);
  SignalSema(gStartDataLoadSema);
  snd_ActivateHandler(&handler->SH);
  snd_UnlockMasterTick();
  return handler->SH.OwnerID;
}

int snd_GetVAGStreamQueueCount(u_int handle) {
  int count = 0;
  struct VAGStreamHandler* handler;
  struct VAGStreamQueEntry* walk;
  struct VAGStreamHeader* stream;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    stream = handler->SH.Sound;
    if (stream->flags & 0x12) {
      ++count;
      // TODO hack
      __asm__("" : "+r"(count));
    }
    if (handler->queued_stream)
      ++count;
    walk = handler->queue;
    while (walk) {
      ++count;
      walk = walk->next;
    }
  }
  snd_UnlockMasterTick();
  return count;
}

void snd_TryPreBufferQueElement(struct VAGStreamHandler* handler) {
  struct VAGStreamHeader* left = 0;
  struct VAGStreamHeader* right = 0;
  int stereo;
  if (handler->queued_stream || !handler->queue)
    return;
  stereo = snd_GetNumStreamChannels(handler->SH.Sound) == 2;
  if (!snd_AllocateStreamResources(&left, &right, stereo, 0, handler->SH.VolGroup))
    return;
  left->flags |= VAG_STREAM_PREBUFFER;
  if (handler->queue->flags & 2)
    left->flags |= 0x100;
  snd_SetupVAGStream(handler, left, handler->queue->location, handler->queue->offset, 1);
  handler->queue->flags = 0;
  handler->queue = handler->queue->next;
  if (stereo) {
    right->flags |= VAG_STREAM_PREBUFFER;
    if (handler->queue->flags & 2)
      right->flags |= 0x100;
    snd_SetupVAGStream(handler, right, handler->queue->location, handler->queue->offset, 1);
    handler->queue->flags = 0;
    handler->queue = handler->queue->next;
  }
  snd_AddVAGStreamLoadBuffer(&left->buffers[0]);
  handler->queued_stream = left;
  SignalSema(gStartDataLoadSema);
}

void snd_SetupVAGStream(struct VAGStreamHandler* handler,
                        struct VAGStreamHeader* stream,
                        int location,
                        int offset,
                        int queued) {
  stream->read_state[4] = VAG_CD_SECTOR_SIZE;
  stream->read_state[1] = location;
  stream->read_state[2] = location;
  stream->read_state[3] = offset;
  stream->buffers[0].flags = VAG_BUFFER_FIRST;
  stream->buffers[1].flags = 0;
  stream->buffers[0].is_end = 0;
  stream->buffers[1].is_end = 0;
  stream->current_buffer = 0;
  stream->saved_spu_address = 0;
  stream->progress_errors = 0;
  stream->last_buffer_tick = 0;
  stream->remaining_state[0] = 0;
  stream->remaining_state[1] = 0;
  stream->pitch = 0;
  stream->handler = handler;
  stream->sample_rate = -1;
  stream->samples_total = 0;
  stream->bytes_played = 0;
  stream->sync_list = 0;
  stream->group = 0;
  if (!queued)
    snd_AddStreamToHandler(handler, stream);
  else
    snd_AddStreamToHandlerQueue(handler, stream);
}

void snd_StopAllStreams(void) {
  int index;
#if SND989_VERSION == 0x020604
  for (index = 0; index < gNumVAGStreams; ++index) {
    if (gVAGStreamHandler[index].SH.Sound)
      snd_StopVAGStream(gVAGStreamHandler[index].SH.OwnerID);
#else
  int done = 0;
  int attempt;
  int ticks;
  for (attempt = 0; !done && attempt < STREAM_STOP_ATTEMPTS; ++attempt) {
    for (index = 0; index < gNumVAGStreams; ++index) {
      if (gVAGStreamHandler[index].SH.Sound) {
        if (!(gVAGStreamHandler[index].SH.OwnerID & STREAM_HANDLE_ACTIVE))
          gVAGStreamHandler[index].SH.Sound = 0;
        else
          snd_StopVAGStream(gVAGStreamHandler[index].SH.OwnerID);
      }
    }
    for (ticks = 0; !done && ticks < STREAM_STOP_WAIT_TICKS; ++ticks) {
      done = 1;
      for (index = 0; index < gNumVAGStreams && done; ++index) {
        if (gVAGStreamHandler[index].SH.Sound)
          done = 0;
      }
      if (!done)
        WaitSema(gSTickSema);
    }
  }
  if (attempt == STREAM_STOP_ATTEMPTS && !done)
    printf("snd_StopAllStreams: ERROR! tried 3 times to stopp all streams!\n");
  if (!gVAGReadBusy && !gDataReadBusy && !VAGStreamLoadList && gCDIdleWaiter) {
    WakeupThread(gCDIdleWaiter);
    gCDIdleWaiter = 0;
#endif
  }
}

void snd_PauseVAGStream(u_int handle) {
  sceSdBatch batch[3];
  int core, voice;
  u_int offset = 0;
  u_int address;
  int buffer = 0;
  u_int second, third;
  struct VAGStreamHeader* stream;
  struct VAGStreamHandler* handler;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler && !(handler->SH.flags & 2)) {
    stream = handler->SH.Sound;
    handler->SH.flags |= 2;
    while (stream) {
      core = stream->voice / SND_VOICES_PER_CORE;
      voice = stream->voice % SND_VOICES_PER_CORE;
      stream->flags |= VAG_STREAM_PREBUFFER;
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (voice << 1);
      batch[0].value = 0;
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = (core | (voice << 1)) | SD_VP_VOLR;
      batch[1].value = 0;
      batch[2].func = SD_BSET_PARAM;
      batch[2].entry = (core | (voice << 1)) | SD_VP_PITCH;
      batch[2].value = 0;
      --gNumStreamsPlaying;
      sceSdProcBatch(batch, 0, 3);
      stream = stream->sync_list;
    }
    stream = handler->SH.Sound;
    while (stream) {
      core = stream->voice / SND_VOICES_PER_CORE;
      voice = stream->voice % SND_VOICES_PER_CORE;
      if (stream == handler->SH.Sound) {
        address = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
        second = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
        third = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
        if (address != second && second != third) {
          address = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
          second = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
          third = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
          if (address != second && second != third) {
#if SND989_VERSION == 0x020604
            printf(stream_message_13);
#else
            snd_ShowError(49, 0, 0, 0, 0);
#endif
            address = stream->saved_spu_address;
          } else if (second == third) {
            address = second;
          }
        } else if (second == third) {
          address = second;
        }
        if (address >= stream->buffers[0].spu_buffer &&
            address <= stream->buffers[0].spu_buffer + stream->buffer_size / 4)
          buffer = 0;
        else
          buffer = 1;
        offset = address - stream->buffers[buffer].spu_buffer;
        offset = (offset >> 4) << 4;
      }
      sceSdSetAddr((core | (voice << 1)) | SD_VA_NAX, stream->buffers[buffer].spu_buffer + offset);
      stream->saved_spu_address = stream->buffers[buffer].spu_buffer + offset;
      stream = stream->sync_list;
    }
  }
  snd_UnlockMasterTick();
}

void snd_PauseAllStreams(void) {
  int index;
  for (index = 0; index < gNumVAGStreams; ++index) {
    if (gVAGStreamHandler[index].SH.Sound)
      snd_PauseVAGStream(gVAGStreamHandler[index].SH.OwnerID);
  }
}

void snd_ContinueVAGStream(u_int handle) {
  sceSdBatch batch[3];
  int state, disabled;
  int core, voice;
  struct VAGStreamHeader* stream;
  struct VAGStreamHandler* handler;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler && (handler->SH.flags & 2)) {
    stream = handler->SH.Sound;
    handler->SH.flags &= ~2;
    disabled = CpuSuspendIntr(&state);
    while (stream) {
      if (stream->flags & 4) {
        core = stream->voice / SND_VOICES_PER_CORE;
        voice = stream->voice % SND_VOICES_PER_CORE;
        sceSdSetParam((core | (voice << 1)) | SD_VP_PITCH, stream->pitch);
      }
      stream = stream->sync_list;
    }
    if (!disabled)
      CpuResumeIntr(state);
    stream = handler->SH.Sound;
    while (stream) {
      stream->flags &= ~VAG_STREAM_PREBUFFER;
      if (stream->flags & 4) {
        core = stream->voice / SND_VOICES_PER_CORE;
        voice = stream->voice % SND_VOICES_PER_CORE;
        batch[0].func = SD_BSET_PARAM;
        batch[0].entry = core | (voice << 1);
        batch[0].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[stream->voice].volume_left,
                                                       gChannelStatus[stream->voice].group) >>
                         1;
        batch[1].func = SD_BSET_PARAM;
        batch[1].entry = (core | (voice << 1)) | SD_VP_VOLR;
        batch[1].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[stream->voice].volume_right,
                                                       gChannelStatus[stream->voice].group) >>
                         1;
        ++gNumStreamsPlaying;
        sceSdProcBatch(batch, 0, 2);
      }
      stream = stream->sync_list;
    }
  }
  snd_UnlockMasterTick();
}

void snd_StopVAGStream(u_int handle) {
  struct VAGStreamHandler* handler;
#if SND989_VERSION == 0x020604
  struct VAGStreamHeader* stream;
  struct VAGStreamHeader* queued;
#endif
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
#if SND989_VERSION == 0x020604
    snd_FreeQueChain(handler->queue);
    stream = handler->SH.Sound;
    while (stream) {
      stream->flags = VAG_STREAM_FREE;
      snd_RemoveVAGStreamDMABuffer(&stream->buffers[0]);
      snd_RemoveVAGStreamDMABuffer(&stream->buffers[1]);
      snd_RemoveVAGStreamLoadBuffer(&stream->buffers[0]);
      snd_RemoveVAGStreamLoadBuffer(&stream->buffers[1]);
      snd_MarkVoicePlaying(&gChannelStatus[stream->voice]);
      gChannelStatus[stream->voice].priority = 0;
      snd_KeyOffVoice(stream->voice);
      --gNumStreamsPlaying;
      stream = stream->sync_list;
    }
    queued = handler->queued_stream;
    if (queued) {
      stream = queued;
      while (stream) {
        stream->flags = VAG_STREAM_FREE;
        snd_RemoveVAGStreamDMABuffer(&stream->buffers[0]);
        snd_RemoveVAGStreamDMABuffer(&stream->buffers[1]);
        snd_RemoveVAGStreamLoadBuffer(&stream->buffers[0]);
        snd_RemoveVAGStreamLoadBuffer(&stream->buffers[1]);
        stream = stream->sync_list;
      }
    }
    snd_DeactivateHandler(&handler->SH, 0);
    SignalSema(gStartDataLoadSema);
#else
    snd_SetVAGStreamVolPan(handle, 0, 0);
    handler->SH.flags = 4;
#endif
  }
  snd_UnlockMasterTick();
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
int snd_StreamHandlerIsBusyWithLoad(struct VAGStreamHandler* handler) {
  struct VAGStreamHeader* stream = handler->SH.Sound;
  while (stream) {
    if (gVAGReadBusy == &stream->buffers[0] || gVAGReadBusy == &stream->buffers[1])
      return 1;
    stream = stream->sync_list;
  }
  return 0;
}
#endif

#if SND989_VERSION == 0x020608
int snd_StreamHandlerIsBusyWithDMA(struct VAGStreamHandler* handler) {
  struct VAGStreamHeader* stream = handler->SH.Sound;
  while (stream) {
    if (gDMABusy[0] == &stream->buffers[0] || gDMABusy[0] == &stream->buffers[1] ||
        gDMABusy[1] == &stream->buffers[0] || gDMABusy[1] == &stream->buffers[1])
      return 1;
    stream = stream->sync_list;
  }
  return 0;
}
#endif

#if SND989_VERSION == 0x020608
void snd_KillVAGStream(u_int handle, int from_where) {
  struct VAGStreamHandler* handler;
  struct VAGStreamHeader* stream;
  struct VAGStreamHeader* queued;
  int disabled, state;
  if (in)
    return;
  in = 1;
  disabled = CpuSuspendIntr(&state);
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    snd_FreeQueChain(handler->queue);
    stream = handler->SH.Sound;
    while (stream) {
      stream->flags = VAG_STREAM_FREE;
      snd_RemoveVAGStreamDMABuffer(&stream->buffers[0]);
      snd_RemoveVAGStreamDMABuffer(&stream->buffers[1]);
      snd_RemoveVAGStreamLoadBuffer(&stream->buffers[0]);
      snd_RemoveVAGStreamLoadBuffer(&stream->buffers[1]);
      if (stream->voice != -1) {
        snd_MarkVoicePlaying(&gChannelStatus[stream->voice]);
        gChannelStatus[stream->voice].priority = 0;
        snd_KeyOffVoice(stream->voice);
        stream->voice = -1;
      }
      --gNumStreamsPlaying;
      stream = stream->sync_list;
    }
    queued = handler->queued_stream;
    if (queued) {
      stream = queued;
      while (stream) {
        stream->flags = VAG_STREAM_FREE;
        snd_RemoveVAGStreamDMABuffer(&stream->buffers[0]);
        snd_RemoveVAGStreamDMABuffer(&stream->buffers[1]);
        snd_RemoveVAGStreamLoadBuffer(&stream->buffers[0]);
        snd_RemoveVAGStreamLoadBuffer(&stream->buffers[1]);
        stream = stream->sync_list;
      }
    }
    snd_DeactivateHandler(&handler->SH, 0);
    SignalSema(gStartDataLoadSema);
  }
  do {
    if (!disabled)
      CpuResumeIntr(state);
  } while (0);
  in = 0;
}
#endif

#endif

void snd_ProcessVAGStreamTick(struct VAGStreamHandler* handler) {
  int voices[SND_NUM_VOICES];
  int dma_needed = 0;
  int start_with_voices = 0;
  int done;
  int voice_count;
  struct VAGStreamHeader* stream;
  struct VAGStreamHeader* walk;
  u_int sync_error, difference;
  u_int read_sectors, played_sectors;
#if SND989_VERSION == 0x020608
  if (handler->SH.flags & 4) {
    if (!snd_StreamHandlerIsBusyWithDMA(handler) && !snd_StreamHandlerIsBusyWithLoad(handler))
      snd_KillVAGStream(handler->SH.OwnerID, 1);
    return;
  }
#endif
  walk = handler->SH.Sound;
  done = 0;
  stream = walk;
  while (walk && !done) {
    done = snd_CheckVAGStreamProgress(walk, &dma_needed);
    walk = walk->sync_list;
  }
  if (!done && stream->sync_list) {
    sync_error = 0;
    walk = stream->sync_list;
    while (walk) {
      if (walk->bytes_played < stream->bytes_played)
        difference = stream->bytes_played - walk->bytes_played;
      else
        difference = walk->bytes_played - stream->bytes_played;
      if (difference > sync_error)
        sync_error = difference;
      walk = walk->sync_list;
    }
    if (sync_error > VAG_CD_SECTOR_SIZE) {
      done = -2;
#if SND989_VERSION == 0x020604
      printf(stream_message_14);
      printf(stream_message_15, sync_error);
#else
      snd_ShowError(50, 0, 0, 0, 0);
#endif
    }
    walk = stream;
    while (walk && !done && !(walk->buffers[0].flags & 0x100)) {
      read_sectors = walk->read_state[1] - walk->read_state[2];
      played_sectors = walk->bytes_played / VAG_CD_SECTOR_SIZE;
      if (read_sectors - played_sectors > walk->buffer_size / 1024 + 1) {
        done = -2;
#if SND989_VERSION == 0x020604
        printf(stream_message_16, read_sectors - played_sectors, walk->buffer_size / 1024);
#else
        snd_ShowError(51, 0, 0, 0, 0);
#endif
      }
      walk = walk->sync_list;
    }
  }
  if (done == -1) {
    walk = stream;
    if (walk) {
      voice_count = 0;
      while (walk) {
        voices[voice_count++] = walk->voice;
        walk->flags = VAG_STREAM_FREE;
        walk = walk->sync_list;
      }
    }
    if (handler->queue)
      snd_TryPreBufferQueElement(handler);
    if (handler->queued_stream) {
      handler->SH.Sound = handler->queued_stream;
      walk = handler->queued_stream;
      voice_count = 0;
      while (walk) {
        walk->voice = voices[voice_count++];
        walk->flags &= ~VAG_STREAM_PREBUFFER;
        if (walk->flags & 0x80) {
          walk->flags &= ~0x80;
          start_with_voices = 1;
        }
        walk = walk->sync_list;
      }
      if (start_with_voices)
        snd_StartVAGStreamSounding(handler->SH.Sound, 1);
      else
        snd_ContinueVAGStream(handler->SH.OwnerID);
      handler->queued_stream = 0;
    } else {
      walk = stream;
      while (walk) {
        walk->flags = VAG_STREAM_FREE;
#if SND989_VERSION == 0x020608
        if (walk->voice != -1) {
#endif
          snd_MarkVoicePlaying(&gChannelStatus[walk->voice]);
          gChannelStatus[walk->voice].priority = 0;
          snd_KeyOffVoice(walk->voice);
#if SND989_VERSION == 0x020608
          walk->voice = -1;
        }
#endif
        --gNumStreamsPlaying;
        walk = walk->sync_list;
      }
      snd_DeactivateHandler(&handler->SH, 0);
    }
  } else if (done == -2) {
#if SND989_VERSION == 0x020604
    printf(stream_message_17);
    snd_StopVAGStream(handler->SH.OwnerID);
#else
    if (!snd_StreamHandlerIsBusyWithDMA(handler) && !snd_StreamHandlerIsBusyWithLoad(handler))
      snd_KillVAGStream(handler->SH.OwnerID, 1);
    else
      handler->SH.flags |= 4;
    return;
#endif
  } else if (dma_needed) {
    SignalSema(gStartDMASema);
  }
  snd_TryPreBufferQueElement(handler);
}

int snd_CheckVAGStreamProgress(struct VAGStreamHeader* stream, int* dma_needed) {
  int core, voice;
  u_int address, second, third;
  int buffer;
  int state, disabled;
  int done = 0;
  int local_dma_needed = 0;
  if ((stream->flags & 0x18) == 8) {
    core = stream->voice / SND_VOICES_PER_CORE;
    voice = stream->voice % SND_VOICES_PER_CORE;
    disabled = CpuSuspendIntr(&state);
    address = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
    second = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
    third = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
    if (!disabled)
      CpuResumeIntr(state);
    if (address != second && second != third) {
#if SND989_VERSION == 0x020604
      printf(stream_message_18, snd_GetTick(), address, second, third);
      printf(stream_message_19);
#endif
      disabled = CpuSuspendIntr(&state);
      address = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
      second = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
      third = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
      if (!disabled)
        CpuResumeIntr(state);
      if (address != second && second != third) {
        address = stream->saved_spu_address;
#if SND989_VERSION == 0x020604
        printf(stream_message_20, address);
#endif
      } else {
        if (second == third)
          address = second;
#if SND989_VERSION == 0x020604
        printf(stream_message_21, address);
#endif
      }
    } else if (second == third) {
      address = second;
    }
    if (address > stream->buffers[0].spu_buffer - 1 && address < stream->buffers[1].spu_buffer) {
      buffer = 0;
    } else if (address > stream->buffers[1].spu_buffer - 1 &&
               address < stream->buffers[1].spu_buffer + stream->buffer_size / 2) {
      buffer = 1;
    } else {
      if (++stream->progress_errors >= 5) {
#if SND989_VERSION == 0x020604
        printf(stream_message_22);
#else
        snd_ShowError(52, 0, 0, 0, 0);
#endif
        return -2;
      }
      if (!stream->bytes_played || !stream->saved_spu_address) {
        address = stream->buffers[0].spu_buffer;
        buffer = 0;
      } else {
#if SND989_VERSION == 0x020604
        printf(stream_message_23);
#else
        snd_ShowError(53, 0, 0, 0, 0);
#endif
        return -1;
      }
    }
    if (stream->flags & 0x200) {
      sceSdSetAddr((core | (voice << 1)) | SD_VA_LSAX, stream->buffers[1].spu_buffer);
      stream->flags &= ~0x200;
    }
    if (!(stream->flags & 0x40)) {
      if (buffer != stream->current_buffer) {
        if (!stream->last_buffer_tick || snd_GetTick() > stream->last_buffer_tick + 10) {
          if ((stream->buffers[0].flags & 0x300) == 0x100 ||
              snd_AddVAGStreamDMABuffer(&stream->buffers[stream->current_buffer])) {
            local_dma_needed = 1;
            stream->current_buffer = buffer;
            stream->last_buffer_tick = snd_GetTick();
          } else {
            done = -2;
          }
        } else {
#if SND989_VERSION == 0x020604
          printf(stream_message_24);
#endif
          if (++stream->progress_errors >= 5) {
#if SND989_VERSION == 0x020604
            printf(stream_message_22);
#else
            snd_ShowError(54, 0, 0, 0, 0);
#endif
            return -2;
          }
        }
      } else if (stream->saved_spu_address && address < stream->saved_spu_address) {
#if SND989_VERSION == 0x020604
        printf(stream_message_25, stream, stream->saved_spu_address, address);
        printf(stream_message_26);
#else
        snd_ShowError(55, 0, 0, 0, 0);
#endif
        done = -2;
      }
      stream->saved_spu_address = address;
    } else {
      u_int envelope;
      core = stream->voice / SND_VOICES_PER_CORE;
      voice = stream->voice % SND_VOICES_PER_CORE;
      envelope = sceSdGetParam((core | (voice << 1)) | SD_VP_ENVX);
      address = sceSdGetAddr((core | (voice << 1)) | SD_VA_NAX);
      if (!envelope)
        done = -1;
      stream->current_buffer = buffer;
    }
    if (!done)
      snd_FixVAGStreamSamplesPlayed(stream, address);
  }
  if ((stream->flags & 6) == 6) {
    stream->flags &= ~2;
    stream->flags |= 8;
  }
  if (local_dma_needed)
    *dma_needed = 1;
  return done;
}

void snd_FixVAGStreamSamplesPlayed(struct VAGStreamHeader* stream, u_int address) {
  int last_buffer;
  if (address > stream->buffers[1].spu_buffer + stream->buffer_size / 2)
    address = stream->buffers[1].spu_buffer + stream->buffer_size / 2;
  last_buffer = 0;
  if (stream->last_spu_address <= stream->buffers[0].spu_buffer - 1 ||
      stream->last_spu_address >= stream->buffers[0].spu_buffer + stream->buffer_size / 2)
    last_buffer = 1;
  if (last_buffer == stream->current_buffer) {
    stream->bytes_played += address - stream->last_spu_address;
  } else {
    stream->bytes_played += stream->buffers[last_buffer].spu_buffer + stream->buffer_size / 2 -
                            stream->last_spu_address;
    stream->bytes_played += address - stream->buffers[stream->current_buffer].spu_buffer;
  }
  stream->last_spu_address = address;
#if SND989_VERSION == 0x020604
  if (stream->bytes_played > stream->samples_total / 28 * 16)
    printf(stream_message_27, stream, stream->bytes_played - stream->samples_total / 28 * 16,
           stream->bytes_played, stream->samples_total, stream->samples_total / 28 * 16);
#endif
}

#if SND989_VERSION == 0x020604
u_int snd_GetVAGStreamLoc(u_int handle) {
  return get_stream_location(handle);
}
#else
u_int snd_GetVAGStreamLoc(u_int handle) {
  struct VAGStreamHandler* handler;
  struct VAGStreamHeader* stream;
  u_int samples, tail;
  u_int seconds;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  seconds = 1;
  if (!handler || ((struct VAGStreamHeader*)handler->SH.Sound)->sample_rate == -seconds) {
    snd_UnlockMasterTick();
    return 0;
  }
  stream = handler->SH.Sound;
  samples = (stream->bytes_played / VAG_FRAME_BYTES) * VAG_FRAME_SAMPLES;
  tail = stream->bytes_played % VAG_FRAME_BYTES;
  if (tail > 4)
    samples += (tail - 4) * 2;
  seconds = samples / stream->sample_rate;
  samples -= seconds * stream->sample_rate;
  samples *= STREAM_TIME_UNITS_PER_SECOND;
  samples /= stream->sample_rate;
  seconds = seconds * STREAM_TIME_UNITS_PER_SECOND + samples;
  snd_UnlockMasterTick();
  return seconds;
}
#endif

u_int snd_GetVAGStreamTimeRemaining(u_int handle) {
#if SND989_VERSION == 0x020604
  u_int position = get_stream_location(handle);
#else
  u_int position = snd_GetVAGStreamLoc(handle);
#endif
  return snd_GetVAGStreamTotalTime(handle) - position;
}

u_int snd_GetVAGStreamTotalTime(u_int handle) {
  struct VAGStreamHandler* handler;
  struct VAGStreamHeader* stream;
  u_int result;
  u_int seconds;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler && ((struct VAGStreamHeader*)handler->SH.Sound)->sample_rate != -1) {
    stream = handler->SH.Sound;
    seconds = stream->samples_total / stream->sample_rate;
    result = stream->samples_total % stream->sample_rate;
    result = (result * STREAM_TIME_UNITS_PER_SECOND) / stream->sample_rate;
    seconds = seconds * STREAM_TIME_UNITS_PER_SECOND + result;
    snd_UnlockMasterTick();
    return seconds;
  }
  seconds = 0;
  snd_UnlockMasterTick();
  return seconds;
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
int snd_IsVAGStreamBuffered(u_int handle) {
  struct GenericSoundHandler* handler = snd_CheckHandlerStillActive(handle);
  int result;
  if (!handler || !(handler->flags & 8))
    result = 0;
  else
    result = 1;
  return result;
}
#endif

#endif

void snd_VAGStreamDataLoadThread(void) {
  int state, disabled;
#if SND989_VERSION == 0x020608
  int result = 0;
#endif
  while (1) {
    if ((!VAGStreamLoadList && !gSSNeedSectors) || gVAGReadBusy || gDataReadBusy) {
#if SND989_VERSION == 0x020604
      WaitSema(gStartDataLoadSema);
#else
      result = WaitSema(gStartDataLoadSema);
#endif
    }
#if SND989_VERSION == 0x020608
    if (result)
      snd_ShowError(56, result, 0, 0, 0);
#endif
    if (gBreakDataRead)
      snd_SignalDataReadDone();
#if SND989_VERSION == 0x020608
    disabled = CpuSuspendIntr(&state);
#endif
    if (!gVAGReadBusy && !gDataReadBusy) {
      if (VAGStreamLoadList) {
#if SND989_VERSION == 0x020604
        disabled = CpuSuspendIntr(&state);
        gVAGReadBusy = VAGStreamLoadList;
        VAGStreamLoadList = VAGStreamLoadList->next;
        if (!VAGStreamLoadList)
          VAGStreamLoadListTail = 0;
        if (!disabled)
          CpuResumeIntr(state);
#else
        while (1) {
          if (!VAGStreamLoadList)
            break;
          gVAGReadBusy = VAGStreamLoadList;
          VAGStreamLoadList = VAGStreamLoadList->next;
          if (!VAGStreamLoadList)
            VAGStreamLoadListTail = 0;
          if (gVAGReadBusy->owner->handler->SH.flags & 4)
            gVAGReadBusy = 0;
          if (gVAGReadBusy)
            break;
        }
#endif
        if (
#if SND989_VERSION == 0x020608
            gVAGReadBusy &&
#endif
            !(gVAGReadBusy->owner->flags & 0x21)) {
#if SND989_VERSION == 0x020608
          if (!disabled)
            CpuResumeIntr(state);
#endif
          snd_KickVAGRead();
#if SND989_VERSION == 0x020608
          disabled = CpuSuspendIntr(&state);
#endif
        } else {
          gVAGReadBusy = 0;
        }
      } else if (gSSNeedSectors) {
        gDataReadBusy = 1;
#if SND989_VERSION == 0x020608
        if (!disabled)
          CpuResumeIntr(state);
#endif
        snd_KickDataRead();
#if SND989_VERSION == 0x020608
        disabled = CpuSuspendIntr(&state);
#endif
      }
    }
#if SND989_VERSION == 0x020608
    if (!disabled)
      CpuResumeIntr(state);
#endif
    if (!gVAGReadBusy && !gDataReadBusy && !VAGStreamLoadList && gCDIdleWaiter) {
      WakeupThread(gCDIdleWaiter);
      gCDIdleWaiter = 0;
    }
  }
}

int snd_StreamSafeCdRead(u_int sector, u_int count, void* destination) {
  sceCdRMode mode;
  int result;
#if SND989_VERSION == 0x020604
  if (!gMasterReadBuffer) {
#else
  if (in_func) {
    result = 0;
    goto done;
  }
  in_func = 1;
  if (!VAGStreamList) {
#endif
    mode.trycount = 100;
    mode.spindlctrl = gCdSpinMode;
    mode.datapattern = 0;
#if SND989_VERSION == 0x020608
    in_func = 0;
#endif
    result = sceCdRead(sector, count, destination, &mode);
    goto done;
  }
  if (snd_StreamSafeCdSync(17) != 1) {
    gReadWaitThread = -1;
    gCurrentReadBuffer = destination;
    gSSReadLoc = sector;
#if SND989_VERSION == 0x020608
    gReadIsEE = 0;
#endif
    gSSNeedSectors = count;
#if SND989_VERSION == 0x020604
    gReadIsEE = 0;
#endif
    SignalSema(gStartDataLoadSema);
#if SND989_VERSION == 0x020608
    in_func = 0;
#endif
    result = 1;
    goto done;
  }
#if SND989_VERSION == 0x020608
  in_func = 0;
#endif
  result = 0;
done:
  return result;
}

void snd_StreamSafeCdBreak(void) {
#if SND989_VERSION == 0x020604
  if (!gMasterReadBuffer)
#else
  if (!VAGStreamList)
#endif
    sceCdBreak();
  else if (gSSNeedSectors)
    gBreakDataRead = 1;
}

int snd_StreamSafeCdReadEEm(u_int sector, u_int count, void* destination) {
  int result;
#if SND989_VERSION == 0x020604
  if (!gMasterReadBuffer)
#else
  if (gSafeCdReadBusy)
    return 0;
  gSafeCdReadBusy = 1;
  if (!VAGStreamList)
#endif
  {
#if SND989_VERSION == 0x020604
    printf(stream_message_28);
#else
    snd_ShowError(57, 0, 0, 0, 0);
    gSafeCdReadBusy = 0;
    return 0;
  }
  if (!gMasterReadBuffer) {
    snd_ShowError(58, 0, 0, 0, 0);
    gSafeCdReadBusy = 0;
    return 0;
  }
  if (snd_StreamSafeCdSync(17) != 1) {
    gReadWaitThread = -1;
    gCurrentReadBuffer = destination;
    gSSReadLoc = sector;
    gReadIsEE = 1;
    gSSNeedSectors = count;
    SignalSema(gStartDataLoadSema);
    result = 1;
  } else {
    stream_report_read_busy();
    snd_SetCDSifReg(0, 19);
#endif
    result = 0;
#if SND989_VERSION == 0x020604
    goto done;
#endif
  }
#if SND989_VERSION == 0x020604
  if (snd_StreamSafeCdSync(17) == 1) {
    int disabled;
    int id;
    printf(stream_message_29);
    gCDSifRegPacket.reg = 8;
    gCDSifRegPacket.value = 0;
    disabled = CpuDisableIntr();
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                       0);
    if (!disabled)
      CpuEnableIntr();
    while (sceSifDmaStat(id) >= 0) {
    }
    gCDSifRegPacket.reg = 9;
    gCDSifRegPacket.value = 19;
    disabled = CpuDisableIntr();
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                       0);
    if (!disabled)
      CpuEnableIntr();
    while (sceSifDmaStat(id) >= 0) {
    }
    result = 0;
    goto done;
  }
  gReadWaitThread = -1;
  gCurrentReadBuffer = destination;
  gSSReadLoc = sector;
  gSSNeedSectors = count;
  gReadIsEE = 1;
  SignalSema(gStartDataLoadSema);
  result = 1;

#endif

done:
#if SND989_VERSION == 0x020608
  gSafeCdReadBusy = 0;
#endif
  return result;
}

int snd_StreamSafeCdSync(int mode) {
#if SND989_VERSION == 0x020604
  if (!gMasterReadBuffer)
#else
  if (!VAGStreamList)
#endif
    return sceCdSync(mode);
  if (gSSNeedSectors || gDataReadBusy) {
    if (mode == 1 || mode == 17)
      return 1;
    if (gReadWaitThread != -1 && (mode == 0 || mode == 16)) {
#if SND989_VERSION == 0x020604
      printf(stream_message_30);
#else
      snd_ShowError(60, 0, 0, 0, 0);
#endif
      return 1;
    }
    while (gSSNeedSectors || gDataReadBusy) {
      gReadWaitThread = GetThreadId();
      SleepThread();
      gReadWaitThread = -1;
    }
  }
  return 0;
}

int snd_StreamSafeCdGetError(void) {
  int error;
#if SND989_VERSION == 0x020604
  if (gMasterReadBuffer)
#else
  if (VAGStreamList)
#endif
    error = gLastDATAReadError;
  else
    error = sceCdGetError();
  return error;
}

void snd_KickDataRead(void) {
  sceCdRMode mode;
  int result;
#if SND989_VERSION == 0x020608
  int last_sector;
#endif
  gSSReadSectors = gSSNeedSectors;
#if SND989_VERSION == 0x020604
  if ((u_int)gMasterReadBufferSize < gSSReadSectors)
    gSSReadSectors = gMasterReadBufferSize;
#else
  if ((u_int)(gHalfBufferSize / 2048) < gSSReadSectors)
    gSSReadSectors = gHalfBufferSize / 2048;
  gMasterReadBufferFirstSector = gSSReadLoc;
  last_sector = gSSReadLoc - 1;
  gMasterReadBufferLastSector = last_sector + gSSReadSectors;
#endif
  sceCdCallback(snd_DoneVAGReadCB);
  mode.trycount = 50;
  mode.spindlctrl = gCdSpinMode;
  mode.datapattern = 0;
#if SND989_VERSION == 0x020604
  result = sceCdRead(gSSReadLoc, gSSReadSectors, gMasterReadBuffer, &mode);
#else
  if (!gReadIsEE) {
    gLastReadSector = gSSReadLoc;
    gLastReadAmount = gSSReadSectors;
    gLastReadIOPLoc = gCurrentReadBuffer;
    result = sceCdRead(gSSReadLoc, gSSReadSectors, gCurrentReadBuffer, &mode);
  } else {
    gLastReadSector = gSSReadLoc;
    gLastReadAmount = gMasterReadBufferSize;
    gLastReadIOPLoc = gMasterReadBuffer;
    result = sceCdRead(gSSReadLoc, gMasterReadBufferSize, gMasterReadBuffer, &mode);
  }
#endif
  if (!result) {
    int thread;
    gLastDATAReadError = sceCdGetError();
    if (!gLastDATAReadError)
      gLastDATAReadError = -1;
#if SND989_VERSION == 0x020604
    printf(stream_message_31);
    snd_ShowCDError(gLastDATAReadError);
#else
    if (!gReadIsEE)
      snd_ShowError(61, gLastDATAReadError, gSSReadLoc, gSSReadSectors, (u_int)gCurrentReadBuffer);
    else
      snd_ShowError(61, gLastDATAReadError, gSSReadLoc, gMasterReadBufferSize,
                    (u_int)gMasterReadBuffer);
#endif
    gDataReadBusy = 0;
    gSSNeedSectors = 0;
    if (gReadWaitThread != -1) {
      thread = gReadWaitThread;
      gReadWaitThread = -1;
      WakeupThread(thread);
    }
    if (gReadIsEE) {
#if SND989_VERSION == 0x020604
      int disabled, id;
      gCDSifRegPacket.reg = 9;
      gCDSifRegPacket.value = gLastDATAReadError;
      disabled = CpuDisableIntr();
      id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0,
                         0, 0);
      if (!disabled)
        CpuEnableIntr();
      while (sceSifDmaStat(id) >= 0) {
      }
      gCDSifRegPacket.reg = 8;
      gCDSifRegPacket.value = 0;
      disabled = CpuDisableIntr();
      id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0,
                         0, 0);
      if (!disabled)
        CpuEnableIntr();
      while (sceSifDmaStat(id) >= 0) {
      }
#else
      snd_SetCDSifReg(0, gLastDATAReadError);
#endif
    }
  }
}

void snd_KickVAGRead(void) {
  sceCdRMode mode;
  int read_bytes;
  u_int read_sectors;
  int result;
#if SND989_VERSION == 0x020608
  if (!gVAGReadBusy)
    return;
#endif
  if (gVAGReadBusy->flags & VAG_BUFFER_FIRST) {
    if (gVAGReadBusy->owner->read_state[3] > 2000)
      read_sectors = 2;
    else
      read_sectors = 1;
  } else {
    ++gVAGReadBusy->owner->remaining_state[0];
    gVAGReadBusy->owner->remaining_state[1] = snd_GetTick();
    read_bytes = gVAGReadBusy->owner->buffer_size / 2;
    if ((u_int)read_bytes >= gVAGReadBusy->owner->read_state[4]) {
      if (!(gVAGReadBusy->flags & 0x100))
        read_bytes = gVAGReadBusy->owner->read_state[4];
      gVAGReadBusy->bytes = gVAGReadBusy->owner->read_state[4];
      gVAGReadBusy->owner->read_state[4] = 0;
      gVAGReadBusy->owner->flags |= 0x20;
      gVAGReadBusy->is_end = 1;
    } else {
      gVAGReadBusy->is_end = 0;
      gVAGReadBusy->bytes = read_bytes;
      gVAGReadBusy->owner->read_state[4] -= read_bytes;
    }
    if (gVAGReadBusy->flags & 0x100)
      read_sectors =
          ((gVAGReadBusy->owner->buffer_size / 2) * gVAGReadBusy->owner->handler->num_streams +
           VAG_CD_SECTOR_SIZE - 1) /
          VAG_CD_SECTOR_SIZE;
    else
      read_sectors = (read_bytes + VAG_CD_SECTOR_SIZE - 1) / VAG_CD_SECTOR_SIZE;
  }
  sceCdCallback(snd_DoneVAGReadCB);
  mode.trycount = 50;
  mode.spindlctrl = gCdSpinMode;
  mode.datapattern = 0;
#if SND989_VERSION == 0x020608
  gLastReadSector = gVAGReadBusy->owner->read_state[1];
  gLastReadAmount = read_sectors;
  gLastReadIOPLoc = gVAGReadBusy->buffer;
#endif
  result = sceCdRead(gVAGReadBusy->owner->read_state[1], read_sectors, gVAGReadBusy->buffer, &mode);
  if (!result) {
    gLastVAGReadError = sceCdGetError();
    if (!gLastVAGReadError)
      gLastVAGReadError = -1;
#if SND989_VERSION == 0x020604
    snd_StopVAGStream(gVAGReadBusy->owner->handler->SH.OwnerID);
#else
    gVAGReadBusy->owner->handler->SH.flags = 4;
#endif
    gVAGReadBusy = 0;
#if SND989_VERSION == 0x020604
    printf(stream_message_32);
    snd_ShowCDError(gLastVAGReadError);
#else
    snd_ShowError(62, gLastVAGReadError, gVAGReadBusy->owner->read_state[1], read_sectors,
                  (u_int)gVAGReadBusy->buffer);
#endif
  } else if (!(gVAGReadBusy->flags & VAG_BUFFER_FIRST)) {
    gVAGReadBusy->owner->read_state[1] += read_sectors;
  }
}

void snd_VAGStreamLoadDoneThread(void) {
  struct VAGStreamHeader *first, *walk, *ordinary, **previous;
#if SND989_VERSION == 0x020604
  struct VAGStreamHeader *stream, *master;
#else
  // TODO hack
  register struct VAGStreamHeader* stream asm("$16");
  register struct VAGStreamHeader* master asm("$17");
#endif
  struct VAGFileHeader* vag;
  struct VPKFileHeader* vpk;
  sceSifDmaData transfer;
  int state, disabled, id, result;
  u_int size, rate, index, header_end, first_end;
  int replace_master;
  int vpk_rate;
#if SND989_VERSION == 0x020608
  register int one asm("$30");
  register int stopped asm("$23");
  int failed = 0;
#endif
  int dma_needed = 0;
#if SND989_VERSION == 0x020604
  int failed = 0;
#else
  one = 1;
  stopped = 4;
#endif
  while (1) {
#if SND989_VERSION == 0x020604
    WaitSema(gDoneLoadSema);
#else
    result = WaitSema(gDoneLoadSema);
    if (result)
      snd_ShowError(63, result, 0, 0, 0);
#endif
    if (gDataReadBusy) {
      gLastDATAReadError = sceCdGetError();
    } else {
      gLastVAGReadError = sceCdGetError();
      if (gLastVAGReadError) {
#if SND989_VERSION == 0x020604
        printf(stream_message_33);
        snd_ShowCDError(gLastVAGReadError);
#else
        snd_ShowError(64, gLastVAGReadError, gLastReadSector, gLastReadAmount,
                      (u_int)gLastReadIOPLoc);
#endif
      }
    }
    if (gDataReadBusy) {
      if (gLastDATAReadError) {
#if SND989_VERSION == 0x020604
        printf(stream_message_34);
        snd_ShowCDError(gLastDATAReadError);
#else
        snd_ShowError(65, gLastDATAReadError, gLastReadSector, gLastReadAmount,
                      (u_int)gLastReadIOPLoc);
#endif
        gSSNeedSectors = 0;
      } else if (gDataReadBusy != -1) {
#if SND989_VERSION == 0x020604
        if (!gReadIsEE) {
          memmove(gCurrentReadBuffer, gMasterReadBuffer, gSSReadSectors * VAG_CD_SECTOR_SIZE);
        } else
#endif
            if (gReadIsEE) {
          transfer.data = (u_int)gMasterReadBuffer;
          transfer.addr = (u_int)gCurrentReadBuffer;
          transfer.size = gSSReadSectors * VAG_CD_SECTOR_SIZE;
          transfer.mode = 0;
          disabled = CpuSuspendIntr(&state);
          id = sceSifSetDma(&transfer, 1);
          if (!disabled)
            CpuResumeIntr(state);
          if (!id) {
#if SND989_VERSION == 0x020604
            printf(stream_message_35);
#else
            snd_ShowError(66, 0, 0, 0, 0);
#endif
          }
          while (sceSifDmaStat(id) >= 0) {
          }
        }
        gSSNeedSectors -= gSSReadSectors;
        if (gSSNeedSectors) {
          gSSReadLoc += gSSReadSectors;
          gCurrentReadBuffer = (char*)gCurrentReadBuffer + gSSReadSectors * VAG_CD_SECTOR_SIZE;
        }
      }
      gDataReadBusy = 0;
#if SND989_VERSION == 0x020604
      if (!gSSNeedSectors || gBreakDataRead == 1)
#else
      if (!gSSNeedSectors || gBreakDataRead == one)
#endif
        snd_SignalDataReadDone();
      if (VAGStreamLoadList || gSSNeedSectors)
        SignalSema(gStartDataLoadSema);
#if SND989_VERSION == 0x020604
      continue;
#endif
    } else if (gVAGReadBusy) {
      stream = gVAGReadBusy->owner;
#if SND989_VERSION == 0x020604
      if (stream->flags & VAG_STREAM_FREE)
        goto no_stream;
      if (gLastVAGReadError) {
        u_int handle = ((struct VAGBuffer*)0)->owner->handler->SH.OwnerID;
        gVAGReadBusy = 0;
        snd_StopVAGStream(handle);
        continue;
      }
#else
      master = stream;
      if ((stream->flags & VAG_STREAM_FREE) || (stream->handler->SH.flags & 4) || gLastVAGReadError)
        goto stop_stream;
#endif
      if (gVAGReadBusy->flags & VAG_BUFFER_FIRST) {
        vag = (struct VAGFileHeader*)((char*)gVAGReadBusy->buffer + stream->read_state[3]);
#if SND989_VERSION == 0x020604
        ordinary = stream;
#endif
        if (vag->magic == VAG_MAGIC_BE || vag->magic == VAG_MAGIC_LE) {
          rate = vag->sample_rate;
          if (vag->magic == VAG_MAGIC_BE)
            rate = VAG_SWAP32(rate);
          stream->sample_rate = rate;
          stream->pitch = (rate << 12) / 48000;
          size = vag->data_size;
          if (vag->magic == VAG_MAGIC_BE)
            size = VAG_SWAP32(size);
          stream->samples_total = (size / 16) * 28;
          stream->bytes_played = 0;
#if SND989_VERSION == 0x020604
          first_end = gVAGReadBusy->owner->read_state[3];
          first_end += VAG_HEADER_SIZE;
#else
          first_end = stream->read_state[3] + VAG_HEADER_SIZE;
#endif
          stream->buffers[0].flags = 9;
          stream->buffers[1].flags = 1;
          stream->read_state[4] = size + first_end;
          if (stream->read_state[4] < stream->buffer_size / 2 + 1)
            stream->buffers[0].flags |= 4;
          else if (stream->read_state[4] < stream->buffer_size + 1)
            stream->buffers[1].flags = 5;
          else
            vag_stream->buffers[0].flags |= 0x20;
          if (vag_stream->sync_list) {
            header_end = vag_stream->sync_list->read_state[3] + VAG_HEADER_SIZE;
            vag_stream->sync_list->read_state[4] = size + header_end;
            vag_stream->sync_list->pitch = vag_stream->pitch;
            vag_stream->sync_list->buffers[0].flags = 9;
            vag_stream->sync_list->buffers[1].flags = 1;
            vag_stream->sync_list->bytes_played = 0;
            vag_stream->sync_list->samples_total = vag_stream->samples_total;
            if (!snd_AddVAGStreamLoadBuffer(&vag_stream->sync_list->buffers[0]))
              failed = 1;
            vag_stream->original_volume = 127;
            vag_stream->group = 0;
            vag_stream->original_pan = 270;
            vag_stream->sync_list->original_volume = 127;
            vag_stream->sync_list->group = 0;
            vag_stream->sync_list->original_pan = 90;
          } else {
            vag_stream->original_volume = 127;
            vag_stream->group = 0;
            vag_stream->original_pan = 0;
          }
          if (!failed) {
            if (vag_stream->read_state[4] > vag_stream->buffer_size / 2) {
              if (vag_stream->sync_list &&
                  !snd_AddVAGStreamLoadBuffer(&vag_stream->sync_list->buffers[1]))
                failed = 1;
              if (!failed && !snd_AddVAGStreamLoadBuffer(&vag_stream->buffers[1]))
                failed = 1;
            }
            if (!failed) {
              snd_KickVAGRead();
#if SND989_VERSION == 0x020604
              continue;
#else
              goto service_waiters;
#endif
            }
          }
          failed = 0;
#if SND989_VERSION == 0x020604
          printf(stream_message_36);
#endif
          gVAGReadBusy = 0;
#if SND989_VERSION == 0x020604
          snd_StopVAGStream(vag_stream->handler->SH.OwnerID);
          continue;
#else
          vag_stream->handler->SH.flags = stopped;
          goto service_waiters;
#endif
        } else if (vag->magic == VPK_MAGIC) {
#if SND989_VERSION == 0x020604
          master = stream;
#endif
          vpk = (struct VPKFileHeader*)vag;
          if (vpk->buffer_size != stream->buffer_size) {
#if SND989_VERSION == 0x020604
            printf(stream_message_37);
            printf(stream_message_38, stream->buffer_size);
            printf(stream_message_39, vpk->buffer_size);
            goto stop_early;
#else
            snd_ShowError(67, stream->buffer_size, vpk->buffer_size, 0, 0);
            goto stop_stream;
#endif
          }
          first = snd_GetFirstOfAdjacentStreamHeaders(stream, vpk->channels);
          if (!first) {
#if SND989_VERSION == 0x020604
            printf(stream_message_40);
          stop_early:
#else
            {
              // TODO hack
              register u_int error asm("$4") = 68;
              register u_int arg1 asm("$5") = 0;
              register u_int arg2 asm("$6") = 0;
              register u_int arg3 asm("$7") = 0;
              asm("" : : "r"(error), "r"(arg1), "r"(arg2), "r"(arg3));
              snd_ShowError(error, arg1, arg2, arg3, 0);
            }
          stop_stream:
#endif
            gVAGReadBusy = 0;
#if SND989_VERSION == 0x020604
            snd_StopVAGStream(stream->handler->SH.OwnerID);
            continue;
#else
            stream->handler->SH.flags = stopped;
            goto service_waiters;
#endif
          }
#if SND989_VERSION == 0x020604
          stream->handler->SH.Sound = first;
          stream->handler->num_streams = vpk->channels;
#else
          snd_LockMasterTick();
          replace_master = 1;
          snd_LockVoiceAllocator(1);
          walk = first;
          for (index = 0; index < vpk->channels; ++index) {
            if (walk == master) {
              replace_master = 0;
              break;
            }
            walk = walk->next;
          }
          previous = &first;
#endif
          walk = first;
          for (index = 0; index < vpk->channels; ++index) {
            walk->handler = master->handler;
            walk->flags = VAG_STREAM_ALLOCATED;
            walk->sync_list = 0;
#if SND989_VERSION == 0x020604
            if (index)
              snd_AddStreamToHandler(walk->handler, walk);
            if (walk != master)
#else
            walk->read_state[2] = master->read_state[2];
            if (!index) {
              walk->voice = master->voice;
              if (walk != master)
                master->voice = -1;
            } else {
              (*previous)->sync_list = walk;
              previous = &walk;
#endif
              walk->voice = snd_AllocateVoice(master->handler->SH.VolGroup, 127);
#if SND989_VERSION == 0x020608
          }
#endif
          gChannelStatus[walk->voice].status = 4;
          vpk_rate = vpk->sample_rate;
          walk->sample_rate = vpk_rate;
          walk->pitch = (vpk_rate << 12) / 48000;
          size = vpk->data_size;
          walk->bytes_played = 0;
          walk->buffers[0].flags = 9;
          walk->buffers[1].flags = 1;
          walk->samples_total = (size / 16) * 28;
          walk->read_state[4] = size;
          walk->buffers[0].flags |= 0x100;
          walk->buffers[1].flags |= 0x100;
          if (!index) {
            walk->buffers[0].flags |= 0x200;
            walk->buffers[1].flags |= 0x200;
          }
          if (walk->read_state[4] < walk->buffer_size / 2 + 1)
            walk->buffers[0].flags |= 4;
          else if (master->read_state[4] < master->buffer_size + 1)
            walk->buffers[1].flags |= 4;
          else
            walk->buffers[0].flags |= 0x20;
          walk->original_volume = vpk->channel[index].volume;
          walk->original_pan = vpk->channel[index].pan;
          walk->group = vpk->channel[index].group;
          walk->current_buffer = 0;
          walk->buffers[0].is_end = 0;
          walk->buffers[1].is_end = 0;
          walk->saved_spu_address = 0;
          walk->progress_errors = 0;
          walk->last_buffer_tick = 0;
          walk->remaining_state[0] = 0;
          walk->remaining_state[1] = 0;
          walk->last_spu_address = 0;
          walk->remaining_state[0] = 0;
          walk->position_state = 0;
#if SND989_VERSION == 0x020604
          walk->progress_errors = 0;
#endif
          walk->read_state[3] = 0;
          if (!index) {
            walk->read_state[1] = master->read_state[2] + 1;
            walk->read_state[2] = master->read_state[2];
          }
          walk = walk->next;
        }
#if SND989_VERSION == 0x020608
        disabled = CpuSuspendIntr(&state);
        if (replace_master) {
          master->flags = VAG_STREAM_FREE;
          master->voice = -1;
        }
        master = first;
        master->handler->SH.Sound = master;
        master->handler->num_streams = vpk->channels;
        walk = master;
        while (walk) {
          int bit = 1 << (walk->voice % SND_VOICES_PER_CORE);
          master->handler->SH.Voices[walk->voice / SND_VOICES_PER_CORE] |= bit;
          walk = walk->sync_list;
        }
#endif
        gVAGReadBusy = &first->buffers[0];
#if SND989_VERSION == 0x020608
        if (!disabled)
          CpuResumeIntr(state);
#endif
        if (!failed) {
          if (first->read_state[4] > first->buffer_size / 2 &&
              !snd_AddVAGStreamLoadBuffer(&first->buffers[1]))
            failed = 1;
          if (!failed) {
            snd_KickVAGRead();
#if SND989_VERSION == 0x020604
            continue;
#else
                goto unlock;
#endif
          }
        }
        failed = 0;
#if SND989_VERSION == 0x020604
        snd_StopVAGStream(master->handler->SH.OwnerID);
        continue;
#else
            gVAGReadBusy = 0;
            master->handler->SH.flags = stopped;
          unlock:
            snd_UnlockVoiceAllocator();
            snd_UnlockMasterTick();
#endif
      } else {
        gVAGReadBusy = 0;
#if SND989_VERSION == 0x020604
        printf(stream_message_41, stream->read_state[2], stream->read_state[3]);
        snd_StopVAGStream(stream->handler->SH.OwnerID);
        continue;
#else
            snd_ShowError(69, stream->read_state[2], stream->read_state[3], 0, 0);
            stream->handler->SH.flags = stopped;
#endif
      }
    } else {
      if (!(gVAGReadBusy->flags & 0x100)) {
        gVAGReadBusy->bytes =
            snd_MakeVAGLoop(gVAGReadBusy->buffer, gVAGReadBusy->bytes, gVAGReadBusy->is_end);
      } else {
        walk = stream;
        index = gVAGReadBusy != &stream->buffers[0];
        while (walk) {
          walk->buffers[index].bytes = snd_MakeVAGLoop(walk->buffers[index].buffer,
                                                       gVAGReadBusy->bytes, gVAGReadBusy->is_end);
          walk = walk->sync_list;
        }
      }
      gVAGReadBusy->flags |= VAG_BUFFER_DMA_READY;
      if (gVAGReadBusy->flags & 1) {
        snd_AddVAGStreamDMABuffer(gVAGReadBusy);
        dma_needed = 1;
      } else if (gVAGReadBusy->flags & 0x40) {
#if SND989_VERSION == 0x020608
        gVAGReadBusy->owner->handler->SH.flags |= 8;
#endif
        snd_StartVAGStreamSounding(gVAGReadBusy->owner, 1);
      }
      gVAGReadBusy = 0;
      if (dma_needed) {
        dma_needed = 0;
        SignalSema(gStartDMASema);
      }
    }
#if SND989_VERSION == 0x020604
  service_waiters:
#else
      } else {
        gVAGReadBusy = 0;
      }
      goto service_waiters;
    service_waiters:
      if ((!gVAGReadBusy && VAGStreamLoadList) || (!gDataReadBusy && gSSNeedSectors))
#endif
    SignalSema(gStartDataLoadSema);
#if SND989_VERSION == 0x020604
    continue;
  }
  else {
  no_stream:
    gVAGReadBusy = 0;
    SignalSema(gStartDataLoadSema);
    continue;
#else
      if (!gVAGReadBusy && !gDataReadBusy && !VAGStreamLoadList && gCDIdleWaiter) {
        WakeupThread(gCDIdleWaiter);
        gCDIdleWaiter = 0;
#endif
  }
}
}

#if SND989_VERSION == 0x020604

#if SND989_VERSION == 0x020604
void snd_DoneVAGReadCB(int code) {
  if (code == 1)
    iSignalSema(gDoneLoadSema);
}
#endif

#endif

void snd_SignalDataReadDone(void) {
  int thread;
  gBreakDataRead = 0;
  gSSNeedSectors = 0;
  if (gReadIsEE) {
#if SND989_VERSION == 0x020604
    int disabled, id;
    gCDSifRegPacket.reg = 9;
    gCDSifRegPacket.value = gLastDATAReadError;
    disabled = CpuDisableIntr();
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                       0);
    if (!disabled)
      CpuEnableIntr();
    while (sceSifDmaStat(id) >= 0) {
    }
    gCDSifRegPacket.reg = 8;
    gCDSifRegPacket.value = 0;
    disabled = CpuDisableIntr();
    id = sceSifSendCmd(SIF_CMD_SET_SYSTEM_REGISTER, &gCDSifRegPacket, sizeof(gCDSifRegPacket), 0, 0,
                       0);
    if (!disabled)
      CpuEnableIntr();
    while (sceSifDmaStat(id) >= 0) {
    }
#else
      snd_SetCDSifReg(0, gLastDATAReadError);
#endif
  }
  if (gReadWaitThread != -1) {
    thread = gReadWaitThread;
    gReadWaitThread = -1;
    WakeupThread(thread);
  }
}

void snd_VAGStreamDMAThread(void) {
  int state, disabled;
#if SND989_VERSION == 0x020608
  int result = 0;
#endif
  while (1) {
    if (!VAGStreamDMAList ||
#if SND989_VERSION == 0x020604
        (gDMABusy[0] && gDMABusy[1])
#else
          (gDMAInUse[0] && gDMAInUse[1])
#endif
    ) {
#if SND989_VERSION == 0x020604
      WaitSema(gStartDMASema);
#else
        result = WaitSema(gStartDMASema);
#endif
    }
#if SND989_VERSION == 0x020608
    if (result)
      snd_ShowError(70, result, 0, 0, 0);
#endif
    if (!gDMABusy[0] && !gDMAInUse[0] && VAGStreamDMAList) {
      disabled = CpuSuspendIntr(&state);
      gDMAInUse[0] = 1;
#if SND989_VERSION == 0x020608
      while (!gDMABusy[0] && VAGStreamDMAList) {
#endif
        gDMABusy[0] = VAGStreamDMAList;
        VAGStreamDMAList = VAGStreamDMAList->next;
        if (!VAGStreamDMAList)
          VAGStreamDMAListTail = 0;
#if SND989_VERSION == 0x020608
        if (gDMABusy[0]->owner->handler->SH.flags & 4)
          gDMABusy[0] = 0;
      }
#endif
      if (!disabled)
        CpuResumeIntr(state);
#if SND989_VERSION == 0x020608
      if (!gDMABusy[0]) {
        gDMAInUse[0] = 0;
      } else {
#endif
        sceSdSetTransIntrHandler(0, snd_VAGDMADone0, 0);
        snd_KickVAGDMA(0);
#if SND989_VERSION == 0x020608
      }
#endif
    }
    if (!gDMABusy[1] && !gDMAInUse[1] && VAGStreamDMAList) {
      disabled = CpuSuspendIntr(&state);
      gDMAInUse[1] = 1;
#if SND989_VERSION == 0x020608
      while (!gDMABusy[1] && VAGStreamDMAList) {
#endif
        gDMABusy[1] = VAGStreamDMAList;
        VAGStreamDMAList = VAGStreamDMAList->next;
        if (!VAGStreamDMAList)
          VAGStreamDMAListTail = 0;
#if SND989_VERSION == 0x020608
        if (gDMABusy[1]->owner->handler->SH.flags & 4)
          gDMABusy[1] = 0;
      }
#endif
      if (!disabled)
        CpuResumeIntr(state);
#if SND989_VERSION == 0x020604
      sceSdSetTransIntrHandler(1, snd_VAGDMADone1, 0);
#else
        if (!gDMABusy[1]) {
          gDMAInUse[1] = 0;
        } else {
          sceSdSetTransIntrHandler(1, snd_VAGDMADone0, 0);
#endif
      snd_KickVAGDMA(1);
#if SND989_VERSION == 0x020608
    }
#endif
  }
}
}

int snd_GetFreeSPUDMA(void) {
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  if (!gDMAInUse[0]) {
    gDMAInUse[0] = 1;
    if (!disabled)
      CpuResumeIntr(intr_state);
    sceSdSetTransIntrHandler(0, 0, 0);
    return 0;
  }
  if (!gDMAInUse[1]) {
    gDMAInUse[1] = 1;
    if (!disabled)
      CpuResumeIntr(intr_state);
    sceSdSetTransIntrHandler(1, 0, 0);
    return 1;
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return -1;
}

void snd_FreeSPUDMA(int channel) {
  gDMAInUse[channel] = 0;
  if (VAGStreamDMAList)
    SignalSema(gStartDMASema);
}

void snd_WaitDMADone(void) {
  while (gDMAInUse[0] || gDMAInUse[1]) {
    gWaitingDMAComplete = GetThreadId();
    SleepThread();
  }
  gWaitingDMAComplete = 0;
}

#if SND989_VERSION == 0x020604

#if SND989_VERSION == 0x020604
int snd_VAGDMADone0(int channel, void* data) {
  gDMAInUse[0] = 0;
  gLastDMA0Complete = 1;
  iSignalSema(gDoneDMASema);
  if (gWaitingDMAComplete > 0) {
    iWakeupThread(gWaitingDMAComplete);
    gWaitingDMAComplete = 0;
  }
  return 0;
}
#endif

#if SND989_VERSION == 0x020604
int snd_VAGDMADone1(int channel, void* data) {
  gDMAInUse[1] = 0;
  gLastDMA1Complete = 1;
  iSignalSema(gDoneDMASema);
  if (gWaitingDMAComplete > 0) {
    iWakeupThread(gWaitingDMAComplete);
    gWaitingDMAComplete = 0;
  }
  return 0;
}
#endif

#endif

void snd_KickVAGDMA(int channel) {
  u_int size;
#if SND989_VERSION == 0x020608
  if (!gDMABusy[channel]) {
    gDMAInUse[channel] = 0;
    return;
  }
#endif
  if (gDMABusy[channel]->flags & 0x100)
    size = (gDMABusy[channel]->owner->buffer_size / 2) *
           gDMABusy[channel]->owner->handler->num_streams;
  else
    size = gDMABusy[channel]->owner->buffer_size / 2;
  sceSdVoiceTrans(channel, 0, gDMABusy[channel]->buffer, gDMABusy[channel]->spu_buffer, size);
}

void snd_VAGStreamDMADoneThread(void) {
#if SND989_VERSION == 0x020608
  int result = 0;
#endif
  while (1) {
    if (!gLastDMA0Complete && !gLastDMA1Complete) {
#if SND989_VERSION == 0x020604
      WaitSema(gDoneDMASema);
#else
          result = WaitSema(gDoneDMASema);
#endif
    }
#if SND989_VERSION == 0x020608
    if (result)
      snd_ShowError(71, result, 0, 0, 0);
#endif
    if (gLastDMA0Complete) {
      gLastDMA0Complete = 0;
      snd_VAGDMADone(0);
    }
    if (gLastDMA1Complete) {
      gLastDMA1Complete = 0;
      snd_VAGDMADone(1);
    }
    if (VAGStreamDMAList)
      SignalSema(gStartDMASema);
  }
}

void snd_VAGDMADone(int channel) {
  u_int flags;
  int first, start, reload;
  int end;
  struct VAGStreamHeader* stream;
  int buffer;
  int core, voice;
#if SND989_VERSION == 0x020608
  if (!gDMABusy[channel]) {
    gDMAInUse[channel] = 0;
    return;
  }
#endif
  flags = gDMABusy[channel]->flags;
  end = gDMABusy[channel]->is_end;
  first = flags & 1;
  start = flags & 4;
  reload = flags & 0x20;
  gDMABusy[channel]->flags &= 0x300;
  if (!(gDMABusy[channel]->owner->flags & VAG_STREAM_FREE)) {
    if (start) {
#if SND989_VERSION == 0x020608
      gDMABusy[channel]->owner->handler->SH.flags |= 8;
#endif
      snd_StartVAGStreamSounding(gDMABusy[channel]->owner, 1);
    } else if (!first) {
      if (gDMABusy[channel]->flags & 0x100) {
        stream = gDMABusy[channel]->owner;
        buffer = gDMABusy[channel] != &stream->buffers[0];
        while (stream) {
          core = stream->voice / SND_VOICES_PER_CORE;
          voice = stream->voice % SND_VOICES_PER_CORE;
#if SND989_VERSION == 0x020604
          gSettingStreamLoopAddress = 1;
#endif
          sceSdSetAddr(core | (voice << 1) | SD_VA_LSAX, stream->buffers[buffer].spu_buffer);
#if SND989_VERSION == 0x020604
          gSettingStreamLoopAddress = 0;
#endif
          stream = stream->sync_list;
        }
      } else {
        core = gDMABusy[channel]->owner->voice / SND_VOICES_PER_CORE;
        voice = gDMABusy[channel]->owner->voice % SND_VOICES_PER_CORE;
#if SND989_VERSION == 0x020604
        gSettingStreamLoopAddress = 1;
#endif
        sceSdSetAddr(core | (voice << 1) | SD_VA_LSAX, gDMABusy[channel]->spu_buffer);
#if SND989_VERSION == 0x020604
        gSettingStreamLoopAddress = 0;
#endif
      }
    }
    if ((gDMABusy[channel]->owner->flags & 0x1a) && !(gDMABusy[channel]->owner->flags & 0x20)) {
      if (reload)
        gDMABusy[channel]->flags |= 0x40;
      if (!snd_AddVAGStreamLoadBuffer(gDMABusy[channel])) {
#if SND989_VERSION == 0x020604
        snd_StopVAGStream(gDMABusy[channel]->owner->handler->SH.OwnerID);
#else
            gDMABusy[channel]->owner->handler->SH.flags = 4;
#endif
      }
    }
    if (end)
      gDMABusy[channel]->owner->flags |= 0x40;
  }
  gDMABusy[channel] = 0;
  SignalSema(gStartDMASema);
  SignalSema(gStartDataLoadSema);
}

void snd_StartVAGStreamSounding(struct VAGStreamHeader* stream, int do_sync_list) {
  sceSdBatch batch[18];
  sceSdBatch* entry;
  sceSdBatch* start;
  int count, paused;
  int voice_index;
  u_int core, voice;
  if (stream)
    start = batch;
  while (stream) {
    entry = start;
    start = 0;
    start = entry;

    count = 0;
    if (stream->voice == -1) {
      stream->flags |= 0x80;
      return;
    }
    core = stream->voice / SND_VOICES_PER_CORE;
    voice = stream->voice % SND_VOICES_PER_CORE;
    paused = stream->flags & VAG_STREAM_PREBUFFER;
    voice_index = *(volatile int*)&stream->voice;
    gChannelStatus[voice_index].volume_left = gChannelStatus[voice_index].volume_right = 0;
    ++gNumStreamsPlaying;
    gChannelStatus[stream->voice].group = stream->handler->SH.VolGroup;
    snd_MakeVolumesB(stream->handler->group_volumes[stream->group], stream->handler->SH.Current_Vol,
                     stream->handler->SH.Current_Pan, stream->original_volume, stream->original_pan,
                     127, 0, (VolPair*)&gChannelStatus[stream->voice].volume_left);
    stream->flags |= 0x200;
    batch[0].func = SD_BSET_PARAM;
    entry->entry = core | (voice << 1);
    if (!paused)
      entry->value = (u_short)snd_AdjustVolToGroup(gChannelStatus[stream->voice].volume_left,
                                                   gChannelStatus[stream->voice].group) >>
                     1;
    else
      entry->value = 0;
    ++entry;
    ++count;
    entry->func = SD_BSET_PARAM;
    entry->entry = (core | (voice << 1)) | SD_VP_VOLR;
    if (!paused)
      entry->value = (u_short)snd_AdjustVolToGroup(gChannelStatus[stream->voice].volume_right,
                                                   gChannelStatus[stream->voice].group) >>
                     1;
    else
      entry->value = 0;
    ++entry;
    ++count;
    entry->func = SD_BSET_SWITCH;
    entry->entry = core | SD_S_VMIXL;
    entry->value = sceSdGetSwitch(core | SD_S_VMIXL) | (1 << voice);
    ++entry;
    ++count;
    entry->func = SD_BSET_SWITCH;
    entry->entry = core | SD_S_VMIXR;
    entry->value = sceSdGetSwitch(core | SD_S_VMIXR) | (1 << voice);
    ++entry;
    ++count;
    entry->func = SD_BSET_PARAM;
    entry->entry = (core | (voice << 1)) | SD_VP_PITCH;
    if (!paused)
      entry->value = stream->pitch;
    else
      entry->value = 0;
    ++entry;
    ++count;
    entry->func = SD_BSET_ADDR;
    entry->entry = (core | (voice << 1)) | SD_VA_SSA;
    {
      u_int offset = stream->read_state[3] + 48;
      entry->value = stream->buffers[0].spu_buffer + offset;
    }
    stream->last_spu_address = entry->value;
    ++entry;
    ++count;
    entry->func = SD_BSET_PARAM;
    entry->entry = (core | (voice << 1)) | SD_VP_ADSR1;
    entry->value = 15;
    ++entry;
    ++count;
    entry->func = SD_BSET_PARAM;
    entry->entry = (core | (voice << 1)) | SD_VP_ADSR2;
    entry->value = 0x1fc0;
    ++count;
    sceSdProcBatch(start, 0, count);
    stream->flags |= 4;
    gAwaitingKeyOn.core[core] |= 1 << voice;
    if (stream->flags & 0x100)
      gReverbVoices.core[core] |= 1 << voice;
    else
      gReverbVoices.core[core] &= ~(1 << voice);
    if (do_sync_list)
      stream = stream->sync_list;
    else
      stream = 0;
  }
}

int snd_MakeVAGLoop(signed char* data, int size, int final) {
  if (size == ADPCM_FRAME_SIZE) {
    memmove(data + ADPCM_FRAME_SIZE, data, ADPCM_FRAME_SIZE);
    memset(data, 0, ADPCM_FRAME_SIZE);
    data[ADPCM_FRAME_SIZE + 1] = ADPCM_LOOP_START | ADPCM_REPEAT | ADPCM_END;
  } else if (size != 2 * ADPCM_FRAME_SIZE) {
    goto normal;
  }
  data[1] = ADPCM_END;
  FlushDcache();
  return 2 * ADPCM_FRAME_SIZE;
normal:
  if (!final) {
    data[1] = ADPCM_LOOP_START | ADPCM_REPEAT;
    data[ADPCM_FRAME_SIZE + 1] = ADPCM_REPEAT;
    (data + size)[1 - ADPCM_FRAME_SIZE] = ADPCM_REPEAT | ADPCM_END;
  } else {
    data[1] = ADPCM_LOOP_START;
    (data + size)[1 - ADPCM_FRAME_SIZE] = ADPCM_LOOP_START | ADPCM_REPEAT | ADPCM_END;
    (data + size)[1 - 2 * ADPCM_FRAME_SIZE] = ADPCM_END;
  }
  FlushDcache();
  return size;
}

struct VAGStreamHeader* snd_GetFreeVAGStream(void) {
  struct VAGStreamHeader* walk;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  walk = VAGStreamList;
  while (walk) {
    if (walk->flags & VAG_STREAM_FREE) {
#if SND989_VERSION == 0x020608
      walk->flags = VAG_STREAM_ALLOCATED;
      walk->sync_list = 0;
#endif
      break;
    }
    walk = walk->next;
  }
  do {
    if (!disabled)
      CpuResumeIntr(intr_state);
  } while (0);
  return walk;
}

int snd_AddVAGStreamDMABuffer(struct VAGBuffer* buffer) {
  int intr_state;
  int disabled;
  if (!(buffer->flags & VAG_BUFFER_DMA_READY)) {
#if SND989_VERSION == 0x020604
    printf(stream_message_42, snd_GetTick());
#else
        snd_ShowError(72, 0, 0, 0, 0);
#endif
    return 0;
  }
  disabled = CpuSuspendIntr(&intr_state);
  if (!VAGStreamDMAListTail)
    VAGStreamDMAList = buffer;
  else
    VAGStreamDMAListTail->next = buffer;
  VAGStreamDMAListTail = buffer;
  buffer->next = 0;
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 1;
}

int snd_AddVAGStreamLoadBuffer(struct VAGBuffer* buffer) {
  struct VAGBuffer* walk;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  walk = VAGStreamLoadList;
  while (walk) {
    if (walk == buffer) {
      if (!disabled)
        CpuResumeIntr(intr_state);
#if SND989_VERSION == 0x020604
      printf(stream_message_43, snd_GetTick());
#else
          snd_ShowError(73, 0, 0, 0, 0);
#endif
      return 0;
    }
    walk = walk->next;
  }
  if (!VAGStreamLoadListTail)
    VAGStreamLoadList = buffer;
  else
    VAGStreamLoadListTail->next = buffer;
  VAGStreamLoadListTail = buffer;
  buffer->next = 0;
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 1;
}

void snd_RemoveVAGStreamLoadBuffer(struct VAGBuffer* buffer) {
  struct VAGBuffer* walk;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  if (VAGStreamLoadList == buffer) {
    VAGStreamLoadList = buffer->next;
    if (!VAGStreamLoadList)
      VAGStreamLoadListTail = 0;
  } else {
    walk = VAGStreamLoadList;
    while (walk) {
      if (walk->next == buffer)
        break;
      walk = walk->next;
    }
    if (walk) {
      walk->next = buffer->next;
      if (!walk->next)
        VAGStreamLoadListTail = walk;
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
}

void snd_RemoveVAGStreamDMABuffer(struct VAGBuffer* buffer) {
  struct VAGBuffer* walk;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  if (VAGStreamDMAList == buffer) {
    VAGStreamDMAList = buffer->next;
    if (!VAGStreamDMAList)
      VAGStreamDMAListTail = 0;
  } else {
    walk = VAGStreamDMAList;
    while (walk) {
      if (walk->next == buffer)
        break;
      walk = walk->next;
    }
    if (walk) {
      walk->next = buffer->next;
      if (!walk->next)
        VAGStreamDMAListTail = walk;
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
}

#if SND989_VERSION == 0x020604

#if SND989_VERSION == 0x020604
void snd_ShowCDError(int error) {
  switch (error) {
    case SCECdErFAIL:
      printf(stream_message_49);
      break;
    case SCECdErNO:
      printf(stream_message_50);
      break;
    case SCECdErEOM:
      printf(stream_message_51);
      break;
    case SCECdErTRMOPN:
      printf(stream_message_52);
      break;
    case SCECdErREAD:
      printf(stream_message_53);
      break;
    case SCECdErPRM:
      printf(stream_message_54);
      break;
    case SCECdErILI:
      printf(stream_message_55);
      break;
    case SCECdErIPI:
      printf(stream_message_56);
      break;
    case SCECdErCUD:
      printf(stream_message_57);
      break;
    case SCECdErNORDY:
      printf(stream_message_58);
      break;
    case SCECdErNODISC:
      printf(stream_message_59);
      break;
    case SCECdErOPENS:
      printf(stream_message_60);
      break;
    case SCECdErCMD:
      printf(stream_message_61);
      break;
    case SCECdErABRT:
      printf(stream_message_62);
      break;
  }
}
#endif

#endif

void snd_AddQueEntryToHandler(struct VAGStreamHandler* handler, struct VAGStreamQueEntry* entry) {
  struct VAGStreamQueEntry* walk;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  walk = handler->queue;
  if (!walk)
    handler->queue = entry;
  else {
    while (walk->next)
      walk = walk->next;
    walk->next = entry;
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
}

struct VAGStreamQueEntry* snd_GetFreeQueSlot(void) {
  int index;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (index = 0; index < VAG_STREAM_QUEUE_CAPACITY; ++index) {
    if (!gVAGStreamQue[index].flags) {
      gVAGStreamQue[index].flags = 1;
      gVAGStreamQue[index].next = 0;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return &gVAGStreamQue[index];
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
}

void snd_FreeQueChain(struct VAGStreamQueEntry* entry) {
  while (entry) {
    entry->flags = 0;
    entry = entry->next;
  }
}

void snd_CleanUpThread(int* id) {
  if (*id) {
    TerminateThread(*id);
    DeleteThread(*id);
    *id = 0;
  }
}

void snd_CleanUpSema(int* id) {
  if (*id) {
    DeleteSema(*id);
    *id = 0;
  }
}

void snd_FreeVAGStreamResources(void) {
  struct VAGStreamHeader* walk = VAGStreamList;
  struct VAGStreamHeader* hold;
  int disabled;
#if SND989_VERSION == 0x020604
  if (gDataLoadThread) {
    TerminateThread(gDataLoadThread);
    DeleteThread(gDataLoadThread);
    gDataLoadThread = 0;
  }
  if (gDMAThread) {
    TerminateThread(gDMAThread);
    DeleteThread(gDMAThread);
    gDMAThread = 0;
  }
  if (gDMADoneThread) {
    TerminateThread(gDMADoneThread);
    DeleteThread(gDMADoneThread);
    gDMADoneThread = 0;
  }
  if (gLoadDoneThread) {
    TerminateThread(gLoadDoneThread);
    DeleteThread(gLoadDoneThread);
    gLoadDoneThread = 0;
  }
  if (gStartDataLoadSema) {
    DeleteSema(gStartDataLoadSema);
    gStartDataLoadSema = 0;
  }
  if (gStartDMASema) {
    DeleteSema(gStartDMASema);
    gStartDMASema = 0;
  }
  if (gDoneDMASema) {
    DeleteSema(gDoneDMASema);
    gDoneDMASema = 0;
  }
  if (gDoneLoadSema) {
    DeleteSema(gDoneLoadSema);
    gDoneLoadSema = 0;
  }
#else
      int intr_state;
      snd_CleanUpThread(&gDataLoadThread);
      snd_CleanUpThread(&gDMAThread);
      snd_CleanUpThread(&gDMADoneThread);
      snd_CleanUpThread(&gLoadDoneThread);
      snd_CleanUpSema(&gStartDataLoadSema);
      snd_CleanUpSema(&gStartDMASema);
      snd_CleanUpSema(&gDoneDMASema);
      snd_CleanUpSema(&gDoneLoadSema);
#endif
  gLastDMA0Complete = 0;
  gLastDMA1Complete = 0;
  if (walk) {
    gFreeProc(walk->buffers[0].buffer);
#if SND989_VERSION == 0x020604
    disabled = CpuDisableIntr();
#else
        disabled = CpuSuspendIntr(&intr_state);
#endif
    snd_SRAMFree(walk->buffers[0].spu_buffer, walk->buffer_size * gNumVAGStreams);
    if (!disabled) {
#if SND989_VERSION == 0x020604
      CpuEnableIntr();
#else
          CpuResumeIntr(intr_state);
#endif
    }
    while (walk) {
      hold = walk->next;
      gFreeProc(walk);
      walk = hold;
    }
  }
  VAGStreamList = 0;
  if (gVAGStreamHandler) {
    gFreeProc(gVAGStreamHandler);
    gVAGStreamHandler = 0;
  }
  gMasterReadBufferSize = 0;
  if (gMasterReadBuffer) {
    gFreeProc(gMasterReadBuffer);
    gMasterReadBuffer = 0;
  }
  gNumVAGStreams = 0;
}

void snd_CloseVAGStreaming(void) {
  if (gVAGStreamingInited) {
#if SND989_VERSION == 0x020604
    int index;
    for (index = 0; index < gNumVAGStreams; ++index) {
      if (gVAGStreamHandler[index].SH.Sound)
        snd_StopVAGStream(gVAGStreamHandler[index].SH.OwnerID);
    }
#else
        snd_StopAllStreams();
#endif
    sceCdCallback(0);
    sceCdStop();
    sceCdSync(0);
    snd_FreeVAGStreamResources();
    sceSdSetTransIntrHandler(0, 0, 0);
    sceSdSetTransIntrHandler(1, 0, 0);
    gVAGStreamingInited = 0;
  }
}

int snd_StreamSafeCheckCDIdle(int block) {
  if (!VAGStreamList) {
#if SND989_VERSION == 0x020604
    printf(stream_message_44);
#else
        snd_ShowError(74, 0, 0, 0, 0);
#endif
    return 1;
  }
#if SND989_VERSION == 0x020604
  if (gVAGReadBusy || gDataReadBusy || gSSNeedSectors) {
#else
      while (gVAGReadBusy || gDataReadBusy || gSSNeedSectors) {
#endif
    if (!block)
      return 0;
    gCDIdleWaiter = GetThreadId();
#if SND989_VERSION == 0x020604
    printf(stream_message_45, gVAGReadBusy, gDataReadBusy, gSSNeedSectors);
#endif
    SleepThread();
#if SND989_VERSION == 0x020604
    printf(stream_message_46);
#endif
    gCDIdleWaiter = 0;
  }
  sceCdCallback(0);
  return 1;
}

struct VAGStreamHandler* snd_GetFreeVAGStreamHandler(void) {
  int index;
  int intr_state;
  int disabled = CpuSuspendIntr(&intr_state);
  for (index = 0; index < gNumVAGStreams; ++index) {
    if (!gVAGStreamHandler[index].SH.Sound) {
      struct VAGStreamHandler* handler = &gVAGStreamHandler[index];
      u_int owner = (u_short)(handler->SH.OwnerID + 1);
      handler->SH.next = 0;
      handler->SH.parent = 0;
      handler->SH.child = 0;
      handler->SH.flags = 0;
      handler->SH.OwnerID = owner | (index << 16) | VAG_STREAM_HANDLE_TYPE;
      if (!disabled)
        CpuResumeIntr(intr_state);
      return &gVAGStreamHandler[index];
    }
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return 0;
}

void snd_AddStreamToHandler(struct VAGStreamHandler* handler, struct VAGStreamHeader* stream) {
  struct VAGStreamHeader* walk;
  if (!handler->SH.Sound) {
    handler->SH.Sound = stream;
  } else {
    walk = handler->SH.Sound;
    while (walk->sync_list) {
      if (walk == stream)
        break;
      walk = walk->sync_list;
    }
    if (walk == stream) {
#if SND989_VERSION == 0x020604
      printf(stream_message_47);
#else
          snd_ShowError(75, 0, 0, 0, 0);
#endif
    } else {
      walk->sync_list = stream;
    }
  }
}

void snd_AddStreamToHandlerQueue(struct VAGStreamHandler* handler, struct VAGStreamHeader* stream) {
  struct VAGStreamHeader* walk;
  if (!handler->queued_stream) {
    handler->queued_stream = stream;
  } else {
    walk = handler->queued_stream;
    while (walk->sync_list) {
      if (walk == stream)
        break;
      walk = walk->sync_list;
    }
    if (walk == stream) {
#if SND989_VERSION == 0x020604
      printf(stream_message_47);
#else
          snd_ShowError(76, 0, 0, 0, 0);
#endif
    } else {
      walk->sync_list = stream;
    }
  }
}

int snd_GetNumStreamChannels(struct VAGStreamHeader* stream) {
  int count = 0;
  while (stream) {
    ++count;
    stream = stream->sync_list;
  }
  return count;
}

#if SND989_VERSION == 0x020604
inline
#endif
    void
    snd_SetVAGStreamVolPan(u_int handle, int volume, int pan) {
  struct VAGStreamHandler* handler;
  struct VAGStreamHeader* stream;
  VolPair output;
  int locked;
  snd_LockMasterTick();
  handler = (struct VAGStreamHandler*)snd_CheckHandlerStillActive(handle);
  if (handler) {
    if (volume < 0)
      handler->SH.Current_Vol = -volume;
    else if (volume != 0x7fffffff)
      handler->SH.Current_Vol = (volume * 127) >> 10;
    if (handler->SH.Current_Vol > 127)
      handler->SH.Current_Vol = 127;
    if (pan == -1)
      handler->SH.Current_Pan = 0;
    else if (pan != -2)
      handler->SH.Current_Pan = pan;
    locked = snd_LockVoiceAllocator(1);
    stream = handler->SH.Sound;
    if (!stream) {
      if (locked)
        snd_UnlockVoiceAllocator();
      snd_UnlockMasterTick();
#if SND989_VERSION == 0x020604
      printf(stream_message_48, handler);
#else
          snd_ShowError(77, 0, 0, 0, 0);
#endif
      return;
    }
    while (stream) {
      if ((u_int)stream->voice < SND_NUM_VOICES) {
        snd_MakeVolumesB(handler->group_volumes[stream->group], handler->SH.Current_Vol,
                         handler->SH.Current_Pan, stream->original_volume, stream->original_pan,
                         127, 0, (VolPair*)&gChannelStatus[stream->voice].volume_left);
        output.left = (u_short)snd_AdjustVolToGroup(gChannelStatus[stream->voice].volume_left,
                                                    gChannelStatus[stream->voice].group) >>
                      1;
        output.right = (u_short)snd_AdjustVolToGroup(gChannelStatus[stream->voice].volume_right,
                                                     gChannelStatus[stream->voice].group) >>
                       1;
        sceSdSetParam(
            (stream->voice / SND_VOICES_PER_CORE) | ((stream->voice % SND_VOICES_PER_CORE) << 1),
            output.left);
        sceSdSetParam(
            ((stream->voice / SND_VOICES_PER_CORE) | ((stream->voice % SND_VOICES_PER_CORE) << 1)) |
                0x100,
            output.right);
      }
      stream = stream->sync_list;
    }
    if (locked)
      snd_UnlockVoiceAllocator();
  }
  snd_UnlockMasterTick();
}

struct VAGStreamHeader* snd_GetFirstOfAdjacentStreamHeaders(struct VAGStreamHeader* consider,
                                                            int channels) {
  int result = 0;
  struct VAGStreamHeader* start = VAGStreamList;
  struct VAGStreamHeader* last_tested = 0;
  int intr_state;
  int index;
  int disabled = CpuSuspendIntr(&intr_state);
  while (start && !result) {
    result = snd_GetFreeStreamLoop(start, consider, channels, &last_tested);
    if (!result)
      start = last_tested ? last_tested->next : 0;
  }
  if (result) {
    last_tested = start;
    for (index = 0; index < channels; ++index) {
#if SND989_VERSION == 0x020604
      last_tested->flags &= ~VAG_STREAM_FREE;
#else
          last_tested->flags = 0;
          last_tested->sync_list = 0;
#endif
      last_tested = last_tested->next;
    }
  } else {
    start = 0;
  }
  if (!disabled)
    CpuResumeIntr(intr_state);
  return start;
}

int snd_GetFreeStreamLoop(struct VAGStreamHeader* start,
                          struct VAGStreamHeader* consider,
                          int channels,
                          struct VAGStreamHeader** last_tested) {
  *last_tested = start;
  if (!(start->flags & VAG_STREAM_FREE) && start != consider)
    return 0;
  if (channels == 1)
    return 1;
  if (start->next)
    return snd_GetFreeStreamLoop(start->next, consider, channels - 1, last_tested);
  return 0;
}
