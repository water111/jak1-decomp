extern int DIntr(void), EIntr(void);
extern void FlushCache(int);
extern int kprintf(const char*, ...);
extern int sceDeci2Open(unsigned short, void*, void (*)(int, int, void*));
extern int sceDeci2ExRecv(int, void*, unsigned short);
extern int sceDeci2ExSend(int, const void*, unsigned short);
extern int sceDeci2ReqSend(int, char);
extern void sceDeci2Poll(int);

typedef struct {
  int capacity;
  int count;
  unsigned char *read, *write;
  unsigned char bytes[256];
} Queue;

typedef struct {
  unsigned short length, reserved, protocol;
  char source, destination;
  unsigned channel;
} TtyHeader;

typedef struct {
  volatile int descriptor;
  volatile unsigned write_size, read_size;
  volatile int writing;
  unsigned char *write, *read;
  Queue* queue;
} TtyInfo;

static Queue* QueueInit(int size) {
  static Queue q;
  q.capacity = size;
  q.count = 0;
  q.write = q.bytes;
  q.read = q.bytes;
  return &q;
}

static void QueuePeekWriteDone(Queue* q) {
  q->count++;
  q->write++;
  if (q->write == q->bytes + q->capacity)
    q->write = q->bytes;
}

static void QueuePeekReadDone(Queue* q) {
  q->count--;
  q->read++;
  if (q->read == q->bytes + q->capacity)
    q->read = q->bytes;
}

static TtyInfo tinfo;
static unsigned char wbuf[320] __attribute__((aligned(64)));
static unsigned char rbuf[320] __attribute__((aligned(64)));

static void sceTtyHandler(int event, int incoming, void* argument) {
  TtyInfo* info = argument;
  register int size __asm__("$16") = incoming;
  TtyHeader* header;
  int i;
  switch (event) {
    case 1:
    case 2:
      if (size) {
        if (info->read_size + size > 320)
          kprintf("TTY: packet size larger than expect\n");
        size = sceDeci2ExRecv(info->descriptor, info->read + info->read_size, size);
        if (size < 0)
          kprintf("TTY: receive error");
        info->read_size += size;
      } else {
        header = (TtyHeader*)info->read;
        for (i = 12; i < header->length; i++) {
          *info->queue->write = info->read[i];
          QueuePeekWriteDone(info->queue);
        }
        info->read_size = 0;
      }
      break;
    case 3: {
      int sent;
      sent = sceDeci2ExSend(info->descriptor, info->write, info->write_size);
      if (sent < 0) {
        kprintf("TTY: send err %d\n", sent);
        info->writing = 0;
      } else {
        info->write += sent;
        info->write_size -= sent;
      }
      break;
    }
    case 4:
      if (info->write_size)
        kprintf("TTY: err ti->wlen=%08x\n", info->write_size);
      info->writing = 0;
      break;
  }
}

int sceTtyWrite(const char* source, int size) {
  int written = 0, consumed = 0;
  TtyHeader* header;
  char* out;
  if (tinfo.writing)
    return -1;
  DIntr();
  header = (TtyHeader*)((unsigned)wbuf | 0x20000000);
  tinfo.writing = 1;
  tinfo.write = (unsigned char*)header;
  out = (char*)(header + 1);
  while (size-- != 0) {
    if (*source == '\n') {
      *out++ = '\r';
      if (++written >= 256)
        break;
    }
    *out++ = *source++;
    written++;
    consumed++;
    if (written >= 256)
      break;
  }
  tinfo.write_size = written + 12;
  header->length = tinfo.write_size;
  if (sceDeci2ReqSend(tinfo.descriptor, header->destination) < 0) {
    tinfo.writing = 0;
    EIntr();
    return -1;
  }
  while (tinfo.writing)
    sceDeci2Poll(tinfo.descriptor);
  EIntr();
  return consumed;
}

int sceTtyRead(char* buffer, int size) {
  int i;
  for (i = 0; i < size; i++) {
    while (!((volatile Queue*)tinfo.queue)->count) {
    }
    buffer[i] = *tinfo.queue->read;
    QueuePeekReadDone(tinfo.queue);
    if (buffer[i] == '\n' || buffer[i] == '\r')
      return i + 1;
  }
  return i;
}

int sceTtyInit(void) {
  TtyHeader* header;
  FlushCache(0);
  tinfo.descriptor = sceDeci2Open(0x210, (void*)&tinfo, sceTtyHandler);
  if (tinfo.descriptor < 0)
    return 0;
  tinfo.writing = 0;
  tinfo.write_size = 0;
  tinfo.read_size = 0;
  tinfo.read = (unsigned char*)((unsigned)rbuf | 0x20000000);
  tinfo.write = (unsigned char*)((unsigned)wbuf | 0x20000000);
  header = (TtyHeader*)tinfo.write;
  header->protocol = 0x210;
  header->source = 'E';
  header->destination = 'H';
  header->reserved = 0;
  header->channel = 0;
  tinfo.queue = QueueInit(256);
  return 1;
}
