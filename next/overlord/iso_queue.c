#include "sce/iop/thsemap/thsemap.h"
#include "sce/iop/thmsgbx/thmsgbx.h"
#include "overlord/iso_queue.h"
#include "overlord/iso.h"
#include "overlord/isocommon.h"
#include "sce/iop/thbase/thread.h"

// Management of queued commands and their buffers.
// The queue picks which command is serviced by the disc next.

#define N_BUFFERS 4
#if REVISION <= 20011002
#define N_STR_BUFFERS 2
#else
#define N_STR_BUFFERS 1
#endif
#define N_PRIORITIES 4
#define PRI_STACK_LENGTH 4
#define N_VAG_CMDS 31

// Buffer with data
typedef struct IsoBuffer {
  IsoBufferHeader header;
  unsigned char data[BUFFER_PAGE_SIZE];
} IsoBuffer;

// Buffer with data
typedef struct IsoStrBuffer {
  IsoBufferHeader header;
  unsigned char data[STR_BUFFER_DATA_SIZE];
} IsoStrBuffer;

// Priority stack
typedef struct PriStackEntry {
  IsoMessage* cmds[PRI_STACK_LENGTH];
  int count;
} PriStackEntry;

// Buffers
static IsoBuffer sBuffer[N_BUFFERS];
static IsoStrBuffer sStrBuffer[N_STR_BUFFERS];

// Free lists
static IsoBuffer* sFreeBuffer;
static IsoStrBuffer* sFreeStrBuffer;

// priority stack
static PriStackEntry gPriStack[N_PRIORITIES];

// VAG command pool
#if REVISION >= 20011031
static int sSema;
static VagCommand vag_cmds[N_VAG_CMDS];
#endif

void InitBuffers(void) {
  int i;
#if REVISION >= 20011031
  struct SemaParam params;
#endif

  // Set up normal buffers and their free list.
  for (i = 0; i < N_BUFFERS; i++) {
    sBuffer[i].header.data = 0;
    sBuffer[i].header.data_size = 0;
    sBuffer[i].header.buffer_size = BUFFER_PAGE_SIZE;
    sBuffer[i].header.next = &sBuffer[i + 1].header;
  }
  sBuffer[N_BUFFERS - 1].header.next = 0;
  sFreeBuffer = sBuffer;

  // Set up STR buffers and their free list.
  for (i = 0; i < N_STR_BUFFERS; i++) {
    sStrBuffer[i].header.data = 0;
    sStrBuffer[i].header.data_size = 0;
    sStrBuffer[i].header.buffer_size = STR_BUFFER_DATA_SIZE;
    sStrBuffer[i].header.next = &sStrBuffer[i + 1].header;
  }
  sStrBuffer[N_STR_BUFFERS - 1].header.next = 0;
  sFreeStrBuffer = sStrBuffer;

  // set up semaphore for VAG pool access.
#if REVISION >= 20011031
  params.attr = SA_THPRI;
  params.initCount = 1;
  params.maxCount = 1;
  params.option = 0;
  sSema = CreateSema(&params);
  if (sSema < 0) {
    for (;;) {
    }
  }
#endif
}

IsoBufferHeader* TryAllocateBuffer(unsigned int size);

// Allocate a buffer or lockup on failure.
IsoBufferHeader* AllocateBuffer(unsigned int size) {
  IsoBufferHeader* buffer = TryAllocateBuffer(size);
  if (buffer)
    return buffer;
  for (;;) {
  }
  return 0;
}

// Allocate a buffer, return null if not found.
// size should be one of the BUFFER_PAGE_SIZE or STR_BUFFER_DATA_SIZE
IsoBufferHeader* TryAllocateBuffer(unsigned int size) {
  IsoBufferHeader* buffer;
  if (size == BUFFER_PAGE_SIZE) {
    if (sFreeBuffer) {
      buffer = (IsoBufferHeader*)sFreeBuffer;
      sFreeBuffer = (IsoBuffer*)buffer->next;
      buffer->data = 0;
      buffer->data_size = 0;
      buffer->next = 0;
      return buffer;
    }
  } else {
    if (sFreeStrBuffer) {
      buffer = (IsoBufferHeader*)sFreeStrBuffer;
      sFreeStrBuffer = (IsoStrBuffer*)buffer->next;
      buffer->data = 0;
      buffer->data_size = 0;
      buffer->next = 0;
      return buffer;
    }
  }
  return 0;
}

// Insert buffer back into free list.
void FreeBuffer(IsoBufferHeader* buffer) {
  if (buffer->buffer_size == BUFFER_PAGE_SIZE) {
    buffer->next = (IsoBufferHeader*)sFreeBuffer;
    sFreeBuffer = (IsoBuffer*)buffer;
  } else {
    IsoStrBuffer* str = (IsoStrBuffer*)buffer;
    str->header.next = (IsoBufferHeader*)sFreeStrBuffer;
    sFreeStrBuffer = str;
  }
}

void DisplayQueue(void) {
  // does nothing.
}

// Try to add a message to the queue. If queue is full, sets error status and returns
// the message to the submitter.
unsigned int QueueMessage(IsoMessage* cmd, int priority, const char* name) {
  PriStackEntry* stack = &gPriStack[priority];
  if (stack->count == PRI_STACK_LENGTH) {
    cmd->status = CMD_STATUS_QUEUE_FULL;
    ReturnMessage(cmd);
    return 0;
  }
  stack->cmds[stack->count] = cmd;
  stack->count++;
  DisplayQueue();
  return 1;
}

// Remove a message from the queue and move other messages forward as needed.
void UnqueueMessage(IsoMessage* cmd) {
  int priority;
  int i;
  PriStackEntry* stack;
  for (priority = 0; priority < N_PRIORITIES; priority++) {
    stack = &gPriStack[priority];
    for (i = 0; i < stack->count; i++) {
      if (stack->cmds[i] == cmd)
        break;
    }
    if (i < stack->count) {
      stack->count--;
      while (i < stack->count) {
        stack->cmds[i] = stack->cmds[i + 1];
        i++;
      }
      DisplayQueue();
      return;
    }
  }
}

// Get the highest priority valid message.
IsoMessage* GetMessage(void) {
  int priority;
  PriStackEntry* stack;
  int i;
  for (priority = N_PRIORITIES - 1; priority >= 0; priority--) {
    stack = &gPriStack[priority];
    for (i = stack->count - 1; i >= 0; i--) {
#if REVISION <= 20011002
      // old version: take first in-progress command with less than 2 buffers.
      if (stack->cmds[i]->fd && stack->cmds[i]->status == CMD_STATUS_IN_PROGRESS) {
        if (!stack->cmds[i]->callback_buffer || !stack->cmds[i]->callback_buffer->next)
          return stack->cmds[i];
      }
#else
      // new version: similar, but require ready_for_data flag (defers vag load if there's no
      // SPU spot for it), and only load the priority 3 (VAG) data if it has 0 buffers.
      // This is likely to support a change to a single VAG STR buffer,
      // in addition to deferring VAG load until there's SPU memory available for the callback
      // This effectively defers VAG loads from issuing until there's only 1 SPU buffer available.
      if (stack->cmds[i]->fd && stack->cmds[i]->status == CMD_STATUS_IN_PROGRESS &&
          stack->cmds[i]->ready_for_data) {
        if (priority == N_PRIORITIES - 1) {
          if (!stack->cmds[i]->callback_buffer)
            return stack->cmds[i];
        } else {
          if (!stack->cmds[i]->callback_buffer || !stack->cmds[i]->callback_buffer->next)
            return stack->cmds[i];
        }
      }
#endif
    }
  }
  return 0;
}

// Send completed buffers to command callbacks
void ProcessMessageData(void) {
  int priority;
  PriStackEntry* stack;
  int i;
  IsoMessage* cmd;
  IsoBufferHeader* buffer;

  // iterate all commands
  for (priority = N_PRIORITIES - 1; priority >= 0; priority--) {
    stack = &gPriStack[priority];
    for (i = stack->count - 1; i >= 0; i--) {
      cmd = stack->cmds[i];

      // skip non-ready commands.
#if REVISION <= 20011002
      if (!cmd->ready_for_data)
        continue;
#endif
      if (cmd->status == CMD_STATUS_IN_PROGRESS) {
        buffer = cmd->callback_buffer;
        if (buffer) {
          // callback. If buffer is done, free it.
          cmd->status = cmd->callback_function(cmd, buffer);
          if (buffer->data_size == 0) {
            cmd->callback_buffer = buffer->next;
            FreeBuffer(buffer);
          }
        }
      }

      // if it fails:
      if (cmd->status != CMD_STATUS_IN_PROGRESS) {
        // unqueue this message, and return.
        ReleaseMessage(cmd);
        ReturnMessage(cmd);
        // restart loop at this priority since we may have shifted message pointers.
        priority++;
        break;
      }
    }
  }
}

void FreeVAGCommand(VagCommand* cmd);

// Return a message to the caller.
// send back to reply messagebox
// wake a sleeping thread
// otherwise, assume it's VAG and free back to pool.
void ReturnMessage(IsoMessage* cmd) {
#if REVISION < 20011031
  if (cmd->messagebox_to_reply) {
    SendMbx(cmd->messagebox_to_reply, (struct MsgPacket*)cmd);
  }
  if (cmd->thread_id) {
    WakeupThread(cmd->thread_id);
  }
#else
  if (cmd->messagebox_to_reply) {
    SendMbx(cmd->messagebox_to_reply, (struct MsgPacket*)cmd);
  } else if (cmd->thread_id) {
    WakeupThread(cmd->thread_id);
  } else {
    FreeVAGCommand((VagCommand*)cmd);
  }
#endif
}

// Release the resource for this message: buffers, files, and priority queue slots.
void ReleaseMessage(IsoMessage* cmd) {
  while (cmd->callback_buffer) {
    IsoBufferHeader* buffer = cmd->callback_buffer;
    cmd->callback_buffer = buffer->next;
    FreeBuffer(buffer);
  }
  if (cmd->fd)
    isofs->close(cmd->fd);
  UnqueueMessage(cmd);
}

// VAG command pool for submission to iso thread.
#if REVISION >= 20011031
static int vag_cmd_used = 0;
static int vag_cmd_cnt = 0;
static int max_vag_cmd_cnt = 0;

VagCommand* GetVAGCommand(void) {
  int i;
  int result;
  for (;;) {
    // delay while there's no command
    while (vag_cmd_cnt == N_VAG_CMDS)
      DelayThread(100);

    // acquire lock
    while ((result = WaitSema(sSema))) {
    }

    // allocate
    for (i = 0; i < N_VAG_CMDS; i++) {
      if (!((vag_cmd_used >> i) & 1)) {
        vag_cmd_used |= 1 << i;
        vag_cmd_cnt++;
        if (vag_cmd_cnt > max_vag_cmd_cnt)
          max_vag_cmd_cnt = vag_cmd_cnt;
        // release lock and return
        SignalSema(sSema);
        return &vag_cmds[i];
      }
    }
    SignalSema(sSema);
  }
}

// Release a command.
void FreeVAGCommand(VagCommand* cmd) {
  int i = cmd - vag_cmds;
  int result;
  if (i < 0 || i >= N_VAG_CMDS || !((vag_cmd_used >> i) & 1))
    return;
  while ((result = WaitSema(sSema))) {
  }
  vag_cmd_used &= ~(1 << i);
  vag_cmd_cnt--;
  SignalSema(sSema);
}
#endif
