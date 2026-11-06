// Machine startup and GOAL interfaces to Sony library functions.
#include "next/kernel/kmachine.h"
#include "next/kernel/fileio.h"
#include "next/kernel/kboot.h"
#include "next/kernel/kdgo.h"
#include "next/kernel/kdsnetm.h"
#include "next/kernel/klink.h"
#include "next/kernel/kprint.h"
#include "next/kernel/kscheme.h"
#include "next/kernel/ksocket.h"
#include "next/kernel/ksound.h"
#include "next/kernel/memory_layout.h"
#include "sce/ee/eeregs.h"
#include "sce/ee/libcdvd/libcdvd.h"
#include "sce/ee/libdma/libdma.h"
#include "sce/ee/libgraph/libgraph.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/libkernl/sif.h"
#include "sce/ee/libkernl/sifdev.h"
#include "sce/ee/libmc/libmc.h"
#include "sce/ee/libpad/libpad.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/libscf/libscf.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/stdlib.h"
#include "sce/ee/newlib/string.h"

#ifdef SHARED_DEMO
#define CDROM_DRIVER_DIRECTORY "cdrom0:\\NAUGHTY.DOG\\DRIVERS\\"
#else
#define CDROM_DRIVER_DIRECTORY "cdrom0:\\DRIVERS\\"
#endif
#define CDROM_DRIVER_PATH(filename) CDROM_DRIVER_DIRECTORY filename ";1"
#if REVISION < 20010920
#define IOP_KERNEL_PATH "cdrom:\\DRIVERS\\IOPRP221.IMG;1"
#else
#define IOP_KERNEL_PATH CDROM_DRIVER_PATH("IOPRP221.IMG")
#endif

// command-line arguments to pass to Overlord to select file-loading mode.
// fakeiso/deviso are not included in retail, but are likely ways to run without
// a actual DVD. iso_cd is the normal one for ISO-filesystem CD or DVD.
#if REVISION < 20010920
struct init_type {
  char* name;
  int length;
};

init_type init_types[] = {{"fakeiso", 8}, {"deviso", 7}, {"iso_cd", 7}};
#else
char* init_types[] = {"fakeiso", "deviso", "iso_cd"};
#endif
// default to the real CD/DVD.
static OverlordDataSource isodrv = iso_cd;
// if set, load IOP modules from CD/DVD, otherwise load from development
// machine.
static unsigned int modsrc = 1;
// if set, reboot IOP using the IOP Kernel from CD/DVD. Otherwise, reboot using
// IOP Kernel from development machine.
static unsigned int reboot = 1;

// DMA buffer for gamepad data (from IOP)
unsigned char pad_dma_buf[4 * SCE_PAD_DMA_BUFFER_SIZE] __attribute__((aligned(64))) = {0};

// Parse command line arguments:
// -cd loads files, IOP modules, and the IOP kernel from CD/DVD.
// -cddata loads game data from CD/DVD and IOP modules/kernel from the
// development host.
// -demo passes the demo boot message to GOAL.
// -deviso loads all data from the development host through the deviso driver.
// -fakeiso loads all data from the development host through the fakeiso driver.
// -boot disables debugging and boots automatically from disk files.
// -debug enables debugging and loads GOAL debug segments.
// -debug-mem enables debugging without loading GOAL debug segments.
// -level <name> boots into the named level.
void InitParms(int argc, char** argv) {
  while (argc) {
    if (!strcmp(*argv, "-cd")) {
      Msg(6, "dkernel: cd mode\n");
      isodrv = iso_cd;
      modsrc = 1;
      reboot = 1;
    } else if (!strcmp(*argv, "-cddata")) {
      Msg(6, "dkernel: cddata mode\n");
      isodrv = iso_cd;
      modsrc = 0;
      reboot = 0;
#if REVISION >= 20010920
    } else if (!strcmp(*argv, "-demo")) {
      Msg(6, "dkernel: demo mode\n");
      kstrcpy(DebugBootMessage, "demo");
#endif
    } else if (!strcmp(*argv, "-deviso")) {
      Msg(6, "dkernel: deviso mode\n");
      isodrv = deviso;
      modsrc = 0;
      reboot = 0;
    } else if (!strcmp(*argv, "-fakeiso")) {
      Msg(6, "dkernel: fakeiso mode\n");
      isodrv = fakeiso;
      modsrc = 0;
      reboot = 0;
    } else if (!strcmp(*argv, "-boot")) {
      Msg(6, "dkernel: boot mode\n");
      MasterDebug = 0;
#if REVISION >= 20011019
      DiskBoot = 1;
#endif
      DebugSegment = 0;
    } else if (!strcmp(*argv, "-debug")) {
      Msg(6, "dkernel: debug mode\n");
      MasterDebug = 1;
      DebugSegment = 1;
    } else if (!strcmp(*argv, "-debug-mem")) {
      Msg(6, "dkernel: debug-mem mode\n");
      MasterDebug = 1;
      DebugSegment = 0;
    } else if (!strcmp(*argv, "-level") && argc > 1) {
      Msg(6, "dkernel: level %s\n", argv[1]);
      kstrcpy(DebugBootLevel, argv[1]);
      argc--;
      argv++;
    }
    argc--;
    argv++;
  }
  fflush(stdout);
}

// Initialize the CD drive and wait for the disk to be detected.
void InitCD() {
  int result;
  printf("Initializing CD drive\nThis may take a while ...\n");
  sceCdInit(SCECdINIT);
#ifdef MEDIA_DVD
  sceCdMmode(SCECdDVD);

#else
  sceCdMmode(SCECdCDROM);
#endif
  while ((result = sceCdDiskReady(0)) == SCECdNotReady) {
    printf("Drive not ready ... insert a disk!\n");
  }
  result = sceCdGetDiskType();
  printf("Disk type %d\n", result);
}

// Reboot the IOP, load its modules, and pass OVERLORD the splash filename.
void InitIOP() {
  int result;
#if REVISION >= 20010920
  char boot_command[256];
  char* command;
#endif
  sceSifInitRpc(0);
  // Set up CD/DVD drive only if we need it.
  if (isodrv == iso_cd || modsrc == 1 || reboot == 1) {
    InitCD();
  }
  if (!reboot) {
    printf("Rebooting IOP...\n");
    while (!sceSifRebootIop("host0:/usr/local/sce/iop/modules/ioprp221.img")) {
      printf("Failed, retrying...\n");
    }
    while (!sceSifSyncIop())
      printf("Syncing...\n");
  } else {
    printf("Rebooting IOP...\n");
    while (!sceSifRebootIop(IOP_KERNEL_PATH)) {
      printf("Failed, retrying...\n");
    }
    while (!sceSifSyncIop())
      printf("Syncing...\n");
  }
  // If the CD drive is still needed, set it up again after rebooting IOP.
  sceSifInitRpc(0);
  if (isodrv == iso_cd || modsrc == 1) {
    InitCD();
    sceFsReset();
  }
  // Set up the arguments passed to OVERLORD.
#if REVISION >= 20010920
  command = boot_command;
  kstrcpy(command, init_types[isodrv]);
  command += strlen(command) + 1;

  // pick splash screen based on region and language selection.
#ifdef REGION_EU
  switch (MasterConfig.language) {
    case GOAL_LANG_FRENCH:
      kstrcpy(command, "SCREEN1.FRE");
      break;
    case GOAL_LANG_GERMAN:
      kstrcpy(command, "SCREEN1.GER");
      break;
    case GOAL_LANG_ITALIAN:
      kstrcpy(command, "SCREEN1.ITA");
      break;
    case GOAL_LANG_SPANISH:
      kstrcpy(command, "SCREEN1.SPA");
      break;
    default:
      kstrcpy(command, "SCREEN1.EUR");
      break;
  }
#elif defined(REGION_JP)
  kstrcpy(command, "SCREEN1.JAP");
#else
  kstrcpy(command, "SCREEN1.USA");
#endif
  command += strlen(command) + 1;
#endif

  // load modules
  if (!modsrc) {
    if (sceSifLoadModule("host0:/usr/local/sce/iop/modules/sio2man.irx", 0, 0) < 0) {
      MsgErr("loading sio2man.irx failed\n");
      exit(0);
    }
    if (sceSifLoadModule("host0:/usr/local/sce/iop/modules/padman.irx", 0, 0) < 0) {
      MsgErr("loading padman.irx failed\n");
      exit(0);
    }
    if (sceSifLoadModule("host0:/usr/local/sce/iop/modules/libsd.irx", 0, 0) < 0) {
      MsgErr("loading libsd.irx failed\n");
      exit(0);
    }
#ifndef DEMO
    if (sceSifLoadModule("host0:/usr/local/sce/iop/modules/mcman.irx", 0, 0) < 0) {
      MsgErr("loading mcman.irx failed\n");
      exit(0);
    }
    if (sceSifLoadModule("host0:/usr/local/sce/iop/modules/mcserv.irx", 0, 0) < 0) {
      MsgErr("loading mcserv.irx failed\n");
      exit(0);
    }
#endif
#if REVISION < 20011019
    if (sceSifLoadModule("host0:/usr/home/src/989snd9/iop/989snd_d.irx", 0, 0) < 0) {
      MsgErr("loading 989snd_d.irx failed\n");
#else
    if (sceSifLoadModule("host0:/usr/home/src/989snd10/iop/989snd.irx", 0, 0) < 0) {
      MsgErr("loading 989snd.irx failed\n");
#endif
      exit(0);
    }
#if REVISION >= 20011019
    sceSifLoadModule("host0:/usr/home/src/989snd10/iop/989ERR.irx", 0, 0);
#endif
#if REVISION < 20010920
    printf("Initializing CD library in %s mode\n", init_types[isodrv].name);
    if (sceSifLoadModule("host0:binee/overlord.irx", init_types[isodrv].length,
                         init_types[isodrv].name) < 0) {
#else
    printf("Initializing CD library\n");
    if (sceSifLoadModule("host0:binee/overlord.irx", command - boot_command, boot_command) < 0) {
#endif
      MsgErr("loading overlord.irx failed\n");
      exit(0);
    }
  } else {
    if (sceSifLoadModule(CDROM_DRIVER_PATH("SIO2MAN.IRX"), 0, 0) < 0) {
      MsgErr("loading sio2man.irx failed\n");
      exit(0);
    }
    if (sceSifLoadModule(CDROM_DRIVER_PATH("PADMAN.IRX"), 0, 0) < 0) {
      MsgErr("loading padman.irx failed\n");
      exit(0);
    }
    if (sceSifLoadModule(CDROM_DRIVER_PATH("LIBSD.IRX"), 0, 0) < 0) {
      MsgErr("loading libsd.irx failed\n");
      exit(0);
    }
#ifndef DEMO
    if (sceSifLoadModule(CDROM_DRIVER_PATH("MCMAN.IRX"), 0, 0) < 0) {
      MsgErr("loading mcman.irx failed\n");
      exit(0);
    }
    if (sceSifLoadModule(CDROM_DRIVER_PATH("MCSERV.IRX"), 0, 0) < 0) {
      MsgErr("loading mcserv.irx failed\n");
      exit(0);
    }
#endif
    if (sceSifLoadModule(CDROM_DRIVER_PATH("989SND.IRX"), 0, 0) < 0) {
      MsgErr("loading 989snd.irx failed\n");
      exit(0);
    }
#if REVISION < 20010920
    printf("Initializing CD library in ISO_CD mode\n");
    if (sceSifLoadModule(CDROM_DRIVER_PATH("OVERLORD.IRX"), init_types[isodrv].length,
                         init_types[isodrv].name) < 0) {
#else
    printf("Initializing CD library in ISO_CD mode\n");
    if (sceSifLoadModule(CDROM_DRIVER_PATH("OVERLORD.IRX"), command - boot_command, boot_command) <
        0) {
#endif
      MsgErr("loading overlord.irx failed\n");
      exit(0);
    }
  }

#ifndef DEMO
  // Initialize memory card driver.
  result = sceMcInit();
  if (result < 0) {
    MsgErr("MC driver init failed %d\n", result);
    exit(0);
  }
#endif
  printf("InitIOP OK\n");
}

// Upload the splash image prepared by OVERLORD and enable its display.
#if REVISION >= 20010920
void InitVideo() {
  if (GAME_TERRITORY == GAME_TERRITORY_SCEE) {
    sceGsResetGraph(0, GS_INTERLACED, GS_PAL, 0);
  } else {
    sceGsResetGraph(0, GS_INTERLACED, GS_NTSC, 0);
  }
  void* gs_registers = (void*)GS_REGISTER_BASE;
  int width = 512;
  int height = 224;
  int buffer_width = (width + 63) / 64;
  int framebuffer = 352;
  int pixel_format = GS_PSMCT32;
  int x = 0;
#ifdef VIDEO_PAL
  int y = 24;
#else
  int y = 8;
#endif
  sceGsLoadImage image;
  unsigned int* pixels = (unsigned int*)SPLASH_IMAGE_ADDRESS;
  sceGsDispEnv display;
  sceGsSetDefLoadImage(&image, framebuffer * 32, buffer_width, pixel_format, 0, 0, width, height);
  FlushCache(WRITEBACK_DCACHE);
  sceGsExecLoadImage((GsQuad*)&image, (GsQuad*)pixels);
  sceGsSetDefLoadImage(&image, framebuffer * 32, buffer_width, pixel_format, 0, height, width,
                       height);
  FlushCache(WRITEBACK_DCACHE);
  sceGsExecLoadImage((GsQuad*)&image, (GsQuad*)(pixels + width * height));
  memset(&display, 0, sizeof(display));
  display.pmode.EN2 = 0;
  display.pmode.MMOD = 1;
  display.pmode.AMOD = 1;
  display.smode2.INT = 1;
  display.smode2.FFMD = 0;
  display.dispfb.FBP = framebuffer;
  display.dispfb.FBW = buffer_width;
  display.dispfb.PSM = pixel_format;
  display.dispfb.DBX = 0;
  display.dispfb.DBY = 0;
  display.display.DX = 652 + (2560 / width) * x;
  display.display.DY = y + 50;
  display.display.MAGH = (width + 2559) / width - 1;
  display.display.MAGV = 0;
  display.display.DW = 2559;
  display.display.DH = height * 2 - 1;
  display.bgcolor.R = 0;
  display.bgcolor.G = 0;
  display.bgcolor.B = 0;
  sceGsSyncV(0);
  sceGsPutDispEnv(&display);
  for (int i = 0; i < 6; i++)
    sceGsSyncV(0);
  display.pmode.EN2 = 1;
  sceGsPutDispEnv(&display);
}
#endif

// Initialize GOAL memory and GOAL.
int InitMachine() {
  int unused;
  int result;

  // debug heap ends at the start of the stack.
  unsigned int debug_heap_end =
      ((unsigned int)_stack - (unsigned int)_stack_size + 1) & EE_MEMORY_MASK;

  // get the libc heap address by allocating something small. GOAL will
  // overwrite the libc heap entirely, breaking memory allocation in C.
  void* heap_start = malloc(16);

  // initialize the global and debug heaps.
  kinitheap(kglobalheap, heap_start, GLOBAL_HEAP_END - (unsigned int)heap_start);
  if (MasterDebug) {
    kinitheap(kdebugheap, (void*)DEBUG_HEAP_START, debug_heap_end - DEBUG_HEAP_START);
  } else {
    kdebugheap = 0;
  }

  // initialize message buffers
  init_output();
  // init hardware
  InitIOP();
  sceGsResetPath();
#if REVISION < 20010920
  sceGsResetGraph(0, 1, 2, 1);
#endif
#if REVISION >= 20010920
  InitVideo();
#endif
  FlushCache(WRITEBACK_DCACHE);
  FlushCache(INVALIDATE_ICACHE);
  sceGsSyncV(0);
  if (scePadInit(0) != 1)
    MsgErr("dkernel: !init pad\n");
  if (MasterDebug)
    InitGoalProto();
  printf("InitSound\n");
  InitSound();
  printf("InitRPC\n");
  InitRPC();

  // init GOAL
  reset_output();
  clear_print();
  result = InitHeapAndSymbol();
  if (result < 0)
    return result;

  // init listener
  printf("InitListenerConnect\n");
  InitListenerConnect();
  printf("InitCheckListener\n");
  InitCheckListener();
  Msg(6, "kernel: machine started\n");
  return 0;
}

// Stop IOP services and close the listener.
int ShutdownMachine() {
#if REVISION >= 20010920
  StopIOP();
#endif
  CloseListener();
  ShutdownSound();
  ShutdownGoalProto();
  Msg(6, "kernel: machine shutdown\n");
  return 0;
}

// Flush both caches. The supplied range is ignored.
void CacheFlush(void* memory, int size) {
  FlushCache(WRITEBACK_DCACHE);
  FlushCache(INVALIDATE_ICACHE);
}

// Steps in the controller setup state machine.
enum CPadSetupState {
  CPAD_DETECT = 0,
  CPAD_CHECK_ANALOG = 40,
  CPAD_REQUEST_ANALOG = 41,
  CPAD_WAIT_ANALOG = 42,
  CPAD_CHECK_ACTUATORS = 70,
#if REVISION < 20011019
  CPAD_REQUEST_PRESSURE = 76,
  CPAD_ALIGN_ACTUATORS = 77,
  CPAD_WAIT_PRESSURE = 78,
  CPAD_UNSUPPORTED = 99,
#else
  CPAD_ALIGN_ACTUATORS = 75,
  CPAD_REQUEST_PRESSURE = 76,
  CPAD_WAIT_PRESSURE = 78,
  CPAD_UNSUPPORTED = 90,
#endif
  CPAD_READY = 99
};

enum CPadMode {
  CPAD_DIGITAL_ID = 4,
  CPAD_ANALOG_ID = 7,
  CPAD_ANALOG_MODE = 1,
  CPAD_LOCK_MODE = 3,
  CPAD_QUERY_COUNT = -1,
  CPAD_INVALID = 0x80
};

// Open a controller port and start its configuration state machine.
void* CPadOpen(void* info, int number) {
  CPadInfo* pad = (CPadInfo*)info;
  if (!pad->cpad_file) {
    pad->cpad_file = scePadPortOpen(number, 0, pad_dma_buf + number * SCE_PAD_DMA_BUFFER_SIZE);
    if (pad->cpad_file < 1)
      MsgErr("dkernel: !open cpad #%d (%d)\n", number, pad->cpad_file);
    pad->new_pad = 1;
    pad->state = CPAD_DETECT;
  }
  return pad;
}

// Configure controller modes and actuators, then read the current input packet.
void* CPadGetData(void* info) {
  CPadInfo* pad = (CPadInfo*)info;
  int pad_state = scePadGetState(pad->number, 0);
  int result;
  if (pad_state == scePadStateDiscon)
    pad->state = CPAD_DETECT;
  pad->valid = pad_state | CPAD_INVALID;
  switch (pad->state) {
    case CPAD_DETECT:
      if (pad_state != scePadStateStable && pad_state != scePadStateFindCTP1)
        break;
      {
        int mode = scePadInfoMode(pad->number, 0, InfoModeCurID, 0);
        if (!mode)
          break;
        int extended_mode = scePadInfoMode(pad->number, 0, InfoModeCurExID, 0);
        if (extended_mode > 0)
          mode = extended_mode;
        switch (mode) {
          case CPAD_DIGITAL_ID:
            pad->state = CPAD_CHECK_ANALOG;
            break;
          case CPAD_ANALOG_ID:
            pad->state = CPAD_CHECK_ACTUATORS;
            break;
          default:
            pad->state = CPAD_UNSUPPORTED;
            break;
        }
      }
      break;
    case CPAD_CHECK_ANALOG:
#if REVISION >= 20011019
      pad->actuators_enabled = 0;
#endif
      if (!scePadInfoMode(pad->number, 0, InfoModeIdTable, CPAD_QUERY_COUNT)) {
        pad->state = CPAD_UNSUPPORTED;
        break;
      }
      pad->state = CPAD_REQUEST_ANALOG;
      // Fall through to request analog mode immediately.
    case CPAD_REQUEST_ANALOG:
      if (scePadSetMainMode(pad->number, 0, CPAD_ANALOG_MODE, CPAD_LOCK_MODE) == 1)
        pad->state = CPAD_WAIT_ANALOG;
      break;
    case CPAD_WAIT_ANALOG:
      if (scePadGetReqState(pad->number, 0) == scePadReqStateFailed)
        pad->state = CPAD_REQUEST_ANALOG;
      if (scePadGetReqState(pad->number, 0) == scePadReqStateComplete)
        pad->state = CPAD_DETECT;
      break;
    case CPAD_CHECK_ACTUATORS:
#if REVISION < 20011019
      if (scePadInfoPressMode(pad->number, 0) == 1) {
        pad->state = CPAD_REQUEST_PRESSURE;
        break;
      }
      pad->state = CPAD_READY;
      break;
#else
      result = scePadInfoAct(pad->number, 0, CPAD_QUERY_COUNT, 0);
      if (result > 0) {
        pad->actuators_enabled = 1;
        pad->state = CPAD_ALIGN_ACTUATORS;
        break;
      }
      pad->actuators_enabled = 0;
      pad->state = CPAD_READY;
      break;
#endif
#if REVISION >= 20011019
    case CPAD_ALIGN_ACTUATORS:
      if (scePadSetActAlign(pad->number, 0, pad->align)) {
        if (scePadInfoPressMode(pad->number, 0) == 1) {
          pad->state = CPAD_REQUEST_PRESSURE;
          break;
        }
        pad->state = CPAD_READY;
      }
      break;
#endif
    case CPAD_REQUEST_PRESSURE:
      if (scePadEnterPressMode(pad->number, 0) == 1)
#if REVISION < 20011019
        pad->state = CPAD_ALIGN_ACTUATORS;
#else
        pad->state = CPAD_WAIT_PRESSURE;
#endif
      break;
#if REVISION < 20011019
    case CPAD_ALIGN_ACTUATORS:
      if (scePadSetActAlign(pad->number, 0, pad->align))
        pad->state = CPAD_WAIT_PRESSURE;
      break;
#endif
    case CPAD_WAIT_PRESSURE:
      if (scePadGetReqState(pad->number, 0) == scePadReqStateFailed)
        pad->state = CPAD_REQUEST_PRESSURE;
      if (scePadGetReqState(pad->number, 0) == scePadReqStateComplete)
        pad->state = CPAD_READY;
      break;
#if REVISION >= 20011019
    case CPAD_UNSUPPORTED:
      break;
#endif
    default:
      if (pad_state == scePadStateStable || pad_state == scePadStateFindCTP1) {
        result = scePadRead(pad->number, 0, (unsigned char*)pad);
        if (result && pad_state != scePadStateExecCmd)
          pad->buttons = ~pad->buttons;
#if REVISION < 20011019
        if (pad->status == 0x79)
#else
        if (pad->actuators_enabled)
#endif
          scePadSetActDirect(pad->number, 0, pad->direct);
        pad->valid = pad_state;
      }
      break;
  }
  return pad;
}

// Replace an interrupt handler and enable its interrupt source.
unsigned int InstallHandler(int source, unsigned int function) {
  DisableIntc(source);
  AddIntcHandler(source, (int (*)(int))function, 0);
  EnableIntc(source);
  return function;
}

// Replace a debug exception handler, returning the previous handler.
unsigned int InstallDebugHandler(int source, unsigned int function) {
  function = SetDebugHandler(source, function);
  return function;
}

// Open a GOAL file stream through the host filesystem.
file_stream* kopen(file_stream* stream, gstring* name, symbol* mode) {
  char path[128];
  stream->mode = mode;
  stream->name = name;
  stream->flags = 0;
  printf("****** CALL TO kopen() ******\n");
  sprintf(path, "host:%s", name->data);
  // The error path isn't used.
  if (1) {
    if (!strcmp(((symbol_info*)((char*)mode + SYMBOL_INFO_OFFSET))->name->data, "read")) {
      stream->descriptor = sceOpen(path, SCE_RDONLY);
    } else {
      stream->descriptor = sceOpen(path, SCE_TRUNC | SCE_CREAT | SCE_WRONLY);
    }
  } else {
    stream->flags = 1;
  }
  return stream;
}

// Measure the file and rewind it, setting the error flag if an error occurs.
int klength(file_stream* stream) {
  int length = 0;
  int result;
  if ((stream->flags ^ 1) & 1) {
    length = sceLseek(stream->descriptor, 0, SCE_SEEK_END);
    result = sceLseek(stream->descriptor, 0, SCE_SEEK_SET);
    if (length < 0 || result < 0)
      stream->flags |= 1;
  }
  return length;
}

// Seek, setting the error flag if an error occurs.
int kseek(file_stream* stream, int offset, int where) {
  int result = -1;
  if ((stream->flags ^ 1) & 1) {
    result = sceLseek(stream->descriptor, offset, where);
    if (result < 0)
      stream->flags |= 1;
  }
  return result;
}

// Read, setting the error flag as needed.
int kread(file_stream* stream, void* buffer, int size) {
  int result = -1;
  if ((stream->flags ^ 1) & 1) {
    result = sceRead(stream->descriptor, buffer, size);
    if (result < 0)
      stream->flags |= 1;
  }
  return result;
}

// Write, setting the error flag as needed.
int kwrite(file_stream* stream, void* buffer, int size) {
  int result = -1;
  if ((stream->flags ^ 1) & 1) {
    result = sceWrite(stream->descriptor, buffer, size);
    if (result < 0)
      stream->flags |= 1;
  }
  return result;
}

// Close a stream if there's no error.
file_stream* kclose(file_stream* stream) {
  if ((stream->flags ^ 1) & 1) {
    sceClose(stream->descriptor);
    stream->descriptor = 0xffffffffU;
  }
  stream->flags = 0;
  return stream;
}

// Submit an EE-to-IOP DMA transfer and wait for completion.
unsigned int dma_to_iop(unsigned int source, unsigned int size, unsigned int destination) {
  SifDmaTransfer transfer;
  unsigned int id;
  transfer.source = source;
  transfer.destination = destination;
  transfer.size = size;
  transfer.attributes = 0;
  id = sceSifSetDma(&transfer, 1);
  if (id) {
    while (sceSifDmaStat(id) >= 0) {
    }
  }
  return id == 0;
}

#if REVISION >= 20010920
unsigned int DecodeLanguage() {
  return MasterConfig.language;
}

unsigned int DecodeAspect() {
  return MasterConfig.aspect;
}

unsigned int DecodeVolume() {
  return MasterConfig.volume;
}

unsigned int DecodeTerritory() {
  return GAME_TERRITORY;
}

unsigned int DecodeTimeout() {
  return MasterConfig.timeout;
}

unsigned int DecodeInactiveTimeout() {
  return MasterConfig.inactive_timeout;
}

// Read the RTC and convert a successful result to local time.
sceCdCLOCK* DecodeTime(sceCdCLOCK* clock) {
#if REVISION < 20011019
  clock->status = sceCdReadClock(clock) == 0;
  if (clock->status)
    sceScfGetLocalTimefromRTC(clock);
#elif REVISION < 20011031
  // likely BUG: failing to convert to the local time.
  sceCdReadClock(clock);
#else
  int result = sceCdReadClock(clock);
  if (result)
    sceScfGetLocalTimefromRTC(clock);
#endif
  return clock;
}

// Set the display environment. For some reason, submit the display environment
// 10 times while disabled before submitting the actual display environment.
void PutDisplayEnv(sceGsDispEnv* environment) {
  // used to get line number in variable to match.
  static int count __asm__("count.600") = 10;
  if (count) {
    count--;
    unsigned int enabled = environment->pmode.EN2;
    environment->pmode.EN2 = 0;
    sceGsPutDispEnv(environment);
    environment->pmode.EN2 = enabled;
  } else {
    sceGsPutDispEnv(environment);
#if REVISION >= 20011019
    *GS_DISPFB1 = *(unsigned long long*)&environment->dispfb;
    *GS_DISPLAY1 = *(unsigned long long*)&environment->display;
#endif
  }
}
#endif

// Set up GOAL symbols for kmachine functions and optionally boot the game if
// DiskBoot is enabled.
void InitMachineScheme() {
  DECLARE_SYMBOL_TABLE();
  symbol* sym;

  // set up functions
#if REVISION < 20010920
  make_function_symbol_from_c("put-display-env", (unsigned int (*)())sceGsPutDispEnv);
#else
  make_function_symbol_from_c("put-display-env", (unsigned int (*)())PutDisplayEnv);
#endif
  make_function_symbol_from_c("syncv", (unsigned int (*)())sceGsSyncV);
  make_function_symbol_from_c("sync-path", (unsigned int (*)())sceGsSyncPath);
  make_function_symbol_from_c("reset-path", (unsigned int (*)())sceGsResetPath);
  make_function_symbol_from_c("reset-graph", (unsigned int (*)())sceGsResetGraph);
  make_function_symbol_from_c("dma-sync", (unsigned int (*)())sceDmaSync);
  make_function_symbol_from_c("gs-put-imr", (unsigned int (*)())sceGsPutIMR);
  make_function_symbol_from_c("gs-get-imr", (unsigned int (*)())sceGsGetIMR);
  make_function_symbol_from_c("gs-store-image", (unsigned int (*)())sceGsExecStoreImage);
  make_function_symbol_from_c("flush-cache", (unsigned int (*)())FlushCache);
  make_function_symbol_from_c("cpad-open", (unsigned int (*)())CPadOpen);
  make_function_symbol_from_c("cpad-get-data", (unsigned int (*)())CPadGetData);
  make_function_symbol_from_c("install-handler", (unsigned int (*)())InstallHandler);
  make_function_symbol_from_c("install-debug-handler", (unsigned int (*)())InstallDebugHandler);
  make_function_symbol_from_c("file-stream-open", (unsigned int (*)())kopen);
  make_function_symbol_from_c("file-stream-close", (unsigned int (*)())kclose);
  make_function_symbol_from_c("file-stream-length", (unsigned int (*)())klength);
  make_function_symbol_from_c("file-stream-seek", (unsigned int (*)())kseek);
  make_function_symbol_from_c("file-stream-read", (unsigned int (*)())kread);
  make_function_symbol_from_c("file-stream-write", (unsigned int (*)())kwrite);
#if REVISION >= 20010920
  make_function_symbol_from_c("scf-get-language", (unsigned int (*)())DecodeLanguage);
  make_function_symbol_from_c("scf-get-time", (unsigned int (*)())DecodeTime);
  make_function_symbol_from_c("scf-get-aspect", (unsigned int (*)())DecodeAspect);
  make_function_symbol_from_c("scf-get-volume", (unsigned int (*)())DecodeVolume);
  make_function_symbol_from_c("scf-get-territory", (unsigned int (*)())DecodeTerritory);
  make_function_symbol_from_c("scf-get-timeout", (unsigned int (*)())DecodeTimeout);
  make_function_symbol_from_c("scf-get-inactive-timeout",
                              (unsigned int (*)())DecodeInactiveTimeout);
  make_function_symbol_from_c("dma-to-iop", (unsigned int (*)())dma_to_iop);
  make_function_symbol_from_c("kernel-shutdown", (unsigned int (*)())KernelShutdown);
#else
  make_function_symbol_from_c("dma-to-iop", (unsigned int (*)())dma_to_iop);
#endif

  // aybabtu is an "anti-piracy" function to stop the game from running if it's
  // not on a DVD. In a few places they seemed to believe pirates would try to
  // run the game from a CD instead of DVD somehow.
  // In non-demo modes, aybabtu (all your base are belong to us) calls
  // sceCdMmode to set the CD drive to DVD mode, which disables the CD drive if
  // it's not a DVD.
#if REVISION > 20011019
#ifdef DEMO
  make_function_symbol_from_c("aybabtu", (unsigned int (*)())FIX_SYM_VALUE(FIX_SYM_NOTHING_FUNC));
#else
  make_function_symbol_from_c("aybabtu", (unsigned int (*)())sceCdMmode);
#endif
#endif

  InitSoundScheme();

  // set kernel variables
  sym = intern_from_c("*stack-top*");
  sym->value = ((unsigned int)_stack - (unsigned int)_stack_size + 1) & EE_MEMORY_MASK;
  sym = intern_from_c("*stack-base*");
  sym->value = (unsigned int)_stack & EE_MEMORY_MASK;
  sym = intern_from_c("*stack-size*");
  sym->value = (unsigned int)_stack_size;
#if REVISION >= 20010920
  sym = intern_from_c("*kernel-boot-message*");
  sym->value = (unsigned int)intern_from_c(DebugBootMessage);
#endif

  // If diskbooting, set GOAL variables for boot
#if REVISION < 20011019
  if (!MasterDebug) {
#else
  if (DiskBoot) {
#endif
    sym = intern_from_c("*kernel-boot-mode*");
    sym->value = (unsigned int)intern_from_c("boot");
    sym = intern_from_c("*kernel-boot-level*");
    sym->value = (unsigned int)intern_from_c(DebugBootLevel);
  }

  // Boot game
#if REVISION < 20011019
  if (!MasterDebug) {
#else
  if (DiskBoot) {
#endif
    // Loading engine, allow out-of-order method defs (loading time not
    // critical)
    (*EnableMethodSet)++;
    // Load full game engine.
    load_and_link_dgo_from_c("game", kglobalheap,
                             LINK_FLAG_OUTPUT_LOAD | LINK_FLAG_EXECUTE | LINK_FLAG_PRINT_LOGIN,
                             0x400000);
    (*EnableMethodSet)--;

    // record that we've loaded the packages in game (not really needed, package
    // system mostly unused)
    kernel_packages->value =
        new_pair(FIX_SYM(FIX_SYM_GLOBAL_HEAP), (type*)FIX_SYM_VALUE(FIX_SYM_PAIR_TYPE),
                 make_string_from_c("engine"), kernel_packages->value);
    kernel_packages->value =
        new_pair(FIX_SYM(FIX_SYM_GLOBAL_HEAP), (type*)FIX_SYM_VALUE(FIX_SYM_PAIR_TYPE),
                 make_string_from_c("game"), kernel_packages->value);
    kernel_packages->value =
        new_pair(FIX_SYM(FIX_SYM_GLOBAL_HEAP), (type*)FIX_SYM_VALUE(FIX_SYM_PAIR_TYPE),
                 make_string_from_c("common"), kernel_packages->value);

    // GOAL function 'play' sets up the GOAL kernel to run the game.
    call_goal_function_by_name("play");
  }
}
