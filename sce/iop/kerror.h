#ifndef _KERROR_H
#define _KERROR_H

enum KernelErrorCode {
  KE_CPUDI = -102,
  KE_SEMA_ZERO = -419,
  KE_MBOX_NOMSG = -424,  //
  KE_WAIT_DELETE = -425
};
#endif
