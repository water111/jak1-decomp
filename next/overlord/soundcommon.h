#ifndef SOUNDCOMMON_H
#define SOUNDCOMMON_H

#define MUSIC_SOUND_ID 666

void PrintBankInfo(SoundBank* bank);
void ReadBankSoundInfo(SoundBank* bank, const char* name, int host);

typedef struct Sound {
  int id, sound_handle, is_music, new_volume, auto_time;
  SoundParams params;
  SoundRecord* bank_entry;
} Sound;

#endif
