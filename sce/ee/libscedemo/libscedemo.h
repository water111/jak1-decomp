#ifndef SCE_EE_LIBSCEDEMO_LIBSCEDEMO_H
#define SCE_EE_LIBSCEDEMO_LIBSCEDEMO_H

#ifdef __cplusplus
extern "C" {
#endif

void sceDemoStart(int argc,
                  char** argv,
                  unsigned short* a,
                  unsigned short* b,
                  unsigned short* c,
                  unsigned short* d,
                  unsigned short* e,
                  unsigned short* f,
                  unsigned short* g,
                  unsigned short* h);
void sceDemoEnd(int result);

typedef enum {
  SCE_DEMO_ENDREASON_ATTRACT_INTERRUPTED,
  SCE_DEMO_ENDREASON_ATTRACT_COMPLETE,
  SCE_DEMO_ENDREASON_PLAYABLE_INACTIVITY_TIMEOUT,
  SCE_DEMO_ENDREASON_PLAYABLE_GAMEPLAY_TIMEOUT,
  SCE_DEMO_ENDREASON_PLAYABLE_COMPLETE,
  SCE_DEMO_ENDREASON_PLAYABLE_QUIT
} sceDemoEndReason;

enum SceDemoLanguage {
  SCE_DEMO_LANGUAGE_ENGLISH = 0,
  SCE_DEMO_LANGUAGE_FRENCH = 1,
  SCE_DEMO_LANGUAGE_GERMAN = 2,
  SCE_DEMO_LANGUAGE_SPANISH = 3,
  SCE_DEMO_LANGUAGE_ITALIAN = 4,
  SCE_DEMO_LANGUAGE_DUTCH = 5,
  SCE_DEMO_LANGUAGE_PORTUGUESE = 6,
  SCE_DEMO_LANGUAGE_JAPANESE = 7
};

enum SceDemoAspect {
  SCE_DEMO_ASPECT_43 = 0,
  SCE_DEMO_ASPECT_169 = 1,
  SCE_DEMO_ASPECT_FULL = 2
};

enum SceDemoPlayMode {
  SCE_DEMO_PLAYMODE_PLAYABLE = 0,
  SCE_DEMO_PLAYMODE_ATTRACT = 1
};

#ifdef __cplusplus
}
#endif

#endif
