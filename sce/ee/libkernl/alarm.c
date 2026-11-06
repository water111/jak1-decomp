typedef void (*AlarmCallback)(int, unsigned short, void*);

typedef struct {
  unsigned short time;
  short id;
  void* callback;
  char* argument;
  unsigned gp;
} Alarm;

typedef struct {
  int number;
  void* address;
} AlarmEntry;

extern AlarmEntry alarm_entries[6];
extern int alarm_count;
extern unsigned long alarm_ids;
extern Alarm alarms[64] __attribute__((section(".bss")));

void* alarm_entry(int number) {
  unsigned i;
  for (i = 0; i < 6; i++)
    if (number == alarm_entries[i].number)
      return alarm_entries[i].address;
  return 0;
}

int alarm_time(int now, int deadline) {
  if (deadline < now)
    return deadline | 0x10000;
  return deadline;
}

int alarm_insert(int now, int deadline) {
  int i, j;
  for (i = 0; i < alarm_count; i++) {
    if (deadline < alarm_time(now, alarms[i].time)) {
      for (j = alarm_count - 1; j >= i; j--)
        alarms[j + 1] = alarms[j];
      break;
    }
  }
  return i;
}

extern void alarm_update(unsigned);

extern __inline__ int alarm_new_id(void) {
  int i;
  for (i = 0; i < 64; i++) {
    if (!((alarm_ids >> i) & 1)) {
      alarm_ids |= 1UL << i;
      return i;
    }
  }
  return -1;
}

int alarm_allocate(int delay, AlarmCallback callback, void* argument) {
  int deadline = delay & 0xffff;
  int now = *(volatile unsigned*)0xb0001800;
  int id, i;
  void* gp;
  deadline += now;
  if (alarm_count >= 64)
    return -1;
  id = alarm_new_id();
  if (id < 0)
    return id;
  __asm__ volatile("move %0,$28" : "=r"(gp));
  i = alarm_insert(now, deadline);
  alarms[i].time = deadline;
  alarms[i].id = id;
  alarms[i].callback = (void*)callback;
  alarms[i].argument = (char*)argument;
  alarms[i].gp = (unsigned)gp;
  alarm_count++;
  alarm_update(alarms[0].time);
  return alarms[i].id;
}

extern void alarm_disable(void);

int alarm_cancel(int id) {
  int result = -1;
  int i, j;
  if (alarm_count > 0) {
    for (i = 0; i < alarm_count; i++) {
      if (id == alarms[i].id) {
        for (j = i; j < alarm_count - 1; j++)
          alarms[j] = alarms[j + 1];
        alarm_count--;
        alarm_ids &= ~(1UL << id);
        if (i == 0)
          alarm_update(alarms[0].time);
        if (alarm_count == 0)
          alarm_disable();
        result = id;
        break;
      }
    }
  }
  __asm__ volatile("sync");
  return result;
}

int alarm_set(unsigned short delay, AlarmCallback callback, void* argument) {
  int result = alarm_allocate(delay, callback, argument);
  __asm__ volatile("sync");
  return result;
}

void alarm_update(unsigned time) {
  *(volatile unsigned*)0xb0001820 = time;
  *(volatile unsigned*)0xb0001810 = 0x583;
  if (!(*(volatile unsigned*)0x1000f010 & 0x1000))
    *(volatile unsigned*)0x1000f010 |= 0x1000;
}

void alarm_disable(void) {
  *(volatile unsigned*)0xb0001810 = 0x503;
  if (*(volatile unsigned*)0x1000f010 & 0x1000)
    *(volatile unsigned*)0x1000f010 |= 0x1000;
}

extern void alarm_call_user(void*, void*, int, unsigned, void*);

void alarm_handler(void) {
  Alarm alarm;
  int i;
  void* gp;
  do {
    alarm = alarms[0];
    alarm_count--;
    for (i = 0; i < alarm_count; i++)
      alarms[i] = alarms[i + 1];
    __asm__ volatile("move %0,$28" : "=r"(gp));
    __asm__ volatile("move $28,%0" : : "r"(alarm.gp));
    alarm_call_user((void*)0x82000, alarm.callback, alarm.id, alarm.time, alarm.argument);
    __asm__ volatile("move $28,%0" : : "r"(gp));
    alarm_ids &= ~(1UL << alarm.id);
  } while (alarm_count > 0 && alarm.time == alarms[0].time);
  if (alarm_count > 0)
    alarm_update(alarms[0].time);
  __asm__ volatile("sync\n\tei");
}

__asm__(
    ".text\n.align 6\n.set noreorder\n.set noat\n"
    ".globl alarm_call_user\nalarm_call_user:\n"
    "lui $26,%hi(alarm_saved_ra)\nsw $31,%lo(alarm_saved_ra)($26)\n"
    "lui $26,%hi(alarm_saved_sp)\nsw $29,%lo(alarm_saved_sp)($26)\n"
    "mtc0 $4,$14\nsync.p\n"
    "move $3,$5\nmove $4,$6\nmove $5,$7\nmove $6,$8\n"
    "mfc0 $26,$12\nori $26,$26,0x12\nmtc0 $26,$12\nsync.p\neret\nnop\n"
    ".globl alarm_return\nalarm_return:\n"
    "mfc0 $1,$12\nli $26,-28\nand $1,$1,$26\nmtc0 $1,$12\nsync.p\n"
    "lui $26,%hi(alarm_saved_ra)\nlw $31,%lo(alarm_saved_ra)($26)\n"
    "lui $26,%hi(alarm_saved_sp)\njr $31\nlw $29,%lo(alarm_saved_sp)($26)\n"
    ".set at\n.set reorder");

extern void alarm_return(void);
int alarm_count = 0;
unsigned long alarm_ids = 0;
AlarmEntry alarm_entries[6] = {{24, alarm_set},    {30, alarm_set},      {25, alarm_cancel},
                               {31, alarm_cancel}, {300, alarm_handler}, {8, alarm_return}};
Alarm alarms[64] __attribute__((section(".bss")));
unsigned alarm_saved_ra[4] __attribute__((section(".bss")));
unsigned alarm_saved_sp[4] __attribute__((section(".bss")));
