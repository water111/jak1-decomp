#include "overlord/isocommon.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/string.h"

// Development utility to read sound bank definition files and apply updates.
// likely used for interactive soundbank file editing.

static void strcpy_toupper(char* dest, const char* src) {
  while (*src) {
    *dest = *src >= 'a' && *src <= 'z' ? *src - 32 : *src;
    dest++;
    src++;
  }
  *dest = 0;
}

static int atoi(const char* str) {
  int value = 0;
  while (*str >= '0' && *str <= '9') {
    value = value * 10 + (*str - '0');
    str++;
  }
  return value;
}

void ReadBankSoundInfo(SoundBank* bank, const char* name, int host) {
  int fd;
  char line[128];
  char* cursor;
  char buffer[128];
  int offset = 0;
  int count = 0;
  int i;
  char filename[128];
  for (i = 0; i < bank->sound_count; i++) {
#if REVISION <= 20010903
    bank->sound[i].falloff_min = 5;
    bank->sound[i].falloff_max = 30;
    bank->sound[i].falloff_curve = 1;
#else
    bank->sound[i].falloff = 5 | (30 << 14) | (1 << 28);
#endif
  }
  if (host)
    sprintf(filename, "host0:data/sound1/%s.txt", name);
  else
    sprintf(filename, "data/sound1/%s.txt", name);
  fd = open(filename, 1);
  if (fd < 0) {
    printf("Can't open %s - using defaults for all sounds\n", filename);
    return;
  }
  printf("file open %s\n", filename);
  cursor = line;
  for (;;) {
    char c;
    if (offset == count) {
      count = read(fd, buffer, 128);
      offset = 0;
    }
    if (offset < count) {
      c = buffer[offset++];
      i = 1;
    } else {
      c = '\n';
      i = 0;
    }
    if (c == '\n' || c == '\r' || !c) {
      *cursor++ = 0;
      cursor = line;
      while (*cursor && *cursor != ' ')
        cursor++;
      if (*cursor) {
        char* source = line;
        char normalized[64];
        char* dest = normalized;
        int index;
        char letter;
        while (source < cursor) {
          letter = *source++;
          if (letter >= 'A' && letter <= 'Z')
            letter += 32;
          else if (letter == '_')
            letter = '-';
          *dest++ = letter;
        }
        while (dest < normalized + 64)
          *dest++ = 0;
        for (index = 0; index < bank->sound_count; index++) {
          if (!memcmp(bank->sound[index].name, normalized, 16)) {
            char* start = ++cursor;
            int min = 5;
            int max = 30;
            int curve = 1;
            while (*cursor && *cursor != ' ')
              cursor++;
            if (*cursor)
              *cursor++ = 0;
            if (cursor > start)
              min = atoi(start);
            start = cursor;
            while (*cursor && *cursor != ' ')
              cursor++;
            if (*cursor)
              *cursor++ = 0;
            if (cursor > start)
              max = atoi(start);
            start = cursor;
            while (*cursor && *cursor != ' ')
              cursor++;
            if (*cursor)
              *cursor++ = 0;
            if (cursor > start)
              curve = atoi(start);
#if REVISION <= 20010903
            bank->sound[index].falloff_min = min;
            bank->sound[index].falloff_max = max;
            bank->sound[index].falloff_curve = curve;
#else
            bank->sound[index].falloff = (max << 14) | min | (curve << 28);
#endif
            break;
          }
        }
        if (index == bank->sound_count)
          printf("Can't find sound %s\n", normalized);
      }
      cursor = line;
      if (!i)
        break;
    } else if (c == '\t' || c == ' ') {
      if (cursor == line)
        *cursor++ = ' ';
      else if (cursor[-1] != ' ')
        *cursor++ = ' ';
    } else {
      *cursor++ = c;
    }
  }
  close(fd);
  printf("Loaded OK\n");
  return;
}

void ReadBankSoundNames(SoundBank* bank, const char* name, int host) {
  char upper[64];
  char prefix[64];
  int prefix_length;
  int fd;
  char line[128];
  char* cursor;
  char buffer[128];
  int offset = 0;
  int count = 0;
  char filename[128];
  bank->bank_handle = 0;
  bank->sound_count = 0;
  memset(bank->name, 0, 16);
  memcpy(bank->name, name, strlen(name));
  if (host)
    sprintf(filename, "host0:data/sound1/%s.h", name);
  else
    sprintf(filename, "data/sound1/%s.h", name);
  fd = open(filename, 1);
  if (fd < 0) {
    printf("sbank: file read !open: '%s' (%d)\n", filename, fd);
    return;
  }
  printf("file open %s\n", filename);
  strcpy_toupper(upper, name);
  sprintf(prefix, "#define %s_BNK", upper);
  prefix_length = strlen(prefix);
  cursor = line;
  for (;;) {
    char c;
    int more;
    if (offset == count) {
      count = read(fd, buffer, 128);
      offset = 0;
    }
    if (offset < count) {
      c = buffer[offset++];
      more = 1;
    } else {
      c = '\n';
      more = 0;
    }
    if (c == '\n' || c == '\r' || !c) {
      *cursor++ = 0;
      if (!strncmp(line, "#define ", 8) && strncmp(line, prefix, prefix_length)) {
        char* source = line + 8;
        char normalized[64];
        char* dest = normalized;
        char letter;
        while (*source && *source != ' ') {
          letter = *source++;
          if (letter >= 'A' && letter <= 'Z')
            letter += 32;
          else if (letter == '_')
            letter = '-';
          *dest++ = letter;
        }
        while (dest < normalized + 64)
          *dest++ = 0;
        memcpy(bank->sound[bank->sound_count].name, normalized, 16);
        bank->sound_count++;
      }
      cursor = line;
      if (!more)
        break;
    } else if (c == '\t' || c == ' ') {
      if (cursor == line)
        *cursor++ = ' ';
      else if (cursor[-1] != ' ')
        *cursor++ = ' ';
    } else {
      *cursor++ = c;
    }
  }
  close(fd);
  printf("Loaded OK\n");
  ReadBankSoundInfo(bank, name, host);
  return;
}

void PrintBankInfo(SoundBank* bank) {
  int i;
  printf("Bank %s\n\n", bank->name);
  for (i = 0; i < bank->sound_count; i++) {
    SoundRecord* sound = &bank->sound[i];
#if REVISION <= 20010903
    printf("%d : %16s : min %d max %d curve %d\n", i, sound->name, sound->falloff_min,
           sound->falloff_max, sound->falloff_curve);
#else
    printf("%d : %16s : min %d max %d curve %d\n", i, sound->name, sound->falloff & 0x3fff,
           (sound->falloff >> 14) & 0x3fff, sound->falloff >> 28);
#endif
  }
}
