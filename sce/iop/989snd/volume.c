#include "sce/iop/989snd/volume.h"

#include "sce/iop/989snd/error.h"
#include "sce/iop/989snd/valloc.h"
#include "sce/iop/libsd/libsd.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysclib/stdlib.h"

int gMasterVol[SND_MASTER_VOLUME_GROUPS] = {1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
                                            1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024};

short gStereoOrMono = 0;
int gCurrentXAVolGroup = -1;
int gCurrentXAVol = 0;
int gCurrentXAPan = 0;

static inline short adjust_volume(int volume, int group) {
  int phase;
  int adjusted;
  if (group >= 7) {
    adjusted = volume;
  } else {
    adjusted = (short)volume * gMasterVol[group] / 1024;
    phase = 1;
    if (adjusted < 0)
      phase = -1;
    adjusted = adjusted * adjusted / 0x7f00 * phase;
  }
  return adjusted;
}

void snd_SetMasterVolume(int group, int volume) {
  if (volume > 1024)
    volume = 1024;
  if (volume < 0)
    volume = 0;
  if (group == 7) {
#if SND989_VERSION == 0x020604
    printf(
        "snd_SetMasterVolume: Setting the master volume of the \"EXTERNAL\" group has no "
        "effect!\n");
#else
    snd_ShowError(83, 0, 0, 0, 0);
#endif
    return;
  }
  gMasterVol[group] = volume;
  if (group != 8) {
    snd_AdjustAllChannelsMasterVolume(volume, group);
  } else {
    sceSdSetParam(SD_P_MVOLL | 1, 0x3fff * volume / 1024);
    sceSdSetParam(SD_P_MVOLR | 1, 0x3fff * volume / 1024);
  }
}

int snd_GetMasterVolume(int group) {
  return gMasterVol[group];
}

void snd_SetPlaybackMode(int mode) {
  gStereoOrMono = mode;
}

int snd_GetPlaybackMode(void) {
  return gStereoOrMono;
}

void snd_MakeVolumes(int snd_vol,
                     int snd_pan,
                     int prog_vol,
                     int prog_pan,
                     int tone_vol,
                     int tone_pan,
                     VolPair* spu_vol) {
  int voll, volr, work_vol;
  work_vol = snd_vol << 8;
  work_vol = work_vol * tone_vol / 127;
  work_vol = work_vol * prog_vol / 127;

  if (work_vol == 0) {
    spu_vol->left = 0;
    spu_vol->right = 0;
    return;
  }
  if (gStereoOrMono == 1) {
    spu_vol->left = work_vol;
    spu_vol->right = work_vol;
    return;
  }
  snd_pan += tone_pan + prog_pan;
  while (snd_pan >= 360)
    snd_pan -= 360;
  if (snd_pan < 270)
    snd_pan += 90;
  else
    snd_pan -= 270;
  if (snd_pan < 180) {
    do {
      voll = work_vol * gPanTable[snd_pan].left / 0x3fff;
      volr = work_vol * gPanTable[snd_pan].right / 0x3fff;
      if (spu_vol->left <= -1 && spu_vol->right <= -1) {
        voll = -voll;
        volr = -volr;
      }
    } while (0);
    if ((spu_vol->left <= -1 && spu_vol->right >= 0) ||
        (spu_vol->left >= 0 && spu_vol->right < 0)) {
      if (abs(spu_vol->left) > abs(spu_vol->right)) {
        voll *= spu_vol->left / abs(spu_vol->left);
        volr *= spu_vol->left / abs(spu_vol->left);
      } else {
        voll *= spu_vol->right / abs(spu_vol->right);
        volr *= spu_vol->right / abs(spu_vol->right);
      }
    }
  } else {
    snd_pan -= 180;
    volr = work_vol * gPanTable[snd_pan].left / 0x3fff;
    voll = work_vol * gPanTable[snd_pan].right / 0x3fff;
    if (gStereoOrMono != 2) {
      int invert_right;
      short phase = spu_vol->left;
      if (phase >= 0 && spu_vol->right >= 0)
        invert_right = volr < voll;
      else {
        if (spu_vol->left >= 0)
          goto invert_right_phase;
        if (spu_vol->right >= 0)
          goto invert_left_phase;
        invert_right = voll < volr;
      }
      if (invert_right)
        goto invert_right_phase;
    invert_left_phase:
      voll = -voll;
      goto phase_done;
    invert_right_phase:
      volr = -volr;
    phase_done:;
    }
  }
  spu_vol->left = voll;
  spu_vol->right = volr;
}

void snd_MakeVolumesB(int bank_vol,
                      int snd_vol,
                      int snd_pan,
                      int prog_vol,
                      int prog_pan,
                      int tone_vol,
                      int tone_pan,
                      VolPair* spu_vol) {
  int voll, volr, work_vol;
  work_vol = snd_vol << 8;
  work_vol = work_vol * tone_vol / 127;
  work_vol = work_vol * prog_vol / 127;
  work_vol = work_vol * bank_vol / 127;

  if (work_vol == 0) {
    spu_vol->left = 0;
    spu_vol->right = 0;
    return;
  }
  if (gStereoOrMono == 1) {
    spu_vol->left = work_vol;
    spu_vol->right = work_vol;
    return;
  }
  snd_pan += tone_pan + prog_pan;
  while (snd_pan >= 360)
    snd_pan -= 360;
  if (snd_pan < 270)
    snd_pan += 90;
  else
    snd_pan -= 270;
  if (snd_pan < 180) {
    do {
      voll = work_vol * gPanTable[snd_pan].left / 0x3fff;
      volr = work_vol * gPanTable[snd_pan].right / 0x3fff;
      if (spu_vol->left <= -1 && spu_vol->right <= -1) {
        voll = -voll;
        volr = -volr;
      }
    } while (0);
    if ((spu_vol->left <= -1 && spu_vol->right >= 0) ||
        (spu_vol->left >= 0 && spu_vol->right < 0)) {
      if (abs(spu_vol->left) > abs(spu_vol->right)) {
        voll *= spu_vol->left / abs(spu_vol->left);
        volr *= spu_vol->left / abs(spu_vol->left);
      } else {
        voll *= spu_vol->right / abs(spu_vol->right);
        volr *= spu_vol->right / abs(spu_vol->right);
      }
    }
  } else {
    snd_pan -= 180;
    volr = work_vol * gPanTable[snd_pan].left / 0x3fff;
    voll = work_vol * gPanTable[snd_pan].right / 0x3fff;
    if (gStereoOrMono != 2) {
      int invert_right;
      short phase = spu_vol->left;
      if (phase >= 0 && spu_vol->right >= 0)
        invert_right = volr < voll;
      else {
        if (spu_vol->left >= 0)
          goto invert_right_phase;
        if (spu_vol->right >= 0)
          goto invert_left_phase;
        invert_right = voll < volr;
      }
      if (invert_right)
        goto invert_right_phase;
    invert_left_phase:
      voll = -voll;
      goto phase_done;
    invert_right_phase:
      volr = -volr;
    phase_done:;
    }
  }
  spu_vol->left = voll;
  spu_vol->right = volr;
}

short snd_AdjustVolToGroup(short volume, int group) {
  int phase;
  int adjusted;
  if (group >= 7)
    return volume;
  adjusted = volume * gMasterVol[group] / 1024;
  phase = 1;
  if (adjusted < 0)
    phase = -1;
  adjusted = adjusted * adjusted / 0x7f00 * phase;
  return adjusted;
}

void snd_AdjustAllChannelsMasterVolume(int volume, int group) {
  int count;
  int core;
  int channel;
  sceSdBatch batch[2];
  if (group >= 7)
    return;
  snd_LockVoiceAllocator(1);
  for (count = 0; count < SND_NUM_VOICES; count++) {
    if ((gChannelStatus[count].status == 1 || gChannelStatus[count].status == 4) &&
        gChannelStatus[count].group == group) {
      core = count / SND_VOICES_PER_CORE;
      channel = count % SND_VOICES_PER_CORE;
      batch[0].func = SD_BSET_PARAM;
      batch[0].entry = core | (channel << 1) | SD_VP_VOLL;
#if SND989_VERSION == 0x020604
      batch[0].value = (u_short)adjust_volume((u_short)gChannelStatus[count].volume_left,
                                              gChannelStatus[count].group) >>
                       1;
#else
      batch[0].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[count].volume_left,
                                                     gChannelStatus[count].group) >>
                       1;
#endif
      batch[1].func = SD_BSET_PARAM;
      batch[1].entry = core | (channel << 1) | SD_VP_VOLR;
#if SND989_VERSION == 0x020604
      batch[1].value = (u_short)adjust_volume((u_short)gChannelStatus[count].volume_right,
                                              gChannelStatus[count].group) >>
                       1;
#else
      batch[1].value = (u_short)snd_AdjustVolToGroup(gChannelStatus[count].volume_right,
                                                     gChannelStatus[count].group) >>
                       1;
#endif
      sceSdProcBatch(batch, 0, 2);
    }
  }
  snd_UnlockVoiceAllocator();
}

short snd_MIDITo360Pan(signed char pan) {
  if (pan < 64)
    return 90 * pan / 64 + 270;
  else
    return 90 * (pan - 64) / 63;
}
