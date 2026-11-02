#include "overlord/ssound.h"
#include "989snd/989snd.h"
#include "overlord/iso.h"
#include "overlord/isocommon.h"
#include "overlord/soundcommon.h"
#include "overlord/srpc.h"
#include "sce/iop/stdio.h"
#include "sce/iop/thread.h"

#define SOUND_SLOT_COUNT 64
#define SOUND_VOLUME_KEEP (-2)
#define SOUND_VOLUME_FADE_STOP (-4)

typedef struct Curve {
  int cubic, quadratic, linear, constant;
} Curve;

VolumePair gPanTable[361];
Curve gCurves[16];
int gVoice;
#if REVISION > 20011002
int gTrapSRAM;
#endif
int gSema, gStreamSRAM;

static int sqrt_table[256] = {
    0,     4096,  5793,  7094,  8192,  9159,  10033, 10837, 11585, 12288, 12953, 13585, 14189,
    14768, 15326, 15864, 16384, 16888, 17378, 17854, 18318, 18770, 19212, 19644, 20066, 20480,
    20886, 21283, 21674, 22058, 22435, 22806, 23170, 23530, 23884, 24232, 24576, 24915, 25249,
    25580, 25905, 26227, 26545, 26859, 27170, 27477, 27780, 28081, 28378, 28672, 28963, 29251,
    29537, 29819, 30099, 30377, 30652, 30924, 31194, 31462, 31727, 31991, 32252, 32511, 32768,
    33023, 33276, 33527, 33776, 34024, 34270, 34514, 34756, 34996, 35235, 35472, 35708, 35942,
    36175, 36406, 36636, 36864, 37091, 37316, 37540, 37763, 37985, 38205, 38424, 38642, 38858,
    39073, 39287, 39500, 39712, 39923, 40132, 40341, 40548, 40755, 40960, 41164, 41368, 41570,
    41771, 41972, 42171, 42369, 42567, 42763, 42959, 43154, 43348, 43541, 43733, 43925, 44115,
    44305, 44494, 44682, 44869, 45056, 45242, 45427, 45611, 45795, 45977, 46160, 46341, 46522,
    46702, 46881, 47059, 47237, 47415, 47591, 47767, 47942, 48117, 48291, 48465, 48637, 48809,
    48981, 49152, 49322, 49492, 49661, 49830, 49998, 50166, 50332, 50499, 50665, 50830, 50995,
    51159, 51323, 51486, 51649, 51811, 51972, 52134, 52294, 52454, 52614, 52773, 52932, 53090,
    53248, 53405, 53562, 53719, 53874, 54030, 54185, 54340, 54494, 54647, 54801, 54954, 55106,
    55258, 55410, 55561, 55712, 55862, 56012, 56162, 56311, 56459, 56608, 56756, 56903, 57051,
    57198, 57344, 57490, 57636, 57781, 57926, 58071, 58215, 58359, 58503, 58646, 58789, 58931,
    59073, 59215, 59357, 59498, 59639, 59779, 59919, 60059, 60199, 60338, 60477, 60615, 60753,
    60891, 61029, 61166, 61303, 61440, 61576, 61712, 61848, 61984, 62119, 62254, 62388, 62523,
    62657, 62790, 62924, 63057, 63190, 63323, 63455, 63587, 63719, 63850, 63982, 64113, 64243,
    64374, 64504, 64634, 64763, 64893, 65022, 65151, 65279, 65408,
};

static int atan_table[257] = {
    0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,
    5,  5,  5,  6,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  10, 10,
    10, 10, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 14, 15, 15, 15,
    15, 15, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 20, 20,
    20, 20, 20, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24,
    25, 25, 25, 25, 25, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 29, 29,
    29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 32, 33,
    33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36,
    36, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 40,
    40, 40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42, 42, 42, 42, 43,
    43, 43, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 44, 44, 44, 45,
};

static Sound gSounds[SOUND_SLOT_COUNT];
static void* SndMemAlloc(int size, int use, int* actual);
static void SndMemFree(void* memory);

void CatalogSRAM(void) {
}

void InitSound(void) {
  int i;
  struct SemaParam sema;
  for (i = 0; i < SOUND_SLOT_COUNT; i++)
    gSounds[i].id = 0;
  SetCurve(1, 0, 0);
  SetCurve(2, 4096, 0);
  SetCurve(3, 0, 4096);
  SetCurve(4, 2048, 0);
  SetCurve(5, 2048, 2048);
  SetCurve(6, -4096, 0);
  SetCurve(7, -2048, 0);
  snd_StartSoundSystem();
  snd_RegisterIOPMemAllocator(SndMemAlloc, SndMemFree);
  snd_LockVoiceAllocator(1);
  i = snd_ExternVoiceAlloc(2, 127);
  snd_UnlockVoiceAllocator();
  gVoice = i / 24 + ((i % 24) << 1);
  gStreamSRAM = snd_SRAMMalloc(
#if REVISION <= 20011002
      0xc000
#else
      0xc030
#endif
  );
#if REVISION > 20011002
  gTrapSRAM = gStreamSRAM + 0xc000;
  snd_SetMixerMode(0, 0);
  for (i = 0; i < 8; i++)
#if REVISION <= 20011019
    snd_SetGroupVoiceRange(i, 15, 47);
#else
    snd_SetGroupVoiceRange(i, 16, 47);
#endif
  snd_SetGroupVoiceRange(1, 0, 15);
  snd_SetGroupVoiceRange(2, 0, 15);
#endif
  snd_SetReverbDepth(3, 0, 0);
  snd_SetReverbType(1, 0);
#if REVISION <= 20011019
  snd_SetReverbType(2, 5);
#else
  snd_SetReverbType(2, 0);
#endif
#if REVISION > 20011019
  CatalogSRAM();
#endif
  for (i = 0; i < 91; i++) {
    int full = 0x3fff;
    int front = i * 0x33ff / 90 + 0xc00;
    int rear_right = i * -0x2800 / 90 + 0x3400;
    int rear_left = i * -0xbff / 90 + 0x3fff;
    gPanTable[90 - i].left = full;
    gPanTable[180 - i].left = front;
    gPanTable[270 - i].left = rear_right;
    gPanTable[360 - i].left = rear_left;
    gPanTable[90 + i].right = full;
    gPanTable[i].right = front;
    gPanTable[270 + i].right = rear_right;
    gPanTable[180 + i].right = rear_left;
  }
  snd_SetPanTable((short*)gPanTable);
  snd_SetPlaybackMode(2);
  sema.attr = SA_THPRI;
  sema.initCount = 1;
  sema.maxCount = 1;
  sema.option = 0;
  gSema = CreateSema(&sema);
  if (gSema < 0)
    for (;;) {
    }
}

Sound* LookupSound(int id) {
  int i;
  if (id) {
    for (i = 0; i < SOUND_SLOT_COUNT; i++) {
      if (gSounds[i].id == id) {
        Sound* sound = &gSounds[i];
        int handle = snd_SoundIsStillPlaying(sound->sound_handle);
        sound->sound_handle = handle;
        if (!handle) {
          sound->id = 0;
          return 0;
        }
        return &gSounds[i];
      }
    }
  }
  return 0;
}

void CleanSounds(void) {
  int i;
  for (i = 0; i < SOUND_SLOT_COUNT; i++) {
    Sound* sound = &gSounds[i];
    if (sound->id) {
      sound->sound_handle = snd_SoundIsStillPlaying(sound->sound_handle);
      if (!sound->sound_handle)
        sound->id = 0;
    }
  }
}

void KillSoundsInGroup(unsigned char group) {
  int i;
  for (i = 0; i < SOUND_SLOT_COUNT; i++) {
    Sound* sound = &gSounds[i];
    if (sound->id) {
      sound->sound_handle = snd_SoundIsStillPlaying(sound->sound_handle);
      if (!sound->sound_handle)
        sound->id = 0;
      else if (sound->params.group & group) {
        snd_StopSound(sound->sound_handle);
        sound->id = 0;
      }
    }
  }
}

Sound* AllocateSound() {
  int i;
  for (i = 0; i < SOUND_SLOT_COUNT; i++)
    if (!gSounds[i].id)
      return &gSounds[i];
  CleanSounds();
  for (i = 0; i < SOUND_SLOT_COUNT; i++)
    if (!gSounds[i].id)
      return &gSounds[i];
  return 0;
}

static Vec3w gEarTrans;
static Vec3w gCamTrans;
static int gCamAngle;

int CalculateFalloffVolume(Vec3w* pos, int volume, int curve, int min, int max) {
  int x, y, z;
  unsigned int squared, distance;
  int shifts;
  unsigned int t, t2, t3;
  int result;
  if (!curve)
    return volume;
  x = gEarTrans.x - ((int*)pos)[0];
  y = gEarTrans.y - ((int*)pos)[1];
  z = gEarTrans.z - ((int*)pos)[2];
  if (x < 0)
    x = -x;
  if (y < 0)
    y = -y;
  if (z < 0)
    z = -z;
  min <<= 8;
  max <<= 8;
  if (x > max)
    return 0;
  if (y > max)
    return 0;
  if (z > max)
    return 0;
  while (max > 0x7fff) {
    max >>= 1;
    min >>= 1;
    x >>= 1;
    y >>= 1;
    z >>= 1;
  }
  squared = x * x + y * y + z * z;
  if (squared) {
    shifts = 0;
    while (!(squared & 0xc0000000)) {
      squared <<= 2;
      shifts++;
    }
    distance = sqrt_table[squared >> 24] >> shifts;
  } else
    distance = 0;
  if (distance <= min)
    return volume;
  if (distance >= max)
    return 0;
  distance -= min;
  max -= min;
  while (distance > 0xffff) {
    distance >>= 1;
    max >>= 1;
  }
  t = (distance * 0x10000) / max;
  if (t == 0x10000)
    return volume;
  t2 = (t * t) >> 16;
  t3 = (t2 * t) >> 16;
  result = ((gCurves[curve].constant << 16) + gCurves[curve].linear * t +
            gCurves[curve].quadratic * t2 + gCurves[curve].cubic * t3) >>
           12;
  if (result < 0)
    result = 0;
  else if (result > 0x10000)
    result = 0x10000;
  return (result * volume) >> 16;
}

unsigned int CalculateAngle(Vec3w* position) {
  int x = gCamTrans.x - ((int*)position)[0];
  int z = gCamTrans.z - ((int*)position)[2];
  int ax = x < 0 ? -x : x;
  int az = z < 0 ? -z : z;
  int angle;
  int index;
  if (!ax && !az)
    return 0;
  if (ax > az) {
    index = (az << 8) / ax;
    angle = atan_table[index];
    if (x < 0) {
      if (z < 0)
        angle = 270 - angle;
      else
        angle += 270;
    } else {
      if (z < 0)
        angle += 90;
      else
        angle = 90 - angle;
    }
  } else {
    index = (ax << 8) / az;
    angle = atan_table[index];
    if (z < 0) {
      if (x < 0)
        angle += 180;
      else
        angle = 180 - angle;
    } else if (x < 0)
      angle = 360 - angle;
    else {
      //
    }
  }
  angle -= gCamAngle;
  angle = (angle + 720) % 360;
  return angle;
}

int GetVolume(Sound* sound) {
  SoundParams* params = &sound->params;
  return CalculateFalloffVolume(&params->position, params->volume, params->fo_curve, params->fo_min,
                                params->fo_max);
}

int GetPan(Sound* sound) {
  SoundParams* params = &sound->params;
  return CalculateAngle(&params->position);
}

static void UpdateLocation(Sound* sound) {
  int id;
#if REVISION > 20011002
  int volume;
#endif
#if REVISION <= 20011002
  if (sound->id && (sound->bank_entry->falloff >> 28)) {
#else
  if (sound->id) {
    if (sound->bank_entry->falloff >> 28) {
#endif
    id = snd_SoundIsStillPlaying(sound->sound_handle);
    sound->sound_handle = id;
    if (!id) {
      sound->id = 0;
      return;
    }
#if REVISION <= 20011002
    snd_SetSoundVolPan(id, GetVolume(sound), GetPan(sound));
#else
      volume = GetVolume(sound);
      if (volume)
        snd_SetSoundVolPan(id, volume, GetPan(sound));
      else
        snd_StopSound(sound->sound_handle);
#endif
#if REVISION > 20011002
  }
#endif
}
}

static void UpdateAutoVol(Sound* sound, unsigned int ticks) {
  int volume, step;
  if (ticks >= sound->auto_time) {
    if (sound->new_volume == SOUND_VOLUME_FADE_STOP) {
      snd_StopSound(sound->sound_handle);
      sound->id = 0;
    } else
      sound->params.volume = sound->new_volume;
    sound->auto_time = 0;
    return;
  }
  volume = sound->new_volume;
  if (volume == SOUND_VOLUME_FADE_STOP)
    volume = 0;
  step = volume - sound->params.volume;
  step *= ticks;
  step /= sound->auto_time;
  // TODO: maybe bug here?
  if (step < 0) {
    if (step > -1)
      step = -1;
    sound->params.volume += step;
    if (sound->params.volume < sound->new_volume)
      sound->params.volume = sound->new_volume;
  } else {
    if (step < 1)
      step = 1;
    sound->params.volume += step;
    if (sound->params.volume > sound->new_volume)
      sound->params.volume = sound->new_volume;
  }
  sound->auto_time -= ticks;
}

void UpdateVolume(Sound* sound) {
  int id = snd_SoundIsStillPlaying(sound->sound_handle);
  sound->sound_handle = id;
  if (!id)
    sound->id = 0;
  else
    snd_SetSoundVolPan(id, GetVolume(sound), SOUND_VOLUME_KEEP);
}

void SetEarTrans(Vec3w* ear, Vec3w* camera, int angle) {
  int i;
  unsigned int delta;
  unsigned int tick = snd_GetTick();
  static unsigned int last_tick = 0;
  delta = tick - last_tick;
  last_tick = tick;
  gEarTrans.x = ((int*)ear)[0];
  gEarTrans.y = ((int*)ear)[1];
  gEarTrans.z = ((int*)ear)[2];
  gCamTrans.x = ((int*)camera)[0];
  gCamTrans.y = ((int*)camera)[1];
  gCamTrans.z = ((int*)camera)[2];
  gCamAngle = angle;
  for (i = 0; i < SOUND_SLOT_COUNT; i++) {
    Sound* sound = &gSounds[i];
    if (sound->id && !sound->is_music) {
      if (sound->auto_time)
        UpdateAutoVol(sound, delta);
      UpdateLocation(sound);
    }
  }
#if REVISION > 20011002
  SetVAGVol();
#endif
}

#if REVISION > 20011002
void PrintActiveSounds(void) {
  int i;
  for (i = 0; i < SOUND_SLOT_COUNT; i++) {
    Sound* sound = &gSounds[i];
    char string[64];
    int length;
    if (sound->id && !sound->is_music) {
      length = strlen(sound->bank_entry->name);
      if (length > 16)
        length = 16;
      sprintf(string, "                 : Vol %d", GetVolume(sound));
      memcpy(string, sound->bank_entry->name, length);
      printf("%s\n", string);
    }
  }
}
#endif

void SetCurve(int curve, int falloff, int ease) {
  gCurves[curve].cubic = 2 * ease;
  gCurves[curve].quadratic = falloff - 3 * ease;
  gCurves[curve].linear = -falloff + ease - 4096;
  gCurves[curve].constant = 4096;
}

void SetMusicVol(void) {
#if REVISION <= 20011002
  snd_SetMasterVolume(1, (gMusicVol * gMusicFade) >> 16);
  snd_SetMasterVolume(2, (gMusicVol * gMusicFade) >> 16);
#else
    int volume = (gMusicVol * gMusicFade) >> 16;
    volume = (volume * gMusicTweak) >> 7;
    snd_SetMasterVolume(1, volume);
    snd_SetMasterVolume(2, volume);
#endif
}

typedef struct SoundMemory {
  void* memory;
  int size, used;
} SoundMemory;

#if REVISION <= 20011002
#define COMMON_BANK_SIZE 0x8000
#define MUSIC_SEQUENCE_BUFFER_SIZE 0x6800
#else
#define COMMON_BANK_SIZE 0xc000
#define MUSIC_SEQUENCE_BUFFER_SIZE 0x7800
#endif
#if REVISION <= 20011019
#define OTHER_BANK_SIZE 0x3000
#else
#define OTHER_BANK_SIZE 0x2000
#endif

#define OTHER_BANK_COUNT 3

enum SoundMemoryUse {
  SOUND_MEM_BANK = 1,
  SOUND_MEM_MUSIC_SEQUENCE = 3,
  SOUND_MEM_BUFFER = 8
};

static unsigned char common_bank_mem[COMMON_BANK_SIZE];
static SoundMemory common_bank = {common_bank_mem, sizeof(common_bank_mem), 0};
static unsigned char other_bank_mem[OTHER_BANK_COUNT][OTHER_BANK_SIZE];
static SoundMemory other_bank[OTHER_BANK_COUNT] = {{other_bank_mem[0], OTHER_BANK_SIZE, 0},
                                                   {other_bank_mem[1], OTHER_BANK_SIZE, 0},
                                                   {other_bank_mem[2], OTHER_BANK_SIZE, 0}};
static unsigned char mmd_bank_mem[MUSIC_SEQUENCE_BUFFER_SIZE];
static SoundMemory mmd_bank = {mmd_bank_mem, sizeof(mmd_bank_mem), 0};
static SoundMemory buffer_mem = {0, 0, 0};

void SetBufferMem(void* memory, unsigned int size) {
  buffer_mem.memory = memory;
  buffer_mem.size = size;
  buffer_mem.used = 0;
}

void ReleaseBufferMem(void) {
  if (buffer_mem.used)
    for (;;) {
    }
  buffer_mem.memory = 0;
  buffer_mem.size = 0;
}

static void* SndMemAlloc(int size, int use, int* actual) {
  int i;
  if (use == SOUND_MEM_BUFFER) {
    if (!buffer_mem.memory)
      for (;;) {
      }
    if (buffer_mem.used)
      for (;;) {
      }
    buffer_mem.used = 1;
    if (actual) {
      if (size < buffer_mem.size)
        *actual = size;
      else
        *actual = buffer_mem.size;
    }
    return buffer_mem.memory;
  }
  if (use == SOUND_MEM_BANK) {
    if (size > (int)sizeof(other_bank_mem[0])) {
      if (common_bank.used)
        return 0;
      if (size > COMMON_BANK_SIZE)
        return 0;
      common_bank.used = 1;
      if (actual)
        *actual = size;
      return common_bank.memory;
    } else {
      for (i = 0; i < OTHER_BANK_COUNT; i++) {
        if (!other_bank[i].used) {
          other_bank[i].used = 1;
          if (actual)
            *actual = size;
          return other_bank[i].memory;
        }
      }
      for (;;) {
      }
    }
  }
  if (use == SOUND_MEM_MUSIC_SEQUENCE) {
    if (mmd_bank.used)
      for (;;) {
      }
    mmd_bank.used = 1;
    if (actual)
      *actual = size;
    return mmd_bank.memory;
  }
  for (;;) {
  }
  return 0;
}

static void SndMemFree(void* memory) {
  int i;
  if (memory == buffer_mem.memory) {
    buffer_mem.used = 0;
    return;
  } else if (memory == mmd_bank.memory) {
    mmd_bank.used = 0;
    return;
  } else if (memory == common_bank.memory) {
    common_bank.used = 0;
    return;
  } else {
    for (i = 0; i < OTHER_BANK_COUNT; i++) {
      if (other_bank[i].memory == memory) {
        other_bank[i].used = 0;
        return;
      }
    }
  }
  for (;;) {
  }
  return;
}
