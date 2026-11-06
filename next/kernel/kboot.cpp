// Launch the GOAL Kernel.
#include "next/kernel/kboot.h"
#include "next/kernel/klisten.h"
#include "next/kernel/kmachine.h"
#include "next/kernel/kscheme.h"
#include "next/kernel/ksocket.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/libscedemo/libscedemo.h"
#include "sce/ee/libscf/libscf.h"
#include "sce/ee/newlib/string.h"

// Flag set to stop kernel loop
unsigned int MasterExit = 0;
// Flag set to enable debug/development features (mostly the listener)
unsigned int MasterDebug = 1;
// Flag set to load the debug segment of GOAL objects
unsigned int DebugSegment = 1;

#if REVISION >= 20011019
// Flag set to automatically boot GOAL entirely from Disk (no developer machine
// connection)
unsigned int DiskBoot = 0;
#endif
// Arguments passed to game on boot
char DebugBootLevel[64] = "#f";

#if REVISION < 20010920
// No DebugBootMessage
#elif defined(SHARED_DEMO)
char DebugBootMessage[64] = "demo-shared";
#elif defined(DEMO)
char DebugBootMessage[64] = "demo";
#else
char DebugBootMessage[64] = "play";
#endif

#if REVISION < 20010920
// The simplest main, before it was filled with settings junk.
int main(int argc, char** argv) {
  init_crc();
  InitParms(argc, argv);
  MasterDebug = 0;
  DebugSegment = 0;
  if (InitMachine() >= 0) {
    KernelCheckAndDispatch();
    ShutdownMachine();
  }
  return 0;
}
#else

// Configure and launch the kernel.
int main(int argc, char** argv) {
  init_crc();

#ifdef SHARED_DEMO
  // In demo mode, get paramters from the demo library
  // and translate them to GOAL (language) and SCE (aspect)
  sceDemoStart(argc, argv, &MasterConfig.language, &MasterConfig.aspect, &MasterConfig.playmode,
               &MasterConfig.inactive_timeout, &MasterConfig.timeout, &MasterConfig.media_type,
               &MasterConfig.volume, &MasterConfig.directory_sector_num);

  // Different language enum for sceDemoStart than for sceScfGetLanguage...
  // and missing breaks in 3 builds!
  switch (MasterConfig.language) {
    case SCE_DEMO_LANGUAGE_FRENCH:
      MasterConfig.language = GOAL_LANG_FRENCH;
#if REVISION >= 20011019
      break;
#endif
    case SCE_DEMO_LANGUAGE_GERMAN:
      MasterConfig.language = GOAL_LANG_GERMAN;
#if REVISION >= 20011019
      break;
#endif
    case SCE_DEMO_LANGUAGE_SPANISH:
      MasterConfig.language = GOAL_LANG_SPANISH;
#if REVISION >= 20011019
      break;
#endif
    case SCE_DEMO_LANGUAGE_ITALIAN:
      MasterConfig.language = GOAL_LANG_ITALIAN;
#if REVISION >= 20011019
      break;
#endif
    case SCE_DEMO_LANGUAGE_JAPANESE:
      MasterConfig.language = GOAL_LANG_JAPANESE;
#if REVISION >= 20011019
      break;
#endif
    default:
      MasterConfig.language = GOAL_LANG_ENGLISH;
#if REVISION >= 20011019
      break;
#endif
  }

  switch (MasterConfig.aspect) {
    case SCE_DEMO_ASPECT_43:
      MasterConfig.aspect = SCE_ASPECT_43;
#if REVISION >= 20011019
      break;
#endif
    case SCE_DEMO_ASPECT_169:
      MasterConfig.aspect = SCE_ASPECT_169;
#if REVISION >= 20011019
      break;
#endif
    default:
      MasterConfig.aspect = SCE_ASPECT_FULL;
#if REVISION >= 20011019
      break;
#endif
  }

#else
  // Otherwise, get from PS2 settings and defaults:
  MasterConfig.aspect = sceScfGetAspect();
  MasterConfig.language = sceScfGetLanguage();
  MasterConfig.inactive_timeout = 0;
  MasterConfig.timeout = 0;
  MasterConfig.volume = 100;

  switch (MasterConfig.language) {
    case SCE_FRENCH_LANGUAGE:  // 2
      MasterConfig.language = GOAL_LANG_FRENCH;
#if REVISION >= 20011031
      break;
#endif
    case SCE_GERMAN_LANGUAGE:  // 4
      MasterConfig.language = GOAL_LANG_GERMAN;
#if REVISION >= 20011031
      break;
#endif
    case SCE_SPANISH_LANGUAGE:  // 3
      MasterConfig.language = GOAL_LANG_SPANISH;
#if REVISION >= 20011031
      break;
#endif
    case SCE_ITALIAN_LANGUAGE:  // 5
      MasterConfig.language = GOAL_LANG_ITALIAN;
#if REVISION >= 20011031
      break;
#endif
// Japanese language is blocked from non-JP builds on 20011031
#if REVISION < 20011031 || defined(REGION_JP)
    case SCE_JAPANESE_LANGUAGE:  // 0
      MasterConfig.language = GOAL_LANG_JAPANESE;
#if REVISION >= 20011031
      break;
#endif
#endif
    default:
      MasterConfig.language = GOAL_LANG_ENGLISH;
#if REVISION >= 20011031
      break;
#endif
  }

    // japanese forced in japan.
#ifdef REGION_JP
  MasterConfig.language = GOAL_LANG_JAPANESE;
#endif
#endif  // not demo

#ifdef SHARED_DEMO
  if (MasterConfig.playmode != SCE_DEMO_PLAYMODE_PLAYABLE) {
    sceDemoEnd(SCE_DEMO_ENDREASON_ATTRACT_COMPLETE);
    return 0;
  }
  MasterConfig.volume = MasterConfig.volume * 10;
#if REVISION < 20011019
  MasterConfig.language = GOAL_LANG_ENGLISH;
#endif
#endif

  if (!strcmp(DebugBootMessage, "demo") || !strcmp(DebugBootMessage, "demo-shared")) {
    MasterConfig.aspect = SCE_ASPECT_FULL;
  }

  // Override defaults for retail:
#ifdef RETAIL
  MasterDebug = 0;
#if REVISION >= 20011019
  DiskBoot = 1;
#endif
  DebugSegment = 0;
#endif

  // Run!
  if (InitMachine() >= 0) {
    KernelCheckAndDispatch();
    ShutdownMachine();
#ifdef SHARED_DEMO
    sceDemoEnd(SCE_DEMO_ENDREASON_PLAYABLE_QUIT);
#endif
  }
  return 0;
}
#endif

// Process listener messages and run one GOAL dispatch per iteration.
void KernelCheckAndDispatch() {
  while (MasterExit == 0) {
    // Poll for message from listener.
    char* message = WaitForMessageAndAck();
    if (message) {
      ProcessListenerMessage(message);
    }
    unsigned int old_listener = *ListenerFunction;
    void (*dispatcher)() = *kernel_dispatcher;

    // Run the GOAL kernel
    GOAL_CALL(dispatcher);

    // Check to see if the GOAL kernel processed the listener and
    // ack if so.
    ClearPending();
    if (MasterDebug && *ListenerFunction != old_listener) {
      SendAck();
    }
  }
}

// Set the shutdown flag
#if REVISION >= 20010920
void KernelShutdown() {
  MasterExit = 1;
}

// Needs to be at the end to match.
KernelConfig MasterConfig = {0};
#endif
