#include "overlord/iso.h"
#include "overlord/ramdisk.h"
#include "overlord/sbank.h"
#include "overlord/srpc.h"
#include "overlord/ssound.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/vblank/vblank.h"

// OVERLORD module entry point.
char* gLanguage = "ENG";

int start(int argc, const char** argv) {
  struct ThreadParam thread;
  int server, player, loader;

  // Initialize SIF/RPC
  FlushDcache();
  CpuEnableIntr();
  if (!sceSifCheckInit())
    sceSifInit();
  sceSifInitRpc(0);

  // Initialize OVERLORD stuff
  InitBanks();
  InitSound();
  InitRamdisk();

  RegisterVblankHandler(0, 0x20, VBlank_Handler, 0);

  // Create threads
  thread.attr = TH_C;
  thread.entry = Thread_Server;
  thread.initPriority = 98;
  thread.stackSize = 0x800;
  thread.option = 0;
  server = CreateThread(&thread);
  if (server <= 0)
    return 1;

  thread.attr = TH_C;
  thread.entry = Thread_Player;
  thread.initPriority = 96;
  thread.stackSize = 0x800;
  thread.option = 0;
  player = CreateThread(&thread);
  if (player <= 0)
    return 1;

  thread.attr = TH_C;
  thread.entry = Thread_Loader;
  thread.initPriority = 99;
  thread.stackSize = 0x1000;
  thread.option = 0;
  loader = CreateThread(&thread);
  if (loader <= 0)
    return 1;

#if REVISION <= 20010903
  InitISOFS(argc >= 2 ? argv[1] : "");
#else
  InitISOFS(argv[1], argv[2]);
#endif

  StartThread(server, 0);
  StartThread(player, 0);
  StartThread(loader, 0);
  return 0;
}

void ExitIOP(void) {
  for (;;) {
  }
}
