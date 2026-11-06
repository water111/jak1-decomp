#ifndef NEXT_KERNEL_KBOOT_H
#define NEXT_KERNEL_KBOOT_H

// Language enum shared with GOAL.
enum GoalLanguage {
  GOAL_LANG_ENGLISH = 0,
  GOAL_LANG_FRENCH = 1,
  GOAL_LANG_GERMAN = 2,
  GOAL_LANG_SPANISH = 3,
  GOAL_LANG_ITALIAN = 4,
  GOAL_LANG_JAPANESE = 5
};

// Configuration for the game.
// This matches the configuration settings the Sony Demo SDK
// provides to each game within a shared demo.
struct KernelConfig {
  unsigned short language;
  unsigned short aspect;
  unsigned short playmode;
  unsigned short inactive_timeout;
  unsigned short timeout;
  unsigned short media_type;
  unsigned short volume;
  unsigned short directory_sector_num;
};

extern unsigned int MasterExit;
extern unsigned int MasterDebug;
extern unsigned int DebugSegment;
#if REVISION >= 20011019
extern unsigned int DiskBoot;
#endif
extern char DebugBootLevel[64];
#if REVISION >= 20010920
extern char DebugBootMessage[64];
extern KernelConfig MasterConfig;
#endif

void KernelCheckAndDispatch();
#if REVISION >= 20010920
void KernelShutdown();
#endif

#endif
