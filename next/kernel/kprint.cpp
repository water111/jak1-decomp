// Print/message buffers and implementation of GOAL format.
#include "next/kernel/kprint.h"
#include "next/kernel/kscheme.h"

#include "next/kernel/fileio.h"
#include "next/kernel/kboot.h"
#include "next/kernel/kdsnetm.h"
#include "next/kernel/klink.h"
#include "next/kernel/kmalloc.h"
#include "sce/ee/libkernl/sifdev.h"
#include "sce/ee/libpc/libpc.h"
#include "sce/ee/newlib/math.h"
#include "sce/ee/newlib/stdarg.h"
#include "sce/ee/newlib/stdio.h"
#include "sce/ee/newlib/string.h"

// Pointers into the Output and Print buffers
char* OutputPending = 0;
char* PrintPending = 0;

// int -> hex conversion
char ConvertTable[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                         '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

// Buffer for constructing ack messages.
char AckBufArea[40] __attribute__((aligned(64))) = {0};

// Allocate message buffers.
// If debug mode is off, only allocate the print buffer.
void init_output() {
  if (MasterDebug) {
    MessBufArea = kmalloc(kdebugheap, DEBUG_MESSAGE_BUFFER_SIZE, KMALLOC_MEMSET | KMALLOC_ALIGN_256,
                          "mess-buf");
    OutputBufArea = kmalloc(kdebugheap, DEBUG_OUTPUT_BUFFER_SIZE,
                            KMALLOC_MEMSET | KMALLOC_ALIGN_256, "output-buf");
    PrintBufArea = kmalloc(kdebugheap, DEBUG_PRINT_BUFFER_SIZE, KMALLOC_MEMSET | KMALLOC_ALIGN_256,
                           "print-buf");
  } else {
    MessBufArea = 0;
    OutputBufArea = 0;
    PrintBufArea =
        kmalloc(kglobalheap, PRINT_BUFFER_SIZE, KMALLOC_MEMSET | KMALLOC_ALIGN_256, "print-buf");
  }
}

// Clear the output buffer.
void clear_output() {
  if (MasterDebug) {
    kstrcpy(OutputBufArea + sizeof(ListenerMessageHeader), (char*)"");
    OutputPending = 0;
  }
}

// Clear the print buffer.
void clear_print() {
  PrintBufArea[sizeof(ListenerMessageHeader)] = 0;
  PrintPending = 0;
}

// Add a message to the output buffer to inform the listener that the kernel was
// reset.
void reset_output() {
  // report the symbol table address:
  DECLARE_SYMBOL_TABLE();
  if (MasterDebug) {
    sprintf(OutputBufArea + sizeof(ListenerMessageHeader), "reset #x%x\n", symbol_table);
    OutputPending = OutputBufArea + sizeof(ListenerMessageHeader);
  }
}

// Notify the compiler that an object was unloaded.
void output_unload(char* name) {
  if (MasterDebug) {
    sprintf(strend(OutputBufArea + sizeof(ListenerMessageHeader)), "unload \"%s\"\n", name);
    OutputPending = OutputBufArea + sizeof(ListenerMessageHeader);
  }
}

// Notify the compiler the name of a segment and its addresses.
void output_segment_load(char* name, link_block_v3* link_block, unsigned int flags) {
  if (MasterDebug) {
    sprintf(strend(OutputBufArea + sizeof(ListenerMessageHeader)),
            "load \"%s\" %s #x%x #x%x #x%x\n", name, (flags & LINK_FLAG_OUTPUT_TRUE) ? "t" : "nil",
            link_block->segments[0].data + 4, link_block->segments[1].data + 4,
            link_block->segments[2].data + 4);
    OutputPending = OutputBufArea + sizeof(ListenerMessageHeader);
  }
}

// Print to print buffer.
void cprintf(char* format, ...) {
  va_list args;
  va_start(args, format);
  PrintPending = strend(PrintPending ? PrintPending : PrintBufArea + sizeof(ListenerMessageHeader));
  vsprintf(PrintPending, format, args);
  va_end(args);
}

// Print to standard output. Category is ignored.
void Msg(int category, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

// Print a warning to standard output.
void MsgWarn(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

// Print an error to standard output.
void MsgErr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

// Reverse a null-terminated string in place.
void reverse(char* string) {
  register int temporary;
  register int left = 0;
  register int right = strlen(string) - 1;
  for (; left < right; left++, right--) {
    temporary = string[left];
    string[left] = string[right];
    string[right] = temporary;
  }
}

// Round a decimal digit sequence, carrying into an extra leading digit if
// needed.
char* round(float fraction,
            int* exponent,
            char* start,
            register char* end,
            char next_digit,
            int* sign) {
  // get last digit
  float digit;
  if (fraction != 0.0f) {
    modff(fraction * 10.0f, &digit);
  } else {
    digit = next_digit - '0';
  }

  // check if we should round up
  if (digit > 4.0f) {
    // round up and do carries
    for (;; end--) {
      if (*end == '.')
        end--;
      if (++*end <= '9')
        break;
      *end = '0';
      if (end == start) {
        if (exponent) {
          *end = '1';
          (*exponent)++;
        } else {
          *--end = '1';
          start--;
        }
        break;
      }
    }
  } else if (*sign == '-') {
    for (;; end--) {
      if (*end == '.')
        end--;
      if (*end != '0')
        break;
      if (end == start)
        *sign = 0;
    }
  }
  return start;
}

// Convert a float to a decimal string.
int cvt_float(float value, int precision, int* sign, char* start, char* end, unsigned int flags) {
  char* reverse_ptr;
  char* output;
  float fraction;
  int exponent = 0;
  int integer_digits;
  int trim_zeros = 0;
  float integer;
  float digit;
  *start = 0;
  integer_digits = 0;
  if (value < 0.0f) {
    value = -value;
    *sign = '-';
  } else {
    *sign = 0;
  }
  if ((*(unsigned int*)&value & 0x7fffffff) == 0x7fffffff) {
    kstrcpy(start, "NaN");
    return 3;
  }
  fraction = modff(value, &integer);
  output = ++start;
  for (reverse_ptr = end - 1; reverse_ptr >= start && integer != 0.0f; integer_digits++) {
    float quotient = integer / 10.0f;
    digit = modff(quotient, &integer);
    *reverse_ptr-- = (int)(digit * 10.0f + 0.5f) + '0';
  }
  if (integer_digits) {
    while (++reverse_ptr < end)
      *output++ = *reverse_ptr;
  } else {
    *output++ = '0';
  }
  if (precision)
    *output++ = '.';
  if (fraction != 0.0f) {
    if (precision) {
      do {
        fraction = modff(fraction * 10.0f, &digit);
        *output++ = (int)digit + '0';
      } while (--precision && fraction != 0.0f);
    }
    if (fraction != 0.0f && (flags & 1)) {
      start = round(fraction, 0, start, output - 1, 0, sign);
    }
  }
  while (--precision != -1)
    *output++ = '0';
  return output - start;
}

// Convert a float to a padded, null-terminated decimal string.
void ftoa(char* output, float value, int length, char pad, int precision, unsigned int flags) {
  char buffer[256];
  char* digits = buffer;
  int count;
  int total;
  int sign;
  int i;
  count = cvt_float(value, precision, &sign, digits, digits + 127, flags);
  if (count > 63) {
    kstrcpy(digits, "NaN");
    count = 3;
    sign = 0;
  }
  if (*digits == 0)
    digits++;
  total = (sign != 0) + count;
  if (length > 0 && total < length) {
    for (i = 0; i < length - total; i++)
      *output++ = pad;
  }
  if (sign)
    *output++ = sign;
  for (i = 0; i < count; i++)
    *output++ = *digits++;
  *output++ = 0;
}

// Convert a 64-bit integer to a string, optionally padding or trimming
// sign-extension digits.
char* kitoa(char* buffer,
            long long value,
            long long base,
            int length,
            char pad,
            unsigned int flags) {
  register int count;
  register int negative = 0;
  int unused;
  char extension;
  register unsigned long long magnitude;
  if (value < 0 && base == 10) {
    magnitude = -value;
    negative = value;
  } else {
    magnitude = value;
  }
  count = 0;
  do {
    buffer[count++] = ConvertTable[magnitude % (int)base];
  } while (magnitude = magnitude / (int)base);
  if (negative < 0)
    buffer[count++] = '-';
  if (length - count > 0) {
    for (length -= count; length > 0; length--)
      buffer[count++] = pad;
  }
  if (length > 0 && value < 0 && (base == 2 || base == 16) && length < count) {
    extension = base == 16 ? 'f' : '1';
    while (length < count) {
      if (buffer[count - 1] != extension)
        break;
      count--;
    }
    if (!(flags & 2))
      buffer[count++] = '-';
  }
  buffer[count] = 0;
  reverse(buffer);
  return buffer;
}

// Print a quadword as two 64-bit halves.
char* kqtoa(char* buffer, int128 value, long long base, int length, char pad, unsigned int flags) {
  long long high;
  long long low;
  asm("pcpyud %0,%1,$0" : "=r"(high) : "r"(value));
  asm("pcpyud %0,$0,%1" : "=r"(low) : "r"(value));
  kitoa(buffer, high, base, length - 16, pad, flags);
  kitoa(strend(buffer), low, base, 16, '0', 0);
  return buffer;
}

// Format a GOAL string into the requested destination.
unsigned int format(unsigned int destination, gstring* control, ...) {
  DECLARE_SYMBOL_TABLE();
  va_list args;
  char* output_ptr;
  char* format_ptr;
  char* format_cstring;
  char* arg_start;
  int indentation;
  char* saved_pending;
  char* start_output;
  char argument_data[4][64];
  int arg_idx;
  int unused;

  PrintPending = strend(PrintPending ? PrintPending : PrintBufArea + sizeof(ListenerMessageHeader));
  output_ptr = PrintPending;
  format_cstring = control->data;
  indentation = *print_column >> 3;
  saved_pending = PrintPending;
  start_output = output_ptr;
  arg_idx = 0;
  unused = 0;
  if (indentation && output_ptr[-1U] == '\n') {
    for (int i = 0; i < indentation; i++)
      *output_ptr++ = ' ';
  }
  va_start(args, control);
  format_ptr = format_cstring;
  while (*format_ptr) {
    if (*format_ptr != '~') {
      *output_ptr++ = *format_ptr;
    } else {
      arg_start = format_ptr;
      arg_idx = 0;
      argument_data[0][0] = -1;
      argument_data[1][0] = -1;
      argument_data[2][0] = -1;
      argument_data[3][0] = -1;
      argument_data[0][1] = 0;
      while ((unsigned int)(format_ptr[1] - '0') < 10 || format_ptr[1] == ',' ||
             format_ptr[1] == '\'' || format_ptr[1] == '`' ||
             (argument_data[arg_idx][0] == -1 && (format_ptr[1] == '-' || format_ptr[1] == '+'))) {
        switch (format_ptr[1]) {
          case ',':
            // Advance to the next directive parameter.
            arg_idx++;
            format_ptr++;
            break;
          case '\'':
            // Use the next character as a parameter.
            argument_data[arg_idx][0] = format_ptr[2];
            format_ptr += 2;
            break;
          case '`': {
            // Read a quoted parameter, such as a type name.
            int i = 0;
            format_ptr++;
            while (format_ptr[1] != '`') {
              argument_data[arg_idx][i++] = format_ptr[1];
              format_ptr++;
            }
            argument_data[arg_idx][i] = 0;
            format_ptr++;
            break;
          }
          case '-':
            // Pad on the right instead of the left.
            argument_data[arg_idx][1] = 1;
            format_ptr++;
            break;
          case '+':
            // Accept an explicit positive sign.
            format_ptr++;
            break;
          default:
            // Accumulate a decimal parameter.
            if (argument_data[arg_idx][0] == -1) {
              argument_data[arg_idx][0] = 0;
            }
            argument_data[arg_idx][0] = argument_data[arg_idx][0] * 10 + (format_ptr[1] - '0');
            format_ptr++;
            break;
        }
      }
      switch (*++format_ptr) {
        case '%':
          // Start a new line and restore indentation.
          *output_ptr++ = '\n';
          if (indentation && format_ptr[1]) {
            for (int i = 0; i < indentation; i++)
              *output_ptr++ = ' ';
          }
          break;
        case '~':
          // Escape a literal tilde.
          *output_ptr++ = '~';
          break;
        case 'H':
        case 'J':
        case 'K':
        case 'L':
        case 'N':
        case 'V':
        case 'W':
        case 'Y':
        case 'Z':
        case 'h':
        case 'j':
        case 'k':
        case 'l':
        case 'n':
        case 'v':
        case 'w':
        case 'y':
        case 'z':
          // Pass through. These codes are used by the font renderer.
          while (arg_start < format_ptr) {
            *output_ptr++ = *arg_start++;
          }
          *output_ptr++ = *format_ptr;
          break;
        case 'G':
        case 'g':
          // Append a C string.
          *output_ptr = 0;
          kstrcat(output_ptr, va_arg(args, char*));
          output_ptr = strend(output_ptr);
          break;
        case 'A':
        case 'a': {
          // Print an object, then truncate or pad to the requested width.
          int desired_length = argument_data[0][0];
          *output_ptr = 0;
          print_object(va_arg(args, unsigned int));
          if (desired_length != -1) {
            int print_len = strlen(output_ptr);
            if (print_len > desired_length) {
              if (desired_length > 1)
                output_ptr[desired_length - 1] = '~';
              output_ptr[desired_length] = 0;
            } else if (print_len < desired_length) {
              if (argument_data[0][1]) {
                output_ptr = strend(output_ptr);
                for (int i = 0; i < desired_length - print_len; i++) {
                  *output_ptr++ = argument_data[1][0] != -1 ? argument_data[1][0] : ' ';
                }
                *output_ptr = 0;
              } else {
                kstrinsert(output_ptr, argument_data[1][0] != -1 ? argument_data[1][0] : ' ',
                           desired_length - print_len);
              }
            }
          }
          output_ptr = strend(output_ptr);
          break;
        }
        case 'S':
        case 's': {
          // Print an object but print strings without quotes.
          int desired_length = argument_data[0][0];
          unsigned int value = va_arg(args, unsigned int);
          *output_ptr = 0;
          if ((value & GOAL_TAG_MASK) == GOAL_BASIC_TAG &&
              ((unsigned int*)value)[-1U] == *(unsigned int*)(symbol_table + FIX_SYM_STRING_TYPE)) {
            cprintf("%s", (char*)value + 4);
          } else {
            print_object(value);
          }
          if (desired_length != -1) {
            int print_len = strlen(output_ptr);
            if (print_len > desired_length) {
              if (desired_length > 1)
                output_ptr[desired_length - 1] = '~';
              output_ptr[desired_length] = 0;
            } else if (print_len < desired_length) {
              if (argument_data[0][1]) {
                output_ptr = strend(output_ptr);
                for (int i = 0; i < desired_length - print_len; i++) {
                  *output_ptr++ = argument_data[1][0] != -1 ? argument_data[1][0] : ' ';
                }
                *output_ptr = 0;
              } else {
                kstrinsert(output_ptr, argument_data[1][0] != -1 ? argument_data[1][0] : ' ',
                           desired_length - print_len);
              }
            }
          }
          output_ptr = strend(output_ptr);
          break;
        }
        case 'C':
        case 'c':
          // Append one character.
          *output_ptr++ = va_arg(args, char);
          break;
        case 'P':
        case 'p': {
          // Print using an optional explicit type.
          symbol* type_symbol;
          type* object_type;
          *output_ptr = 0;
          if (argument_data[0][0] != -1 && (type_symbol = find_symbol_from_c(argument_data[0])) &&
              (object_type = (type*)type_symbol->value)) {
            call_method_of_type(va_arg(args, unsigned int), object_type, GOAL_PRINT_METHOD);
          } else {
            print_object(va_arg(args, unsigned int));
          }
          output_ptr = strend(output_ptr);
          break;
        }
        case 'I':
        case 'i': {
          // Inspect using an optional explicit type.
          symbol* type_symbol;
          type* object_type;
          *output_ptr = 0;
          if (argument_data[0][0] != -1 && (type_symbol = find_symbol_from_c(argument_data[0])) &&
              (object_type = (type*)type_symbol->value)) {
            call_method_of_type(va_arg(args, unsigned int), object_type, GOAL_INSPECT_METHOD);
          } else {
            inspect_object(va_arg(args, unsigned int));
          }
          output_ptr = strend(output_ptr);
          break;
        }
        case 'Q':
        case 'q': {
          // The formatter never reads a quadword argument here - it's broken
          int128 value = 0;
          kqtoa(output_ptr, value, 16, argument_data[0][0],
                argument_data[1][0] != -1 ? argument_data[1][0] : '0', 0);
          output_ptr = strend(output_ptr);
          break;
        }
        case 'X':
        case 'x':
          // Print a hexadecimal integer.
          kitoa(output_ptr, va_arg(args, long long), 16, argument_data[0][0],
                argument_data[1][0] != -1 ? argument_data[1][0] : '0', 0);
          output_ptr = strend(output_ptr);
          break;
        case 'D':
        case 'd':
          // Print a decimal integer.
          kitoa(output_ptr, va_arg(args, long long), 10, argument_data[0][0],
                argument_data[1][0] != -1 ? argument_data[1][0] : ' ', 0);
          output_ptr = strend(output_ptr);
          break;
        case 'B':
        case 'b':
          // Print a binary integer.
          kitoa(output_ptr, va_arg(args, long long), 2, argument_data[0][0],
                argument_data[1][0] != -1 ? argument_data[1][0] : '0', 0);
          output_ptr = strend(output_ptr);
          break;
        case 'F': {
          // Print a float with width 12 and four fractional digits.
          float value = va_arg(args, float);
          ftoa(output_ptr, value, 12, ' ', 4, 0);
          output_ptr = strend(output_ptr);
          break;
        }
        case 'f': {
          // Print a float with configurable width, padding, and precision.
          float value = va_arg(args, float);
          ftoa(output_ptr, value, argument_data[0][0],
               argument_data[1][0] != -1 ? argument_data[1][0] : ' ',
               argument_data[2][0] != -1 ? argument_data[2][0] : 4, 0);
          output_ptr = strend(output_ptr);
          break;
        }
        case 'R':
        case 'r':
          // Convert rotation units to degrees.
          ftoa(output_ptr, va_arg(args, float) * 360.0f / 65536.0f, argument_data[0][0],
               argument_data[1][0] != -1 ? argument_data[1][0] : ' ',
               argument_data[2][0] != -1 ? argument_data[2][0] : 4, 0);
          output_ptr = strend(output_ptr);
          break;
        case 'M':
        case 'm':
          // Convert distance units to meters.
          ftoa(output_ptr, va_arg(args, float) / 4096.0f, argument_data[0][0],
               argument_data[1][0] != -1 ? argument_data[1][0] : ' ',
               argument_data[2][0] != -1 ? argument_data[2][0] : 4, 0);
          output_ptr = strend(output_ptr);
          break;
        case 'E':
        case 'e':
          // Convert clock ticks to seconds.
#if REVISION < 20011019
          ftoa(output_ptr, (float)va_arg(args, unsigned int) / 60.0f,
#else
          ftoa(output_ptr, (float)va_arg(args, unsigned int) / 300.0f,
#endif
               argument_data[0][0], argument_data[1][0] != -1 ? argument_data[1][0] : ' ',
               argument_data[2][0] != -1 ? argument_data[2][0] : 4, 0);
          output_ptr = strend(output_ptr);
          break;
        case 'T':
        case 't':
          // Append a tab.
          sprintf(output_ptr, "\t");
          output_ptr = strend(output_ptr);
          break;
        default:
          // Report an unsupported directive.
#if REVISION < 20011019
          MsgErr("format: unknown code %c\n", *format_ptr);
#else
          MsgErr("format: unknown code 0x%02x\n", *format_ptr);
#endif
          break;
      }
    }
    format_ptr++;
  }
  *output_ptr++ = 0;
  va_end(args);
  if (destination == symbol_table + FIX_SYM_TRUE) {
    // Leave the text queued for the listener.
  } else if (destination == symbol_table) {
    // Allocate a new string and copy to there.
    unsigned int result = make_string_from_c(start_output);
    PrintPending = saved_pending;
    *start_output = 0;
    return result;
  } else if (destination == 0) {
    // Print to stdout and flush.
    printf("%s", start_output);
    fflush(stdout);
    PrintPending = saved_pending;
    *start_output = 0;
  } else if ((destination & GOAL_TAG_MASK) == GOAL_BASIC_TAG) {
    unsigned int object_type = ((unsigned int*)destination)[-1U];
    if (object_type == FIX_SYM_VALUE(FIX_SYM_STRING_TYPE)) {
      // append to existing GOAL string
      kstrncat(((gstring*)destination)->data, start_output, ((gstring*)destination)->length);
      PrintPending = saved_pending;
      *start_output = 0;
    } else if (object_type == FIX_SYM_VALUE(FIX_SYM_FILE_STREAM_TYPE)) {
      // write to GOAL file stream.
      sceWrite(((file_stream*)destination)->descriptor, start_output, strlen(start_output));
      PrintPending = saved_pending;
      *start_output = 0;
    }
  }
  return 0;
}

int MessCount = 0;
char* MessBufArea = 0;
char* OutputBufArea = 0;
char* PrintBufArea = 0;
