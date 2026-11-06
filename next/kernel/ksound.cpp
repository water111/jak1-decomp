#include "next/kernel/ksound.h"
#include "next/kernel/kdgo.h"
#include "next/kernel/kscheme.h"
#include "sce/ee/libpc/libpc.h"

void InitSound() {
}

void ShutdownSound() {
}

void InitSoundScheme() {
  make_function_symbol_from_c("rpc-call", (unsigned int (*)())RpcCall);
  make_function_symbol_from_c("rpc-busy?", (unsigned int (*)())RpcBusy);
  make_function_symbol_from_c("test-load-dgo-c", (unsigned int (*)())LoadDGOTest);
}
