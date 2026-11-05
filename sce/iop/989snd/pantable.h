#ifndef SCE_IOP_989SND_PANTABLE_H
#define SCE_IOP_989SND_PANTABLE_H
#define SND_PAN_TABLE_SIZE 180

typedef struct VolPair {
  short left;
  short right;
} VolPair;

extern VolPair gPanTable1[SND_PAN_TABLE_SIZE];
extern VolPair gPanTable2[SND_PAN_TABLE_SIZE];
extern VolPair* gPanTable;
void snd_SetPanTable(VolPair* table);
#endif
