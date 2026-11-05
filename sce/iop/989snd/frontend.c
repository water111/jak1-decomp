#include "sce/iop/989snd/frontend.h"

#include "sce/iop/989snd/autopan.h"
#include "sce/iop/989snd/autopitch.h"
#include "sce/iop/989snd/autopitchbend.h"
#include "sce/iop/989snd/autovol.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/extern_proc_internal.h"
#include "sce/iop/989snd/midi_control.h"
#include "sce/iop/989snd/playback.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sifcmd/sifrpc.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/stdlib.h"
#include "sce/iop/sysclib/string.h"
#include "sce/iop/sysmem/sysmem.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/types.h"

u_short g989Version = 0x0206;
struct ModuleInfo Module = {"989snd_Library", 0x0206};

int gThreadPriority_TICK = 9;
int gThreadPriority_RPC = 58;
int gThreadPriority_STREAM = 60;
#if SND989_VERSION == 0x020604
extern u_int snd_MESSAGE_RETURN_BUFFER[136];
u_int* gWriteBackdataOffset = snd_MESSAGE_RETURN_BUFFER + 1;
#else
int gDoRPC = 1;
char* snd_MESSAGE_RECIEVE_BUFFER = 0;
u_int* snd_MESSAGE_RETURN_BUFFER = 0;
u_int* gWriteBackdataOffset = 0;
#endif
struct Extern989Handler* gExternHandlersList = 0;

extern void (*gCommandFunc[])(char* message);
extern u_int* gWriteBackdataOffset;
#if SND989_VERSION == 0x020604
extern u_int snd_MESSAGE_RETURN_BUFFER[136];
// TODO hack
__asm__(".comm snd_MESSAGE_RETURN_BUFFER,544");
extern char snd_MESSAGE_RECIEVE_BUFFER[];
#else
extern u_int* snd_MESSAGE_RETURN_BUFFER;
extern char* snd_MESSAGE_RECIEVE_BUFFER;
#endif

struct SoundBank;
struct MultiMIDIBlockHeader;
struct SoundBank* snd_BankLoad(char* name, int offset);
struct SoundBank* snd_BankLoadByLoc(int sector, int offset);
struct MultiMIDIBlockHeader* snd_MMDLoad(char* name, int offset);
struct MultiMIDIBlockHeader* snd_MMDLoadByLoc(int sector, int offset);
extern u_int snd_LOADER_MESSAGE_RETURN_BUFFER[];

enum {
  SL_LOADBANK = 2,
  SL_LOADBANKBYLOC = 3,
  SL_LOADMMD = 4,
  SL_LOADMMDBYLOC = 5,
};

extern char snd_LOADER_MESSAGE_RECIEVE_BUFFER[];

int snd_RegisterExternProcHandler(struct Extern989Handler* handler) {
  struct Extern989Handler* work;
  if (snd_FindExternProcHandler(handler->proc_id)) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_RegisterExternProcHandler: Couldn't register extern proc handler... ID 0x%x (%d) "
        "exists\n",
        handler->proc_id, handler->proc_id);
#else
    snd_ShowError(1, handler->proc_id, handler->proc_id, 0, 0);
#endif
    return 0;
  }
  handler->next = 0;
  handler->reserved = 0;
  if (!gExternHandlersList) {
    gExternHandlersList = handler;
  } else {
    work = gExternHandlersList;
    while (work->next) {
      work = work->next;
    }
    work->next = handler;
  }
  return 1;
}

int snd_UnregisterExternProcHandler(struct Extern989Handler* handler) {
  struct Extern989Handler* work;
  if (!snd_FindExternProcHandler(handler->proc_id)) {
#if SND989_VERSION == 0x020604
    printf("snd_RegisterExternProcHandler: Couldn't find extern proc handler ID 0x%x (%d)\n",
           handler->proc_id, handler->proc_id);
#else
    snd_ShowError(2, handler->proc_id, handler->proc_id, 0, 0);
#endif
    return 0;
  }
  if (gExternHandlersList == handler) {
    gExternHandlersList = handler->next;
  } else {
    work = gExternHandlersList;
    while (work) {
      if (work->next == handler)
        break;
      work = work->next;
    }
    work->next = work->next->next;
  }
  return 1;
}

struct Extern989Handler* snd_FindExternProcHandler(u_int id) {
  return find_handler(id);
}

int snd_DoExternCall(u_int id, int index, int arg1, int arg2, int arg3, int arg4, int arg5) {
  int result;
  struct Extern989Handler* work;
#if SND989_VERSION == 0x020604
  work = find_handler(id);
#else
  work = snd_FindExternProcHandler(id);
#endif
  if (!work) {
#if SND989_VERSION == 0x020604
    printf("snd_DoExternCall: Couldn't find extern proc 0x%x (%d)\n", id, id);
#else
    snd_ShowError(3, id, id, 0, 0);
#endif
    result = 0;
  } else if (index < work->num_funcs) {
    result = work->procs[index](arg1, arg2, arg3, arg4, arg5);
  } else {
#if SND989_VERSION == 0x020604
    printf("snd_DoExternCall: Attempt to call function out of range in extern proc id 0x%x\n", id);
#else
    snd_ShowError(4, id, 0, 0, 0);
#endif
    result = 0;
  }
  return result;
}

struct SoundBank;
struct MultiMIDIBlockHeader;
extern u_int* gWriteBackdataOffset;
int snd_StartSoundSystem(void);
void snd_StopSoundSystem(void);
struct SoundBank* snd_BankLoad(char* name, int offset);
struct SoundBank* snd_BankLoadByLoc(int sector, int offset);
struct MultiMIDIBlockHeader* snd_MMDLoad(char* name, int offset);
struct MultiMIDIBlockHeader* snd_MMDLoadByLoc(int sector, int offset);
void snd_UnloadBank(struct SoundBank* bank);
void snd_UnloadMMD(struct MultiMIDIBlockHeader* midi);
void snd_ResolveBankXREFS(void);
void snd_SetMasterVolume(int group, int volume);
int snd_GetMasterVolume(int group);
void snd_SetPlaybackMode(int mode);
int snd_GetPlaybackMode(void);
void snd_SetMixerMode(int channel_mode, int reverb_mode);
void snd_SetGroupVoiceRange(int group, int first, int last);
void snd_SetReverbType(int core, int type);
void snd_SetReverbDepth(int core, int left, int right);
void snd_AutoReverb(int core, int depth, int ticks, int channels);

void snd_CMD_SL_INIT(char* message) {
  snd_StartSoundSystem();
}

void snd_CMD_SL_CLOSE(char* message) {
  snd_StopSoundSystem();
}

void snd_CMD_SL_LOADBANK(char* message) {
#if SND989_VERSION == 0x020604
  printf("Can't load bank by name using 989snd.irx!\n");
  *gWriteBackdataOffset = 0;
#else
  *gWriteBackdataOffset = (u_int)snd_BankLoad(message + 4, *(int*)message);
#endif
}

void snd_CMD_SL_LOADBANKBYLOC(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = (u_int)snd_BankLoadByLoc(data[0], data[1]);
}

void snd_CMD_SL_LOADMMD(char* message) {
#if SND989_VERSION == 0x020604
  printf("Can't load mmd by name using 989snd.irx!\n");
  *gWriteBackdataOffset = 0;
#else
  *gWriteBackdataOffset = (u_int)snd_MMDLoad(message + 4, *(int*)message);
#endif
}

void snd_CMD_SL_LOADMMDBYLOC(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = (u_int)snd_MMDLoadByLoc(data[0], data[1]);
}

void snd_CMD_SL_UNLOADBANK_A(char* message) {
  snd_UnloadBank(*(struct SoundBank**)message);
}

void snd_CMD_SL_UNLOADMIDI_A(char* message) {
  snd_UnloadMMD(*(struct MultiMIDIBlockHeader**)message);
}

void snd_CMD_SL_RESOLVEBANKS(char* message) {
  snd_ResolveBankXREFS();
}

void snd_CMD_SL_SETMASTERVOL_A(char* message) {
  int* data = (int*)message;
  snd_SetMasterVolume(data[0], data[1]);
}

void snd_CMD_SL_GETMASTERVOL(char* message) {
  *gWriteBackdataOffset = snd_GetMasterVolume(*(int*)message);
}

void snd_CMD_SL_SETPLAYBACKMODE_A(char* message) {
  snd_SetPlaybackMode(*(int*)message);
}

void snd_CMD_SL_GETPLAYBACKMODE(char* message) {
  *gWriteBackdataOffset = snd_GetPlaybackMode();
}

void snd_CMD_SL_SETMIXERMODE_A(char* message) {
  int* data = (int*)message;
  snd_SetMixerMode(data[0], data[1]);
}

void snd_CMD_SL_SETGROUPVOICERANGE(char* message) {
  int* data = (int*)message;
  snd_SetGroupVoiceRange(data[0], data[1], data[2]);
}

void snd_CMD_SL_SETREVERBTYPE(char* message) {
  int* data = (int*)message;
  snd_SetReverbType(data[0], data[1]);
}

void snd_CMD_SL_SETREVERBDEPTH(char* message) {
  int* data = (int*)message;
  snd_SetReverbDepth(data[0], data[1], data[2]);
}

void snd_CMD_SL_AUTOVERB(char* message) {
  int* data = (int*)message;
  snd_AutoReverb(data[0], data[1], data[2], data[3]);
}

void snd_PauseAllSoundsInGroup(u_int groups);
void snd_ContinueAllSoundsInGroup(u_int groups);
void snd_StopAllSounds(void);

void snd_CMD_SL_PLAYSOUND(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = snd_PlaySoundVolPanPMPB((struct SoundBank*)data[0], data[1], data[2],
                                                  data[3], *(short*)&data[4], *(short*)&data[5]);
}

void snd_CMD_SL_PLAYSOUND_A(char* message) {
  int* data = (int*)message;
  snd_PlaySoundVolPanPMPB((struct SoundBank*)data[0], data[1], data[2], data[3], *(short*)&data[4],
                          *(short*)&data[5]);
}

void snd_CMD_SL_PAUSESOUND(char* message) {
  snd_PauseSound(*(u_int*)message);
}

void snd_CMD_SL_CONTINUESOUND(char* message) {
  snd_ContinueSound(*(u_int*)message);
}

void snd_CMD_SL_STOPSOUND(char* message) {
  snd_StopSound(*(u_int*)message);
}

void snd_CMD_SL_PAUSEALLSOUNDS(char* message) {
  snd_PauseAllSoundsInGroup(*(u_int*)message);
}

void snd_CMD_SL_CONTINUEALLSOUNDS(char* message) {
  snd_ContinueAllSoundsInGroup(*(u_int*)message);
}

void snd_CMD_SL_STOPALLSOUNDS(char* message) {
  snd_StopAllSounds();
}

void snd_CMD_SL_SOUNDISSTILLPLAYING(char* message) {
  *gWriteBackdataOffset = snd_SoundIsStillPlaying(*(u_int*)message);
}

void snd_CMD_SL_ISSOUNDALOOPER(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = snd_IsSoundALooper((struct SoundBank*)data[0], data[1]);
}

void snd_CMD_SL_SETSOUNDVOLPAN_A(char* message) {
  int* data = (int*)message;
  snd_SetSoundVolPan(data[0], data[1], data[2]);
}

void snd_CMD_SL_GETSOUNDORIGPITCH(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = snd_GetSoundOriginalPitch((struct SoundBank*)data[0], data[1]);
}

void snd_CMD_SL_GETSOUNDCURRPITCH(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = snd_GetSoundCurrentPitch(data[0]);
}

void snd_CMD_SL_SETSOUNDPITCH_A(char* message) {
  int* data = (int*)message;
  snd_SetSoundPitch(data[0], data[1]);
}

void snd_CMD_SL_SETSOUNDPITCHBEND_A(char* message) {
  int* data = (int*)message;
  snd_SetSoundPitchBend(data[0], *(short*)&data[1]);
}

void snd_CMD_SL_SETSOUNDPITCHMOD_A(char* message) {
  int* data = (int*)message;
  snd_SetSoundPitchModifier(data[0], *(short*)&data[1]);
}

void snd_CMD_SL_SETSOUNDPARAMS(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset =
      snd_SetSoundParams(data[0], data[1], data[2], data[3], *(short*)&data[4], *(short*)&data[5]);
}

void snd_CMD_SL_AUTOVOL_A(char* message) {
  int* data = (int*)message;
  snd_AutoVol(data[0], *(short*)&data[1], *(short*)&data[2], *(short*)&data[3]);
}

void snd_CMD_SL_AUTOPAN_A(char* message) {
  int* data = (int*)message;
  snd_AutoPan(data[0], data[1], data[2], data[3], data[4]);
}

void snd_CMD_SL_AUTOPITCH_A(char* message) {
  int* data = (int*)message;
  snd_AutoPitch(data[0], data[1], data[2], data[3]);
}

void snd_CMD_SL_AUTOPITCHBEND_A(char* message) {
  int* data = (int*)message;
  snd_AutoPitchBend(data[0], data[1], data[2], data[3]);
}

void snd_CMD_SL_GETGLOBALEXCITE(char* message) {
  *gWriteBackdataOffset = snd_GetGlobalExcite();
}

void snd_CMD_SL_SETGLOBALEXCITE_A(char* message) {
  int* data = (int*)message;
  snd_SetGlobalExcite(data[0]);
}

void snd_CMD_SL_GETMIDIREGISTER(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = snd_GetMIDIRegister(data[0], data[1]);
}

#if SND989_VERSION == 0x020604
void snd_InitVAGStreaming(int channels, int buffer_size, int mode);
#else
void snd_InitVAGStreaming(int channels, int buffer_size, int mode, int enable_read_buffer);
#endif
u_int snd_PlayVAGStreamByLoc(u_int first_sector,
                             u_int second_sector,
                             int first_offset,
                             int second_offset,
                             int volume,
                             int pan,
                             int volume_group,
                             int queue,
                             u_int flags);

void snd_CMD_SL_SETMIDIREGISTER_A(char* message) {
  int* data = (int*)message;
  snd_SetMIDIRegister(data[0], data[1], *(short*)&data[2]);
}

void snd_CMD_SL_INITVAGSTREAMING(char* message) {
  int* data = (int*)message;
#if SND989_VERSION == 0x020604
  snd_InitVAGStreaming(data[0], data[1], data[2]);
#else
  snd_InitVAGStreaming(data[0], data[1], data[2], data[3]);
#endif
}

void snd_CMD_SL_PLAYVAGSTREAMBYNAME(char* message) {
}

void snd_CMD_SL_PLAYVAGSTREAMBYLOC(char* message) {
  u_int* data = (u_int*)message;
  *gWriteBackdataOffset =
      snd_PlayVAGStreamByLoc(data[0], data[1], data[2] & 0xffff, data[3] & 0xffff, data[2] >> 16,
                             data[3] >> 16, data[4], data[5], data[6]);
}

void snd_PauseVAGStream(u_int);
void snd_ContinueVAGStream(u_int);
void snd_StopVAGStream(u_int);
int snd_GetVAGStreamQueueCount(u_int);
u_int snd_GetVAGStreamLoc(u_int);
int snd_GetVAGStreamTimeRemaining(u_int);
int snd_GetVAGStreamTotalTime(u_int);
int snd_IsVAGStreamBuffered(u_int);
void snd_StopAllStreams(void);
void snd_CloseVAGStreaming(void);
int snd_StreamSafeCheckCDIdle(int);
int snd_StreamSafeCdBreak(void);

void snd_CMD_SL_PAUSEVAGSTREAM(char* message) {
  snd_PauseVAGStream(*(u_int*)message);
}

void snd_CMD_SL_CONTINUEVAGSTREAM(char* message) {
  snd_ContinueVAGStream(*(u_int*)message);
}

void snd_CMD_SL_STOPVAGSTREAM(char* message) {
  snd_StopVAGStream(*(u_int*)message);
}

void snd_CMD_SL_GETVAGSTREAMQUEUECOUNT(char* message) {
  *gWriteBackdataOffset = snd_GetVAGStreamQueueCount(*(u_int*)message);
}

void snd_CMD_SL_GETVAGSTREAMLOC(char* message) {
  *gWriteBackdataOffset = snd_GetVAGStreamLoc(*(u_int*)message);
}

void snd_CMD_SL_GETVAGSTREAMTIMEREMAINING(char* message) {
  *gWriteBackdataOffset = snd_GetVAGStreamTimeRemaining(*(u_int*)message);
}

void snd_CMD_SL_GETVAGSTREAMTOTALTIME(char* message) {
  *gWriteBackdataOffset = snd_GetVAGStreamTotalTime(*(u_int*)message);
}

#if SND989_VERSION == 0x020608
void snd_CMD_SL_ISVAGSTREAMBUFFERED(char* message) {
  *gWriteBackdataOffset = snd_IsVAGStreamBuffered(*(u_int*)message);
}
#endif

void snd_CMD_SL_STOPALLVAGSTREAMS(char* message) {
  snd_StopAllStreams();
}

void snd_CMD_SL_CLOSEVAGSTREAMING(char* message) {
  snd_CloseVAGStreaming();
}

void snd_CMD_SL_STREAMSAFECHECKCDIDLE(char* message) {
  *gWriteBackdataOffset = snd_StreamSafeCheckCDIdle(*(int*)message);
}

void snd_CMD_SL_STREAMSAFECDBREAK(char* message) {
  *gWriteBackdataOffset = snd_StreamSafeCdBreak();
}

int snd_StreamSafeCdReadEEm(u_int sector, u_int count, void* destination);

void snd_CMD_SL_STREAMSAFECDREAD(char* message) {
  u_int* data = (u_int*)message;
  snd_StreamSafeCdReadEEm(data[0], data[1], (void*)data[2]);
}

void* snd_InitMovieSound(int buffer_size, int volume, int pan, int volume_group);
void snd_CloseMovieSound(void);
void snd_ResetMovieSound(void);
void snd_StartMovieSound(void* buffer, int size, int offset);

void snd_CMD_SL_ALLOCIOPRAM(char* message) {
  *gWriteBackdataOffset = (u_int)gAllocProc(*(int*)message, 7, 0);
}

void snd_CMD_SL_FREEIOPRAM(char* message) {
  gFreeProc(*(void**)message);
}

void snd_CMD_SL_INITMOVIESOUND(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = (u_int)snd_InitMovieSound(data[0], data[1], data[2], data[3]);
}

void snd_CMD_SL_CLOSEMOVIESOUND(char* message) {
  snd_CloseMovieSound();
}

void snd_CMD_SL_RESETMOVIESOUND(char* message) {
  snd_ResetMovieSound();
}

void snd_CMD_SL_STARTMOVIESOUND(char* message) {
  int* data = (int*)message;
  snd_StartMovieSound((void*)data[0], data[1], data[2]);
}

u_int snd_PauseMovieSound(void);
u_int snd_GetMovieBufferPos(void);
u_int snd_GetTick(void);
int snd_GetVoiceStatus(int voice);

void snd_CMD_SL_PAUSEMOVIESOUND(char* message) {
  *gWriteBackdataOffset = snd_PauseMovieSound();
}

void snd_CMD_SL_GETTRANSSTATUS(char* message) {
  *gWriteBackdataOffset = snd_GetMovieBufferPos();
}

void snd_CMD_SL_GETSTICK(char* message) {
  *gWriteBackdataOffset = snd_GetTick();
}

void snd_CMD_SL_GETVOICEALLOC(char* message) {
  int core = *(int*)message;
  int voice;
  int* result = (int*)gWriteBackdataOffset;
  *result = 0;
  for (voice = 0; voice < 24; voice++) {
    if (snd_GetVoiceStatus(voice + 24 * core))
      *result |= 1 << voice;
  }
}

int snd_LockVoiceAllocator(int blocking);
int snd_ExternVoiceAlloc(int group, int priority);
void snd_ExternVoiceFree(u_int voice);
void snd_UnlockVoiceAllocator(void);

void snd_CMD_SL_LOCKVALLOC(char* message) {
  *gWriteBackdataOffset = snd_LockVoiceAllocator(0);
}

void snd_CMD_SL_EXTERNVALLOC(char* message) {
  int* data = (int*)message;
  *gWriteBackdataOffset = snd_ExternVoiceAlloc(data[0], data[1]);
}

void snd_CMD_SL_EXTERNVFREE(char* message) {
  snd_ExternVoiceFree(*(u_int*)message);
}

void snd_CMD_SL_UNLOCKVALLOC(char* message) {
  snd_UnlockVoiceAllocator();
}

void snd_CMD_SL_SRAMMALLOC(char* message) {
  int disabled = CpuDisableIntr();
  *gWriteBackdataOffset = snd_SRAMMalloc(*(u_int*)message);
  if (!disabled)
    CpuEnableIntr();
}

void snd_CMD_SL_SRAMMARKUSED(char* message) {
  u_int* data = (u_int*)message;
  int disabled = CpuDisableIntr();
  *gWriteBackdataOffset = snd_SRAMMarkUsed(data[0], data[1]);
  if (!disabled)
    CpuEnableIntr();
}

void snd_CMD_SL_SRAMFREE(char* message) {
  u_int* data = (u_int*)message;
  int disabled = CpuDisableIntr();
  snd_SRAMFree(data[0], data[1]);
  if (!disabled)
    CpuEnableIntr();
}

void snd_CMD_SL_SRAMGETFREE(char* message) {
  int disabled = CpuDisableIntr();
  *gWriteBackdataOffset = snd_SRAMGetFreeMem();
  if (!disabled)
    CpuEnableIntr();
}

void snd_CMD_SL_SRAMMAXFREE(char* message) {
  int disabled = CpuDisableIntr();
  *gWriteBackdataOffset = snd_SRAMMaxFreeBlockSize();
  if (!disabled)
    CpuEnableIntr();
}

void snd_CMD_SL_EXTERNCALL(char* message) {
  int* data = (int*)message;
#if SND989_VERSION == 0x020604
  u_int id = data[0];
  int index = data[1];
  int arg1 = data[2];
  int arg2 = data[3];
  int arg3 = data[4];
  int arg4 = data[5];
  int arg5 = data[6];

  struct Extern989Handler* handler = find_handler(id);
  int result;
  if (!handler) {
    printf("snd_DoExternCall: Couldn't find extern proc 0x%x (%d)\n", id, id);
    result = 0;
  } else if (index >= handler->num_funcs) {
    printf("snd_DoExternCall: Attempt to call function out of range in extern proc id 0x%x\n", id);
    result = 0;
  } else {
    result = handler->procs[index](arg1, arg2, arg3, arg4, arg5);
  }
  *gWriteBackdataOffset = result;
#else
  *gWriteBackdataOffset =
      snd_DoExternCall(data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
#endif
}

void snd_SetReverbEx(int core, int type, int depth, int delay, int feedback);
void snd_PreAllocReverbWorkArea(int core, int type);
int snd_GetLastLoadError(void);

#if SND989_VERSION == 0x020608
void snd_CMD_SL_SETREVERBEX(char* message) {
  int* data = (int*)message;
  snd_SetReverbEx(data[0], data[1], data[2], data[3], data[4]);
}

void snd_CMD_SL_PREALLOCREVERBWORKAREA(char* message) {
  int* data = (int*)message;
  snd_PreAllocReverbWorkArea(data[0], data[1]);
}

void snd_CMD_SL_GETLASTLOADERROR(char* message) {
  *gWriteBackdataOffset = snd_GetLastLoadError();
}
#endif

struct SoundCommand {
  int arguments[7];
  int command;
};

struct SoundCommandBatch {
  int count;
  struct SoundCommand entries[1];
};

extern void (*gCommandFunc[])(char* message);

void snd_CMD_SL_COMMAND_BATCH(char* message) {
  struct SoundCommandBatch* batch = (struct SoundCommandBatch*)message;
  int i;
  for (i = 0; i < batch->count; i++) {
    gCommandFunc[batch->entries[i].command]((char*)&batch->entries[i]);
    gWriteBackdataOffset++;
  }
  gWriteBackdataOffset--;
}

void (*gCommandFunc[])(char* message) = {
    snd_CMD_SL_INIT,
    snd_CMD_SL_CLOSE,
    snd_CMD_SL_LOADBANK,
    snd_CMD_SL_LOADBANKBYLOC,
    snd_CMD_SL_LOADMMD,
    snd_CMD_SL_LOADMMDBYLOC,
    snd_CMD_SL_UNLOADBANK_A,
    snd_CMD_SL_UNLOADMIDI_A,
    snd_CMD_SL_RESOLVEBANKS,
    snd_CMD_SL_SETMASTERVOL_A,
    snd_CMD_SL_GETMASTERVOL,
    snd_CMD_SL_SETPLAYBACKMODE_A,
    snd_CMD_SL_GETPLAYBACKMODE,
    snd_CMD_SL_SETMIXERMODE_A,
    snd_CMD_SL_SETREVERBTYPE,
    snd_CMD_SL_SETREVERBDEPTH,
    snd_CMD_SL_AUTOVERB,
    snd_CMD_SL_PLAYSOUND,
    snd_CMD_SL_PLAYSOUND_A,
    snd_CMD_SL_PAUSESOUND,
    snd_CMD_SL_CONTINUESOUND,
    snd_CMD_SL_STOPSOUND,
    snd_CMD_SL_PAUSEALLSOUNDS,
    snd_CMD_SL_CONTINUEALLSOUNDS,
    snd_CMD_SL_STOPALLSOUNDS,
    snd_CMD_SL_SOUNDISSTILLPLAYING,
    snd_CMD_SL_ISSOUNDALOOPER,
    snd_CMD_SL_SETSOUNDVOLPAN_A,
    snd_CMD_SL_GETSOUNDORIGPITCH,
    snd_CMD_SL_GETSOUNDCURRPITCH,
    snd_CMD_SL_SETSOUNDPITCH_A,
    snd_CMD_SL_SETSOUNDPITCHBEND_A,
    snd_CMD_SL_SETSOUNDPITCHMOD_A,
    snd_CMD_SL_SETSOUNDPARAMS,
    snd_CMD_SL_AUTOVOL_A,
    snd_CMD_SL_AUTOPAN_A,
    snd_CMD_SL_AUTOPITCH_A,
    snd_CMD_SL_AUTOPITCHBEND_A,
    snd_CMD_SL_GETGLOBALEXCITE,
    snd_CMD_SL_SETGLOBALEXCITE_A,
    snd_CMD_SL_GETMIDIREGISTER,
    snd_CMD_SL_SETMIDIREGISTER_A,
    snd_CMD_SL_INITVAGSTREAMING,
    snd_CMD_SL_PLAYVAGSTREAMBYNAME,
    snd_CMD_SL_PLAYVAGSTREAMBYLOC,
    snd_CMD_SL_PAUSEVAGSTREAM,
    snd_CMD_SL_CONTINUEVAGSTREAM,
    snd_CMD_SL_STOPVAGSTREAM,
    snd_CMD_SL_GETVAGSTREAMQUEUECOUNT,
    snd_CMD_SL_GETVAGSTREAMLOC,
    snd_CMD_SL_GETVAGSTREAMTIMEREMAINING,
    snd_CMD_SL_GETVAGSTREAMTOTALTIME,
    snd_CMD_SL_STOPALLVAGSTREAMS,
    snd_CMD_SL_CLOSEVAGSTREAMING,
    snd_CMD_SL_STREAMSAFECHECKCDIDLE,
    snd_CMD_SL_STREAMSAFECDBREAK,
    snd_CMD_SL_STREAMSAFECDREAD,
    snd_CMD_SL_ALLOCIOPRAM,
    snd_CMD_SL_FREEIOPRAM,
    snd_CMD_SL_INITMOVIESOUND,
    snd_CMD_SL_CLOSEMOVIESOUND,
    snd_CMD_SL_RESETMOVIESOUND,
    snd_CMD_SL_STARTMOVIESOUND,
    snd_CMD_SL_PAUSEMOVIESOUND,
    snd_CMD_SL_GETTRANSSTATUS,
    snd_CMD_SL_GETSTICK,
    snd_CMD_SL_GETVOICEALLOC,
    snd_CMD_SL_LOCKVALLOC,
    snd_CMD_SL_EXTERNVALLOC,
    snd_CMD_SL_EXTERNVFREE,
    snd_CMD_SL_UNLOCKVALLOC,
    snd_CMD_SL_SRAMMALLOC,
    snd_CMD_SL_SRAMMARKUSED,
    snd_CMD_SL_SRAMFREE,
    snd_CMD_SL_SRAMGETFREE,
    snd_CMD_SL_SRAMMAXFREE,
    snd_CMD_SL_EXTERNCALL,
    snd_CMD_SL_COMMAND_BATCH,
    snd_CMD_SL_SETGROUPVOICERANGE,
#if SND989_VERSION == 0x020608
    snd_CMD_SL_ISVAGSTREAMBUFFERED,
    snd_CMD_SL_SETREVERBEX,
    snd_CMD_SL_PREALLOCREVERBWORKAREA,
    snd_CMD_SL_GETLASTLOADERROR,
#endif
};

void* snd_EEMessageParser(u_int command, void* data, int size) {
  gWriteBackdataOffset = &snd_MESSAGE_RETURN_BUFFER[1];
  gCommandFunc[command](data);
  gWriteBackdataOffset[1] = -1;
#if SND989_VERSION == 0x020604
  if (snd_MESSAGE_RETURN_BUFFER[0] != (u_int)-1) {
    printf("snd_EEMessageParser: snd_MESSAGE_RETURN_BUFFER WAS NOT 0xFFFFFFFF!!!!!\n");
    while (1) {
    }
  }
#endif
  return snd_MESSAGE_RETURN_BUFFER;
}

int snd_StartEEMessaging(void) {
  sceSifQueueData queue;
  sceSifServeData server;
  int disabled;
  snd_MESSAGE_RETURN_BUFFER[0] = -1;
  disabled = CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, 0x123456, snd_EEMessageParser, snd_MESSAGE_RECIEVE_BUFFER, 0, 0,
                    &queue);
  if (!disabled)
    CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

void* snd_EELoaderMessageParser(u_int command, void* message, int size) {
  u_int result = 0;
  int* data = (int*)message;
  switch (command) {
    case SL_LOADBANK:
#if SND989_VERSION == 0x020604
      printf("Can't load bank by name using 989snd.irx!\n");
#else
      result = (u_int)snd_BankLoad((char*)&data[1], data[0]);
#endif
      break;
    case SL_LOADBANKBYLOC:
      result = (u_int)snd_BankLoadByLoc(data[0], data[1]);
      break;
    case SL_LOADMMD:
#if SND989_VERSION == 0x020604
      printf("Can't load mmd by name using 989snd.irx!\n");
#else
      result = (u_int)snd_MMDLoad((char*)&data[1], data[0]);
#endif
      break;
    case SL_LOADMMDBYLOC:
      result = (u_int)snd_MMDLoadByLoc(data[0], data[1]);
      break;
  }
  snd_LOADER_MESSAGE_RETURN_BUFFER[0] = result;
  return snd_LOADER_MESSAGE_RETURN_BUFFER;
}

int snd_StartEELoaderMessaging(void) {
  sceSifQueueData queue;
  sceSifServeData server;
  int disabled = CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, 0x123457, snd_EELoaderMessageParser, snd_LOADER_MESSAGE_RECIEVE_BUFFER,
                    0, 0, &queue);
  if (!disabled)
    CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

#if SND989_VERSION == 0x020608
extern char* snd_MESSAGE_RECIEVE_BUFFER;
extern u_int* snd_MESSAGE_RETURN_BUFFER;
#endif
extern u_int* gWriteBackdataOffset;
int InitResidentModule(void);
void snd_DumpVersionAndInfo(void);

int _start(int argc, char** argv) {
  struct ThreadParam thread;
  int id;
  int i;
  FlushDcache();
  for (i = 1; i < argc; i++)
    snd_ParseCommandLineArg(argv[i]);
  InitResidentModule();
  snd_DumpVersionAndInfo();
  CpuEnableIntr();
  if (!sceSifCheckInit())
    sceSifInit();
  sceSifInitRpc(0);
#if SND989_VERSION == 0x020608
  if (!gDoRPC)
    return 0;
  CpuDisableIntr();
  snd_MESSAGE_RECIEVE_BUFFER = AllocSysMemory(0, 0x1010, 0);
  if (!snd_MESSAGE_RECIEVE_BUFFER) {
    snd_ShowError(5, 0, 0, 0, 0);
    goto failed;
  }
  snd_MESSAGE_RETURN_BUFFER = AllocSysMemory(0, 0xa0, 0);
  if (!snd_MESSAGE_RETURN_BUFFER) {
    snd_ShowError(6, 0, 0, 0, 0);
  failed:
    return 1;
  }
  gWriteBackdataOffset = snd_MESSAGE_RETURN_BUFFER + 1;
#endif
  thread.attr = TH_C;
  thread.entry = snd_StartEEMessaging;
  thread.initPriority = gThreadPriority_RPC;
  thread.stackSize = 0x800;
  thread.option = 0;
  id = CreateThread(&thread);
  if (id <= 0)
    goto failed;
  StartThread(id, 0);
  thread.attr = TH_C;
  thread.entry = snd_StartEELoaderMessaging;
  thread.initPriority = gThreadPriority_RPC + 1;
  thread.stackSize = 0x800;
  thread.option = 0;
  id = CreateThread(&thread);
  if (id <= 0)
    goto failed;
  StartThread(id, 0);
  return 0;
#if SND989_VERSION == 0x020604
failed:
  return 1;
#endif
}

void snd_ParseCommandLineArg(char* argument) {
  if (!strncmp(argument, "tick_priority", 13)) {
    gThreadPriority_TICK = snd_GetVal(argument + 13);
  } else if (!strncmp(argument, "rpc_priority", 12)) {
    gThreadPriority_RPC = snd_GetVal(argument + 12);
  } else if (!strncmp(argument, "stream_priority", 15)) {
    gThreadPriority_STREAM = snd_GetVal(argument + 15);
#if SND989_VERSION == 0x020608
  } else if (!strncmp(argument, "do_rpc", 6)) {
    gDoRPC = snd_GetVal(argument + 6) == 1;
#endif
  } else {
#if SND989_VERSION == 0x020604
    printf("989snd: Unrecognized command line argument -> %s\n", argument);
#else
    snd_ShowError(7, (unsigned int)argument, 0, 0, 0);
#endif
  }
}

int snd_GetVal(char* text) {
  int result;
  while (*text != '-' && (*text < '0' || *text >= ':') && *text)
    text++;
  if (*text)
    result = strtol(text, 0, 10);
  else
    result = 0;
  return result;
}

#if SND989_VERSION == 0x020604
#define SND_VERSION_PATCH 4
#else
#define SND_VERSION_PATCH 8
#endif

void snd_DumpVersionAndInfo(void) {
  printf("\n");
  printf("========================================================\n");
  printf("989snd (c)2000, 2001 Sony Computer Entertainment America\n");
  printf("by Buzz Burrowes                               v%d.%d.%d\n", 2, 6, SND_VERSION_PATCH);
  printf("(build with sce library rev %d.%d.%d)\n", 2, 3, 0);
  printf("========================================================\n");
  printf("    Thread Priorities:\n");
  printf("                         Sound Tick = %d\n", gThreadPriority_TICK);
  printf("                           Main RPC = %d\n", gThreadPriority_RPC);
  printf("                        Loading RPC = %d\n", gThreadPriority_RPC + 1);
  printf("                          Streaming = %d, %d, %d, %d\n", gThreadPriority_STREAM,
         gThreadPriority_STREAM + 1, gThreadPriority_STREAM + 2, gThreadPriority_STREAM + 3);
  printf("========================================================\n");
  printf("\n");
}

#if SND989_VERSION == 0x020608

#if SND989_VERSION == 0x020608
SndErrorDisplayFunc gErrorDisplayFunc = 0;

void snd_RegisterErrorDisplayFunc(SndErrorDisplayFunc function) {
  gErrorDisplayFunc = function;
}

void snd_ShowError(int number, u_int arg1, u_int arg2, u_int arg3, u_int arg4) {
  if (!gErrorDisplayFunc)
    printf("989snd Error: cause %d -> %u, %u, %u, %u\n", number, arg1, arg2, arg3, arg4);
  else
    gErrorDisplayFunc(number, arg1, arg2, arg3, arg4);
}
#endif

#endif
#if SND989_VERSION == 0x020604

#if SND989_VERSION == 0x020604
void snd_DumpIOPMemoryUsage(void) {
  int disabled = CpuDisableIntr();
  int biggest = QueryMaxFreeMemSize();
  int total = QueryTotalFreeMemSize();
  printf("IOP system memory:  bytes free    -> 0x%x (%d)\n", total, total);
  printf("                    biggest block -> 0x%x (%d)\n", biggest, biggest);
  if (!disabled)
    CpuEnableIntr();
}
#endif

#endif
