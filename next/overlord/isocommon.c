#include "overlord/isocommon.h"

// Convert to 8.3 padded with spaces and null terminator.
// FOO     BAR\0
void MakeISOName(char* dest, const char* src) {
  int i = 0;
  char* out = dest;
  char c;

  while (i < 8 && *src && *src != '.') {
    c = *src++;
    if (c >= 'a' && c <= 'z')
      c -= 0x20;
    *out++ = c;
    i++;
  }
  while (i < 8) {
    *out++ = ' ';
    i++;
  }
  if (*src == '.')
    src++;
  while (i < 11 && *src) {
    c = *src++;
    if (c >= 'a' && c <= 'z')
      c -= 0x20;
    *out++ = c;
    i++;
  }
  while (i < 11) {
    *out++ = ' ';
    i++;
  }
  *out = 0;
}

// ISO name back to normal: FOO.BAR
void UnmakeISOName(char* dest, const char* src) {
  int i = 0;
  const char* in = src;

  while (i < 8 && *in != ' ') {
    *dest++ = *in++;
    i++;
  }
  in += 8 - i;
  if (*in != ' ') {
    *dest++ = '.';
    for (i = 0; i < 3 && *in != ' '; i++)
      *dest++ = *in++;
  }
  *dest++ = 0;
}

// shorten animation name to ISO name.
// Keep first two characters of first word.
// take one character from each following dash-separated word.
// words ending in a digit add that digit
// abbreviate resolution as z, accept as y, reject as n.
// fill remaining space with rest of the last word, up to 8 chars
// eg: minershort-resolution-1-orbs -> MIZ1ORBS
void ISONameFromAnimationName(IsoName* dest, const char* src) {
  int i = 0;
  const char* next;
  char c;

  dest->chars[i++] = src[0];
  dest->chars[i++] = src[1];
  while (*src && *src != '-')
    src++;
  if (*src) {
    src++;
    while (i < 8) {
      next = src;
      while (*next && *next != '-')
        next++;
      if (!*next)
        break;
      if (next[-1] >= '0' && next[-1] <= '9') {
        c = next[-1];
      } else if (next - src == 10 && !memcmp(src, "resolution", 10)) {
        c = 'z';
      } else if (next - src == 6 && !memcmp(src, "accept", 6)) {
        c = 'y';
      } else if (next - src == 6 && !memcmp(src, "reject", 6)) {
        c = 'n';
      } else {
        c = *src;
      }
      dest->chars[i++] = c;
      src = next + 1;
    }
    while (*src && i < 8)
      dest->chars[i++] = *src++;
  }
  while (i < 8) {
    dest->chars[i++] = ' ';
  }
  i = 0;
  while (i < 8) {
    if (dest->chars[i] >= 'a' && dest->chars[i] <= 'z')
      dest->chars[i] -= 0x20;
    i++;
  }
  strcpy(dest->chars + 8, "STR");
}
