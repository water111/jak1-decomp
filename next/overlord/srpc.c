#include "989snd/989snd.h"
#include "common/overlord_rpc.h"
#include "overlord/iso.h"
#include "overlord/iso_api.h"
#include "overlord/isocommon.h"
#include "overlord/overlord.h"
#include "overlord/sbank.h"
#include "overlord/soundcommon.h"
#include "overlord/ssound.h"
#include "overlord/ramdisk.h"
#include "sce/iop/intrman.h"
#include "sce/iop/sifrpc.h"
#include "sce/iop/sif.h"
#include "sce/iop/sysmem.h"
#include "sce/iop/thread.h"

// Sound RPC for sound effects and loading.

// Disk speed benchmark.
#if REVISION <= 20011002
extern unsigned int speeds[41];
extern unsigned int speeds_seqno;
#endif

unsigned int gFrameNum = -1;
static unsigned int gInfoEE = 0;
unsigned int gFreeMem = 0;
#if REVISION > 20011002
unsigned int gNoCD = 0;
unsigned int gDirtyCD = 0;
unsigned int gDiskSpeed[2] = {0, 0};
unsigned int gLastSpeed;
int gDupSeg = -1;
int gFlava = 0;
#endif
#if REVISION >= 20011104 && !defined(DEMO) && !defined(REGION_KR)
int gFPS = 60;
#endif
int gMusicVol = 0x400;
int gMusicFade = 0;
int gMusicFadeDir = 0;
#if REVISION > 20011002
int gMusicTweak = 0x80;
#endif
#if REVISION > 20010903
int gMusicPause = 0;
int gSoundEnable = 1;
#endif
static unsigned char gPlayerBuf[80 * 128];
static unsigned char gLoaderBuf[80];
static void* gMusic;

static void* RPC_Player(unsigned int fno, void* data, int size);
static void* RPC_Loader(unsigned int fno, void* data, int size);

unsigned int Thread_Player(void) {
  sceSifQueueData queue;
  sceSifServeData server;
  CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, 0xdeb1, RPC_Player, gPlayerBuf, 0, 0, &queue);
  CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

unsigned int Thread_Loader(void) {
  sceSifQueueData queue;
  sceSifServeData server;
  CpuDisableIntr();
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, 0xdeb2, RPC_Loader, gLoaderBuf, 0, 0, &queue);
  CpuEnableIntr();
  sceSifRpcLoop(&queue);
  return 0;
}

#if REVISION <= 20010903
static void* RPC_Player(unsigned int fno, void* data, int size) {
  unsigned char* cursor = data;
  int count = size / 80;
  int result;
  gFreeMem = QueryTotalFreeMemSize();
  result = PollSema(gSema);
  if (!result) {
    if (gMusic) {
      int id = MUSIC_SOUND_ID;
      if (!LookupSound(id)) {
        Sound* music = AllocateSound(1);
        if (music) {
          gMusicFade = 0;
          gMusicFadeDir = 1;
          SetMusicVol();
          music->sound_handle = snd_PlaySoundVolPanPMPB(gMusic, 0, 0x400, -1, 0, 0);
          music->id = id;
          music->is_music = 1;
        }
      }
    }
    SignalSema(gSema);
  }
  while (count > 0) {
    SoundRpcCommand* command = (SoundRpcCommand* )cursor;
    switch (command->command) {
      case PLAY_SOUND: {
        SoundRpcCommand* cmd = command;
        if (cmd->args.play.id) {
          SoundBank* bank;
          int index = LookupSoundIndex(cmd->args.play.name, &bank);
          if (index < 0) {
            if (!memcmp(cmd->args.play.name, "spool-", 6)) {
              IsoName name;
              FileRecord* file;
              char* literal = &cmd->args.play.name[6];
              int length = strlen(literal);
              int i;
              if (length > 8) {
                memcpy(name.chars, literal, 8);
              } else {
                memset(name.chars, ' ', 8);
                memcpy(name.chars, literal, length);
              }
              for (i = 0; i < 8; i++) {
                if (name.chars[i] >= 'a' && name.chars[i] <= 'z') name.chars[i] -= 32;
              }
              strcpy(&name.chars[8], "INT");
              file = isofs->find_in(name.chars);
              if (!file) {
                strcpy(&name.chars[8], gLanguage);
                file = isofs->find_in(name.chars);
              }
              if (!file) {
              } else {
                PlayVAGStream(file, cmd->args.play.id, cmd->args.play.params.volume, 0);
              }
            }
          } else {
            unsigned int id = cmd->args.play.id;
            Sound* sound;
            int volume;
            int pan;
            sound = LookupSound(id);
            if (sound) {
              memcpy(&sound->params, &cmd->args.play.params, sizeof(SoundParams));
              sound->bank_entry = &bank->sound[index];
              sound->is_music = 0;
              if (!(sound->params.mask & 0x40))
                sound->params.fo_min = sound->bank_entry->falloff_min;
              if (!(sound->params.mask & 0x80))
                sound->params.fo_max = sound->bank_entry->falloff_max;
              if (!(sound->params.mask & 0x100))
                sound->params.fo_curve = sound->bank_entry->falloff_curve;
              UpdateVolume(sound);
            } else {
              sound = AllocateSound();
              if (!sound) goto play_done;
              memcpy(&sound->params, &cmd->args.play.params, sizeof(SoundParams));
              sound->bank_entry = &bank->sound[index];
              sound->is_music = 0;
              sound->auto_time = 0;
              if (!(sound->params.mask & 0x40))
                sound->params.fo_min = sound->bank_entry->falloff_min;
              if (!(sound->params.mask & 0x80))
                sound->params.fo_max = sound->bank_entry->falloff_max;
              if (!(sound->params.mask & 0x100))
                sound->params.fo_curve = sound->bank_entry->falloff_curve;
              volume = GetVolume(sound);
              pan = GetPan(sound);
              sound->sound_handle =
                  snd_PlaySoundVolPanPMPB(bank->bank_handle, index, volume, pan,
                                          sound->params.pitch_mod, sound->params.bend);
              if (!sound->sound_handle) goto play_done;
              sound->id = id;
            }
          }
        }
      }
      play_done:
        break;
      case PAUSE_SOUND:
      case STOP_SOUND:
      case CONTINUE_SOUND:
      case SET_PARAM: {
        SoundRpcCommand* cmd = command;
        Sound* sound;
        if (cmd->args.param.id) {
          sound = LookupSound(cmd->args.param.id);
          if (!sound) {
            if (cmd->args.param.id == gVAG_Id) {
              switch (cmd->command) {
                case PAUSE_SOUND:
                  PauseVAGStream();
                  break;
                case STOP_SOUND:
                  StopVAGStream(0, 0);
                  break;
                case CONTINUE_SOUND:
                  UnpauseVAGStream();
                  break;
                case SET_PARAM: {
                  SoundRpcCommand* param = command;
                  unsigned short mask = param->args.param.params.mask;
                  if (mask & 1) SetVAGStreamVolume(param->args.param.params.volume);
                } break;
              }
            }
          } else {
            switch (cmd->command) {
              case PAUSE_SOUND:
                snd_PauseSound(sound->sound_handle);
                break;
              case STOP_SOUND:
                snd_StopSound(sound->sound_handle);
                break;
              case CONTINUE_SOUND:
                snd_ContinueSound(sound->sound_handle);
                break;
              case SET_PARAM: {
                SoundRpcCommand* param = command;
                unsigned short mask = param->args.param.params.mask;
                int time = *(int *)&param->args.param.automatic[0];
                int from = *(int *)&param->args.param.automatic[4];
                if (mask & 1) {
                  if (mask & 0x10) {
                    sound->auto_time = time;
                    sound->new_volume = param->args.param.params.volume;
                  } else {
                    sound->params.volume = param->args.param.params.volume;
                  }
                }
                if (mask & 0x20) {
                  sound->params.position.x = param->args.param.params.position.x;
                  sound->params.position.y = param->args.param.params.position.y;
                  sound->params.position.z = param->args.param.params.position.z;
                }
                if (mask & 0x21) UpdateVolume(sound);
                if (mask & 2) {
                  sound->params.pitch_mod = param->args.param.params.pitch_mod;
                  if (mask & 0x10)
                    snd_AutoPitch(sound->sound_handle, sound->params.pitch_mod, time, from);
                  else
                    snd_SetSoundPitchModifier(sound->sound_handle,
                                              param->args.param.params.pitch_mod);
                }
                if (mask & 4) {
                  sound->params.bend = param->args.param.params.bend;
                  if (mask & 0x10)
                    snd_AutoPitchBend(sound->sound_handle, sound->params.bend, time, from);
                  else
                    snd_SetSoundPitchBend(sound->sound_handle, param->args.param.params.bend);
                }
              } break;
            }
          }
        }
      } break;
      case PAUSE_GROUP: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.group.group;
        snd_PauseAllSoundsInGroup(group);
        if (group & 4) PauseVAGStream();
      } break;
      case CONTINUE_GROUP: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.group.group;
        snd_ContinueAllSoundsInGroup(group);
        if (group & 4) UnpauseVAGStream();
      } break;
      case STOP_GROUP: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.group.group;
        KillSoundsInGroup(group);
        if (group & 4) StopVAGStream(0, 0);
      } break;
      case SET_MASTER_VOLUME: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.master.group;
        int i;
        for (i = 0; i < 32; i++) {
          if ((group >> i) & 1) {
            if (i == 1) {
              gMusicVol = cmd->args.master.volume;
              if (!gMusicFadeDir) SetMusicVol();
            }
            else if (i == 2) {
            } else
              snd_SetMasterVolume(i, cmd->args.master.volume);
          }
        }
      } break;
      case SET_REVERB: {
        SoundRpcCommand* cmd = command;
        snd_SetReverbType(cmd->args.reverb.core, cmd->args.reverb.type);
        snd_SetReverbDepth(cmd->args.reverb.core, cmd->args.reverb.left,
                           cmd->args.reverb.right);
      } break;
      case SET_EAR_TRANS: {
        SoundRpcCommand* cmd = command;
        SetEarTrans(&cmd->args.ear.ear, &cmd->args.ear.camera, cmd->args.ear.angle);
      } break;
      case SET_FALLOFF_CURVE: {
        SoundRpcCommand* cmd = command;
        SetCurve(cmd->args.curve.curve, cmd->args.curve.falloff, cmd->args.curve.ease);
      } break;
      case SET_SOUND_FALLOFF: {
        SoundRpcCommand* cmd = command;
        SoundBank* bank;
        int index = LookupSoundIndex(cmd->args.falloff.name, &bank);
        if (index < 0) goto falloff_done;
        bank->sound[index].falloff_curve = cmd->args.falloff.curve;
        bank->sound[index].falloff_min = cmd->args.falloff.min;
        bank->sound[index].falloff_max = cmd->args.falloff.max;
      falloff_done:;
      } break;
      case SET_FLAVA: {
        SoundRpcCommand* cmd = command;
        Sound* music = LookupSound(MUSIC_SOUND_ID);
        if (music) {
          snd_SetMIDIRegister(music->sound_handle, 0, cmd->args.flava.value);
          __asm__(".set noreorder\n\tj 1f\n\tnop\n1:\n\t.set reorder");
        }
      }
        break;
      default:
        break;
    }
    count--;
    cursor += 80;
  }
  return 0;
}
#else
static void* RPC_Player(unsigned int fno, void* data, int size) {
  unsigned char* cursor = data;
  int count = size / 80;
  int result;
  if (!gSoundEnable)
    return 0;
  gFreeMem = QueryTotalFreeMemSize();
  result = PollSema(gSema);
  if (!result) {
    if (gMusic && !gMusicPause) {
      int id = MUSIC_SOUND_ID;
#if REVISION <= 20011002
      if (!LookupSound(id)) {
        Sound* music = AllocateSound(1);
#else
      Sound* music = LookupSound(id);
      if (!music) {
        music = AllocateSound(1);
#endif
        if (music) {
          gMusicFade = 0;
          gMusicFadeDir = 1;
          SetMusicVol();
          music->sound_handle = snd_PlaySoundVolPanPMPB(gMusic, 0, 0x400, -1, 0, 0);
          music->id = id;
          music->is_music = 1;
        }
      }
    }
    SignalSema(gSema);
  }
#if REVISION > 20011002
  {
    Sound* music = LookupSound(MUSIC_SOUND_ID);
    SetMusicVol();
    if (music) {
      snd_SetSoundVolPan(music->sound_handle, 0x7fffffff, 0);
#if REVISION > 20011002
      snd_SetMIDIRegister(music->sound_handle, 0, gFlava);
#endif
    }
  }
#endif
  while (count > 0) {
    SoundRpcCommand* command = (SoundRpcCommand*)cursor;
    switch (command->command) {
      case PLAY_SOUND: {
        SoundRpcCommand* cmd = command;
        if (cmd->args.play.id) {
#if REVISION <= 20011002
          SoundBank* bank;
          int index = LookupSoundIndex(cmd->args.play.name, &bank);
          if (index < 0) {
#endif
            if (!memcmp(cmd->args.play.name, "spool-", 6)) {
              IsoName name;
              FileRecord* file;
              VagDirEntry* vag;
              char* literal = &cmd->args.play.name[6];
              int length = strlen(literal);
              int i;
              if (length > 8) {
                memcpy(name.chars, literal, 8);
              } else {
                memset(name.chars, ' ', 8);
                memcpy(name.chars, literal, length);
              }
              for (i = 0; i < 8; i++) {
                if (name.chars[i] >= 'a' && name.chars[i] <= 'z')
                  name.chars[i] -= 32;
              }
              vag = FindVAGFile(name.chars);
              memcpy(name.chars, "VAGWAD  ", 8);
              strcpy(&name.chars[8], gLanguage);
              file = isofs->find_in(name.chars);
              if (vag) {
#if REVISION <= 20011002
                PlayVAGStream(file, vag, cmd->args.play.id, cmd->args.play.params.volume, 0);
#else
              if (cmd->args.play.params.pitch_mod)
                PlayVAGStream(file, vag, cmd->args.play.id, cmd->args.play.params.volume, 0,
                              &cmd->args.play.params.position);
              else
                PlayVAGStream(file, vag, cmd->args.play.id, cmd->args.play.params.volume, 0, 0);
#endif
              }
            }
#if REVISION <= 20011002
          } else {
#else
          else {
#endif
            unsigned int id = cmd->args.play.id;
            Sound* sound;
#if REVISION > 20011002
            SoundBank* bank;
            int index;
#endif
            int volume;
            int pan;
#if REVISION > 20011002
            index = LookupSoundIndex(cmd->args.play.name, &bank);
            if (index < 0)
              break;
#endif
            sound = LookupSound(id);
            if (sound) {
              memcpy(&sound->params, &cmd->args.play.params, sizeof(SoundParams));
              sound->bank_entry = &bank->sound[index];
              sound->is_music = 0;
              if (!(sound->params.mask & 0x40))
                sound->params.fo_min = sound->bank_entry->falloff & 0x3fff;
              if (!(sound->params.mask & 0x80))
                sound->params.fo_max = (sound->bank_entry->falloff >> 14) & 0x3fff;
              if (!(sound->params.mask & 0x100))
                sound->params.fo_curve = sound->bank_entry->falloff >> 28;
              UpdateVolume(sound);
              snd_SetSoundPitchModifier(sound->sound_handle, cmd->args.play.params.pitch_mod);
              snd_SetSoundPitchBend(sound->sound_handle, cmd->args.play.params.bend);
            } else {
              sound = AllocateSound();
              if (!sound)
                goto play_done;
              memcpy(&sound->params, &cmd->args.play.params, sizeof(SoundParams));
              sound->bank_entry = &bank->sound[index];
              sound->is_music = 0;
              sound->auto_time = 0;
              if (!(sound->params.mask & 0x40))
                sound->params.fo_min = sound->bank_entry->falloff & 0x3fff;
              if (!(sound->params.mask & 0x80))
                sound->params.fo_max = (sound->bank_entry->falloff >> 14) & 0x3fff;
              if (!(sound->params.mask & 0x100))
                sound->params.fo_curve = sound->bank_entry->falloff >> 28;
              volume = GetVolume(sound);
              pan = GetPan(sound);
              sound->sound_handle =
                  snd_PlaySoundVolPanPMPB(bank->bank_handle, index, volume, pan,
                                          sound->params.pitch_mod, sound->params.bend);
              if (!sound->sound_handle)
                goto play_done;
              sound->id = id;
            }
          }
        }
      }
      play_done:
        break;
      case PAUSE_SOUND:
      case STOP_SOUND:
      case CONTINUE_SOUND:
      case SET_PARAM: {
        SoundRpcCommand* cmd = command;
        Sound* sound;
        if (cmd->args.param.id) {
          sound = LookupSound(cmd->args.param.id);
          if (!sound) {
            if (cmd->args.param.id == gVAG_Id) {
              switch (cmd->command) {
                case PAUSE_SOUND:
                  PauseVAGStream();
                  break;
                case STOP_SOUND:
                  StopVAGStream(0, 0);
                  break;
                case CONTINUE_SOUND:
                  UnpauseVAGStream();
                  break;
                case SET_PARAM: {
                  SoundRpcCommand* param = command;
                  unsigned short mask = param->args.param.params.mask;
                  if (mask & 1)
                    SetVAGStreamVolume(param->args.param.params.volume);
                } break;
              }
            }
          } else {
            switch (cmd->command) {
              case PAUSE_SOUND:
                snd_PauseSound(sound->sound_handle);
                break;
              case STOP_SOUND:
                snd_StopSound(sound->sound_handle);
                break;
              case CONTINUE_SOUND:
                snd_ContinueSound(sound->sound_handle);
                break;
              case SET_PARAM: {
                SoundRpcCommand* param = command;
                unsigned short mask = param->args.param.params.mask;
                int time = *(int*)&param->args.param.automatic[0];
                int from = *(int*)&param->args.param.automatic[4];
                if (mask & 1) {
                  if (mask & 0x10) {
                    sound->auto_time = time;
                    sound->new_volume = param->args.param.params.volume;
                  } else {
                    sound->params.volume = param->args.param.params.volume;
                  }
                }
                if (mask & 0x20) {
                  sound->params.position.x = param->args.param.params.position.x;
                  sound->params.position.y = param->args.param.params.position.y;
                  sound->params.position.z = param->args.param.params.position.z;
                }
                if (mask & 0x21)
                  UpdateVolume(sound);
                if (mask & 2) {
                  sound->params.pitch_mod = param->args.param.params.pitch_mod;
                  if (mask & 0x10)
                    snd_AutoPitch(sound->sound_handle, sound->params.pitch_mod, time, from);
                  else
                    snd_SetSoundPitchModifier(sound->sound_handle,
                                              param->args.param.params.pitch_mod);
                }
                if (mask & 4) {
                  sound->params.bend = param->args.param.params.bend;
                  if (mask & 0x10)
                    snd_AutoPitchBend(sound->sound_handle, sound->params.bend, time, from);
                  else
                    snd_SetSoundPitchBend(sound->sound_handle, param->args.param.params.bend);
                }
              } break;
            }
          }
        }
      } break;
      case PAUSE_GROUP: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.group.group;
        snd_PauseAllSoundsInGroup(group);
        if (group & 4)
          PauseVAGStream();
        if (group & 2)
          gMusicPause = 1;
      } break;
      case CONTINUE_GROUP: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.group.group;
        snd_ContinueAllSoundsInGroup(group);
        if (group & 4)
          UnpauseVAGStream();
        if (group & 2)
          gMusicPause = 0;
      } break;
      case STOP_GROUP: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.group.group;
        KillSoundsInGroup(group);
        if (group & 4)
          StopVAGStream(0, 0);
      } break;
      case SET_MASTER_VOLUME: {
        SoundRpcCommand* cmd = command;
        unsigned char group = cmd->args.master.group;
        int i;
        for (i = 0; i < 32; i++) {
          if ((group >> i) & 1) {
#if REVISION <= 20011002
            if (i == 1) {
              gMusicVol = cmd->args.master.volume;
              if (!gMusicFadeDir)
                SetMusicVol();
            }
#else
            if (i == 1)
              gMusicVol = cmd->args.master.volume;
#endif
            else if (i == 2)
              SetDialogVolume(cmd->args.master.volume);
            else
              snd_SetMasterVolume(i, cmd->args.master.volume);
          }
        }
      } break;
      case SET_REVERB: {
#if REVISION < 20011031
        SoundRpcCommand* cmd = command;
        snd_SetReverbType(cmd->args.reverb.core, cmd->args.reverb.type);
        snd_SetReverbDepth(cmd->args.reverb.core, cmd->args.reverb.left, cmd->args.reverb.right);
#endif
      } break;
      case SET_EAR_TRANS: {
        SoundRpcCommand* cmd = command;
        SetEarTrans(&cmd->args.ear.ear, &cmd->args.ear.camera, cmd->args.ear.angle);
      } break;
      case SET_FALLOFF_CURVE: {
        SoundRpcCommand* cmd = command;
        SetCurve(cmd->args.curve.curve, cmd->args.curve.falloff, cmd->args.curve.ease);
      } break;
      case SET_SOUND_FALLOFF: {
        SoundRpcCommand* cmd = command;
#if REVISION < 20011031
        SoundBank* bank;
        int index = LookupSoundIndex(cmd->args.falloff.name, &bank);
        if (index < 0)
          goto falloff_done;
        bank->sound[index].falloff =
            cmd->args.falloff.min | (cmd->args.falloff.max << 14) | (cmd->args.falloff.curve << 28);
      falloff_done:;
#else
        {
          SoundBank* bank;
          int index = LookupSoundIndex(cmd->args.falloff.name, &bank);
          if (index < 0)
            goto falloff_done;
          bank->sound[index].falloff = cmd->args.falloff.min | (cmd->args.falloff.max << 14) |
                                       (cmd->args.falloff.curve << 28);
        falloff_done:;
        }
#endif
      } break;
      case SET_FLAVA: {
        SoundRpcCommand* cmd = command;
#if REVISION < 20011031
        Sound* music = LookupSound(MUSIC_SOUND_ID);
#endif
#if REVISION > 20011002
        gFlava = cmd->args.flava.value;
#else
        if (music) {
          snd_SetMIDIRegister(music->sound_handle, 0, cmd->args.flava.value);
          // TODO can't match.
          __asm__(".set noreorder\n\tj 1f\n\tnop\n1:\n\t.set reorder");
        }
#endif
      } break;
#if REVISION >= 20011104 && !defined(DEMO) && !defined(REGION_KR)
      case SET_FPS: {
        SoundRpcCommand* cmd = command;
        gFPS = cmd->args.fps.fps;
      } break;
#endif
      case SHUTDOWN:
        gSoundEnable = 0;
        snd_StopSoundSystem();
        ShutdownFilingSystem();
        return 0;
      default:
        break;
    }
    count--;
    cursor += 80;
  }
#if REVISION >= 20011104 && !defined(DEMO) && !defined(REGION_KR)
  {
    int voice;
    int active = 0;
    for (voice = 16; voice < 48; voice++) {
      if (snd_GetVoiceStatus(voice + 1) == 1)
        active++;
    }
    if (active >= 31)
      PrintActiveSounds();
  }
#endif
  return 0;
}
#endif

static void* RPC_Loader(unsigned int fno, void* data, int size) {
  unsigned char* cursor = data;
  int count = size / 80;
#if REVISION > 20010903
  if (!gSoundEnable)
    return 0;
#endif
  while (count > 0) {
    SoundRpcCommand* command = (SoundRpcCommand*)cursor;
    switch (command->command) {
      case LOAD_BANK: {
        SoundRpcCommand* cmd = command;
        SoundBank* bank = LookupBank(cmd->args.bank.name);
        if (bank)
          goto load_bank_done;
        bank = AllocateBank();
        if (!bank)
          goto load_bank_done;
        LoadSoundBank(cmd->args.bank.name, bank);
      load_bank_done:;
      } break;
      case UNLOAD_BANK: {
        SoundRpcCommand* cmd = command;
        SoundBank* bank = LookupBank(cmd->args.bank.name);
        void* handle;
        if (!bank)
          goto unload_bank_done;
        handle = bank->bank_handle;
        bank->bank_handle = 0;
        snd_UnloadBank(handle);
        snd_ResolveBankXREFS();
      unload_bank_done:;
      } break;
      case LOAD_MUSIC_CMD: {
        SoundRpcCommand* cmd = command;
        int result;
        while ((result = WaitSema(gSema))) {
        }
        if (gMusic) {
          gMusicFadeDir = -1;
          while (gMusicFade)
            DelayThread(1000);
          snd_UnloadBank(gMusic);
          snd_ResolveBankXREFS();
          gMusic = 0;
        }
        LoadMusic(cmd->args.bank.name, &gMusic);
        SignalSema(gSema);
      } break;
#if REVISION > 20011002
      case UNLOAD_MUSIC: {
#if REVISION >= 20011019 && REVISION < 20011031
        SoundRpcCommand* cmd = command;
#endif
        int result;
        while ((result = WaitSema(gSema))) {
        }
        if (gMusic) {
          gMusicFadeDir = -1;
          while (gMusicFade)
            DelayThread(1000);
          snd_UnloadBank(gMusic);
          snd_ResolveBankXREFS();
          gMusic = 0;
        }
        SignalSema(gSema);
      } break;
#endif
      case GET_IRX_VERSION: {
        SoundRpcCommand* cmd = command;
        gInfoEE = cmd->args.version.ee_addr;
        cmd->args.version.major = 2;
        cmd->args.version.minor = 0;
        return cmd;
      } break;
      case SET_LANGUAGE: {
        SoundRpcCommand* cmd = command;
        static char* languages[] = {"ENG", "FRE", "GER", "SPA", "ITA", "JAP", "UKE"};
        gLanguage = languages[cmd->args.language.id];
      } break;
      case RELOAD_INFO:
        ReloadBankInfo();
        break;
#if REVISION > 20011002
      case LIST_SOUNDS:
        PrintActiveSounds();
        break;
#endif
      default:
        break;
    }
    count--;
    cursor += 80;
  }
  return 0;
}

int VBlank_Handler(void* arg) {
  sceSifDmaData dma;
  int i;
  static int dmaid = 0;
  static SoundIopInfo info;
#if REVISION > 20010903
  if (!gSoundEnable)
    return 1;
#endif
  if (gMusicFadeDir < 0) {
    gMusicFade -= 512;
    if (gMusicFade < 0) {
      gMusicFade = 0;
      gMusicFadeDir = 0;
    }
#if REVISION <= 20011002
    SetMusicVol();
#endif
  } else if (gMusicFadeDir > 0) {
#if REVISION <= 20011002
    gMusicFade += 512;
#else
    gMusicFade += 1024;
#endif
    if (gMusicFade > 0x10000) {
      gMusicFade = 0x10000;
      gMusicFadeDir = 0;
    }
#if REVISION <= 20011002
    SetMusicVol();
#endif
  }
  if (!gInfoEE)
    return 1;
  gFrameNum++;
  if (gFakeVAGClockRunning && !gFakeVAGClockPaused) {
#if REVISION >= 20011104 && !defined(DEMO) && !defined(REGION_KR)
    gFakeVAGClock += 0x400 / gFPS;
#else
    gFakeVAGClock += 17;
#endif
  }
  if (dmaid) {
    if (sceSifDmaStat(dmaid) >= 0)
      return 1;
    dmaid = 0;
  }
  info.frame = gFrameNum;
  info.strpos = GetVAGStreamPos();
  info.stream_id = gVAG_Id;
  info.freemem = gFreeMem;
  info.freemem2 = gMemFreeAtStart;
#if REVISION <= 20011002
  info.times_seq = speeds_seqno;
#endif
#if REVISION > 20011002
  info.nocd = gNoCD;
  info.dirtycd = gDirtyCD;
  info.diskspeed[0] = gDiskSpeed[0];
  info.diskspeed[1] = gDiskSpeed[1];
  info.lastspeed = gLastSpeed;
  info.dupseg = gDupSeg;
#endif
  // Bug: check voices 1..48 and write one byte beyond chinfo[48].
  for (i = 0; i < 48; i++) {
#if REVISION <= 20011002
    info.chinfo[i + 1] = snd_GetVoiceStatus(i) == 1 ? 255 : 0;
#elif REVISION >= 20011019 && REVISION < 20011031
    info.chinfo[i + 1] = snd_GetVoiceStatus(i) == 1 ? 255 : 0;
#else
    info.chinfo[i + 1] = snd_GetVoiceStatus(i + 1) == 1 ? 255 : 0;
#endif
  }
#if REVISION <= 20011002
  for (i = 0; i < 41; i++) {
    info.times[i] = speeds[i];
  }
#endif
  dma.data = (unsigned int)&info;
  dma.addr = gInfoEE;
  // Bug: transfer size wrong?
#if REVISION <= 20011002
  dma.size = 0xf0;
#else
  dma.size = 0x110;
#endif
  dma.mode = 0;
  dmaid = sceSifSetDma(&dma, 1);
  return 1;
}
