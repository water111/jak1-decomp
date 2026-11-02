#include "overlord/isocommon.h"
#include "overlord/soundcommon.h"

// Three soundbanks: single very large common bank and 2 smaller level banks.
// these banks only hold metadata, the actual bank data is managed by 989snd.
static unsigned char gCommonBank[10 * SECTOR_SIZE];
static unsigned char gLevelBank[2][SECTOR_SIZE];
static SoundBank* gBanks[3] = {(SoundBank*)gCommonBank, (SoundBank*)gLevelBank[0],
                               (SoundBank*)gLevelBank[1]};

// Initialize bank structures.
void InitBanks(void) {
  int i;
  for (i = 0; i < 3; i++) {
    gBanks[i]->bank_handle = 0;
    gBanks[i]->sound_count = 0;
    strcpy(gBanks[i]->name, "<unused>");
  }
}

// Odd function to return the next free bank (no handle)
// Gross hack. The loader needs to know if it's loading a common vs. level soundbank,
// since they have different header sizes. Set the sound_count to the smaller count for level
// banks, so the loader can tell which to load. After loading, it is overwritten with the
// real value.
SoundBank* AllocateBank(void) {
  int i;
  for (i = 0; i < 3; i++) {
    if (!gBanks[i]->bank_handle) {
      if (i)
        gBanks[i]->sound_count = 101;
      else
        gBanks[i]->sound_count = 1022;
      return gBanks[i];
    }
  }
  return 0;
}

// Look up a sound by name, returning the bank and index.
int LookupSoundIndex(const char* name, SoundBank** bank_out) {
  int i;
  const unsigned int* words = (const unsigned int*)name;
  for (i = 0; i < 3; i++) {
    SoundBank* bank = gBanks[i];
    int j;
    if (!bank->bank_handle)
      continue;
    for (j = 0; j < bank->sound_count; j++) {
      const unsigned int* candidate = (const unsigned int*)bank->sound[j].name;
      // Bug fix: don't check last byte.
      if (words[0] == candidate[0] && words[1] == candidate[1] && words[2] == candidate[2] &&
#if REVISION > 20011019
          (words[3] & 0xffffff) == (candidate[3] & 0xffffff)) {
#else
          words[3] == candidate[3]) {
#endif
        *bank_out = bank;
        return j;
      }
    }
  }
  return -1;
}

// Look up a bank by name.
SoundBank* LookupBank(const char* name) {
  int i;
  const unsigned int* words = (const unsigned int*)name;
  // not sure why this was reversed.
#if REVISION <= 20011002
  for (i = 0; i < 3; i++) {
#else
  for (i = 2; i >= 0; i--) {
#endif
    SoundBank* bank = gBanks[i];
    const unsigned int* candidate = (const unsigned int*)bank->name;
    if (!bank->bank_handle)
      continue;
    if (words[0] == candidate[0] && words[1] == candidate[1] && words[2] == candidate[2] &&
        words[3] == candidate[3])
      return bank;
  }
  return 0;
}

// Reload bank info, likely to allow interactive editing of soundbanks during dev.
void ReloadBankInfo(void) {
  int i;
  for (i = 0; i < 3; i++) {
    SoundBank* bank = gBanks[i];
    if (bank->bank_handle)
      ReadBankSoundInfo(bank, bank->name, 1);
  }
}
