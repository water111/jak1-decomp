#include "sce/iop/989snd/ame.h"

#include "sce/iop/989snd/bank.h"
#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/handlers.h"
#include "sce/iop/989snd/midi_control.h"
#include "sce/iop/989snd/tick.h"
#include "sce/iop/989snd/valloc.h"

u_int snd_PlayAMESound(struct MIDISound* sound, int volume, int pan, short modifier, short bend) {
  struct AMEHandler* handler;
  MultiMIDIBlockHeader* master = (MultiMIDIBlockHeader*)sound->midi;
#if SND989_VERSION == 0x020608
  if (!master) {
    snd_ShowError(89, 0, 0, 0, 0);
    return 0;
  }
#endif
  if (master->flags & 2)
    return 0;
  handler = snd_GetFreeAMEHandler();
  if (!handler)
    return 0;
  master->flags |= 2;
  if (volume == 0x7fffffff)
    volume = 1024;
  handler->SH.Current_Vol = (volume * sound->volume) >> 10;
  if (handler->SH.Current_Vol > 127)
    handler->SH.Current_Vol = 127;
  if (pan == -1 || pan == -2)
    handler->SH.Current_Pan = sound->pan;
  else
    handler->SH.Current_Pan = pan;
  handler->SH.Current_PM = modifier;
  handler->SH.Sound = sound;
  handler->SH.Effects = 0;
  handler->SH.VolGroup = sound->volume_group;
  snd_StartAMESegment(handler, 0);
  snd_ActivateHandler(&handler->SH);
  return handler->SH.OwnerID;
}

int snd_StartAMESegment(struct AMEHandler* parent, int segment) {
  struct GenericSoundHandler* walk;
  struct AMEStreamHeader* stream;
  MultiMIDIBlockHeader* master;
  struct MIDISound* sound = parent->SH.Sound;
  master = (MultiMIDIBlockHeader*)sound->midi;
  stream = (struct AMEStreamHeader*)master->blocks[segment];
  if ((int)stream->handler.SH.OwnerID < 0)
    return 0;
  if (!stream->block.bank)
    return 0;
  walk = &parent->SH;
  while (walk->child)
    walk = walk->child;
  walk->child = &stream->handler.SH;
  stream->handler.SH.parent = walk;
  stream->handler.SH.child = 0;
  stream->handler.SH.Current_Vol = parent->SH.Current_Vol;
  stream->handler.SH.Current_Pan = parent->SH.Current_Pan;
  stream->handler.SH.Current_PM = parent->SH.Current_PM;
  stream->handler.SH.flags = parent->SH.flags;
  stream->handler.SH.VolGroup = parent->SH.VolGroup;
  stream->handler.SH.Sound = parent->SH.Sound;
  stream->handler.SH.Voices[1] = 0;
  stream->handler.SH.Voices[0] = 0;
  stream->handler.repeats = 0;
  snd_SetupMIDIStreamHandler(&stream->handler, &stream->block);
  snd_ActivateHandler(&stream->handler.SH);
  return 1;
}

void snd_StopAMESegment(struct MIDIHandler* handler) {
  if ((int)handler->SH.OwnerID < 0) {
    snd_StopHandlerPtr(&handler->SH, 0, 0);
    handler->midi_flags &= ~1;
  }
}

struct AMEStreamHeader* snd_FindAMEStream(struct MIDIHandler* handler, int index) {
  MultiMIDIBlockHeader* master =
      (MultiMIDIBlockHeader*)((struct MIDISound*)handler->SH.Sound)->midi;
  return (struct AMEStreamHeader*)master->blocks[index];
}

void snd_SetAMESoundVolumePan(u_int handle, int volume, int pan) {
  struct GenericSoundHandler* handler = snd_CheckHandlerStillActive(handle);
  struct GenericSoundHandler* child;
  if (!handler)
    return;
  snd_LockVoiceAllocator(0);
  if (volume < 0)
    handler->Current_Vol = -volume;
  else if (volume != 0x7fffffff)
    handler->Current_Vol = (volume * ((Sound*)handler->Sound)->volume) >> 10;
  if (handler->Current_Vol > 127)
    handler->Current_Vol = 127;
  if (pan == -1)
    handler->Current_Pan = ((Sound*)handler->Sound)->pan;
  else if (pan != -2)
    handler->Current_Pan = pan;
  snd_UnlockVoiceAllocator();
  for (child = handler->child; child; child = child->child)
    snd_SetMIDIHandlerVolumePan((struct MIDIHandler*)child, volume, pan);
}

void snd_SetAMESoundPitchModifier(u_int handle, short modifier) {
  struct GenericSoundHandler* handler = snd_CheckHandlerStillActive(handle);
  struct GenericSoundHandler* child;
  if (!handler)
    return;
  snd_LockVoiceAllocator(0);
  handler->Current_PM = modifier;
  snd_UnlockVoiceAllocator();
  for (child = handler->child; child; child = child->child)
    snd_SetMIDIHandlerPitchModifier((struct MIDIHandler*)child, modifier);
}

extern int gGlobalExcite;

signed char* snd_AMEFunction(struct MIDIHandler* stream, u_char* ame_header) {
  int compare_value;
  u_char* command_ptr;
  short next_command;
  int ignore_next_command;
  struct AMEStreamHeader* stream_to_stop;
  int y;
  int group;
  int counter;
  int done;
  int stop;
  signed char* stream_end;
  struct AMEHandler* ame_handler;

  if (stream == 0) {
    return (signed char*)1;
  }

  ame_handler = (struct AMEHandler*)stream;
  while (ame_handler->SH.parent)
    ame_handler = (struct AMEHandler*)ame_handler->SH.parent;

  command_ptr = ame_header;
  ignore_next_command = 0;
  done = 0;
  stop = 0;

  while (!done) {
    next_command = *command_ptr++;
    switch (next_command) {
      case 0:
        if (!ignore_next_command) {
          if (gGlobalExcite < command_ptr[0] + 1)
            ignore_next_command = 1;
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 1:
        if (!ignore_next_command) {
          if (gGlobalExcite != command_ptr[0] + 1)
            ignore_next_command = 1;
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 2:
        if (!ignore_next_command) {
          if (gGlobalExcite > command_ptr[0] - 1)
            ignore_next_command = 1;
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 3:
        if (!ignore_next_command) {
          stream_to_stop = snd_FindAMEStream(stream, command_ptr[0]);
          if (&stream_to_stop->handler == stream) {
            stop = 1;
            done = 1;
          } else {
            snd_StopHandlerPtr(&stream_to_stop->handler.SH, 0, 0);
          }
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 4:
        if (ignore_next_command == 1)
          ignore_next_command = 2;
        break;
      case 5:
        if (ignore_next_command == 2)
          ignore_next_command = 0;
        break;
      case 6:
        if (!ignore_next_command) {
          if (command_ptr[1] - 1 < ame_handler->registers[command_ptr[0]])
            ignore_next_command = 1;
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 2;
        break;
      case 7:
        if (!ignore_next_command) {
          if (ame_handler->registers[command_ptr[0]] < command_ptr[1] + 1)
            ignore_next_command = 1;
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 2;
        break;
      case 11:
        ame_handler->macros[command_ptr[0]] = &command_ptr[1];
        command_ptr++;
        while (*command_ptr != 0xf7) {
          command_ptr++;
        }
        command_ptr++;
        break;
      case 12:
        if (!ignore_next_command) {
          stream_end = snd_AMEFunction(stream, ame_handler->macros[command_ptr[0]]);
          if (stream_end == 0) {
            stop = 1;
            done = 1;
          }
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 13:
        if (!ignore_next_command) {
          stop = 1;
          done = 1;
          snd_StartAMESegment((struct AMEHandler*)stream,
                              ame_handler->registers[command_ptr[0]] - 1);
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 14:
        if (!ignore_next_command) {
          snd_StartAMESegment((struct AMEHandler*)stream,
                              ame_handler->registers[command_ptr[0]] - 1);
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 15:
        if (!ignore_next_command) {
          group = *command_ptr++;
          ame_handler->groups[group].basis = *command_ptr++;
          for (y = 0; *command_ptr != 0xf7; y++) {
            ame_handler->groups[group].channel[y] = *command_ptr++;
            ame_handler->groups[group].excite_min[y] = *command_ptr++;
            ame_handler->groups[group].excite_max[y] = *command_ptr++;
          }
          ame_handler->groups[group].channel_count = y;
          command_ptr += 1;
        } else {
          while (*command_ptr != 0xf7) {
            command_ptr++;
          }
          command_ptr++;
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        break;
      case 16:
        if (!ignore_next_command) {
          if (ame_handler->groups[command_ptr[0]].basis == 0) {
            compare_value = gGlobalExcite;
          } else {
            compare_value = ame_handler->registers[ame_handler->groups[command_ptr[0]].basis - 1];
          }
          for (counter = 0; counter < ame_handler->groups[command_ptr[0]].channel_count;
               counter++) {
            if (ame_handler->groups[command_ptr[0]].excite_min[counter] - 1 < compare_value &&
                ame_handler->groups[command_ptr[0]].excite_max[counter] + 1 > compare_value) {
              snd_UnmuteMIDIChannel(stream, ame_handler->groups[command_ptr[0]].channel[counter]);
            } else {
              snd_MuteMIDIChannel(stream, ame_handler->groups[command_ptr[0]].channel[counter]);
            }
          }
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 17:
        if (!ignore_next_command) {
          stop = 1;
          done = 1;
          snd_StartAMESegment((struct AMEHandler*)stream, command_ptr[0]);
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 18:
        if (!ignore_next_command) {
          snd_StartAMESegment((struct AMEHandler*)stream, command_ptr[0]);
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 19:
        if (!ignore_next_command) {
          ame_handler->registers[command_ptr[0]] = command_ptr[1];
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 2;
        break;
      case 20:
        if (!ignore_next_command) {
          if (ame_handler->registers[command_ptr[0]] < 127) {
            ame_handler->registers[command_ptr[0]]++;
          }
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 21:
        if (!ignore_next_command) {
          if (ame_handler->registers[command_ptr[0]] > 0) {
            ame_handler->registers[command_ptr[0]]--;
          }
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 1;
        break;
      case 22:
        if (!ignore_next_command) {
          if (ame_handler->registers[command_ptr[0]] != command_ptr[1])
            ignore_next_command = 1;
        } else {
          if (ignore_next_command == 1) {
            ignore_next_command = 0;
          }
        }
        command_ptr += 2;
        break;

      default:
        break;
    }

    if (*command_ptr == 0xf7) {
      done = 1;
    }
  }

  if (stop) {
    return 0;
  }

  return (signed char*)command_ptr;
}

int gGlobalExcite = 0;

void snd_SetMIDIRegister(u_int handle, int reg, short value) {
  struct AMEHandler* handler;
#if SND989_VERSION == 0x020608
  if (((handle >> 24) & 31) != 2)
    return;
#endif
  handler = (struct AMEHandler*)snd_CheckHandlerStillActive(handle);
  if (!handler)
    return;
  snd_LockMasterTick();
  if (value > 127)
    value = 127;
  if (value < 0)
    value = 0;
  handler->registers[reg] = value;
  snd_UnlockMasterTick();
}

int snd_GetMIDIRegister(u_int handle, int reg) {
  struct AMEHandler* handler;
#if SND989_VERSION == 0x020608
  if (((handle >> 24) & 31) != 2)
    return 0;
#endif
  handler = (struct AMEHandler*)snd_CheckHandlerStillActive(handle);
  return handler ? handler->registers[reg] : 0;
}

void snd_SetGlobalExcite(int value) {
  if (value > 127)
    value = 127;
  if (value < 0)
    value = 0;
  gGlobalExcite = value;
}

int snd_GetGlobalExcite(void) {
  return gGlobalExcite;
}
