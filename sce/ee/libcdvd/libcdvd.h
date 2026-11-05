#ifndef SCE_EE_LIBCDVD_LIBCDVD_H
#define SCE_EE_LIBCDVD_LIBCDVD_H

typedef struct {
  unsigned char status, second, minute, hour, reserved, day, month, year;
} sceCdCLOCK;

#endif
