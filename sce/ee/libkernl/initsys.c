extern void InitAlarm(void);
extern void InitThread(void);

void _InitSys(void) {
  InitAlarm();
  InitThread();
}
