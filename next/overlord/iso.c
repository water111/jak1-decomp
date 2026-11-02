#include "overlord/iso.h"
#include "989snd/989snd.h"
#include "common/overlord_rpc.h"
#include "overlord/dma.h"
#include "overlord/iso_api.h"
#include "overlord/iso_cd.h"
#include "overlord/iso_queue.h"
#include "overlord/isocommon.h"
#include "overlord/ssound.h"
#include "overlord/stream.h"
#include "sce/iop/intrman.h"
#include "sce/iop/kerror.h"
#include "sce/iop/libsd.h"
#include "sce/iop/loadcore.h"
#include "sce/iop/sdmacro.h"
#include "sce/iop/sifrpc.h"
#include "sce/iop/thread.h"

// This file contains the main disc I/O logic,
// and some of the code that handles data off the disc.
// It is... a complete mess.

// iso_mbx receives requests from clients to
// read data off the disc.
#if REVISION <= 20011019
static int iso_mbx = -1;
#else
int iso_mbx = -1;
#endif

// iso_thread processes iso_mbx messages from within OVERLORD
static int iso_thread = -1;

// DGO RPC runs on a separate thread, receiving commands from the EE to load,
// sends messages to iso_thread through iso_mbx, and receives response in
// dgo_mbx.
static int dgo_mbx = -1;
static int dgo_thread = -1;

// RPC handler threads for streaming file and streaming audio RPCs.
static int str_thread = -1;
static int play_thread = -1;

// misc sync mbx, used for startup and dgo state machine.
static int sync_mbx = -1;

// interface to the disc drive.
IsoFs* isofs = 0;

// The VAG clock is tied to audio playback progress.
// If audio playback fails, use this fake clock.
int gFakeVAGClockRunning = 0;
int gFakeVAGClockPaused = 0;
int gPlaying = 0;
int gRealVAGClockRunning = 0;

// The sound ID of the VAG command playing.
#if REVISION <= 20011002
int gVAG_Id = -1;
#else
int gVAG_Id = 1;
#endif

// TODO
int gDialogVolume = 1024;

#if REVISION > 20011002
// The VAG command in progress.
// This is owned by the ISO thread, it's not a command from a user.
// The ISO thread keeps its own longer-lived VAG commands to track queueing and
// playback.
VagCommand* gVAGCMD = 0;
static unsigned char VAG_SilentLoop[48] = {0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif

// VAG clocks published to the user so they can see audio playback progress.
// In units of 1/1024 of a second.
int gRealVAGClock;
int gFakeVAGClock;

// Current VAG's sample rate.
int gSampleRate;
#if REVISION > 20011002
// Which of the two SPU buffers VAG playback was most recently observed in.
int gLastVAGHalf;
#endif

// Position in SPU gStreamSRAM buffer (both halves), in bytes.
unsigned int gPlayPos;
// Total number of ADPCM bytes consumed.
unsigned int gRealVAGClockS;

#if REVISION > 20011002
// adjustments to music volume.
MusicTweaks gMusicTweakInfo;
#endif

// location of audio files in WAD files.
VagDir gVagDir;

// result of iso thread initialization, set by iso thread.
static unsigned int iso_init_flag;

// TODO
static DgoRpcCommand sRPCBuf;

#if REVISION >= 20011121 && !defined(REGION_KR)
// bug fix -- they put a MsgPacket on a stack, sent it, and returned,
// allowing IOP kernel to read/write to some random stack address.
static struct MsgPacket _not_on_stack_sync;
#endif

static DgoCommand scmd;

// Initialize the drive's driver, providing it with
// some temporary buffer space. This reads the filesystem
// metadata and builds tables for faster file lookup.
// Once complete, signal to InitISOFS this is done with sync_mbx.
void InitDriver(unsigned char* buffer) {
  int result = isofs->init(buffer);
#if REVISION < 20011121 || defined(REGION_KR)
  struct MsgPacket packet;
#endif
  if (!result) {
    iso_init_flag = 0;
  }

  // BUG FIX after NTSC -- avoid sending an address of the stack, which is
  // invalid after this function returns.
#if REVISION >= 20011121 && !defined(REGION_KR)
  SendMbx(sync_mbx, &_not_on_stack_sync);
#else
  SendMbx(sync_mbx, &packet);
#endif
}

// Check to see if there's a message, if so, pop it.
// Note that anything other than NOMSG counts as ready
static int LookMbx(int mbx) {
  struct MsgPacket* packet;
  int result = PollMbx(&packet, mbx);
  if (result != KE_MBOX_NOMSG) {
    return 1;
  }
  return 0;
}

// Wait on a message using DelayThread(100)'s. Not the most efficient,
// since it has to wake the thread and run logic here.
static void WaitMbx(int mbx) {
  while (!LookMbx(mbx)) {
    DelayThread(100);
  }
}

static unsigned int ISOThread(void);
static unsigned int DGOThread(void);

// Initialize the iso system and its threads.
// This includes the DGO, STR, PLAY RPCs as well.
// return 1 on success.
unsigned int InitISOFS(const char* fs_mode, const char* loading_screen) {
  struct ThreadParam thread;
  struct MbxParam mbx;
  FileRecord* file;
  isofs = &iso_cd;

  // threads will set this to 0 on failure
  iso_init_flag = 1;

  // set up SPU memory with a silent loop.
  // If streaming audio fails to load the next chunk in time,
  // it jumps into this trap and plays silence instead of junk.
#if REVISION > 20011002
  while (!DMA_SendToSPUAndSync(VAG_SilentLoop, 48, gTrapSRAM)) {
    DelayThread(1000);
  }
#endif

  // Create messageboxes
  mbx.attr = 0;
  mbx.option = 0;
  iso_mbx = CreateMbx(&mbx);
  if (iso_mbx <= 0)
    return 1;

  mbx.attr = 0;
  mbx.option = 0;
  dgo_mbx = CreateMbx(&mbx);
  if (dgo_mbx <= 0)
    return 1;

  mbx.attr = 0;
  mbx.option = 0;
  sync_mbx = CreateMbx(&mbx);
  if (sync_mbx <= 0)
    return 1;

  // Create threads
  thread.attr = TH_C;
  thread.entry = ISOThread;
  thread.initPriority = 100;
  thread.stackSize = 0x1000;
  thread.option = 0;
  iso_thread = CreateThread(&thread);
  if (iso_thread <= 0)
    return 1;

  thread.attr = TH_C;
  thread.entry = DGOThread;
  thread.initPriority = 98;
  thread.stackSize = 0x800;
  thread.option = 0;
  dgo_thread = CreateThread(&thread);
  if (dgo_thread <= 0)
    return 1;

  thread.attr = TH_C;
  thread.entry = STRThread;
  thread.initPriority = 97;
  thread.stackSize = 0x800;
  thread.option = 0;
  str_thread = CreateThread(&thread);
  if (str_thread <= 0)
    return 1;

  thread.attr = TH_C;
  thread.entry = PLAYThread;
  thread.initPriority = 97;
  thread.stackSize = 0x800;
  thread.option = 0;
  play_thread = CreateThread(&thread);
  if (play_thread <= 0)
    return 1;

  StartThread(iso_thread, 0);
  StartThread(dgo_thread, 0);
  StartThread(str_thread, 0);
  StartThread(play_thread, 0);
  WaitMbx(sync_mbx);

  // Load the VAGDIR file containing audio file locations.
  file = FindISOFile("VAGDIR.AYB");
  if (!file) {
    // oops, no vagdir...
  } else {
    LoadISOFileToIOP(file, &gVagDir, sizeof(gVagDir));
  }

  // Load the splash screen (specified as argument to OVERLORD by EE).
  file = FindISOFile(loading_screen);
  if (file) {
    LoadISOFileToEE(file, SPLASH_SCREEN_ADDR, 0x800000);
  }
  return iso_init_flag;
}

// Look up file by name (normal name), return NULL if not found
FileRecord* FindISOFile(const char* name) {
  return isofs->find(name);
}

// Get the length of a file.
// This is likely exposed as a function so development modes can
// dynamically update a file size when it's replaced with a newer version.
int GetISOFileLength(FileRecord* file) {
  return isofs->get_length(file);
}

// Look up a VAG file's entry by name (treated as 8-bytes)
// TODO: cleanup?
VagDirEntry* FindVAGFile(const char* name) {
  int i;
  unsigned int first = *(unsigned int*)name;
  unsigned int second = *(unsigned int*)(name + 4);
  VagDirEntry* entry = gVagDir.vag;
  for (i = 0; i < gVagDir.count; i++, entry++) {
    unsigned int a = *(unsigned int*)entry->name;
    unsigned int b = *(unsigned int*)(entry->name + 4);
    if (first == a && second == b)
      return entry;
  }
  return 0;
}

static unsigned int NullCallback(IsoMessage*, IsoBufferHeader*);
static unsigned int RunDGOStateMachine(IsoMessage*, IsoBufferHeader*);
static unsigned int CopyDataToEE(IsoMessage*, IsoBufferHeader*);
static unsigned int CopyDataToIOP(IsoMessage*, IsoBufferHeader*);
static void StopVAG(VagCommand*);
static void InitVAGCmd(VagCommand*, unsigned int);
static void PauseVAG(VagCommand*);
static void UnpauseVAG(VagCommand*);
#if REVISION <= 20011002
static void SetVAGVol(VagCommand*);
#endif
static int CheckVAGStreamProgress(VagCommand*);
static void InitVAGCmd(VagCommand*, unsigned int);
static unsigned int ProcessVAGData(IsoMessage*, IsoBufferHeader*);

// This function receives commands in the iso_mbx and overlaps disc
// reading with buffer callbacks.
static unsigned int ISOThread(void) {
  IsoBufferHeader* buffer = 0;
  IsoMessage* pending = 0;

  // set to our local persistent vag command when it's in progress.
  VagCommand* vag = 0;
  int paused = 0;
  int resume = 0;

  // storage for command referenced by vag above.
  static VagCommand s_vag_cmd;

  // Before starting, let driver initialize (read file locations for CD)
  InitBuffers();
  buffer = AllocateBuffer(BUFFER_PAGE_SIZE);
  InitDriver((unsigned char*)(buffer + 1));
  FreeBuffer(buffer);

  // Loop to process incoming commands, kick off reads, and run callbacks on
  // buffered data.
  for (;;) {
    struct MsgPacket* packet;
    int result;
#if REVISION <= 20011002
    DelayThread(100);
#endif

    // Check command messagebox
    result = PollMbx(&packet, iso_mbx);
    if (result == 0) {
      // This very long block will handle things like opening a file and fast
      // metadata work, modify the message as needed, and then add it to the
      // queue for later scheduling of drive use.
      IsoMessage* msg = (IsoMessage*)packet;
      msg->callback_buffer = 0;
      msg->ready_for_data = 1;
      msg->callback_function = NullCallback;
      msg->fd = 0;

      // Streaming file load to EE or IOP.
      if (msg->cmd_id == LOAD_TO_EE_CMD_ID || msg->cmd_id == LOAD_TO_IOP_CMD_ID ||
          msg->cmd_id == LOAD_TO_EE_OFFSET_CMD_ID) {
        // Add message to queue - if queuing fails, QueueMessage returns failure
        // to caller.
        if (QueueMessage(msg, 2, "LoadSingle")) {
          // Open the file
          IsoCommandLoadSingle* cmd = (IsoCommandLoadSingle*)msg;
          if (msg->cmd_id == LOAD_TO_EE_OFFSET_CMD_ID) {
            msg->fd = isofs->open(cmd->file_record, cmd->offset);
          } else {
            msg->fd = isofs->open(cmd->file_record, -1);
          }

          if (!msg->fd) {
            // file doesn't exist, unqueue and return to caller
            msg->status = CMD_STATUS_FAILED_TO_OPEN;
            UnqueueMessage(msg);
            ReturnMessage(msg);
          } else {
            // Set up pointers for streaming data back
            cmd->dst_ptr = cmd->dest_addr;
            cmd->bytes_done = 0;
            cmd->length_to_copy = isofs->get_length(cmd->file_record);
            if (!cmd->length_to_copy) {
              cmd->length_to_copy = cmd->length;
            } else if (cmd->length_to_copy > cmd->length) {
              cmd->length_to_copy = cmd->length;
            }
            msg->status = CMD_STATUS_IN_PROGRESS;

            // install copying callback
            switch (msg->cmd_id) {
              case LOAD_TO_EE_CMD_ID:
              case LOAD_TO_EE_OFFSET_CMD_ID:
                cmd->header.callback_function = CopyDataToEE;
                break;
              case LOAD_TO_IOP_CMD_ID:
                cmd->header.callback_function = CopyDataToIOP;
                break;
            }
          }
        }
      } else if (msg->cmd_id == LOAD_DGO_CMD_ID) {
        // DGO package load. This is an optimized loader for game packages.
        if (QueueMessage(msg, 0, "LoadDGO")) {
          DgoCommand* cmd = (DgoCommand*)msg;
          msg->fd = isofs->open(cmd->file_record, -1);
          if (!msg->fd) {
            msg->status = CMD_STATUS_FAILED_TO_OPEN;
            UnqueueMessage(msg);
            ReturnMessage(msg);
          } else {
            // All logic is handled in the DGO State machine
            msg->status = CMD_STATUS_IN_PROGRESS;
            cmd->state = DGO_INIT;
            cmd->header.callback_function = RunDGOStateMachine;
          }
        }
      } else if (msg->cmd_id == LOAD_SOUND_BANK) {
        // Unlike other commands, this one is completed here, including blocking
        // on the CD drive to read data... For this reason it's not attempted
        // when streaming audio, since it probably blocks for too long.
        if (vag && !vag->paused) {
          // rejected. caller can check status to see.
          SendMbx(iso_mbx, (struct MsgPacket*)msg);
        } else {
          // Temporary buffer is needed for soundbank loader
          IsoBufferHeader* temp = TryAllocateBuffer(BUFFER_PAGE_SIZE);
          if (!temp) {
            // rejected, no buffer available.
            SendMbx(iso_mbx, (struct MsgPacket*)msg);
          } else {
            SoundBankLoadCommand* cmd = (SoundBankLoadCommand*)msg;
            // run the load, with the temporary buffer installed, then return.
            SetBufferMem(temp + 1, BUFFER_PAGE_SIZE);
            msg->status = isofs->load_sound_bank(cmd->bank_name, cmd->bank);
            ReleaseBufferMem();
            FreeBuffer(temp);
            ReturnMessage(msg);
          }
        }
      } else if (msg->cmd_id == LOAD_MUSIC) {
        // Like sound bank loads, this command is entirely completed here,
        // including blocking on the CD drive to read data... For this reason
        // it's not attempted when streaming audio, since it probably blocks for
        // too long.
        if (vag && !vag->paused) {
          SendMbx(iso_mbx,
                  (struct MsgPacket*)msg);  // audio running, can't interrupt it.
        } else {
          // again, set up temporary buffer and load music.
          IsoBufferHeader* temp = TryAllocateBuffer(BUFFER_PAGE_SIZE);
          if (!temp) {
            SendMbx(iso_mbx, (struct MsgPacket*)msg);
          } else {
            MusicLoadCommand* cmd = (MusicLoadCommand*)msg;
            SetBufferMem(temp + 1, BUFFER_PAGE_SIZE);
            msg->status = isofs->load_music(cmd->music_name, cmd->music_handle);
            ReleaseBufferMem();
            FreeBuffer(temp);
            ReturnMessage(msg);
          }
        }
      } else if (msg->cmd_id == QUEUE_VAG_STREAM) {
        // Queue streamed audio for future playback
        VagCommand* cmd = (VagCommand*)msg;
#if REVISION <= 20011002
        if (vag && vag->vag_entry == cmd->vag_entry) {
        } else if (vag && (vag->started || !vag->paused)) {
        } else {
          // cancel existing vag, this one wins.
          if (vag) {
            StopVAG(vag);
            ReleaseMessage((IsoMessage*)vag);
            vag = 0;
          }

          // Set up a copy -- we need this to outlast the request since
          // we'll track the queuing progress across separate queue/play
          // commands from the caller.
          vag = &s_vag_cmd;
          memcpy(vag, cmd, sizeof(VagCommand));
          InitVAGCmd(vag, 1);

          // Queue and open file. The queueing adds this command to the disc I/O
          // scheduling below.
          if (QueueMessage((IsoMessage*)vag, 3, "QueueVAGStream")) {
            vag->header.fd =
                vag->vag_entry ? isofs->open_wad(vag->file_record, vag->vag_entry->offset) : 0;
            vag->header.status = CMD_STATUS_IN_PROGRESS;
            vag->header.callback_function = ProcessVAGData;
          } else {
            // queue failed, it returns the command, none in progress now.
            vag = 0;
          }
        }
#else
        // take the command only if it has an entry (found the audio), and it
        // changes the current vag (either current vag isn't set, or its a
        // different entry, or it's a different file (changing language)).
        if (cmd->vag_entry &&
            (!vag || vag->vag_entry != cmd->vag_entry || vag->file_record != cmd->file_record)) {
          if (vag && (paused || !vag->paused)) {
            // can't replace an existing vag when either the entire game is
            // paused, or the vag is actively playing. (a paused vag when the
            // game is paused can't be replaced.)
          } else {
            // cancel existing vag, this one wins.
            if (vag) {
              gVAGCMD = 0;
              StopVAG(vag);
              ReleaseMessage((IsoMessage*)vag);
              vag = 0;
            }

            // Set up a copy -- we need this to outlast the request since
            // we'll track the queuing progress across separate queue/play
            // commands from the caller.
            vag = &s_vag_cmd;
            memcpy(vag, cmd, sizeof(VagCommand));
            InitVAGCmd(vag, 1);

            // Queue and open file. The queueing adds this command to the disc I/O
            // scheduling below.
            if (QueueMessage((IsoMessage*)vag, 3, "QueueVAGStream")) {
              vag->header.fd =
                  vag->vag_entry ? isofs->open_wad(vag->file_record, vag->vag_entry->offset) : 0;
              vag->header.status = CMD_STATUS_IN_PROGRESS;
              vag->header.callback_function = ProcessVAGData;
              // make available outside iso.
              gVAGCMD = vag;
            } else {
              // queue failed, it returns the command, none in progress now.
              vag = 0;
            }
          }
        }
#endif

        // Return the message back to the caller as soon as the queueing
        // has started. Unlike other commands, returning doesn't
        // mean it's completed, the caller should watch vag status
        ReturnMessage(msg);
      } else if (msg->cmd_id == PLAY_VAG_STREAM) {
        VagCommand* cmd = (VagCommand*)msg;
        int play = 1;

        // first, check if the existing vag matches
        if (vag &&
            vag->vag_entry == cmd->vag_entry
#if REVISION > 20011002
            // don't forget file -- changes when game language changes.
            && vag->file_record == cmd->file_record
#endif
        ) {

          // update volume and ID (used by caller to track)
          vag->volume = cmd->volume;
#if REVISION > 20011002
          vag->sound_id = cmd->sound_id;
#endif

          // this command is a PLAY, handle unpausing
          if (vag->paused) {
            // if globally paused, don't unpause yet, just set a flag so we
            // unpause next time paused turns off.
            if (paused) {
              resume = 1;
            } else {
              // otherwise, unpause now.
              UnpauseVAG(vag);
            }
          }
        } else {
          // prevent a lower priority vag playback from interrupting a non-paused
          // higher priority vag playback.
          if (vag && !vag->paused && cmd->priority < vag->priority) {
            play = 0;
          }
          if (play) {
            // stop in progress playback
            if (vag) {
#if REVISION > 20011002
              gVAGCMD = 0;
#endif
              StopVAG(vag);                      // stop playback
              ReleaseMessage((IsoMessage*)vag);  // stop streaming data.
              vag = 0;
            }

            // set up vag command, again with our local copy.
            vag = &s_vag_cmd;
            memcpy(vag, cmd, sizeof(VagCommand));

            // start up the vag command, set resume flag if we should unpause the
            // vag on game unpause
            if (paused) {
              InitVAGCmd(vag, 1);
              resume = 1;
            } else {
              InitVAGCmd(vag, 0);
            }
            vag->header.messagebox_to_reply = 0;
            vag->header.thread_id = 0;

            // Queue and open the file. This sets up disc streaming of data.
            if (QueueMessage((IsoMessage*)vag, 3, "PlayVAGStream")) {
              vag->header.fd =
                  vag->vag_entry ? isofs->open_wad(vag->file_record, vag->vag_entry->offset) : 0;
              vag->header.status = CMD_STATUS_IN_PROGRESS;
              vag->header.callback_function = ProcessVAGData;
#if REVISION > 20011002
              gVAGCMD = vag;
#endif
            } else {
              // queue failed, kill command.
              vag = 0;
            }
          }
        }

        // start clocks if playing
        if (play) {
          if (vag && !vag->header.fd) {
            // no file exists, set up fake clock
            gFakeVAGClock = 0;
            gFakeVAGClockRunning = 1;
            gFakeVAGClockPaused = 0;
          } else {
            // reset clock to 0 for consumers to see until
            // playback actually starts
            gRealVAGClock = 0;
            gRealVAGClockS = 0;
            gRealVAGClockRunning = 1;
          }
          // BUG: if queueing fails, this derefences the null command.
          gVAG_Id = vag->sound_id;
        }

        // notify caller that we've received the message, playback
        // has not started yet.
        ReturnMessage(msg);
      } else if (msg->cmd_id == STOP_VAG_STREAM) {
        VagCommand* cmd = (VagCommand*)msg;

        // stop if we have a vag command, it isn't a different audio,
        // and the command isn't lower in priority.
        if (vag && (!cmd->vag_entry || vag->vag_entry == cmd->vag_entry) &&
            cmd->priority >= vag->priority) {
          // nothing playing
#if REVISION > 20011002
          gVAGCMD = 0;
#endif
          // stop audio and streaming of data.
          StopVAG(vag);
          ReleaseMessage((IsoMessage*)vag);
          vag = 0;
        }
        // clear pausing state machine
#if REVISION > 20011002
        paused = 0;
        resume = 0;
#endif
        ReturnMessage(msg);
      } else if (msg->cmd_id == PAUSE_VAG_STREAM) {
        // Apply global pause.
        gFakeVAGClockPaused = 1;
        if (!paused) {  // toggling on
#if REVISION < 20011031
          if (vag && vag->started && !vag->paused) {
#else
          if (vag && !vag->paused) {
#endif
            // running vag. Pause it, but set to resume on
            // global unpause by setting resume = 1.
            PauseVAG(vag);
            resume = 1;
          } else {
            // otherwise, vag doesn't need auto-resume.
            resume = 0;
          }
          paused = 1;
        }
        ReturnMessage(msg);
      } else if (msg->cmd_id == CONTINUE_VAG_STREAM) {
        // Undo global pause.
        gFakeVAGClockPaused = 0;
        if (paused) {
          if (resume)
            UnpauseVAG(vag);
          paused = 0;
          resume = 0;
        }
        ReturnMessage(msg);
      } else if (msg->cmd_id == SET_VAG_VOLUME) {
        // Update volume
        VagCommand* cmd = (VagCommand*)msg;
        if (vag) {
          vag->volume = cmd->volume;
#if REVISION <= 20011002
          SetVAGVol(vag);
#else
          SetVAGVol();
#endif
        }
        ReturnMessage(msg);
      } else if (msg->cmd_id == SET_DIALOG_VOLUME) {
        // Update volume.
        VagCommand* cmd = (VagCommand*)msg;
        gDialogVolume = cmd->volume;
        if (vag) {
#if REVISION <= 20011002
          SetVAGVol(vag);
#else
          SetVAGVol();
#endif
        }
        ReturnMessage(msg);
      } else if (msg->cmd_id == SHUTDOWN_ISO_CMD_ID) {
        // Suspicious, just hangs.
        ReturnMessage(msg);
        for (;;) {
        }
      } else {
        msg->status = CMD_STATUS_UNKNOWN_COMMAND;
        ReturnMessage(msg);
      }
    } else if (result == KE_WAIT_DELETE) {
      // Messagebox was deleted, return, IOP shut down.
      return 0;
    } else if (result != KE_MBOX_NOMSG) {
      // Other message, unhandled.
    }

    // PART 2: vag servicing -- unqueue completed vag commands
    // so we stop loading VAG data from disc.
    if (vag && !CheckVAGStreamProgress(vag)) {
#if REVISION > 20011002
      gVAGCMD = 0;
#endif
      StopVAG(vag);
      ReleaseMessage((IsoMessage*)vag);
      vag = 0;
    }

    // PART 3: get the highest priority queued message and start reading.
    buffer = 0;
    pending = GetMessage();
    if (pending) {
      // Get buffer for data
      if (pending->callback_function == ProcessVAGData) {
        buffer = AllocateBuffer(STR_BUFFER_DATA_SIZE);
      } else {
        buffer = AllocateBuffer(BUFFER_PAGE_SIZE);
      }
      // Start read into buffer (async)
      if (buffer) {
        if (pending->callback_function == ProcessVAGData) {
          pending->status = isofs->begin_read(pending->fd, buffer + 1, STR_BUFFER_DATA_SIZE);
        } else {
          pending->status = isofs->begin_read(pending->fd, buffer + 1, BUFFER_PAGE_SIZE);
        }
        // Failed read, free buffer immediately.
        if (pending->status != CMD_STATUS_IN_PROGRESS) {
          FreeBuffer(buffer);
          buffer = 0;
          pending = 0;
        }
      } else {
        pending = 0;
      }
    }
#if REVISION <= 20011002
    else {
      // no pending message.
    }
#endif

    // If there's no read to start, check to see the DVD wasn't removed
#if REVISION >= 20011031
    if (!pending)
      isofs->poll_drive();
#endif

    // Process completed read data from the last read (not the one started above,
    // the one before)
    ProcessMessageData();

    if (buffer) {
      // Complete the read started above.
      unsigned int status = isofs->sync_read();
      if (status == CMD_STATUS_READ_ERR) {
        FreeBuffer(buffer);
      } else {
        pending->status = status;

        // Sete up buffer pointer to point to the read data.
        if (pending->callback_function == ProcessVAGData) {
          buffer->data = buffer + 1;
          buffer->data_size = STR_BUFFER_DATA_SIZE;
        } else {
          buffer->data = buffer + 1;
          buffer->data_size = BUFFER_PAGE_SIZE;
        }

        // Insert buffer into buffer chain.
        if (pending->callback_buffer) {
          IsoBufferHeader* tail = pending->callback_buffer;
          while (tail->next)
            tail = tail->next;
          tail->next = buffer;
        } else {
          pending->callback_buffer = buffer;
        }

        // now the buffer will be processed on the next ProcessMessageData.
      }
      buffer = 0;
      pending = 0;
#if REVISION > 20011002
    } else {
      DelayThread(100);
#endif
    }
  }
  return 0;
}

// DGO double buffered state machine. Has two optimizations:
// - loads to a pair of temporary buffers, allowing processing from one buffer
//   while another is being read from DVD.
// - final object is loaded directly to the top of the heap, allowing it to
// overlap
//   the temporary buffers and fill the heap completely.
static unsigned int RunDGOStateMachine(IsoMessage* _cmd, IsoBufferHeader* buffer) {
  DgoCommand* cmd = (DgoCommand*)_cmd;
  unsigned int result = CMD_STATUS_IN_PROGRESS;
  unsigned char* data = buffer->data;
  int left = buffer->data_size;

  // loop until all data processed.
  while (left) {
    switch (cmd->state) {
      case DGO_INIT:
        // initialize and move to next state.
        cmd->bytes_processed = 0;
        cmd->state = DGO_READ_HEADER;
        cmd->finished_first_object = 0;
        cmd->abort_requested = 0;
        break;
      case DGO_READ_HEADER: {
        // grab as much of the header as possible.
        int count = sizeof(DgoHeader) - cmd->bytes_processed;
        if (count > left)
          count = left;
        memcpy((unsigned char*)&cmd->dgo_header + cmd->bytes_processed, data, count);
        data += count;
        left -= count;
        cmd->bytes_processed += count;
        if (cmd->bytes_processed == sizeof(DgoHeader)) {
          // we got the whole header, reset the object loading state.
          cmd->bytes_processed = 0;
          cmd->objects_loaded = 0;
          if (cmd->dgo_header.object_count == 1) {
            // only one object, load straight to heap.
            cmd->buffer_index = 0;
            cmd->ee_destination_buffer = cmd->buffer_heap_top;
            cmd->state = DGO_READ_OBJ_HEADER;
          } else {
            // start loading to buffer 1.
            cmd->buffer_index = 1;
            cmd->ee_destination_buffer = cmd->buffer1;
            cmd->state = DGO_READ_OBJ_HEADER;
          }
        }
        break;
      }
      case DGO_FINISH_OBJ: {
        // object is in EE memory.
        // Wait for EE to process, unless it's the first object,
        // in that case we move on to filling the second (empty) temp buffer.
        if (cmd->finished_first_object) {
          if (!LookMbx(sync_mbx))
            goto done;
          if (cmd->abort_requested) {
            cmd->state = DGO_FINISH;
            break;
          }
        }

        // Advance.
        cmd->finished_first_object = 1;
        cmd->header.status = CMD_STATUS_IN_PROGRESS;
        cmd->selected_buffer = cmd->buffer_index == 1 ? cmd->buffer1 : cmd->buffer2;
        // Let EE know where we loaded.
        ReturnMessage((IsoMessage*)cmd);
        if (cmd->buffer_index == 1) {
          cmd->ee_destination_buffer = cmd->buffer2;
          cmd->buffer_index = 2;
        } else {
          cmd->ee_destination_buffer = cmd->buffer1;
          cmd->buffer_index = 1;
        }
        if (cmd->objects_loaded + 1 == cmd->dgo_header.object_count) {
          cmd->state = DGO_READ_LAST_OBJ;
        } else {
          cmd->state = DGO_READ_OBJ_HEADER;
        }
        break;
      }
      case DGO_READ_LAST_OBJ: {
        // extra sync to drain pipeline here.
        // make sure EE is totally done with both temporary buffers
        // before loading straight to the heap.
        if (!LookMbx(sync_mbx))
          goto done;
        if (cmd->abort_requested) {
          cmd->state = DGO_FINISH;
        } else {
          cmd->ee_destination_buffer = cmd->buffer_heap_top;
          cmd->buffer_index = 0;
          cmd->state = DGO_READ_OBJ_HEADER;
        }
        break;
      }
      case DGO_READ_OBJ_HEADER: {
        // read the object header and send it to the EE.
        int count = sizeof(ObjectHeader) - cmd->bytes_processed;
        if (count > left)
          count = left;
        memcpy((unsigned char*)&cmd->object_header + cmd->bytes_processed, data, count);
        data += count;
        left -= count;
        cmd->bytes_processed += count;
        if (cmd->bytes_processed == sizeof(ObjectHeader)) {
          DMA_SendToEE(&cmd->object_header, sizeof(ObjectHeader),
                       (unsigned int)cmd->ee_destination_buffer);
          DMA_Sync();
          cmd->ee_destination_buffer += sizeof(ObjectHeader);
          cmd->object_header.size = (cmd->object_header.size + 15) & ~15;
          cmd->state = DGO_READ_OBJ_DATA;
          cmd->bytes_processed = 0;
        }
        break;
      }
      case DGO_READ_OBJ_DATA: {
        // Read a chunk of data from an object and send it to the EE.
        int count = cmd->object_header.size - cmd->bytes_processed;
        if (count > left)
          count = left;
        DMA_SendToEE(data, count, (unsigned int)cmd->ee_destination_buffer);
        DMA_Sync();
        data += count;
        left -= count;
        cmd->ee_destination_buffer += count;
        cmd->bytes_processed += count;
        if (cmd->bytes_processed == cmd->object_header.size) {
          cmd->objects_loaded++;
          if (cmd->objects_loaded == cmd->dgo_header.object_count) {
            cmd->state = DGO_FINISH;
          } else {
            cmd->state = DGO_FINISH_OBJ;
            cmd->bytes_processed = 0;
          }
        }
        break;
      }
      case DGO_FINISH:
        result = CMD_STATUS_DONE;
        goto done;
        /* Retained to reproduce the original unoptimized switch branch. */
        break;
    }
  }
done:
  // update buffer, so the disc code advances.
  if (!result) {
    buffer->data = 0;
    buffer->data_size = 0;
  } else {
    if (!left) {
      buffer->data = 0;
      buffer->data_size = 0;
    } else {
      buffer->data = data;
      buffer->data_size = left;
    }
  }
  return result;
}

// Data callback to send data to the EE.
static unsigned int CopyDataToEE(IsoMessage* _cmd, IsoBufferHeader* buffer_header) {
  IsoCommandLoadSingle* cmd = (IsoCommandLoadSingle*)_cmd;

  int bytes_to_send = cmd->length_to_copy - cmd->bytes_done;

  if (bytes_to_send > buffer_header->data_size) {
    bytes_to_send = (int)buffer_header->data_size;
  }

  DMA_SendToEE((buffer_header + 1), bytes_to_send, (unsigned int)cmd->dst_ptr);
  DMA_Sync();

  cmd->dst_ptr += bytes_to_send;
  cmd->bytes_done += bytes_to_send;
  buffer_header->data = 0;
  buffer_header->data_size = 0;
  if (cmd->bytes_done == cmd->length_to_copy) {
    return CMD_STATUS_DONE;
  }
  return CMD_STATUS_IN_PROGRESS;
}

// Data callback to memcpy to IOP address.
static unsigned int CopyDataToIOP(IsoMessage* _cmd, IsoBufferHeader* buffer_header) {
  IsoCommandLoadSingle* cmd = (IsoCommandLoadSingle*)_cmd;

  int bytes_to_send = cmd->length_to_copy - cmd->bytes_done;

  if (bytes_to_send > buffer_header->data_size) {
    bytes_to_send = (int)buffer_header->data_size;
  }

  memcpy(cmd->dst_ptr, (buffer_header + 1), bytes_to_send);

  cmd->dst_ptr += bytes_to_send;
  cmd->bytes_done += bytes_to_send;
  buffer_header->data = 0;
  buffer_header->data_size = 0;
  if (cmd->bytes_done == cmd->length_to_copy) {
    return CMD_STATUS_DONE;
  }
  return CMD_STATUS_IN_PROGRESS;
}

// Callback which does nothing.
static unsigned int NullCallback(IsoMessage* _cmd, IsoBufferHeader* buffer_header) {
  (void)_cmd;
  buffer_header->data_size = 0;
  return CMD_STATUS_NULL_CB;
}

// Set up the VAG command.
static void InitVAGCmd(VagCommand* cmd, unsigned int paused) {
  cmd->buffer_count = 0;
  cmd->bytes_remaining = 0;
  cmd->started = 0;
  cmd->paused = paused;
  cmd->sample_rate = 0;
  cmd->invalid_header = 0;
  cmd->end_offset = -1;
  cmd->end_offset_armed = 0;
  // seek past the header.
  gPlayPos = VAG_HEADER_SIZE;
  cmd->header.messagebox_to_reply = 0;
  cmd->header.thread_id = 0;
}

// Swap byte order for endianness conversion.
static int bswap(int in) {
  int out = ((in >> 24) & 0xff) | ((in >> 8) & 0xff00) | ((in << 8) & 0xff0000) |
            ((in << 24) & 0xff000000);
  return out;
}

#define VAG_HEADER_MAGIC 0x56414770
#define VAG_HEADER_MAGIC_SWAPPED 0x70474156

static void VAG_MarkLoopEnd(void* data, unsigned int size);

// Handle incoming VAG data by DMAing to SPU buffers and starting plyback.
static unsigned int ProcessVAGData(IsoMessage* _cmd, IsoBufferHeader* buffer_header) {
  VagCommand* vag = (VagCommand*)_cmd;
  sceSdBatch batch[9];
  int count = 0;

  // reject data if header is invalid.
  if (vag->invalid_header) {
    buffer_header->data_size = 0;
#if REVISION <= 20011002
    return CMD_STATUS_DONE;
#else
    return CMD_STATUS_IN_PROGRESS;
#endif
  }

  // if this is our first buffer, it contains the header with metadata we need
  if (vag->buffer_count == 0) {
    VagHeader* data = (VagHeader*)buffer_header->data;
#if REVISION <= 20011002
    int loop = 0;
#endif

    // VAG headers can have either endianness.
    if (data->magic != VAG_HEADER_MAGIC_SWAPPED && data->magic != VAG_HEADER_MAGIC) {
      vag->invalid_header = 1;
      buffer_header->data_size = 0;
      return CMD_STATUS_IN_PROGRESS;
    }

    vag->sample_rate = data->sample_rate;
    vag->bytes_remaining = data->data_size;

    // if the magic is byte-swapped, so are the other fields.
    if (data->magic == VAG_HEADER_MAGIC_SWAPPED) {
      vag->sample_rate = bswap(vag->sample_rate);
      vag->bytes_remaining = bswap(vag->bytes_remaining);
    }

    gSampleRate = vag->sample_rate;
#if REVISION > 20011002
    gLastVAGHalf = 0;
#endif

    // The data size doesn't include the VAG header, but the header is
    // part of the buffer. Include those bytes in the remaining bytes
    // to process.
    vag->bytes_remaining += VAG_HEADER_SIZE;

    // If the entire VAG fits in this buffer, we can compute the
    // position of the final ADPCM block.
    if (vag->bytes_remaining <= buffer_header->data_size) {
      vag->end_offset = vag->bytes_remaining - ADPCM_BLOCK_SIZE;
    }

    // Upload to SPU, wait until completion.
    if (!DMA_SendToSPUAndSync(buffer_header->data, buffer_header->data_size, gStreamSRAM)) {
      return CMD_STATUS_IN_PROGRESS;
    }

    // Prepare SPU:
    // Set left and right volume to 0.
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLL;
    batch[count].value = 0;
    count++;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLR;
    batch[count].value = 0;
    count++;
    // Add this voice to the output mix.
    batch[count].func = SD_BSET_SWITCH;
    batch[count].entry = (gVoice & 1) | SD_S_VMIXL;
    batch[count].value = sceSdGetSwitch((gVoice & 1) | SD_S_VMIXL) | (1 << (gVoice >> 1));
    count++;
    batch[count].func = SD_BSET_SWITCH;
    batch[count].entry = (gVoice & 1) | SD_S_VMIXR;
    batch[count].value = sceSdGetSwitch((gVoice & 1) | SD_S_VMIXR) | (1 << (gVoice >> 1));
    count++;
    // Set pitch to 0 to stop playback.
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_PITCH;
    batch[count].value = 0;
    count++;
    // Set the playback address to the start of ADPCM data.
    batch[count].func = SD_BSET_ADDR;
    batch[count].entry = gVoice | SD_VA_SSA;
    batch[count].value = gStreamSRAM + VAG_HEADER_SIZE;
    count++;
    // Effectively disable ADSR, key on/key off just starts/stops soun.d
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_ADSR1;
    batch[count].value = VAG_ADSR1;
    count++;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_ADSR2;
    batch[count].value = VAG_ADSR2;
    count++;

    // Set LSAX (destination after reaching ADPCM end) to the silent trap.
    // If we fail to load the next block in time, we'll jump to silence.
    if (vag->end_offset == -1) {
      batch[count].func = SD_BSET_ADDR;
      batch[count].entry = gVoice | SD_VA_LSAX;
#if REVISION <= 20011002
      batch[count].value = gStreamSRAM + loop;
#else
      batch[count].value = gTrapSRAM;
#endif
      count++;
    }

    // Key on, BUT playback is effectively paused due to PITCH and VOLUME of 0.
    // However, those register writes won't take effect until the end of this
    // function in the batch set... a little scary.
    snd_KeyOnVoiceRaw(gVoice & 1, gVoice >> 1);
    vag->started = 1;
    vag->bytes_remaining -= buffer_header->data_size;
    buffer_header->data_size = 0;
  } else if (vag->buffer_count == 1) {
    // Got second buffer.

    // If this second buffer ends the stream, set end block location.
    if (vag->bytes_remaining <= buffer_header->data_size) {
      vag->end_offset = vag->bytes_remaining + VAG_SPU_BUFFER_LAST_BLOCK;
    } else {
      // otherwise, patch this second buffer to loop to LSAX address.
      VAG_MarkLoopEnd(buffer_header->data, buffer_header->data_size);
      FlushDcache();
    }

    // Upload to second SPU buffer.
    if (!DMA_SendToSPUAndSync(buffer_header->data, buffer_header->data_size,
                              gStreamSRAM + VAG_SPU_BUFFER_HALF_SIZE)) {
      return CMD_STATUS_IN_PROGRESS;
    }

    // If we aren't paused, start playback!
    // This is a little odd, it reuses the unpause code by faking this as paused
    // then running unpause, which clears paused.
    if (!vag->paused) {
      vag->paused = 1;
      UnpauseVAG(vag);
    }

    // No more room for data, stop streaming data.
    // CheckVAGStreamProgress will re-enable when it's time.
    vag->header.ready_for_data = 0;
    vag->bytes_remaining -= buffer_header->data_size;
    buffer_header->data_size = 0;
    gPlayPos = VAG_HEADER_SIZE;
    gPlaying = 1;
  } else {
    // otherwise, normal buffer maintenance
    // this is done collaboratively with CheckVAGStreamProgress,
    // this function just uploads buffer and sets LSAX.
    // CheckVAGStreamProgress is the one responsible for handling
    // setting LSAX to the trap.
    if ((vag->buffer_count & 1) != 0) {
      // fill upper buffer
#if REVISION > 20011002
      sceSdBatch single;
#endif

      // handle VAG end or patch loop
      if (vag->bytes_remaining <= buffer_header->data_size) {
        vag->end_offset = vag->bytes_remaining + VAG_SPU_BUFFER_LAST_BLOCK;
      } else {
        VAG_MarkLoopEnd(buffer_header->data, buffer_header->data_size);
        FlushDcache();
      }

      // upload to upper half.
      if (!DMA_SendToSPUAndSync(buffer_header->data, buffer_header->data_size,
                                gStreamSRAM + VAG_SPU_BUFFER_HALF_SIZE)) {
        return CMD_STATUS_IN_PROGRESS;
      }

      // Update LSAX to jump to upper half when lower half is done,
      // rather than jumping to trap.
#if REVISION > 20011002
      single.func = SD_BSET_ADDR;
      single.entry = gVoice | SD_VA_LSAX;
      single.value = gStreamSRAM + VAG_SPU_BUFFER_HALF_SIZE;
      sceSdProcBatch(&single, 0, 1);
#endif
    } else {
      // fill lower buffer.
#if REVISION > 20011002
      sceSdBatch single;
#endif

      // patch ADPCM: in early revision set LoopStart here, so
      // later completion of the upper buffer jumps back.
      // In later revisions, both jumps use LSAX, which points
      // to the trap before the buffer is installed.
      if (vag->bytes_remaining <= buffer_header->data_size) {
        vag->end_offset = vag->bytes_remaining - 16;
      } else {
#if REVISION <= 20011002
        VAG_MarkLoopStart(buffer_header->data);
#else
        VAG_MarkLoopEnd(buffer_header->data, buffer_header->data_size);
#endif
        FlushDcache();
      }

      // Upload
      if (!DMA_SendToSPUAndSync(buffer_header->data, buffer_header->data_size, gStreamSRAM)) {
        return CMD_STATUS_IN_PROGRESS;
      }

      // patch LSAX to point to first buffer.
#if REVISION > 20011002
      single.func = SD_BSET_ADDR;
      single.entry = gVoice | SD_VA_LSAX;
      single.value = gStreamSRAM;
      sceSdProcBatch(&single, 0, 1);
#endif
    }

    // Both buffers filled, stop requesting data.
    vag->header.ready_for_data = 0;
    vag->bytes_remaining -= buffer_header->data_size;
    buffer_header->data_size = 0;
  }

  // run batched register sets.
  if (count) {
    sceSdProcBatch(batch, 0, count);
  }
  vag->buffer_count++;
  return CMD_STATUS_IN_PROGRESS;
}

// Stop a playing VAG command.
static void StopVAG(VagCommand* vag) {
  gPlaying = 0;
  PauseVAG(vag);
  snd_KeyOffVoiceRaw(gVoice & 1, gVoice >> 1);
  gFakeVAGClockRunning = 0;
  gRealVAGClockRunning = 0;
#if REVISION <= 20011002
  gVAG_Id = -1;
#else
  gVAG_Id = 1;
#endif
}

// Pause a playing VAG command. Keeps the note on but sets volume/pitch to 0.
static void PauseVAG(VagCommand* vag) {
  gFakeVAGClockPaused = 1;
  if (vag->paused)
    return;
#if REVISION > 20011002
  vag->paused = 1;
#endif
  if (vag->started) {
    sceSdBatch batch[3];
    int count = 0;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLL;
    batch[count].value = 0;
    count++;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLR;
    batch[count].value = 0;
    count++;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_PITCH;
    batch[count].value = 0;
    count++;
    sceSdProcBatch(batch, 0, count);
  }
#if REVISION <= 20011002
  vag->paused = 1;
#endif
}

// Compute the left/right volumes for VAG playback.
#if REVISION > 20011002
static void CalculateVAGVolumes(int volume,
                                int positioned,
                                Vec3w* position,
                                int* left,
                                int* right) {
  unsigned int adjusted;
  unsigned int angle;
  VolumePair* pan;
  if (!positioned) {
    // just use gDialogVolume to scale
    adjusted = (volume * gDialogVolume) >> 6;
    if ((int)adjusted >= 0x4000)
      adjusted = 0x3fff;
    *left = adjusted;
    *right = adjusted;
  } else {
    // compute falloff and pan effect
    volume = (volume * gDialogVolume) >> 10;
    adjusted = CalculateFalloffVolume(position, volume, 1, 10, 50);
    angle = CalculateAngle(position);
    // 630 = 360 + 270. The 360 keeps the angle positive, and the 270 is
    // the offset between game angle and the pan table audio.
    angle = (630 - angle) % 360;
    pan = &gPanTable[angle];
    *left = (pan->left * adjusted) >> 10;
    *right = (pan->right * adjusted) >> 10;
    if (*left >= 0x4000)
      *left = 0x3fff;
    if (*right >= 0x4000)
      *right = 0x3fff;
  }
}
#endif

// Start playback if paused and update volume
static void UnpauseVAG(VagCommand* vag) {
  gFakeVAGClockPaused = 0;
  if (!vag->paused)
    return;
  if (vag->started) {
    sceSdBatch batch[3];
    int count = 0;

    // compute volume
#if REVISION <= 20011002
    int adjusted;
    adjusted = (vag->volume * gDialogVolume) >> 6;
    if (adjusted >= 0x4000)
      adjusted = 0x3fff;
#else
    int left, right;
    CalculateVAGVolumes(vag->volume, vag->is_positioned, &vag->position, &left, &right);
#endif

    // set volume registers
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLL;
#if REVISION <= 20011002
    batch[count].value = adjusted;
#else
    batch[count].value = left;
#endif
    count++;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLR;
#if REVISION <= 20011002
    batch[count].value = adjusted;
#else
    batch[count].value = right;
#endif
    count++;

    // Set pitch based on sample rate.
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_PITCH;
    batch[count].value = (vag->sample_rate * 4096) / SPU2_SAMPLE_RATE;
    count++;
    sceSdProcBatch(batch, 0, count);
  }
  vag->paused = 0;
}

// Update the volume of VAG playback.
#if REVISION <= 20011002
static void SetVAGVol(VagCommand* vag) {
#else
void SetVAGVol(void) {
  VagCommand* vag = gVAGCMD;
#endif
#if REVISION <= 20011002
  if (vag->started && !vag->paused) {
#else
  if (vag && vag->started && !vag->paused) {
#endif
    sceSdBatch batch[2];
    int count = 0;
#if REVISION <= 20011002
    int adjusted;
    adjusted = (vag->volume * gDialogVolume) >> 6;
    if (adjusted >= 0x4000)
      adjusted = 0x3fff;
#else
    int left, right;
    CalculateVAGVolumes(vag->volume, vag->is_positioned, &vag->position, &left, &right);
#endif
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLL;
#if REVISION <= 20011002
    batch[count].value = adjusted;
#else
    batch[count].value = left;
#endif
    count++;
    batch[count].func = SD_BSET_PARAM;
    batch[count].entry = gVoice | SD_VP_VOLR;
#if REVISION <= 20011002
    batch[count].value = adjusted;
#else
    batch[count].value = right;
#endif
    count++;
    sceSdProcBatch(batch, 0, count);
  }
}

// Get the current position, relative to the full VAG buffer
#if REVISION > 20011002
static int GetPlayPos(void) {
  unsigned int a, b, c;
  // Loop, reading the voice position three times, exiting when
  // at least two agree. Maybe a hardware bug workaround?
  for (;;) {
    a = sceSdGetAddr(gVoice | SD_VA_NAX);
    b = sceSdGetAddr(gVoice | SD_VA_NAX);
    c = sceSdGetAddr(gVoice | SD_VA_NAX);
    if (a == b || a == c)
      break;
    if (b == c) {
      a = b;
      break;
    }
  }
  // relative to start
  a -= gStreamSRAM;

  // if outside StreamSRAM (like in the silent trap), return -1.
  if (a < VAG_SPU_BUFFER_BOTH_SIZE)
    return a;
  return -1;
}
#endif

// Set the VAG clocks based on buffering progress.
static unsigned int UpdatePlayPos(void) {
  unsigned int pos;
  unsigned int samples;
  if (!gPlaying)
    return gPlayPos;
#if REVISION <= 20011002
  {
    unsigned int b, c;
    __asm__("nop");  // TODO
    for (;;) {
      pos = sceSdGetAddr(gVoice | SD_VA_NAX);
      b = sceSdGetAddr(gVoice | SD_VA_NAX);
      c = sceSdGetAddr(gVoice | SD_VA_NAX);
      if (pos == b || pos == c)
        break;
      if (b == c) {
        pos = b;
        break;
      }
    }
    pos -= gStreamSRAM;
  }
#else
  pos = GetPlayPos();
  if (pos == -1) {
    // if in trap, set to the end of the buffer we were most recently in.
    if (!gLastVAGHalf)
      pos = VAG_SPU_BUFFER_HALF_SIZE;
    else
      pos = VAG_SPU_BUFFER_BOTH_SIZE;
  } else {
    // not in trap, remember which side we're on in case
    // we later run into the trap.
    if (pos < VAG_SPU_BUFFER_HALF_SIZE)
      gLastVAGHalf = 0;
    else
      gLastVAGHalf = 1;
  }
#endif

  // handle wrapping, update total ADPCM byte count.
  if (pos < gPlayPos)
    gRealVAGClockS += pos + (VAG_SPU_BUFFER_BOTH_SIZE - gPlayPos);
  else
    gRealVAGClockS += pos - gPlayPos;

  // Convert ADPCM byte count to sample count.
  samples = 28 * (gRealVAGClockS / 16);
  // Convert samples to 1024 of a second.
  gRealVAGClock = (samples << 8) / gSampleRate;
  gRealVAGClock *= 4;

  // buffer address
  gPlayPos = pos;
  return pos;
}

// Once VAG playback has finished a buffer, release it and set
// up disc system to re-fill it.
static int CheckVAGStreamProgress(VagCommand* vag) {
  unsigned int pos;
  sceSdBatch batch;
#if REVISION > 20011002
  if (vag->invalid_header) {
    return 0;
  }
#endif

  if (!vag->started) {
    return 1;
  }

  pos = gPlayPos;

  // If the final block is in SRAM, set up LSAX to loop on the final block.
  // NOTE that this is slightly odd, it can overwrite LSAX set to jump
  // to the final half and drop the end of audio. Which is why it's fixed later.
  if (vag->end_offset != -1) {
#if REVISION <= 20011002
    if (!vag->end_offset_armed && (unsigned int)vag->end_offset > pos) {
      batch.func = SD_BSET_ADDR;
      batch.entry = gVoice | SD_VA_LSAX;
      batch.value = gStreamSRAM + vag->end_offset;
      sceSdProcBatch(&batch, 0, 1);
      vag->end_offset_armed = 1;
    }
#endif

    // return if we're at the end.
    if ((int)(pos & 0xFFFFFFF0) == vag->end_offset) {
      return 0;
    }

#if REVISION > 20011002
    // Only setup the final loop if we're not at the end yet, and we're on the final buffer half.
    // This makes sure not to overwrite a pending jump to the other buffer.
    if (((pos < VAG_SPU_BUFFER_HALF_SIZE) && (vag->end_offset < VAG_SPU_BUFFER_HALF_SIZE)) ||
        ((0x5fff < pos && (0x5fff < vag->end_offset)))) {
      if ((vag->end_offset_armed == 0) && (vag->end_offset > pos)) {
        batch.func = SD_BSET_ADDR;
        batch.entry = gVoice | SD_VA_LSAX;
        batch.value = gStreamSRAM + vag->end_offset;
        sceSdProcBatch(&batch, 0, 1);
        vag->end_offset_armed = 1;
      }

      return 1;
    }
#else
    return 1;
#endif
  }

  if (pos < VAG_SPU_BUFFER_HALF_SIZE) {
    // If we're in the first half and most recently filled second,
    // then we're ready to fill first again, and set up trap
    // until that's filled.
    if ((vag->buffer_count & 1) != 0) {
      vag->header.ready_for_data = 1;
#if REVISION > 20011002
      batch.func = SD_BSET_ADDR;
      batch.entry = gVoice | SD_VA_LSAX;
      batch.value = gTrapSRAM;
      sceSdProcBatch(&batch, 0, 1);
#endif
    }

  } else {
    if ((vag->buffer_count & 1) == 0) {
      // If we're in the second half and most recently filled first,
      // then we're ready to fill second again, and set up trap
      // until that's filled.
      vag->header.ready_for_data = 1;
#if REVISION > 20011002
      batch.func = SD_BSET_ADDR;
      batch.entry = gVoice | SD_VA_LSAX;
      batch.value = gTrapSRAM;
      sceSdProcBatch(&batch, 0, 1);
#endif
    }
  }

  return 1;
}

// ISO API inclusion.

// ISO RPCs

// RPC thread for DGO.
static void* RPC_DGO(unsigned int, void*, int);
static unsigned int DGOThread(void) {
  sceSifQueueData dq;
  sceSifServeData serve;

  CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&dq, GetThreadId());
  sceSifRegisterRpc(&serve, DGO_RPC_ID, RPC_DGO, &sRPCBuf, 0, 0, &dq);
  CpuEnableIntr();
  sceSifRpcLoop(&dq);
  return 0;
}

// RPC handler callback.
static void LoadDGO(DgoRpcCommand* cmd);
static void LoadNextDGO(DgoRpcCommand* cmd);
static void CancelDGO(DgoRpcCommand* cmd);
static void* RPC_DGO(unsigned int fno, void* _cmd, int y) {
  DgoRpcCommand* cmd = (DgoRpcCommand*)_cmd;

  if (fno == DGO_RPC_LOAD_FNO) {
    LoadDGO(cmd);
  } else if (fno == DGO_RPC_LOAD_NEXT_FNO) {
    LoadNextDGO(cmd);
  } else if (fno == DGO_RPC_CANCEL_FNO) {
    CancelDGO(cmd);
  } else {
    cmd->result = DGO_RPC_RESULT_ERROR;
  }
  return cmd;
}

// Start loading a DGO.
static void LoadDGO(DgoRpcCommand* cmd) {
  // find file.
  FileRecord* file_record = isofs->find(cmd->name);
  if (!file_record) {
    cmd->result = DGO_RPC_RESULT_ERROR;
    return;
  }

  // cancel an in-progress load... with a null pointer for the command?
  CancelDGO(0);

  // copy data to our local command, keep it alive past the RPC
  // returning.
  scmd.header.cmd_id = LOAD_DGO_CMD_ID;
  scmd.header.messagebox_to_reply = dgo_mbx;
  scmd.header.thread_id = 0;
  scmd.file_record = file_record;
  scmd.buffer1 = (unsigned char*)cmd->buffer1;
  scmd.buffer2 = (unsigned char*)cmd->buffer2;
  scmd.buffer_heap_top = (unsigned char*)cmd->buffer_heap_top;

  // Inform ISO thread of the command
  SendMbx(iso_mbx, (struct MsgPacket*)&scmd);

  // Wait for the first object file to load
  WaitMbx(dgo_mbx);

  // Reply to EE where it was loaded.
  if (scmd.header.status == CMD_STATUS_IN_PROGRESS) {
    cmd->result = DGO_RPC_RESULT_MORE;
  } else if (scmd.header.status == CMD_STATUS_DONE) {
    cmd->result = DGO_RPC_RESULT_DONE;
    cmd->buffer1 = cmd->buffer_heap_top;
    scmd.header.cmd_id = 0;
  } else {
    cmd->result = DGO_RPC_RESULT_ERROR;
    scmd.header.cmd_id = 0;
  }
}

// Advance DGO loading.
static void LoadNextDGO(DgoRpcCommand* cmd) {
  struct MsgPacket packet;

  if (scmd.header.cmd_id == 0) {
    cmd->result = DGO_RPC_RESULT_ERROR;
  } else {
    // update heap address for loader
    scmd.buffer_heap_top = (unsigned char*)cmd->buffer_heap_top;

    // release DGO state machine, allow it to continue.
    SendMbx(sync_mbx, &packet);

    // Wait for next load to complete
    WaitMbx(dgo_mbx);

    // Inform EE where it went.
    if (scmd.header.status == CMD_STATUS_IN_PROGRESS) {
      cmd->result = DGO_RPC_RESULT_MORE;
      cmd->buffer1 = (unsigned int)scmd.selected_buffer;
    } else if (scmd.header.status == CMD_STATUS_DONE) {
      cmd->result = DGO_RPC_RESULT_DONE;
      cmd->buffer1 = cmd->buffer_heap_top;
      scmd.header.cmd_id = 0;
    } else {
      cmd->result = DGO_RPC_RESULT_ERROR;
      scmd.header.cmd_id = 0;
    }
  }
}

// Cancel an in-progress load. Not used, writes to a possibly-NULL command.
static void CancelDGO(DgoRpcCommand* cmd) {
  struct MsgPacket packet;
  if (!scmd.header.cmd_id)
    return;
  scmd.abort_requested = 1;

  // Inform loader and wait for ack.
  SendMbx(sync_mbx, &packet);
  WaitMbx(dgo_mbx);

  cmd->result = DGO_RPC_RESULT_ABORTED;

  scmd.header.cmd_id = 0;
}

// Get the current play position, either from real or fake VAG timing.
int GetVAGStreamPos(void) {
  UpdatePlayPos();
  if (gFakeVAGClockRunning) {
    return gFakeVAGClock;
  }
  if (gRealVAGClockRunning) {
    return gRealVAGClock;
  }
  return -1;
}

static void VAG_MarkLoopStart(unsigned char* data) {
  data[1] = 6;
  data[17] = 2;
}

static void VAG_MarkLoopEnd(void* data, unsigned int size) {
  data = (unsigned char*)data + (size - 16);
  ((unsigned char*)data)[1] = 3;
}

#if REVISION > 20011002
static void VAG_MarkNonloopStart(unsigned char* data) {
  data[1] = 4;
}

static void VAG_MarkNonloopEnd(unsigned char* data, unsigned int size) {
  data += size - 32;
  data[1] = 1;
  data[17] = 7;
}
#endif
