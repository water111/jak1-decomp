#ifndef SCE_IOP_THMSGBX_H
#define SCE_IOP_THMSGBX_H

#include "sce/iop/kerror.h"

#include "sce/iop/types.h"

struct MsgPacket {
  struct MsgPacket* next;
  u_char msgPriority;
  u_char dummy[3];
};

extern int SendMbx(int mbxid, struct MsgPacket* sendmsg);
extern int PollMbx(struct MsgPacket** recvmsg, int mbxid);

struct MbxParam {
  u_int attr;
  u_int option;
};

extern int CreateMbx(struct MbxParam* param);

#endif
