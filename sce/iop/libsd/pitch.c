#include "sce/iop/libsd/libsd.h"

u_short sd_note_pitch[12] = {0x8000, 0x879c, 0x8fac, 0x9837, 0xa145, 0xaadc,
                             0xb504, 0xbfc8, 0xcb2f, 0xd744, 0xe411, 0xf1a1};
u_short sd_fine_pitch[128] = {
    0x8000, 0x800e, 0x801d, 0x802c, 0x803b, 0x804a, 0x8058, 0x8067, 0x8076, 0x8085, 0x8094, 0x80a3,
    0x80b1, 0x80c0, 0x80cf, 0x80de, 0x80ed, 0x80fc, 0x810b, 0x811a, 0x8129, 0x8138, 0x8146, 0x8155,
    0x8164, 0x8173, 0x8182, 0x8191, 0x81a0, 0x81af, 0x81be, 0x81cd, 0x81dc, 0x81eb, 0x81fa, 0x8209,
    0x8218, 0x8227, 0x8236, 0x8245, 0x8254, 0x8263, 0x8272, 0x8282, 0x8291, 0x82a0, 0x82af, 0x82be,
    0x82cd, 0x82dc, 0x82eb, 0x82fa, 0x830a, 0x8319, 0x8328, 0x8337, 0x8346, 0x8355, 0x8364, 0x8374,
    0x8383, 0x8392, 0x83a1, 0x83b0, 0x83c0, 0x83cf, 0x83de, 0x83ed, 0x83fd, 0x840c, 0x841b, 0x842a,
    0x843a, 0x8449, 0x8458, 0x8468, 0x8477, 0x8486, 0x8495, 0x84a5, 0x84b4, 0x84c3, 0x84d3, 0x84e2,
    0x84f1, 0x8501, 0x8510, 0x8520, 0x852f, 0x853e, 0x854e, 0x855d, 0x856d, 0x857c, 0x858b, 0x859b,
    0x85aa, 0x85ba, 0x85c9, 0x85d9, 0x85e8, 0x85f8, 0x8607, 0x8617, 0x8626, 0x8636, 0x8645, 0x8655,
    0x8664, 0x8674, 0x8683, 0x8693, 0x86a2, 0x86b2, 0x86c1, 0x86d1, 0x86e0, 0x86f0, 0x8700, 0x870f,
    0x871f, 0x872e, 0x873e, 0x874e, 0x875d, 0x876d, 0x877d, 0x878c};

u_short sceSdNote2Pitch(int center_note, u_short center_fine, int note, short fine) {
  int fine_sum = fine + center_fine;
  int fine_note = fine_sum / 128;
  short relative_note = note + fine_note - center_note;
  short octave = relative_note / 12;
  short shift = octave - 2;
  short semitone = relative_note % 12;
  int fraction;
  u_int pitch;
  note = fine_note;
  fraction = fine_sum - note * 128;
  if (semitone < 0 || (semitone == 0 && fraction < 0)) {
    semitone += 12;
    shift = octave - 3;
  }
  if (fraction < 0) {
    semitone = (short)(semitone - 1) + note;
    fraction += (note + 1) * 128;
  }
  pitch = (sd_note_pitch[semitone] * sd_fine_pitch[fraction]) >> 16;
  if (shift < 0) {
    pitch += 1 << (-shift - 1);
    pitch >>= -shift;
  }
  return pitch;
}

u_short sceSdPitch2Note(int center_note, int center_fine, u_short pitch) {
  int i;
  int bit = 0;
  int semitone = 0;
  short fraction = 0;

  if (pitch >= 0x4000)
    pitch = 0x3fff;
  for (i = 0; i < 14; i++) {
    if ((pitch >> i) & 1)
      bit = i;
  }
  pitch = pitch << (15 - bit);
  for (i = 11; i >= 0; i--) {
    if (pitch >= sd_note_pitch[i]) {
      semitone = i;
      break;
    }
  }
  pitch = ((u_int)pitch << 15) / sd_note_pitch[(u_short)semitone];
  for (i = 127; i >= 0; i--) {
    if (pitch >= sd_fine_pitch[i]) {
      fraction = i;
      break;
    }
  }
  fraction = center_fine + (u_int)(fraction + 1);
  {
    u_int fine = (u_short)fraction;
    int result = semitone + (center_note + (bit - 12) * 12);
    u_int value;
    result += (u_short)fine / 128;
    value = (fine & 0x7e) + (result << 8);
    return value & 0xfffe;
  }
}
