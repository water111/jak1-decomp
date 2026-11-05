#ifndef SCE_IOP_989SND_TIMER_H
#define SCE_IOP_989SND_TIMER_H

struct SoundTimerInfo {
  int* thread_id;
  unsigned int compare;
};

extern int gKillTimer;
extern int gTimerThreadID;
extern volatile int gTimerID;
int snd_InstallSoundTick(void);
void snd_RemoveSoundTick(void);
int snd_TimerLLTick(struct SoundTimerInfo* timer);
int snd_TimerLLIRQLoop(void);
#endif
