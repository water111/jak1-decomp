#ifndef SCE_EE_LIBSCF_LIBSCF_H
#define SCE_EE_LIBSCF_LIBSCF_H

#ifdef __cplusplus
extern "C" {
#endif

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

enum SceLanguage {
  SCE_JAPANESE_LANGUAGE = 0,
  SCE_ENGLISH_LANGUAGE = 1,
  SCE_FRENCH_LANGUAGE = 2,
  SCE_SPANISH_LANGUAGE = 3,
  SCE_GERMAN_LANGUAGE = 4,
  SCE_ITALIAN_LANGUAGE = 5,
  SCE_DUTCH_LANGUAGE = 6,
  SCE_PORTUGUESE_LANGUAGE = 7
};

// PS2 system configuration display modes.
enum SceAspect {
  SCE_ASPECT_43 = 0,
  SCE_ASPECT_FULL = 1,
  SCE_ASPECT_169 = 2
};

#ifdef __cplusplus
}
#endif

#endif
