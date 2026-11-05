#ifndef SCE_IOP_989SND_VAG_STREAM_H
#define SCE_IOP_989SND_VAG_STREAM_H

#include "sce/iop/989snd/handlers.h"
#include "sce/iop/types.h"

struct VAGBuffer;
struct VAGStreamHandler;
struct VAGStreamHeader;
struct VAGStreamQueEntry;

#if SND989_VERSION == 0x020604
void snd_InitVAGStreaming(int channels, int buffer_size, int mode);
#else
void snd_InitVAGStreaming(int channels, int buffer_size, int mode, int enable_read_buffer);
#endif
int snd_AllocateStreamResources(struct VAGStreamHeader** left,
                                struct VAGStreamHeader** right,
                                int stereo,
                                int get_voices,
                                int volume_group);
u_int snd_PlayVAGStreamByLoc(u_int first_sector,
                             u_int second_sector,
                             int first_offset,
                             int second_offset,
                             int volume,
                             int pan,
                             int volume_group,
                             int queue,
                             u_int flags);
void snd_TryPreBufferQueElement(struct VAGStreamHandler* handler);
void snd_PauseVAGStream(u_int handle);
void snd_ContinueVAGStream(u_int handle);
void snd_StopVAGStream(u_int handle);
void snd_ProcessVAGStreamTick(struct VAGStreamHandler* handler);
int snd_CheckVAGStreamProgress(struct VAGStreamHeader* stream, int* dma_needed);
void snd_FixVAGStreamSamplesPlayed(struct VAGStreamHeader* stream, u_int address);
void snd_VAGStreamDataLoadThread(void);
void snd_KickDataRead(void);
void snd_KickVAGRead(void);
void snd_VAGStreamLoadDoneThread(void);
void snd_VAGStreamDMAThread(void);
void snd_VAGDMADone(int channel);
void snd_StartVAGStreamSounding(struct VAGStreamHeader* stream, int do_sync_list);
void snd_FreeVAGStreamResources(void);
struct VAGStreamHandler* snd_GetFreeVAGStreamHandler(void);
void snd_SetVAGStreamVolPan(u_int handle, int volume, int pan);
void snd_PauseAllStreams(void);
int snd_GetVAGStreamQueueCount(u_int handle);
u_int snd_GetVAGStreamLoc(u_int handle);
u_int snd_GetVAGStreamTimeRemaining(u_int handle);
u_int snd_GetVAGStreamTotalTime(u_int handle);
void snd_StopAllStreams(void);
int snd_StreamSafeCheckCDIdle(int block);
void snd_CloseVAGStreaming(void);
int snd_StreamSafeCdRead(u_int sector, u_int count, void* destination);
int snd_StreamSafeCdReadEEm(u_int sector, u_int count, void* destination);
int snd_StreamSafeCdSync(int mode);
void snd_StreamSafeCdBreak(void);
int snd_StreamSafeCdGetError(void);
int snd_GetFreeSPUDMA(void);
void snd_FreeSPUDMA(int channel);
void snd_WaitDMADone(void);
void snd_SetupVAGStream(struct VAGStreamHandler* handler,
                        struct VAGStreamHeader* stream,
                        int location,
                        int offset,
                        int queued);
void snd_VAGStreamDMADoneThread(void);
#if SND989_VERSION == 0x020604
int snd_VAGDMADone0(int channel, void* data);
int snd_VAGDMADone1(int channel, void* data);
#endif
void snd_KickVAGDMA(int channel);
#if SND989_VERSION == 0x020604
void snd_DoneVAGReadCB(int code);
#endif
void snd_SignalDataReadDone(void);
int snd_MakeVAGLoop(signed char* data, int size, int final);
struct VAGStreamHeader* snd_GetFreeVAGStream(void);
int snd_AddVAGStreamDMABuffer(struct VAGBuffer* buffer);
void snd_RemoveVAGStreamDMABuffer(struct VAGBuffer* buffer);
int snd_AddVAGStreamLoadBuffer(struct VAGBuffer* buffer);
void snd_RemoveVAGStreamLoadBuffer(struct VAGBuffer* buffer);
#if SND989_VERSION == 0x020604
void snd_ShowCDError(int error);
#endif
void snd_AddQueEntryToHandler(struct VAGStreamHandler* handler, struct VAGStreamQueEntry* entry);
struct VAGStreamQueEntry* snd_GetFreeQueSlot(void);
void snd_FreeQueChain(struct VAGStreamQueEntry* entry);
void snd_AddStreamToHandler(struct VAGStreamHandler* handler, struct VAGStreamHeader* stream);
void snd_AddStreamToHandlerQueue(struct VAGStreamHandler* handler, struct VAGStreamHeader* stream);
int snd_GetNumStreamChannels(struct VAGStreamHeader* stream);
struct VAGStreamHeader* snd_GetFirstOfAdjacentStreamHeaders(struct VAGStreamHeader* consider,
                                                            int channels);
int snd_GetFreeStreamLoop(struct VAGStreamHeader* start,
                          struct VAGStreamHeader* consider,
                          int channels,
                          struct VAGStreamHeader** last_tested);
void snd_CleanUpThread(int* id);
void snd_CleanUpSema(int* id);
#if SND989_VERSION == 0x020608
int snd_StreamHandlerIsBusyWithLoad(struct VAGStreamHandler* handler);
int snd_StreamHandlerIsBusyWithDMA(struct VAGStreamHandler* handler);
void snd_KillVAGStream(u_int handle, int from_where);
int snd_IsVAGStreamBuffered(u_int handle);
#endif

#define VAG_BUFFER_FIRST 0x10u
#define VAG_CD_SECTOR_SIZE 2048
#define VAG_BUFFER_DMA_READY 0x80u

struct VAGBuffer {
  u_int flags;
  void* buffer;
  u_int spu_buffer;
  struct VAGBuffer* next;
  struct VAGStreamHeader* owner;
  int is_end;
  u_int bytes;
};

struct VAGStreamQueEntry {
  u_int flags;
  int location;
  int offset;
  struct VAGStreamQueEntry* next;
};

#define VAG_STREAM_QUEUE_CAPACITY 64
extern struct VAGStreamQueEntry gVAGStreamQue[VAG_STREAM_QUEUE_CAPACITY];

struct VAGStreamHeader {
  u_int flags;
  signed char original_volume;
  signed char group;
  short original_pan;
  u_int pitch;
  u_int buffer_size;
  struct VAGBuffer buffers[2];
  u_int current_buffer;
  u_int saved_spu_address;
  u_int last_buffer_tick;
  int progress_errors;
  u_int remaining_state[2];
  int voice;
  u_int read_state[5];
  u_int samples_total;
  u_int bytes_played;
  int sample_rate;
  u_int last_spu_address;
  u_int position_state;
  struct VAGStreamHeader* sync_list;
  struct VAGStreamHeader* next;
  struct VAGStreamHandler* handler;
};

#define VAG_STREAM_FREE 0x1u
#define VAG_STREAM_ALLOCATED 0x2u
#define VAG_STREAM_PREBUFFER 0x10u

extern struct VAGBuffer* VAGStreamLoadList;
extern struct VAGBuffer* VAGStreamLoadListTail;
extern struct VAGBuffer* VAGStreamDMAList;
extern struct VAGBuffer* VAGStreamDMAListTail;
#endif
