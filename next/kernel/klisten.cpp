#include "next/kernel/klisten.h"
#include "next/kernel/kboot.h"
#include "next/kernel/kdsnetm.h"
#include "next/kernel/klink.h"
#include "next/kernel/kmalloc.h"
#include "next/kernel/kprint.h"
#include "next/kernel/kscheme.h"
#include "next/kernel/ksocket.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/stdlib.h"
#include "sce/ee/newlib/string.h"

unsigned int* ListenerLinkBlock = 0;
unsigned int* ListenerFunction = 0;
void (**kernel_dispatcher)() = 0;
symbol* kernel_packages = 0;
unsigned int* print_column = 0;
unsigned int ListenerStatus = 0;

// Set the listener symbols and add the kernel to the package list.
void InitListener() {
  DECLARE_SYMBOL_TABLE();
  ListenerLinkBlock = (unsigned int*)intern_from_c("*listener-link-block*");
  ListenerFunction = (unsigned int*)intern_from_c("*listener-function*");
  kernel_dispatcher = (void (**)())intern_from_c("kernel-dispatcher");
  kernel_packages = intern_from_c("*kernel-packages*");
  print_column = (unsigned int*)intern_from_c("*print-column*");
  *ListenerLinkBlock = symbol_table;
  *ListenerFunction = symbol_table;
  kernel_packages->value =
      new_pair(FIX_SYM(FIX_SYM_GLOBAL_HEAP), (type*)FIX_SYM_VALUE(FIX_SYM_PAIR_TYPE),
               make_string_from_c("kernel"), kernel_packages->value);
  if (MasterDebug)
    SendFromBufferD(MSG_ACK, 0, AckBufArea + sizeof(ListenerMessageHeader), 0);
}

// Flush to the listener if connected, or to stdout in retail mode.
void ClearPending() {
  if (MasterDebug) {
    if (ListenerStatus) {
      if (OutputPending) {
        SendFromBuffer(OutputBufArea + sizeof(ListenerMessageHeader),
                       strlen(OutputBufArea + sizeof(ListenerMessageHeader)));
        clear_output();
      }
      if (PrintPending) {
        int remaining = strlen(PrintBufArea + sizeof(ListenerMessageHeader));
        int size;
        char* message = PrintBufArea + sizeof(ListenerMessageHeader);
        while (remaining > 0) {
          // clang-format off
          size = remaining <? 64000;
          // clang-format on
          SendFromBufferD(MSG_PRINT, 0, message, size);
          remaining -= size;
          message += size;
        }
        clear_print();
      }
    }
  } else {
    if (PrintPending) {
      int size = strlen(PrintBufArea + sizeof(ListenerMessageHeader));
      if (size > 0)
        printf("%s", PrintBufArea + sizeof(ListenerMessageHeader));
      clear_print();
    }
  }
}

// Send back the message ID in an ack message.
void SendAck() {
  if (MasterDebug)
    SendFromBufferD(MSG_ACK, GoalProtoBlock.msg_id, AckBufArea + sizeof(ListenerMessageHeader), 0);
}

// Handle commands from the listener.
void ProcessListenerMessage(char* incoming) {
  DECLARE_SYMBOL_TABLE();
  char* message = incoming;
  int unused;
  int kind = GoalProtoBlock.msg_kind;

  // record that the listener is actually connected
  ListenerStatus = 1;

  switch (kind) {
    case LTT_MSG_RESET:
      MasterExit = 1;
      break;
    case LTT_MSG_POKE:
      ClearPending();
      break;
    case LTT_MSG_CODE: {
      int size = MessCount;
      link_block_v3* block;
      char* buffer = kmalloc(kdebugheap, size, 0, "listener-link-buf");
      memcpy(buffer, message, size);
      block = (link_block_v3*)(buffer + GOAL_BASIC_TAG);
      *ListenerLinkBlock = (unsigned int)block;
      void* entry = link_and_exec(buffer, "*listener*", size - block->length, kdebugheap,
                                  LINK_FLAG_FORCE_DEBUG);
      *ListenerFunction = (unsigned int)entry;
      return;
    }
    case LTT_MSG_INSPECT:
      inspect_object(atoi(message));
      ClearPending();
      break;
    case LTT_MSG_PRINT:
      print_object(atoi(message));
      ClearPending();
      break;
    case LTT_MSG_PRINT_SYMBOLS: {
      unsigned int sym;
      for (sym = symbol_table; sym < LastSymbol; sym += 8) {
        if (SYMBOL_HASH(sym))
          inspect_object(sym);
      }
      for (sym = SymbolTable2; sym < symbol_table; sym += 8) {
        if (SYMBOL_HASH(sym))
          inspect_object(sym);
      }
      ClearPending();
      break;
    }
    default:
      MsgErr("dkernel: unknown message error: <%d> of %d bytes\n", kind, MessCount);
      break;
  }
  SendAck();
}
