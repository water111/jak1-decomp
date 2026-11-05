#include "sce/iop/989snd/blocksound.h"

#include "sce/iop/989snd/effects.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/loader_cd.h"
#include "sce/iop/989snd/sram.h"
#include "sce/iop/989snd/vag_stream.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/thbase/thread.h"

// TODO: hacks here...
#define SND_STORAGE(type, name, count, bytes)                                           \
  extern type name[count];                                                              \
  typedef char name##_size_check[(sizeof(name) == (bytes)) ? 1 : -1];                   \
  asm(".align 4\n.globl " #name "\n.type " #name ",@object\n" #name ":\n.space " #bytes \
      "\n.size " #name "," #bytes)
asm(".section .bss");
#if SND989_VERSION == 0x020604
SND_STORAGE(u_int, snd_MESSAGE_RETURN_BUFFER, 136, 544);
SND_STORAGE(char, snd_MESSAGE_RECIEVE_BUFFER, 0x1010, 4112);
#endif
SND_STORAGE(char, snd_LOADER_MESSAGE_RECIEVE_BUFFER, 256, 256);
SND_STORAGE(u_int, snd_LOADER_MESSAGE_RETURN_BUFFER, 4, 16);
SND_STORAGE(struct VoiceAttributes, gChannelStatus, SND_NUM_VOICES, 2688);
SND_STORAGE(char, gFileLoadBuffer, 2048, 2048);
#if SND989_VERSION == 0x020604
extern struct ThreadParam timer_thread_param;
typedef char timer_param_size_check[(sizeof(timer_thread_param) == 20) ? 1 : -1];
asm(".align 4\n.globl timer_thread_param\ntimer_thread_param:\n.space 20");
#endif
SND_STORAGE(struct SRAMNode, gSRAMFreeNodeStorage, 129, 3096);
SND_STORAGE(short, gEnvelopeStatus, SND_NUM_VOICES, 96);
SND_STORAGE(signed char, gAutoFreeState, SND_NUM_VOICES, 48);
#if SND989_VERSION == 0x020604
SND_STORAGE(struct BlockSoundHandler, gBlockSoundHandler, 64, 17408);
#else
SND_STORAGE(struct BlockSoundHandler, gBlockSoundHandler, 64, 17664);
#endif
SND_STORAGE(struct BasicEffect, gBasicEffect, 64, 1792);
SND_STORAGE(struct StreamSoundHandler, gStreamSoundHandler, 48, 2688);
SND_STORAGE(struct AMEHandler, gAMEHandler, 4, 3824);
SND_STORAGE(struct MIDIHandler, gMIDIHandler, 32, 5888);
SND_STORAGE(struct VAGStreamQueEntry, gVAGStreamQue, VAG_STREAM_QUEUE_CAPACITY, 1024);
extern struct SndCDSifRegPacket gCDSifRegPacket;
typedef char cd_packet_size_check[(sizeof(gCDSifRegPacket) == 24) ? 1 : -1];
asm(".align 4\n.globl gCDSifRegPacket\ngCDSifRegPacket:\n.space 24");
extern struct BasicEffect gVerbFX[2][2];
typedef char verb_fx_size_check[(sizeof(gVerbFX) == 112) ? 1 : -1];
asm(".align 4\n.globl gVerbFX\ngVerbFX:\n.space 112\n.align 4\n.text");
#undef SND_STORAGE
