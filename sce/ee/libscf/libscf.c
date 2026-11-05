#include "sce/ee/libscf/libscf.h"
#include "sce/ee/libkernl/eekernel.h"
#include "sce/ee/libkernl/sifdev.h"
#include "sce/ee/newlib/assert.h"
#include "sce/ee/newlib/stdio.h"

static char __ps2_libinfo__[16] = "PsIIlibscf  2200";
static sceScfT10kConfig gConfig = {540, 0, 0, 0, 0, 0, 0};
static char gRomName[15] = {0};

static char* GetRomName(void) {
  if (!gRomName[0]) {
    int fd = sceOpen("rom0:ROMVER", 1);
    if (fd == -1)
      printf("Can't open rom0:ROMVER\n");
    if (sceRead(fd, gRomName, 14) == -1)
      printf("Can't read rom error\n");
    sceClose(fd);
  }
  return gRomName;
}

static int IsT10K(void) {
  if (!gRomName[0])
    GetRomName();
  return gRomName[4] == 'T';
}

int sceScfGetLanguage(void) {
  unsigned config;
  int result;
  GetOsdConfigParam(&config);
  if (IsT10K())
    result = gConfig.language;
  else {
    GetOsdConfigParam(&config);
    if (!((config >> 13) & 7))
      result = (config >> 4) & 1;
    else
      result = (config >> 16) & 31;
  }
  return result;
}

void sceScfSetT10kConfig(sceScfT10kConfig* config) {
  gConfig = *config;
}

int sceScfGetAspect(void) {
  unsigned config;
  if (IsT10K())
    return gConfig.aspect;
  GetOsdConfigParam(&config);
  return (config >> 1) & 3;
}

int sceScfGetSpdif(void) {
  unsigned config;
  if (IsT10K())
    return gConfig.spdif;
  GetOsdConfigParam(&config);
  return config & 1;
}

int sceScfGetTimeZone(void) {
  unsigned config;
  int timezone;
  if (IsT10K())
    timezone = gConfig.timezone;
  else {
    GetOsdConfigParam(&config);
    timezone = 540;
    if ((config >> 13) & 7) {
      timezone = (int)config >> 21;
      printf("Timezone=%d\n", timezone);
    }
  }
  return timezone;
}

int sceScfGetDateNotation(void) {
  struct {
    unsigned base;
    unsigned char extended;
  } config;

  int result;
  if (IsT10K())
    result = gConfig.date_format;
  else {
    GetOsdConfigParam(&config.base);
    if (!((config.base >> 13) & 7))
      result = 0;
    else {
      GetOsdConfigParam2(&config.extended, 1, 1);
      result = config.extended >> 6;
      printf("DateNotation=%d\n", result);
    }
  }
  return result;
}

int sceScfGetSummerTime(void) {
  struct {
    unsigned base;
    unsigned char extended;
  } config;

  int result;
  if (IsT10K())
    result = gConfig.summer;
  else {
    GetOsdConfigParam(&config.base);
    if (!((config.base >> 13) & 7))
      result = 0;
    else {
      GetOsdConfigParam2(&config.extended, 1, 1);
      result = (config.extended >> 4) & 1;
      printf("SummerTime=%d\n", result);
    }
  }
  return result;
}

int sceScfGetTimeNotation(void) {
  struct {
    unsigned base;
    unsigned char extended;
  } config;

  int result;
  if (IsT10K())
    result = gConfig.time_format;
  else {
    GetOsdConfigParam(&config.base);
    if (!((config.base >> 13) & 7))
      result = 0;
    else {
      GetOsdConfigParam2(&config.extended, 1, 1);
      result = (config.extended >> 5) & 1;
      printf("TimeNotation=%d\n", result);
    }
  }
  return result;
}

#define CHECK(condition, line, message) \
  if (!(condition))                     \
  __assert("libscf.c", line, message)
#define CHECK_CLOCK(line) CHECK(prtc != 0, line, "prtc != NULL")

static unsigned char tobcd(unsigned char c) {
  CHECK(c <= 99, 281, "c <=99");
  return c + (c / 10) * 6;
}

static unsigned char frombcd(unsigned char c) {
  CHECK(c <= 0x99, 294, "c <= 0x99");
  {
    unsigned short product = (c >> 4) * 6;
    return c - product;
  }
}

static void convertfrombcd(sceCdCLOCK* prtc) {
  CHECK_CLOCK(306);
  prtc->year = frombcd(prtc->year);
  prtc->month = frombcd(prtc->month);
  prtc->day = frombcd(prtc->day);
  prtc->hour = frombcd(prtc->hour);
  prtc->minute = frombcd(prtc->minute);
  prtc->second = frombcd(prtc->second);
}

static void converttobcd(sceCdCLOCK* prtc) {
  CHECK_CLOCK(321);
  prtc->year = tobcd(prtc->year);
  prtc->month = tobcd(prtc->month);
  prtc->day = tobcd(prtc->day);
  prtc->hour = tobcd(prtc->hour);
  prtc->minute = tobcd(prtc->minute);
  prtc->second = tobcd(prtc->second);
}

static void adddate(sceCdCLOCK* prtc) {
  char days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  CHECK_CLOCK(336);
  prtc->day++;
  if (prtc->year % 4 == 0)
    days[1] = 29;
  if (prtc->day > days[prtc->month - 1]) {
    prtc->day = 1;
    if (++prtc->month == 13) {
      if (prtc->year == 99)
        prtc->year = 0;
      else
        prtc->year++;
      prtc->month = 1;
    }
  }
}

static void subdate(sceCdCLOCK* prtc) {
  char days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  CHECK_CLOCK(360);
  prtc->day--;
  if (prtc->year % 4 == 0)
    days[1] = 29;
  if (!prtc->day) {
    if (!--prtc->month) {
      if (!prtc->year)
        prtc->year = 99;
      else
        prtc->year--;
      prtc->month = 12;
    }
    prtc->day = days[prtc->month - 1];
  }
}

static void addhour(sceCdCLOCK* prtc) {
  CHECK_CLOCK(385);
  if (++prtc->hour == 24) {
    prtc->hour = 0;
    adddate(prtc);
  }
}

static void subhour(sceCdCLOCK* prtc) {
  CHECK_CLOCK(398);
  if (!prtc->hour) {
    prtc->hour = 23;
    subdate(prtc);
  } else
    prtc->hour--;
}

static void AdjustTime(sceCdCLOCK* prtc, int offset) {
  CHECK_CLOCK(416);
  CHECK(offset >= -1440 && offset <= 1440, 417, "-60*24<=diff && diff <= 60*24");
  convertfrombcd(prtc);
  {
    int minute = prtc->minute;
    minute += offset;
    offset = minute;
  }
  if (offset >= 0) {
    while (offset > 60) {
      offset -= 60;
      addhour(prtc);
    }
  } else {
    while (offset < 0) {
      offset += 60;
      subhour(prtc);
    }
  }
  prtc->minute = offset;
  converttobcd(prtc);
}

void sceScfGetGMTfromRTC(sceCdCLOCK* prtc) {
  CHECK_CLOCK(444);
  AdjustTime(prtc, -540);
}

void sceScfGetLocalTimefromRTC(sceCdCLOCK* prtc) {
  int offset = sceScfGetTimeZone();
  int summer = sceScfGetSummerTime() * 60 - 540;
  offset += summer;
  CHECK_CLOCK(455);
  AdjustTime(prtc, offset);
}
