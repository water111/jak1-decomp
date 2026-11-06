#include "sce/iop/sdrdrv/commands.h"
#include "sce/iop/sdrdrv/internal.h"

#if SDRDRV_SDK == 2400
char sdrdrv_build_id[16] = "PsIIsdrdrv  2400";
#else
char sdrdrv_build_id[16] = "PsIIsdrdrv  2700";
#endif
int return_value = 0;
static sceSdrUserCommandFunction volatile user_functions[SDR_USER_COMMAND_COUNT];
unsigned int sdr_command_buffer[16];

// The effect reply has sixteen unused bytes after the attributes in both revisions.
struct SdrEffectReply {
  SdEffectAttr attributes;
  unsigned int reserved[4];
};

struct SdrEffectReply sdr_effect_buffer;
volatile int sdr_callback_tid;

int sdr_rpc_thread(void) {
  sceSifQueueData queue;
  sceSifServeData server;
  int i;
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, SDR_RPC_COMMAND, sdr_dispatch, sdr_command_buffer, 0, 0, &queue);
  for (i = 0; i < SDR_USER_COMMAND_COUNT; i++)
    user_functions[i] = 0;
  sceSifRpcLoop(&queue);
  return 0;
}

void* sdr_dispatch(unsigned int command, void* data, int size) {
  unsigned int* args = data;
  struct ThreadParam thread;
  return_value = 0;
  switch (command & SDR_COMMAND_MASK) {
    case SDR_INIT:
      return_value = sceSdInit(args[1]);
      break;
    case SDR_SET_PARAM:
      sceSdSetParam(args[1], args[2]);
      break;
    case SDR_SET_SWITCH:
      sceSdSetSwitch(args[1], args[2]);
      break;
    case SDR_SET_ADDR:
      sceSdSetAddr(args[1], args[2]);
      break;
    case SDR_SET_CORE_ATTR:
      sceSdSetCoreAttr(args[1], args[2]);
      break;
    case SDR_GET_PARAM:
      return_value = sceSdGetParam(args[1]);
      break;
    case SDR_GET_SWITCH:
      return_value = sceSdGetSwitch(args[1]);
      break;
    case SDR_GET_ADDR:
      return_value = sceSdGetAddr(args[1]);
      break;
    case SDR_GET_CORE_ATTR:
      return_value = sceSdGetCoreAttr(args[1]);
      break;
    case SDR_NOTE_TO_PITCH:
      return_value = sceSdNote2Pitch((u_short)args[1], args[2], (u_short)args[3], args[4]);
      break;
    case SDR_PITCH_TO_NOTE:
      return_value = sceSdPitch2Note((u_short)args[1], (u_short)args[2], args[3]);
      break;
    case SDR_BATCH:
      return_value = sceSdProcBatch((sceSdBatch*)args[1], (u_int*)args[2], args[3]);
      break;
    case SDR_BATCH_EX:
      return_value = sceSdProcBatchEx((sceSdBatch*)args[1], (u_int*)args[2], args[3], args[4]);
      break;
#if SDRDRV_SDK >= 2700
    case SDR_BATCH_INLINE:
      return_value =
          sceSdProcBatch((sceSdBatch*)(args + 2), sdr_batch_buffer + 1, ((u_short*)args)[1]);
      sdr_batch_buffer[0] = return_value;
      break;
    case SDR_BATCH_EX_INLINE:
      return_value = sceSdProcBatchEx((sceSdBatch*)(args + 2), sdr_batch_buffer + 1,
                                      ((u_short*)args)[1], args[1]);
      sdr_batch_buffer[0] = return_value;
      break;
#endif
    case SDR_VOICE_TRANSFER:
      return_value = sceSdVoiceTrans(args[1], args[2], (u_char*)args[3], args[4], args[5]);
      break;
    case SDR_BLOCK_TRANSFER:
      return_value = sceSdBlockTrans(args[1], args[2], (void*)args[3], args[4], args[5]);
      break;
    case SDR_VOICE_TRANSFER_STATUS:
      return_value = sceSdVoiceTransStatus(args[1], args[2]);
      break;
    case SDR_BLOCK_TRANSFER_STATUS:
      return_value = sceSdBlockTransStatus(args[1], args[2]);
      break;
#if SDRDRV_SDK >= 2700
    case SDR_STOP_TRANSFER:
      return_value = sceSdStopTrans(args[1]);
      break;
#endif
    case SDR_SET_TRANSFER_HANDLER:
#if SDRDRV_SDK >= 2700
      if (args[1] == 0) {
        if (args[2] == 0)
          return_value = (int)sceSdSetTransIntrHandler(0, 0, 0);
        else
          return_value = (int)sceSdSetTransIntrHandler(0, sdr_transfer0_callback, 0);
      } else {
        if (args[2] == 0)
          return_value = (int)sceSdSetTransIntrHandler(1, 0, 0);
        else
          return_value = (int)sceSdSetTransIntrHandler(1, sdr_transfer1_callback, 0);
      }
#else
      if (args[1] == 0)
        return_value = (int)sceSdSetTransIntrHandler(0, sdr_transfer0_callback, 0);
      else
        return_value = (int)sceSdSetTransIntrHandler(1, sdr_transfer1_callback, 0);
#endif
      break;
    case SDR_SET_SPU2_HANDLER:
#if SDRDRV_SDK >= 2700
      if (args[1] == 0)
        return_value = (int)sceSdSetSpu2IntrHandler(0, 0);
      else
        return_value = (int)sceSdSetSpu2IntrHandler(sdr_spu2_callback, 0);
#else
      return_value = (int)sceSdSetSpu2IntrHandler(sdr_spu2_callback, 0);
#endif
      break;
    case SDR_SET_EFFECT_ATTR:
      return_value = sceSdSetEffectAttr(command & SDR_CORE_MASK, (SdEffectAttr*)args);
      break;
    case SDR_GET_EFFECT_ATTR:
#if SDRDRV_SDK >= 2700
      sceSdGetEffectAttr(command & SDR_CORE_MASK, &sdr_effect_buffer.attributes);
#else
      // BUG: The effect attributes overwrite the four-byte return value and adjacent storage.
      sceSdGetEffectAttr(command & SDR_CORE_MASK, (SdEffectAttr*)&return_value);
#endif
      break;
#if SDRDRV_SDK >= 2700
    case SDR_SET_EFFECT_MODE:
      return_value = sceSdSetEffectMode(command & SDR_CORE_MASK, (SdEffectAttr*)args);
      break;
    case SDR_SET_EFFECT_MODE_PARAMS:
      return_value = sceSdSetEffectModeParams(command & SDR_CORE_MASK, (SdEffectAttr*)args);
      break;
#endif
    case SDR_CLEAR_EFFECT_WORK_AREA:
      return_value = sceSdClearEffectWorkArea(args[1], args[2], args[3]);
      break;
#if SDRDRV_SDK >= 2700
    case SDR_CLEAN_EFFECT_WORK_AREA:
      return_value = sceSdCleanEffectWorkArea(args[1], args[2], args[3]);
      break;
#endif
    case SDR_CHANGE_THREAD_PRIORITY:
      return_value = sceSdrChangeThreadPriority(args[1], args[2]);
      break;
    case SDR_USER_0:
    case SDR_USER_1:
    case SDR_USER_2:
    case SDR_USER_3:
    case SDR_USER_4:
    case SDR_USER_5:
    case SDR_USER_6:
    case SDR_USER_7:
    case SDR_USER_8:
    case SDR_USER_9:
    case SDR_USER_10:
    case SDR_USER_11:
    case SDR_USER_12:
    case SDR_USER_13:
    case SDR_USER_14:
    case SDR_USER_15:
      if (user_functions[(command & 0xf0) >> 4])
        return_value = user_functions[(command & 0xf0) >> 4](command, args, size);
      break;
    case SDR_INIT_CALLBACK:
      thread.attr = TH_C;
      thread.entry = sdr_callback_thread;
      thread.stackSize = SDR_THREAD_STACK_SIZE;
      thread.option = 0;
      thread.initPriority = sdr_callback_thread_priority;
      sdr_callback_tid = CreateThread(&thread);
      StartThread(sdr_callback_tid, 0);
      printf("SDR callback thread created\n");
      break;
    default:
#if SDRDRV_SDK >= 2700
      printf("SDR driver ERROR: unknown command %x \n", command & SDR_COMMAND_MASK);
#else
      printf("SDR driver ERROR: unknown command %d \n", args[0]);
#endif
      break;
  }
#if SDRDRV_SDK >= 2700
  switch (command & SDR_COMMAND_MASK) {
    case SDR_GET_EFFECT_ATTR:
      return &sdr_effect_buffer.attributes;
    case SDR_BATCH_INLINE:
    case SDR_BATCH_EX_INLINE:
      return sdr_batch_buffer;
    default:
      return &return_value;
  }
#else
  return &return_value;
#endif
}

sceSdrUserCommandFunction sceSdrSetUserCommandFunction(int command,
                                                       sceSdrUserCommandFunction function) {
  sceSdrUserCommandFunction previous;
  if (command < SDR_USER_0 || command > SDR_USER_15)
    return (sceSdrUserCommandFunction)-1;
  command = (command & 0xf0) >> 4;
  previous = user_functions[command];
  user_functions[command] = function;
  return previous;
}
