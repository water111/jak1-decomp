#ifndef SBANK_H
#define SBANK_H

void InitBanks(void);
int LookupSoundIndex(const char* name, SoundBank** bank_out);
SoundBank* LookupBank(const char* name);
SoundBank* AllocateBank(void);
void ReloadBankInfo(void);

#endif
