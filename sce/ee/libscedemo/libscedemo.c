#include "sce/ee/libscedemo/libscedemo.h"
#include "sce/ee/eeregs.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/newlib/stdio.h"

static char* libSceDemoVersion = "1.0p2";
char** argv_copy;
int argc_copy;

void sceDemoStart(int argc,
                  char** argv,
                  unsigned short* a,
                  unsigned short* b,
                  unsigned short* c,
                  unsigned short* d,
                  unsigned short* e,
                  unsigned short* f,
                  unsigned short* g,
                  unsigned short* h) {
  void simpleatoi(char* text, unsigned short* value) {
    int i;
    *value = 0;
    for (i = 0; text[i]; i++)
      *value = *value * 10 + text[i] - '0';
  }
  argc_copy = argc;
  argv_copy = argv;
  if (argc >= 9) {
    simpleatoi(argv[1], a);
    simpleatoi(argv[2], b);
    simpleatoi(argv[3], c);
    simpleatoi(argv[4], d);
    simpleatoi(argv[5], e);
    simpleatoi(argv[6], f);
    simpleatoi(argv[7], g);
    simpleatoi(argv[8], h);
  }
}

void sceDemoEnd(int result) {
  *GS_BGCOLOR = 0;
  *GS_PMODE = 0;
  sprintf(argv_copy[9], "%d", result);
  LoadExecPS2("cdrom0:\\SCE_DD\\SCE_DD.ELF;1", argc_copy, argv_copy);
}
