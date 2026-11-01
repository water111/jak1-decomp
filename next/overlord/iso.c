#include "overlord/iso.h"
#include "common/overlord_rpc.h"
#include "overlord/dma.h"
#include "overlord/iso_api.h"
#include "overlord/iso_cd.h"
#include "overlord/iso_queue.h"
#include "overlord/isocommon.h"
#include "overlord/ssound.h"
#include "overlord/stream.h"
#include "sce/iop/kerror.h"
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
IsoFs *isofs = 0;

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
VagCommand *gVAGCMD = 0;
static unsigned char VAG_SilentLoop[48] = {
    0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif

// VAG clocks published to the user so they can see audio playback progress.
int gRealVAGClock;
int gFakeVAGClock;

// TODO
int gSampleRate;
#if REVISION > 20011002
// TODO
int gLastVAGHalf;
#endif

// TODO
unsigned int gPlayPos;
// TODO
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
void InitDriver(unsigned char *buffer) {
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
  struct MsgPacket *packet;
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
unsigned int InitISOFS(const char *fs_mode, const char *loading_screen) {
  struct ThreadParam thread;
  struct MbxParam mbx;
  FileRecord *file;
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
FileRecord *FindISOFile(const char *name) { return isofs->find(name); }

// Get the length of a file.
// This is likely exposed as a function so development modes can
// dynamically update a file size when it's replaced with a newer version.
int GetISOFileLength(FileRecord *file) { return isofs->get_length(file); }

// Look up a VAG file's entry by name (treated as 8-bytes)
// TODO: cleanup?
VagDirEntry *FindVAGFile(const char *name) {
  int i;
  unsigned int first = *(unsigned int *)name;
  unsigned int second = *(unsigned int *)(name + 4);
  VagDirEntry *entry = gVagDir.vag;
  for (i = 0; i < gVagDir.count; i++, entry++) {
    unsigned int a = *(unsigned int *)entry->name;
    unsigned int b = *(unsigned int *)(entry->name + 4);
    if (first == a && second == b)
      return entry;
  }
  return 0;
}

static unsigned int NullCallback(IsoMessage *, IsoBufferHeader *);
static unsigned int RunDGOStateMachine(IsoMessage *, IsoBufferHeader *);
static unsigned int CopyDataToEE(IsoMessage *, IsoBufferHeader *);
static unsigned int CopyDataToIOP(IsoMessage *, IsoBufferHeader *);
static void StopVAG(VagCommand *);
static void InitVAGCmd(VagCommand *, unsigned int);
static void PauseVAG(VagCommand *);
static void UnpauseVAG(VagCommand *);
#if REVISION <= 20011002
static void SetVAGVol(VagCommand *);
#endif
static int CheckVAGStreamProgress(VagCommand *);
static void InitVAGCmd(VagCommand *, unsigned int);
static unsigned int ProcessVAGData(IsoMessage *, IsoBufferHeader *);

// This function receives commands in the iso_mbx and overlaps disc
// reading with buffer callbacks.
static unsigned int ISOThread(void) {
  IsoBufferHeader *buffer = 0;
  IsoMessage *pending = 0;

  // set to our local persistent vag command when it's in progress.
  VagCommand *vag = 0;
  int paused = 0;
  int resume = 0;

  // storage for command referenced by vag above.
  static VagCommand s_vag_cmd;

  // Before starting, let driver initialize (read file locations for CD)
  InitBuffers();
  buffer = AllocateBuffer(BUFFER_PAGE_SIZE);
  InitDriver((unsigned char *)(buffer + 1));
  FreeBuffer(buffer);

  // Loop to process incoming commands, kick off reads, and run callbacks on
  // buffered data.
  for (;;) {
    struct MsgPacket *packet;
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
      IsoMessage *msg = (IsoMessage *)packet;
      msg->callback_buffer = 0;
      msg->ready_for_data = 1;
      msg->callback_function = NullCallback;
      msg->fd = 0;

      // Streaming file load to EE or IOP.
      if (msg->cmd_id == LOAD_TO_EE_CMD_ID ||
          msg->cmd_id == LOAD_TO_IOP_CMD_ID ||
          msg->cmd_id == LOAD_TO_EE_OFFSET_CMD_ID) {

        // Add message to queue - if queuing fails, QueueMessage returns failure
        // to caller.
        if (QueueMessage(msg, 2, "LoadSingle")) {

          // Open the file
          IsoCommandLoadSingle *cmd = (IsoCommandLoadSingle *)msg;
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
          DgoCommand *cmd = (DgoCommand *)msg;
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
          SendMbx(iso_mbx, (struct MsgPacket *)msg);
        } else {
          // Temporary buffer is needed for soundbank loader
          IsoBufferHeader *temp = TryAllocateBuffer(BUFFER_PAGE_SIZE);
          if (!temp) {
            // rejected, no buffer available.
            SendMbx(iso_mbx, (struct MsgPacket *)msg);
          } else {
            SoundBankLoadCommand *cmd = (SoundBankLoadCommand *)msg;
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
          SendMbx(
              iso_mbx,
              (struct MsgPacket *)msg); // audio running, can't interrupt it.
        } else {
          // again, set up temporary buffer and load music.
          IsoBufferHeader *temp = TryAllocateBuffer(BUFFER_PAGE_SIZE);
          if (!temp) {
            SendMbx(iso_mbx, (struct MsgPacket *)msg);
          } else {
            MusicLoadCommand *cmd = (MusicLoadCommand *)msg;
            SetBufferMem(temp + 1, BUFFER_PAGE_SIZE);
            msg->status = isofs->load_music(cmd->music_name, cmd->music_handle);
            ReleaseBufferMem();
            FreeBuffer(temp);
            ReturnMessage(msg);
          }
        }
      } else if (msg->cmd_id == QUEUE_VAG_STREAM) {
        // Queue streamed audio for future playback
        VagCommand *cmd = (VagCommand *)msg;
#if REVISION <= 20011002
        if (vag && vag->vag_entry == cmd->vag_entry) {
        } else if (vag && (vag->started || !vag->paused)) {
        } else {
#else
        // take the command only if it has an entry (found the audio), and it
        // changes the current vag (either current vag isn't set, or its a
        // different entry, or it's a different file (changing language)).
        if (cmd->vag_entry && (!vag || vag->vag_entry != cmd->vag_entry ||
                               vag->file_record != cmd->file_record)) {
          if (vag && (paused || !vag->paused)) {
            // can't replace an existing vag when either the entire game is
            // paused, or the vag is actively playing. (a paused vag when the
            // game is paused can't be replaced.)
          } else {
#endif
          // cancel existing vag, this one wins.
          if (vag) {
#if REVISION > 20011002
            gVAGCMD = 0;
#endif
            StopVAG(vag);
            ReleaseMessage((IsoMessage *)vag);
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
          if (QueueMessage((IsoMessage *)vag, 3, "QueueVAGStream")) {
            vag->header.fd =
                vag->vag_entry
                    ? isofs->open_wad(vag->file_record, vag->vag_entry->offset)
                    : 0;
            vag->header.status = CMD_STATUS_IN_PROGRESS;
            vag->header.callback_function = ProcessVAGData;
            // make available outside iso.
#if REVISION > 20011002
            gVAGCMD = vag;
#endif
          } else {
            // queue failed, it returns the command, none in progress now.
            vag = 0;
          }
#if REVISION > 20011002
        }
#endif
      }

      // Return the message back to the caller as soon as the queueing
      // has started. Unlike other commands, returning doesn't
      // mean it's completed, the caller should watch vag status
      ReturnMessage(msg);
    } else if (msg->cmd_id == PLAY_VAG_STREAM) {
      VagCommand *cmd = (VagCommand *)msg;
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
            ReleaseMessage((IsoMessage *)vag); // stop streaming data.
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
          if (QueueMessage((IsoMessage *)vag, 3, "PlayVAGStream")) {
            vag->header.fd =
                vag->vag_entry
                    ? isofs->open_wad(vag->file_record, vag->vag_entry->offset)
                    : 0;
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
      VagCommand *cmd = (VagCommand *)msg;

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
        ReleaseMessage((IsoMessage *)vag);
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
      if (!paused) { // toggling on
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
      VagCommand *cmd = (VagCommand *)msg;
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
      VagCommand *cmd = (VagCommand *)msg;
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
  }
  else if (result == KE_WAIT_DELETE) {
    // Messagebox was deleted, return, IOP shut down.
    return 0;
  }
  else if (result != KE_MBOX_NOMSG) {
    // Other message, unhandled.
  }

  // PART 2: vag servicing -- unqueue completed vag commands
  // so we stop loading VAG data from disc.
  if (vag && !CheckVAGStreamProgress(vag)) {
#if REVISION > 20011002
    gVAGCMD = 0;
#endif
    StopVAG(vag);
    ReleaseMessage((IsoMessage *)vag);
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
        pending->status =
            isofs->begin_read(pending->fd, buffer + 1, STR_BUFFER_DATA_SIZE);
      } else {
        pending->status =
            isofs->begin_read(pending->fd, buffer + 1, BUFFER_PAGE_SIZE);
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
        IsoBufferHeader *tail = pending->callback_buffer;
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
static unsigned int RunDGOStateMachine(IsoMessage *_cmd,
                                       IsoBufferHeader *buffer) {
  DgoCommand *cmd = (DgoCommand *)_cmd;
  unsigned int result = CMD_STATUS_IN_PROGRESS;
  unsigned char *data = buffer->data;
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
      memcpy((unsigned char *)&cmd->dgo_header + cmd->bytes_processed, data,
             count);
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
      cmd->selected_buffer =
          cmd->buffer_index == 1 ? cmd->buffer1 : cmd->buffer2;
      // Let EE know where we loaded.
      ReturnMessage((IsoMessage *)cmd);
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
      memcpy((unsigned char *)&cmd->object_header + cmd->bytes_processed, data,
             count);
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
static unsigned int CopyDataToEE(IsoMessage *_cmd, IsoBufferHeader *buffer_header) {
  IsoCommandLoadSingle *cmd = (IsoCommandLoadSingle *)_cmd;

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
static unsigned int CopyDataToIOP(IsoMessage *_cmd, IsoBufferHeader *buffer_header) {
  IsoCommandLoadSingle *cmd = (IsoCommandLoadSingle *)_cmd;

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
static unsigned int NullCallback(IsoMessage *_cmd, IsoBufferHeader *buffer_header) {
  (void)_cmd;
  buffer_header->data_size = 0;
  return CMD_STATUS_NULL_CB;
}

// Set up the VAG command.
static void InitVAGCmd(VagCommand *cmd, unsigned int paused) {
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

// TODO left off here.
