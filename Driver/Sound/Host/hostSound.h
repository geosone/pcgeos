/*
 * hostSound.h -- the host sound driver (hostsnd.geo): FM voices and DACs
 * mixed into ghal_audio (hostSynth.c), the driver's functions
 * (hostSound.c).
 */
#ifndef HOSTSOUND_H
#define HOSTSOUND_H

#include <geos.h>
#include <heap.h>
#include <sound.h>
#include <ghal.h>
#include <geosdriver.h>
#include <Internal/soundDrv.h>
#include <Internal/strDrInt.h>
#include "hostInstruments.h"

#define HS_VOICES	16
#define HS_DACS		2
#define HS_RATE		44100		/* the mix: mono, 16 bits */

/* a function of its own, for tests: DR_farPtr <- the ghal_audio */
#define HOSTSND_GET_AUDIO	0x7e00

/* hostSynth.c, all inside ghal_crit */
void	HostSynthInit(void);
void	HostVoiceEnvelope(word voice, word patch);
void	HostVoiceOn(word voice, word freq, word volume);
void	HostVoiceOff(word voice);
void	HostVoiceSilence(word voice);
/* n frames of the voices and DACs added into mix (zeroed by the caller) */
void	HostMix(int32_t *mix, unsigned n);

typedef struct {
    StreamToken	HD_stream;		/* 0: not attached */
    word	HD_rate;		/* samples a second, 0: not set */
    Boolean	HD_stereo;
    dword	HD_pos;			/* 16.16, between prev and cur */
    dword	HD_step;		/* 16.16 input samples an output one */
    sword	HD_prev, HD_cur;	/* 16-bit samples */
    Boolean	HD_empty;		/* the writer was told so */
} HostDAC;

extern HostDAC hostDACs[HS_DACS];
extern word hostMasterLeft, hostMasterRight;	/* MixerLevel */

#endif
