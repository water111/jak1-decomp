typedef struct {
  int status;
  void (*entry)(void*);
  void* stack;
  int stack_size;
  void* gp;
  int initial_priority, current_priority;
  unsigned attributes, option;
  int wait_type, wait_id, wakeups;
} Thread;

typedef struct {
  int count, maximum, initial, waiters;
  unsigned attributes, option;
} Semaphore;

typedef struct {
  unsigned read, write;

  struct {
    unsigned char operation, argument;
  } entries[512];
} ThreadQueue;

static int topId = 0;
static unsigned char stack[1024] __asm__("stack.6") __attribute__((aligned(16)));
static int topSema;
static ThreadQueue topArg __attribute__((aligned(8)));
extern char _gp[];
extern int WaitSema(int), CreateSema(Semaphore*), DeleteSema(int), iSignalSema(int);
extern int CreateThread(Thread*), StartThread(int, void*), GetThreadId(void);
extern int ChangeThreadPriority(int, int), WakeupThread(int);
extern int RotateThreadReadyQueue(int), SuspendThread(int);
extern int _iWakeupThread(int), _iSuspendThread(int);
extern int kprintf(const char*, ...);

static void topThread(void* argument) {
  ThreadQueue* queue = argument;
  unsigned i;
  for (;;) {
    WaitSema(topSema);
    i = queue->read & 511;
    queue->read = i + 1;
    switch (queue->entries[i].operation) {
      case 0:
        WakeupThread(queue->entries[i].argument);
        break;
      case 1:
        RotateThreadReadyQueue(queue->entries[i].argument);
        break;
      case 2:
        SuspendThread(queue->entries[i].argument);
        break;
      default:
        kprintf("## internel error in libkernl.a!\n");
        break;
    }
  }
}

int InitThread(void) {
  Thread thread;
  Semaphore semaphore;
  if (topId > 0)
    return -1;
  semaphore.maximum = 255;
  semaphore.initial = 0;
  topSema = CreateSema(&semaphore);
  if (topSema < 0)
    return -1;
  thread.entry = topThread;
  thread.stack = stack;
  thread.stack_size = sizeof(stack);
  thread.gp = _gp;
  thread.initial_priority = 0;
  topId = CreateThread(&thread);
  if (topId < 0) {
    DeleteSema(topSema);
    return -1;
  }
  topArg.read = 0;
  topArg.write = 0;
  StartThread(topId, &topArg);
  ChangeThreadPriority(GetThreadId(), 1);
  return topId;
}

int iWakeupThread(int id) {
  register int result __asm__("$2");
  int current;
  unsigned index;
  __asm__ volatile("li $3,-47\n\tsyscall" : "=r"(result) : : "$3");
  current = result;
  if (current != id)
    return _iWakeupThread(id);
  if ((unsigned)current >= 256 || !topId)
    return -1;
  index = topArg.write & 511;
  topArg.write = index + 1;
  topArg.entries[index].operation = 0;
  topArg.entries[index].argument = current;
  iSignalSema(topSema);
  return current;
}

int iRotateThreadReadyQueue(int priority) {
  unsigned index;
  if ((unsigned)priority >= 128 || !topId)
    return -1;
  index = topArg.write & 511;
  topArg.write = index + 1;
  topArg.entries[index].operation = 1;
  topArg.entries[index].argument = priority;
  iSignalSema(topSema);
  return priority;
}

int iSuspendThread(int id) {
  register int result __asm__("$2");
  int current;
  unsigned index;
  __asm__ volatile("li $3,-47\n\tsyscall" : "=r"(result) : : "$3");
  current = result;
  if (current != id)
    return _iSuspendThread(id);
  if ((unsigned)current >= 256 || !topId)
    return -1;
  index = topArg.write & 511;
  topArg.write = index + 1;
  topArg.entries[index].operation = 2;
  topArg.entries[index].argument = current;
  iSignalSema(topSema);
  return current;
}
