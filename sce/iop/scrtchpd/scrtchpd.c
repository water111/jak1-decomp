#include "sce/iop/scrtchpd/scrtchpd.h"

#include "sce/iop/intrman/intrman.h"
#include "sce/iop/loadcore/loadcore.h"

#define SCRATCHPAD_BASE 0x1f800000
#define SCRATCHPAD_BANK_0 0x1f800800
#define SCRATCHPAD_BANK_1 0x1f800c00
#define SCRATCHPAD_MAPPING (*(unsigned int*)0xfffe0144)

#define SCRATCHPAD_INVALID_MODE (-405)
#define SCRATCHPAD_MAPPING_CHANGED (-428)
#define SCRATCHPAD_BUSY (-429)
#define SCRATCHPAD_NOT_ALLOCATED (-430)

struct ModuleInfo Module = {"ScratchPad_service", 0x0101};
char scrtchpd_build_id[] = "PsIIscrtchpd2700";

struct ScratchPadState {
  int allocated;
  unsigned int generation;
};

struct ScratchPadState state;
extern unsigned int scrtpad_stub[];

int start(void) {
  int intr_state;
  int result;
  CpuSuspendIntr(&intr_state);
  result = RegisterLibraryEntries(scrtpad_stub);
  CpuResumeIntr(intr_state);
  if (result)
    return 1;
  state.allocated = 0;
  state.generation = 0;
  return 2;
}

void* AllocScratchPad(int mode) {
  int intr_state;
  unsigned int address;
  if (mode)
    return (void*)SCRATCHPAD_INVALID_MODE;
  CpuSuspendIntr(&intr_state);
  if (state.allocated) {
    CpuResumeIntr(intr_state);
    return (void*)SCRATCHPAD_BUSY;
  }
  state.allocated = 1;
  state.generation++;
  address = (state.generation & 1) ? SCRATCHPAD_BANK_0 : SCRATCHPAD_BANK_1;
  SCRATCHPAD_MAPPING = address;
  CpuResumeIntr(intr_state);
  return (void*)address;
}

int FreeScratchPad(void* address) {
  int intr_state;
  unsigned int expected;
  CpuSuspendIntr(&intr_state);
  if (!state.allocated) {
    CpuResumeIntr(intr_state);
    return SCRATCHPAD_NOT_ALLOCATED;
  }
  expected = (state.generation & 1) ? SCRATCHPAD_BANK_0 : SCRATCHPAD_BANK_1;
  // BUG: The supplied address is never validated, so an unrelated pointer can free the allocation.
  if (SCRATCHPAD_MAPPING != expected) {
    CpuResumeIntr(intr_state);
    return SCRATCHPAD_MAPPING_CHANGED;
  } else {
    SCRATCHPAD_MAPPING = SCRATCHPAD_BASE;
    state.allocated = 0;
    CpuResumeIntr(intr_state);
    return 0;
  }
}

void scrtchpd_shutdown(int mode) {
  if (!mode) {
    SCRATCHPAD_MAPPING = SCRATCHPAD_BASE;
    state.allocated = 0;
  }
}

void* scrtchpd_get_state(void) {
  return &state;
}
