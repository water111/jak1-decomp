#ifndef SCE_IOP_XTIMRMAN_H
#define SCE_IOP_XTIMRMAN_H
int AllocHardTimer(int source, int size, int prescale);
int SetTimerHandler(int timer_id, unsigned int compare, int (*handler)(void*), void* arg);
int SetupHardTimer(int timer_id, int source, int mode, int prescale);
int StartHardTimer(int timer_id);
int StopHardTimer(int timer_id);
int FreeHardTimer(int timer_id);
#endif
