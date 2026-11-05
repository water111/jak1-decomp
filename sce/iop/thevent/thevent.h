#ifndef SCE_IOP_THEVENT_H
#define SCE_IOP_THEVENT_H

struct EventFlagParam {
  unsigned int attr;
  unsigned int option;
  unsigned int initPattern;
};

#define EA_MULTI 2
#define EW_AND 0
#define EW_OR 1
#define EW_CLEAR 0x10

int CreateEventFlag(struct EventFlagParam* param);
int DeleteEventFlag(int id);
int iClearEventFlag(int id, unsigned int bits);
int SetEventFlag(int id, unsigned int bits);
int iSetEventFlag(int id, unsigned int bits);
int ClearEventFlag(int id, unsigned int bits);
int WaitEventFlag(int id, unsigned int bits, int mode, unsigned int* result);

#endif
