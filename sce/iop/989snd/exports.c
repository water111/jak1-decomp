#include "sce/iop/989snd/autopan.h"

#include "sce/iop/989snd/autopitch.h"
#include "sce/iop/989snd/autopitchbend.h"
#include "sce/iop/989snd/autoverb.h"
#include "sce/iop/989snd/autovol.h"
#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/blocksound.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/export_gp.h"
#include "sce/iop/989snd/extern_proc.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/init.h"
#include "sce/iop/989snd/loader_dma.h"
#include "sce/iop/989snd/loader_memory.h"
#include "sce/iop/989snd/loader_midi.h"
#include "sce/iop/989snd/loader_source.h"
#include "sce/iop/989snd/midi.h"
#include "sce/iop/989snd/midi_control.h"
#include "sce/iop/989snd/pantable.h"
#include "sce/iop/989snd/playback.h"
#include "sce/iop/989snd/reverb.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/989snd/stream_cd.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/989snd/timer.h"
#include "sce/iop/989snd/vag_stream.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/989snd/volume.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"

struct SoundBank* snd_BankLoad(char* name, int offset);
struct SoundBank* snd_BankLoadByLoc(int sector, int offset);

extern char snd989_stub[];
int gResLibInited = 0;

int InitResidentModule(void) {
  unsigned int caller_gp;
  int result = 0;
  SND_SAVE_GP(caller_gp);
  if (!gResLibInited) {
    if (RegisterLibraryEntries(snd989_stub)) {
      result = 1;
      goto done;
    }
    result = 0;
  }
  gResLibInited = 1;
done:
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_StartSoundSystem_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StartSoundSystem();
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_StopSoundSystem_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StopSoundSystem();
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_SystemRunning_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_SystemRunning();
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_InstallSoundTick_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_InstallSoundTick();
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_RemoveSoundTick_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_RemoveSoundTick();
  SND_RESTORE_GP(caller_gp);
}

struct SoundBank* snd_BankLoad_E(char* name, int offset) {
#if SND989_VERSION == 0x020604
  return 0;
#else
  unsigned int caller_gp;
  struct SoundBank* result;
  SND_SAVE_GP(caller_gp);
  result = snd_BankLoad(name, offset);
  SND_RESTORE_GP(caller_gp);
  return result;
#endif
}

struct SoundBank* snd_BankLoadByLoc_E(int sector, int offset) {
  unsigned int caller_gp;
  struct SoundBank* result;
  SND_SAVE_GP(caller_gp);
  result = snd_BankLoadByLoc(sector, offset);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_UnloadBank_E(struct SoundBank* bank) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_UnloadBank(bank);
  SND_RESTORE_GP(caller_gp);
}

MultiMIDIBlockHeader* snd_MMDLoad_E(char* name, int offset) {
#if SND989_VERSION == 0x020604
  return 0;
#else
  unsigned int caller_gp;
  MultiMIDIBlockHeader* result;
  SND_SAVE_GP(caller_gp);
  result = snd_MMDLoad(name, offset);
  SND_RESTORE_GP(caller_gp);
  return result;
#endif
}

MultiMIDIBlockHeader* snd_MMDLoadByLoc_E(int sector, int offset) {
  unsigned int caller_gp;
  MultiMIDIBlockHeader* result;
  SND_SAVE_GP(caller_gp);
  result = snd_MMDLoadByLoc(sector, offset);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_UnloadMMD_E(MIDIBlockHeader* midi) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_UnloadMMD(midi);
  SND_RESTORE_GP(caller_gp);
}

void snd_ResolveBankXREFS_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ResolveBankXREFS();
  SND_RESTORE_GP(caller_gp);
}

void snd_SetMasterVolume_E(int group, int volume) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetMasterVolume(group, volume);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetMasterVolume_E(int group) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetMasterVolume(group);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetPlaybackMode_E(int mode) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetPlaybackMode(mode);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetPlaybackMode_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetPlaybackMode();
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetMixerMode_E(int channel_mode, int reverb_mode) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetMixerMode(channel_mode, reverb_mode);
  SND_RESTORE_GP(caller_gp);
}

void snd_SetReverbType_E(int cores, int type) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetReverbType(cores, type);
  SND_RESTORE_GP(caller_gp);
}

void snd_SetReverbDepth_E(int cores, int left, int right) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetReverbDepth(cores, (short)left, (short)right);
  SND_RESTORE_GP(caller_gp);
}

void snd_AutoReverb_E(int cores, int depth, int delta_time, int channel_flags) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_AutoReverb(cores, (short)depth, (short)delta_time, channel_flags);
  SND_RESTORE_GP(caller_gp);
}

u_int snd_PlaySoundVolPanPMPB_E(struct SoundBank* bank,
                                int index,
                                int volume,
                                int pan,
                                int pitch_modifier,
                                int bend) {
  unsigned int caller_gp;
  u_int result;
  // TODO hack
  __asm__("move %0,$28\n\tla $28,_gp"
          : "=r"(caller_gp)
          : "r"(pitch_modifier), "r"(bend)
          : "$28", "memory");
  result = snd_PlaySoundVolPanPMPB(bank, index, volume, pan, (short)pitch_modifier, (short)bend);
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_SoundIsStillPlaying_E(u_int handle) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_SoundIsStillPlaying(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_IsSoundALooper_E(struct SoundBank* bank, int index) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_IsSoundALooper(bank, index);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_PauseSound_E(u_int handle) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_PauseSound(handle);
  SND_RESTORE_GP(caller_gp);
}

void snd_ContinueSound_E(u_int handle) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ContinueSound(handle);
  SND_RESTORE_GP(caller_gp);
}

void snd_StopSound_E(u_int handle) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_StopSound(handle);
  SND_RESTORE_GP(caller_gp);
}

void snd_StopAllSoundsInBank_E(struct SoundBank* bank, int silence) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_StopAllSoundsInBank(bank, silence);
  SND_RESTORE_GP(caller_gp);
}

void snd_StopAllSounds_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_StopAllSounds();
  SND_RESTORE_GP(caller_gp);
}

void snd_SetSoundVolPan_E(u_int handle, int volume, int pan) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetSoundVolPan(handle, volume, pan);
  SND_RESTORE_GP(caller_gp);
}

void snd_SetSoundPitch_E(u_int handle, int pitch) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetSoundPitch(handle, pitch);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetSoundOriginalPitch_E(struct SoundBank* bank, int index) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetSoundOriginalPitch(bank, index);
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_GetSoundCurrentPitch_E(u_int handle) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetSoundCurrentPitch(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetSoundPitchBend_E(u_int handle, int bend) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetSoundPitchBend(handle, (short)bend);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetSoundPitchBend_E(u_int handle) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetSoundPitchBend(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetSoundPitchModifier_E(u_int handle, int modifier) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetSoundPitchModifier(handle, (short)modifier);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetSoundPitchModifier_E(u_int handle) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetSoundPitchModifier(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_AutoVol_E(u_int handle, int volume, int ticks, int delta_from) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_AutoVol(handle, volume, ticks, delta_from);
  SND_RESTORE_GP(caller_gp);
}

void snd_AutoPan_E(u_int handle, int pan, int direction, int ticks, int delta_from) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_AutoPan(handle, pan, direction, ticks, delta_from);
  SND_RESTORE_GP(caller_gp);
}

void snd_AutoPitch_E(u_int handle, int pitch, int ticks, int delta_from) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_AutoPitch(handle, pitch, ticks, delta_from);
  SND_RESTORE_GP(caller_gp);
}

void snd_AutoPitchBend_E(u_int handle, int pitch, int ticks, int delta_from) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_AutoPitchBend(handle, pitch, ticks, delta_from);
  SND_RESTORE_GP(caller_gp);
}

void snd_SetMIDIRegister_E(u_int handle, int reg, int value) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetMIDIRegister(handle, reg, (short)value);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetMIDIRegister_E(u_int handle, int reg) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetMIDIRegister(handle, reg);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetGlobalExcite_E(int value) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetGlobalExcite(value);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetGlobalExcite_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetGlobalExcite();
  SND_RESTORE_GP(caller_gp);
  return result;
}

#if SND989_VERSION == 0x020604
void snd_InitVAGStreaming_E(int channels, int buffer_size, int mode) {
#else
void snd_InitVAGStreaming_E(int channels, int buffer_size, int mode, int enable_read_buffer) {
#endif
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
#if SND989_VERSION == 0x020604
  snd_InitVAGStreaming(channels, buffer_size, mode);
#else
  snd_InitVAGStreaming(channels, buffer_size, mode, enable_read_buffer);
#endif
  SND_RESTORE_GP(caller_gp);
}

u_int snd_PlayVAGStreamByLoc_E(u_int first_sector,
                               u_int second_sector,
                               int first_offset,
                               int second_offset,
                               int volume,
                               int pan,
                               int volume_group,
                               int queue,
                               u_int flags) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_PlayVAGStreamByLoc(first_sector, second_sector, first_offset, second_offset, volume,
                                  pan, volume_group, queue, flags);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_PauseVAGStream_E(u_int handle) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_PauseVAGStream(handle);
  SND_RESTORE_GP(caller_gp);
}

void snd_PauseAllStreams_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_PauseAllStreams();
  SND_RESTORE_GP(caller_gp);
}

void snd_ContinueVAGStream_E(u_int handle) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ContinueVAGStream(handle);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetVAGStreamQueueCount_E(u_int handle) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetVAGStreamQueueCount(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_GetVAGStreamLoc_E(u_int handle) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetVAGStreamLoc(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_GetVAGStreamTimeRemaining_E(u_int handle) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetVAGStreamTimeRemaining(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_GetVAGStreamTotalTime_E(u_int handle) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetVAGStreamTotalTime(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetVAGStreamVolPan_E(u_int handle, int volume, int pan) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetVAGStreamVolPan(handle, volume, pan);
  SND_RESTORE_GP(caller_gp);
}

void snd_StopVAGStream_E(u_int handle) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_StopVAGStream(handle);
  SND_RESTORE_GP(caller_gp);
}

void snd_CloseVAGStreaming_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_CloseVAGStreaming();
  SND_RESTORE_GP(caller_gp);
}

void snd_StopAllStreams_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_StopAllStreams();
  SND_RESTORE_GP(caller_gp);
}

int snd_StreamSafeCdRead_E(u_int sector, u_int count, void* destination) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StreamSafeCdRead(sector, count, destination);
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_StreamSafeCdReadEEm_E(u_int sector, u_int count, void* destination) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StreamSafeCdReadEEm(sector, count, destination);
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_StreamSafeCdSync_E(int mode) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StreamSafeCdSync(mode);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_StreamSafeCdBreak_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_StreamSafeCdBreak();
  SND_RESTORE_GP(caller_gp);
}

int snd_StreamSafeCdGetError_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StreamSafeCdGetError();
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_PauseAllSoundsInGroup_E(u_int groups) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_PauseAllSoundsInGroup(groups);
  SND_RESTORE_GP(caller_gp);
}

void snd_ContinueAllSoundsInGroup_E(u_int groups) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ContinueAllSoundsInGroup(groups);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetVoiceStatus_E(int voice) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetVoiceStatus(voice);
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_GetTick_E(void) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetTick();
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_LockVoiceAllocator_E(int blocking) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_LockVoiceAllocator(blocking);
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_ExternVoiceAlloc_E(int group, int priority) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_ExternVoiceAlloc(group, priority);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_ExternVoiceFree_E(int voice) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ExternVoiceFree(voice);
  SND_RESTORE_GP(caller_gp);
}

void snd_UnlockVoiceAllocator_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_UnlockVoiceAllocator();
  SND_RESTORE_GP(caller_gp);
}

u_int snd_SRAMMalloc_E(u_int size) {
  unsigned int caller_gp;
  int disabled;
  u_int result;
  SND_SAVE_GP(caller_gp);
  disabled = CpuDisableIntr();
  result = snd_SRAMMalloc(size);
  if (!disabled)
    CpuEnableIntr();
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_SRAMMarkUsed_E(u_int location, u_int size) {
  unsigned int caller_gp;
  int disabled;
  u_int result;
  SND_SAVE_GP(caller_gp);
  disabled = CpuDisableIntr();
  result = snd_SRAMMarkUsed(location, size);
  if (!disabled)
    CpuEnableIntr();
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SRAMFree_E(u_int location, u_int size) {
  unsigned int caller_gp;
  int disabled;
  SND_SAVE_GP(caller_gp);
  disabled = CpuDisableIntr();
  snd_SRAMFree(location, size);
  if (!disabled)
    CpuEnableIntr();
  SND_RESTORE_GP(caller_gp);
}

int snd_GetFreeSPUDMA_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetFreeSPUDMA();
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_FreeSPUDMA_E(int channel) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_FreeSPUDMA(channel);
  SND_RESTORE_GP(caller_gp);
}

int snd_GetDopplerPitchMod_E(int approaching_mph) {
  unsigned int caller_gp;
  int result;
  // TODO hack
  __asm__ __volatile__("move %0,$28\n\tla $28,_gp" : "=r"(caller_gp) : : "$28", "memory");
  result = (approaching_mph * 1524) / 741;
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_MIDINoteOn_E(struct MIDIHandler* handler) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_MIDINoteOn(handler);
  SND_RESTORE_GP(caller_gp);
}

void snd_MIDINoteOff_E(struct MIDIHandler* handler) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_MIDINoteOff(handler);
  SND_RESTORE_GP(caller_gp);
}

void snd_PitchBend_E(struct MIDIHandler* handler) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_PitchBend(handler);
  SND_RESTORE_GP(caller_gp);
}

short snd_MIDITo360Pan_E(int pan) {
  unsigned int caller_gp;
  short result;
  SND_SAVE_GP(caller_gp);
  result = snd_MIDITo360Pan((signed char)pan);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_ReleaseDamper_E(struct MIDIHandler* handler) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ReleaseDamper(handler);
  SND_RESTORE_GP(caller_gp);
}

void snd_ResetControllers_E(struct MIDIHandler* handler) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_ResetControllers(handler);
  SND_RESTORE_GP(caller_gp);
}

int snd_StreamSafeCheckCDIdle_E(int block) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_StreamSafeCheckCDIdle(block);
  SND_RESTORE_GP(caller_gp);
  return result;
}

u_int snd_PlaySFXGrain_E(struct SFXBlock2* block, int sound_index, int grain_index) {
  unsigned int caller_gp;
  u_int result;
  SND_SAVE_GP(caller_gp);
  result = snd_PlaySFXGrain(block, sound_index, grain_index);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_KeyOnVoiceRaw_E(int core, int voice) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_KeyOnVoiceRaw(core, voice);
  SND_RESTORE_GP(caller_gp);
}

void snd_KeyOffVoiceRaw_E(int core, int voice) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_KeyOffVoiceRaw(core, voice);
  SND_RESTORE_GP(caller_gp);
}

void snd_SetPanTable_E(VolPair* table) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetPanTable(table);
  SND_RESTORE_GP(caller_gp);
}

int snd_SRAMGetFreeMem_E(void) {
  unsigned int caller_gp;
  int disabled;
  int result;
  SND_SAVE_GP(caller_gp);
  disabled = CpuDisableIntr();
  result = snd_SRAMGetFreeMem();
  if (!disabled)
    CpuEnableIntr();
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_SRAMMaxFreeBlockSize_E(void) {
  unsigned int caller_gp;
  int disabled;
  int result;
  SND_SAVE_GP(caller_gp);
  disabled = CpuDisableIntr();
  result = snd_SRAMMaxFreeBlockSize();
  if (!disabled)
    CpuEnableIntr();
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_RegisterExternProcHandler_E(struct Extern989Handler* handler) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_RegisterExternProcHandler(handler);
  SND_RESTORE_GP(caller_gp);
  return result;
}

int snd_UnregisterExternProcHandler_E(struct Extern989Handler* handler) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_UnregisterExternProcHandler(handler);
  SND_RESTORE_GP(caller_gp);
  return result;
}

struct Extern989Handler* snd_FindExternProcHandler_E(u_int id) {
  unsigned int caller_gp;
  struct Extern989Handler* result;
  SND_SAVE_GP(caller_gp);
  result = snd_FindExternProcHandler(id);
  SND_RESTORE_GP(caller_gp);
  return result;
}

void snd_SetGroupVoiceRange_E(int group, int first, int last) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetGroupVoiceRange(group, first, last);
  SND_RESTORE_GP(caller_gp);
}

void snd_RegisterIOPMemAllocator_E(void* (*allocate)(int, int, int*), void (*release)(void*)) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_RegisterIOPMemAllocator(allocate, release);
  SND_RESTORE_GP(caller_gp);
}

#if SND989_VERSION == 0x020608
#if SND989_VERSION == 0x020608
int snd_FileRead_E(u_int sector, int offset, int size, void* destination) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_FileRead(sector, offset, size, destination);
  SND_RESTORE_GP(caller_gp);
  return result;
}
#endif

#if SND989_VERSION == 0x020608
int snd_IsVAGStreamBuffered_E(u_int handle) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_IsVAGStreamBuffered(handle);
  SND_RESTORE_GP(caller_gp);
  return result;
}
#endif

#if SND989_VERSION == 0x020608
void snd_RegisterErrorDisplayFunc_E(SndErrorDisplayFunc function) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_RegisterErrorDisplayFunc(function);
  SND_RESTORE_GP(caller_gp);
}
#endif

#if SND989_VERSION == 0x020608
void snd_SetReverbEx_E(int cores, int type, int depth, int delay, int feedback) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SetReverbEx(cores, type, depth, delay, feedback);
  SND_RESTORE_GP(caller_gp);
}
#endif

#if SND989_VERSION == 0x020608
void snd_PreAllocReverbWorkArea_E(int cores, int type) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_PreAllocReverbWorkArea(cores, type);
  SND_RESTORE_GP(caller_gp);
}
#endif

#if SND989_VERSION == 0x020608
int snd_GetLastLoadError_E(void) {
  unsigned int caller_gp;
  int result;
  SND_SAVE_GP(caller_gp);
  result = snd_GetLastLoadError();
  SND_RESTORE_GP(caller_gp);
  return result;
}
#endif

#if SND989_VERSION == 0x020608
void snd_SRAMDumpFreeBlocks_E(void) {
  unsigned int caller_gp;
  SND_SAVE_GP(caller_gp);
  snd_SRAMDumpFreeBlocks();
  SND_RESTORE_GP(caller_gp);
}
#endif
#endif
