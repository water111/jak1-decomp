#include "overlord/iso.h"
#include "overlord/iso_queue.h"
#include "overlord/isocommon.h"
#include "overlord/srpc.h"
#include "sce/iop/thread.h"
#include "sce/iop/string.h"

#ifdef ISO_API_EMBEDDED
#line 1076 "iso.c"
#endif

// Blocking load file to IOP. This thread sleeps, allowing others to run while the load
// happens.
int LoadISOFileToIOP(FileRecord* file, void* dest, int length) {
  // Create a command on this thread's stack.
  IsoCommandLoadSingle cmd;
  cmd.header.cmd_id = LOAD_TO_IOP_CMD_ID;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  cmd.file_record = file;
  cmd.dest_addr = (unsigned char*)dest;
  cmd.length = length;

  // Send to iso thread
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  // Wait for thread to be woken by iso thread on completion.
  SleepThread();
  return !cmd.header.status ? cmd.length_to_copy : 0;
}

// Blocking load file to EE. This thread sleeps, allowing others to run while
// the load happens.
int LoadISOFileToEE(FileRecord* file, unsigned int dest, int length) {
  IsoCommandLoadSingle cmd;
  cmd.header.cmd_id = LOAD_TO_EE_CMD_ID;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  cmd.file_record = file;
  cmd.dest_addr = (unsigned char*)dest;
  cmd.length = length;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
  return !cmd.header.status ? cmd.length_to_copy : 0;
}

// Blocking load file to EE. This thread sleeps, allowing others to run while
// the load happens.
int LoadISOFileChunkToEE(FileRecord* file, unsigned int dest, int length, unsigned int offset) {
  IsoCommandLoadSingle cmd;
  cmd.header.cmd_id = LOAD_TO_EE_OFFSET_CMD_ID;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  cmd.file_record = file;
  cmd.dest_addr = (unsigned char*)dest;
  cmd.length = length;
  cmd.offset = offset;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
  return !cmd.header.status ? cmd.length_to_copy : 0;
}

// Blocking load sound bank.
void LoadSoundBank(const char* name, SoundBank* bank) {
  SoundBankLoadCommand cmd;
  cmd.header.cmd_id = LOAD_SOUND_BANK;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  strcpy(cmd.bank_name, name);
  cmd.bank = bank;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
  return;
}

// Blocking load music, and apply tweaks.
void LoadMusic(const char* name, void** bank) {
  MusicLoadCommand cmd;
#if REVISION > 20011002
  int i;
#endif
  cmd.header.cmd_id = LOAD_MUSIC;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  strcpy(cmd.music_name, name);
  cmd.music_handle = bank;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#if REVISION > 20011002
  for (i = 0; i < gMusicTweakInfo.count; i++) {
    if (!strcmp(gMusicTweakInfo.tweak[i].name, name)) {
      gMusicTweak = gMusicTweakInfo.tweak[i].volume;
      return;
    }
  }
  gMusicTweak = 0x80;
#endif
  return;
}

#if REVISION > 20010903
// The actual implementation of this ends up
// making the iso thread spin forever, unclear why you'd want this.
void ShutdownFilingSystem(void) {
  IsoMessage cmd;
  cmd.cmd_id = SHUTDOWN_ISO_CMD_ID;
  cmd.messagebox_to_reply = 0;
  cmd.thread_id = GetThreadId();
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
  return;
}

#endif

// The situation with VAG commands is somewhat complicated.
// The original design is that calling these functions waits until the iso thread loop
// has seen the command and sets up its own internal VagCommand which lasts longer.
// However, a later revision changed this logic and makes these vag functions use a pool of
// commands and returns immediately, leaving two layers of longer-lived VAG commands: one in the
// pool, and one owned by iso thread. Very confusing.

// Only a single VAG stream is effectively handled by the iso thread at a time, however multiple
// commands can be in the queue and are handled based on priority. This logic isn't amazing,
// and the game itself has a better implementation of this.

// Queue a VAG stream to be pre-loaded.
void QueueVAGStream(FileRecord* file,
#if REVISION > 20010903
                    VagDirEntry* vag,
#endif
                    unsigned int id, unsigned int priority) {
#if REVISION > 20011002
  if (vag) {
#endif
#if REVISION < 20011031
    VagCommand cmd;
    cmd.header.cmd_id = QUEUE_VAG_STREAM;
    cmd.header.messagebox_to_reply = 0;
    cmd.header.thread_id = GetThreadId();
    cmd.file_record = file;
#if REVISION > 20010903
    cmd.vag_entry = vag;
#endif
    cmd.sound_id = id;
    cmd.priority = priority;
#if REVISION > 20011002
    cmd.is_positioned = 0;
#endif
    SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
    SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = QUEUE_VAG_STREAM;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  cmd->file_record = file;
  cmd->vag_entry = vag;
  cmd->sound_id = id;
  cmd->priority = priority;
  cmd->is_positioned = 0;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
#if REVISION > 20011002
  }
#endif
}

// Start playing a vag stream. Queued files start immediately,
// otherwise loads prior to playing.
void PlayVAGStream(FileRecord* file,
#if REVISION > 20010903
                   VagDirEntry* vag,
#endif
                   unsigned int id,
                   int volume,
                   unsigned int priority
#if REVISION > 20011002
                   ,
                   Vec3w* position
#endif
) {
#if REVISION < 20011031
  VagCommand cmd;
  cmd.header.cmd_id = PLAY_VAG_STREAM;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  cmd.file_record = file;
#if REVISION > 20010903
  cmd.vag_entry = vag;
#endif
  cmd.sound_id = id;
  cmd.volume = volume;
  cmd.priority = priority;
#if REVISION > 20011002
  if (position) {
    cmd.position.x = ((int*)position)[0];
    cmd.position.y = ((int*)position)[1];
    cmd.position.z = ((int*)position)[2];
    cmd.is_positioned = 1;
  } else
    cmd.is_positioned = 0;
#endif
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = PLAY_VAG_STREAM;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  cmd->file_record = file;
  cmd->vag_entry = vag;
  cmd->sound_id = id;
  cmd->volume = volume;
  cmd->priority = priority;
  if (position) {
    cmd->position.x = ((int*)position)[0];
    cmd->position.y = ((int*)position)[1];
    cmd->position.z = ((int*)position)[2];
    cmd->is_positioned = 1;
  } else
    cmd->is_positioned = 0;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
}

// Adjust the volume of the currently playing VAG.
void SetVAGStreamVolume(int volume) {
#if REVISION < 20011031
  VagCommand cmd;
  cmd.header.cmd_id = SET_VAG_VOLUME;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  cmd.volume = volume;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = SET_VAG_VOLUME;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  cmd->volume = volume;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
}

#if REVISION > 20010903
// Adjust the persistent global "dialog" volume multiplier that applies to all VAGs.
void SetDialogVolume(int volume) {
#if REVISION < 20011031
  VagCommand cmd;
  cmd.header.cmd_id = SET_DIALOG_VOLUME;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  cmd.volume = volume;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = SET_DIALOG_VOLUME;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  cmd->volume = volume;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
}

#endif

// Stop VAG and unload it.
#if REVISION <= 20010903
void StopVAGStream(FileRecord* file, unsigned int priority) {
#else
void StopVAGStream(VagDirEntry* vag, unsigned int priority) {
#endif
#if REVISION < 20011031
  VagCommand cmd;
  cmd.header.cmd_id = STOP_VAG_STREAM;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
#if REVISION <= 20010903
  cmd.file_record = file;
#else
  cmd.vag_entry = vag;
#endif
  cmd.priority = priority;
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = STOP_VAG_STREAM;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  cmd->vag_entry = vag;
  cmd->priority = priority;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
}

// Pause VAG playback, can be unpaused later.
void PauseVAGStream(void) {
#if REVISION < 20011031
  VagCommand cmd;
  cmd.header.cmd_id = PAUSE_VAG_STREAM;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = PAUSE_VAG_STREAM;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
}

// Unpause VAG playback paused by PauseVAGStream.
void UnpauseVAGStream(void) {
#if REVISION < 20011031
  VagCommand cmd;
  cmd.header.cmd_id = CONTINUE_VAG_STREAM;
  cmd.header.messagebox_to_reply = 0;
  cmd.header.thread_id = GetThreadId();
  SendMbx(iso_mbx, (struct MsgPacket*)&cmd);
  SleepThread();
#else
  VagCommand* cmd = GetVAGCommand();
  cmd->header.cmd_id = CONTINUE_VAG_STREAM;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
  SendMbx(iso_mbx, (struct MsgPacket*)cmd);
#endif
}
