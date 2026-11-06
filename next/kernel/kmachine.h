#ifndef NEXT_KERNEL_KMACHINE_H
#define NEXT_KERNEL_KMACHINE_H

#include "sce/ee/libcdvd/libcdvd.h"

void InitParms(int argc, char** argv);
int InitMachine();
int ShutdownMachine();
void InitCD();
void InitIOP();
void InitVideo();
void InitMachineScheme();

void CacheFlush(void* memory, int size);
unsigned int InstallHandler(int source, unsigned int function);
unsigned int InstallDebugHandler(int source, unsigned int function);
unsigned int dma_to_iop(unsigned int source, unsigned int size, unsigned int destination);

unsigned int DecodeLanguage();
unsigned int DecodeAspect();
unsigned int DecodeVolume();
unsigned int DecodeTerritory();
unsigned int DecodeTimeout();
unsigned int DecodeInactiveTimeout();
sceCdCLOCK* DecodeTime(sceCdCLOCK* clock);
struct sceGsDispEnv;
void PutDisplayEnv(sceGsDispEnv* environment);

void* CPadOpen(void* info, int number);
void* CPadGetData(void* info);

enum OverlordDataSource {
  fakeiso,
  deviso,
  iso_cd
};

#define GAME_TERRITORY_SCEA 0
#define GAME_TERRITORY_SCEE 1
#define GAME_TERRITORY_SCEI 2

#ifdef REGION_EU
#define GAME_TERRITORY GAME_TERRITORY_SCEE
#elif defined(REGION_JP)
#define GAME_TERRITORY GAME_TERRITORY_SCEI
#else
#define GAME_TERRITORY GAME_TERRITORY_SCEA
#endif

struct CPadInfo {
  unsigned char valid, status;
  unsigned short buttons;
  unsigned char sticks[4], pressure[12], packet_reserved[12];
  int number, cpad_file;
  unsigned int button_absolute[3], button_shadow, button_relative[3];
  float stick_direction, stick_speed;
  int new_pad, state;
  unsigned char align[6], direct[6];
  unsigned char rumble_state[28];
  int actuators_enabled;
};

#endif
