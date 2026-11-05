#ifndef SCE_EE_LIBSCF_LIBSCF_H
#define SCE_EE_LIBSCF_LIBSCF_H

#include "sce/ee/libcdvd/libcdvd.h"

typedef struct {
  short timezone;
  unsigned char aspect, date_format, language, spdif, summer, time_format;
} sceScfT10kConfig;

void sceScfSetT10kConfig(sceScfT10kConfig* config);
int sceScfGetLanguage(void);
int sceScfGetAspect(void);
int sceScfGetSpdif(void);
int sceScfGetTimeZone(void);
int sceScfGetDateNotation(void);
int sceScfGetSummerTime(void);
int sceScfGetTimeNotation(void);
void sceScfGetGMTfromRTC(sceCdCLOCK* rtc);
void sceScfGetLocalTimefromRTC(sceCdCLOCK* rtc);

#endif
