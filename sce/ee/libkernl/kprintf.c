#include "sce/ee/newlib/stdarg.h"

extern int kputs(const char*);
void kprintf(const char*, ...);
static void serialPutchar(int);

static char linebuf[128];
static int count = 0;
__asm__(".local kputchar,deci2Putchar,linebuf,serialPutchar");
static void (*_putchar)(int) = serialPutchar;

static int kputchar(int c) {
  while (*(volatile unsigned*)0x1000f130 & 0x8000) {
  }
  *(volatile unsigned char*)0x1000f180 = c;
  return c;
}

static void deci2Putchar(int c) {
  if (count >= 126) {
    count = 0;
    linebuf[127] = 0;
    kputs(linebuf);
  }
  if (c == '\n') {
    linebuf[count] = c;
    linebuf[count + 1] = 0;
    count = 0;
    kputs(linebuf);
  } else
    linebuf[count++] = c;
}

static void serialPutchar(int c) {
  if (c == '\n') {
    kputchar('\r');
    kputchar('\n');
  } else
    kputchar(c);
}

static int ftoi(unsigned long bits) {
  register unsigned long upper __asm__("$2");
  register long exponent_bits __asm__("$6");
  unsigned long value = bits;
  long exponent;
  upper = value << 1;
  exponent_bits = upper >> 53;
  exponent = exponent_bits - 1075;
  if (exponent < -53)
    return 0;
  if (exponent >= 13)
    return 9999;
  value = value << 12 >> 12;
  value |= 0x10000000000000UL;
  if (exponent < 0) {
    exponent = -exponent;
    value >>= (int)(exponent - 2);
    if ((value & 3) == 3)
      value = (value >> 2) + 1;
    else
      value >>= 2;
  } else
    value <<= (int)exponent;
  return value;
}

static void printfloat(double value) {
  int exponent = 0, converted;
  if (value < 0.0) {
    value = 0.0 - value;
    _putchar('-');
  }
  if (value < 0.1) {
    while (value < 0.1) {
      value *= 10.0;
      exponent--;
    }
  } else if (value >= 1.0) {
    while (value >= 1.0) {
      value /= 10.0;
      exponent++;
    }
  }
  converted = ftoi(value * 1000000.0);
  kprintf("0.%d", converted);
  if (exponent >= 0)
    kprintf("e+%d", exponent);
  else
    kprintf("e%d", exponent);
}

void _printf(const char* format, va_list arguments) {
  char buffer[32];
  char *cursor, *padding;
  const char* next;
  char* string;
  register int product __asm__("$3");
  int modifier, width;
  register int second __asm__("$6");
  unsigned long unsigned_value;
  long signed_value;
  float real;
  while (*format) {
    padding = 0;
    modifier = 0;
    if (*format == '%') {
    again:
      next = ++format;
      switch (*format) {
        case '0':
          width = format[1] - '0';
          second = format[2];
          if ((unsigned char)width < 10) {
            if ((unsigned)(second - '0') < 10) {
              product = width * 10 - '0';
              width = product + second;
              if (width > 31)
                width = 31;
              format += 2;
            } else
              format++;
            padding = &buffer[31 - width];
            while (width > 0)
              buffer[31 - width--] = '0';
            goto again;
          }
          goto again;
        case 'l':
          modifier = 'l';
          goto again;
        case 'h':
          modifier = 'h';
          goto again;
        case 'o':
          if (modifier == 'l')
            unsigned_value = va_arg(arguments, unsigned long);
          else if (modifier == 'h')
            unsigned_value = va_arg(arguments, unsigned short);
          else
            unsigned_value = va_arg(arguments, unsigned int);
          cursor = &buffer[31];
          *cursor = 0;
          if (!unsigned_value)
            *--cursor = '0';
          else
            while (unsigned_value) {
              *--cursor = (unsigned_value & 7) + '0';
              unsigned_value >>= 3;
            }
          if (padding && padding < cursor)
            cursor = padding;
          while (*cursor)
            _putchar(*cursor++);
          break;
        case 'x':
          if (modifier == 'l')
            unsigned_value = va_arg(arguments, unsigned long);
          else if (modifier == 'h')
            unsigned_value = va_arg(arguments, unsigned short);
          else
            unsigned_value = va_arg(arguments, unsigned int);
          cursor = &buffer[31];
          *cursor = 0;
          if (!unsigned_value)
            *--cursor = '0';
          else
            while (unsigned_value) {
              unsigned long digit = unsigned_value & 15;
              if (digit < 10)
                *--cursor = digit + '0';
              else
                *--cursor = digit + 'a' - 10;
              unsigned_value >>= 4;
            }
          if (padding && padding < cursor)
            cursor = padding;
          while (*cursor)
            _putchar(*cursor++);
          break;
        case 'd':
          if (modifier == 'l')
            signed_value = va_arg(arguments, long);
          else if (modifier == 'h')
            signed_value = va_arg(arguments, short);
          else
            signed_value = va_arg(arguments, int);
          cursor = &buffer[31];
          *cursor = 0;
          if (!signed_value)
            *--cursor = '0';
          else {
            if (signed_value < 0) {
              signed_value = -signed_value;
              _putchar('-');
            }
            while (signed_value) {
              *--cursor = signed_value % 10 + '0';
              signed_value /= 10;
            }
          }
          if (padding && padding < cursor)
            cursor = padding;
          while (*cursor)
            _putchar(*cursor++);
          break;
        case 'u':
          if (modifier == 'l')
            unsigned_value = va_arg(arguments, unsigned long);
          else if (modifier == 'h')
            unsigned_value = va_arg(arguments, unsigned short);
          else
            unsigned_value = va_arg(arguments, unsigned int);
          cursor = &buffer[31];
          *cursor = 0;
          if (!unsigned_value)
            *--cursor = '0';
          else
            while (unsigned_value) {
              *--cursor = unsigned_value % 10 + '0';
              unsigned_value /= 10;
            }
          if (padding && padding < cursor)
            cursor = padding;
          while (*cursor)
            _putchar(*cursor++);
          break;
        case 'f':
        case 'e':
          real = va_arg(arguments, float);
          if (real == 0.0f)
            _putchar('0');
          else
            printfloat(real);
          break;
        case 's':
          string = va_arg(arguments, char*);
          if (!*string) {
            _putchar('(');
            _putchar('n');
            _putchar('u');
            _putchar('l');
            _putchar('l');
            _putchar(')');
          } else {
            cursor = string;
            for (; *cursor; cursor++)
              _putchar(*cursor);
          }
          break;
        case 'c':
          signed_value = va_arg(arguments, char);
          _putchar((int)signed_value);
          break;
      }
      goto increment;
    }
    _putchar(*format);
    next = format;
  increment:
    next++;
    format = next;
  }
}

void kprintf(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  _printf(format, arguments);
  va_end(arguments);
}

void scePrintf(const char* format, ...) {
  va_list arguments;
  void (*saved)(int) = _putchar;
  _putchar = deci2Putchar;
  va_start(arguments, format);
  _printf(format, arguments);
  va_end(arguments);
  _putchar = saved;
}
