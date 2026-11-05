#ifndef SCE_IOP_989SND_LOADER_STATE_H
#define SCE_IOP_989SND_LOADER_STATE_H
struct SoundBank;
struct SFXBlock2;
struct MIDIBlockHeader;
extern int gTransfering;
extern void (*gTransferDoneCallback)(void);
extern struct SoundBank* gBankListHead;
extern struct SFXBlock2* gBlockListHead;
extern struct MIDIBlockHeader* gMIDIListHead;
int snd_BankTransfer(struct SoundBank* bank,
                     void* samples,
                     int size,
                     int offset,
                     int continuing,
                     void (*done)(void));
int snd_EndBankTransfer(struct SoundBank* bank);
#if SND989_VERSION == 0x020604
int snd_BlockTransfer(struct SFXBlock2* block,
                      void* samples,
                      int size,
                      int offset,
                      int continuing,
                      void (*done)(void));
int snd_EndBlockTransfer(struct SFXBlock2* block);
#endif
void snd_InitLoader(void);
int snd_IsCurrentTransferComplete(void);
#endif
