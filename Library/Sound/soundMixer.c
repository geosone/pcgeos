/*
 * soundMixer.c -- the mixer of the sound library (soundMixer.asm): it
 * asks the sound driver (DRE_SOUND_MIXER_*).  A level pair travels as
 * the asm's al (left) and ah (right) in DR_ax.
 *
 * Port32: the routines the asm had with registers only are C routines
 * (sound.h); every one returns 0 or the driver's error.
 */
#include "soundLocal.h"
#include <string.h>

static word Mixer(word fn, DriverRegs *r)
{
    if (SoundSynth(fn, r))
	return (word) r->DR_ax ? (word) r->DR_ax :
	    SOUND_ERROR_MIXER_UNKNOWN_DEVICE;
    return 0;
}

static void Levels(const DriverRegs *r, MixerLevel *left, MixerLevel *right)
{
    if (left)
	*left = (MixerLevel) r->DR_ax;
    if (right)
	*right = (MixerLevel) (r->DR_ax >> 8);
}

/* SOUNDMIXERGETMASTERVOLUME: the levels as words */
Boolean SoundMixerGetMasterVolume(word *left, word *right)
{
    DriverRegs r;
    word err;

    memset(&r, 0, sizeof(r));
    err = Mixer(DRE_SOUND_MIXER_GET_MASTER_VOLUME, &r);
    if (err)
	return (Boolean) err;
    if (left)
	*left = (word) (r.DR_ax & 0xff);
    if (right)
	*right = (word) ((r.DR_ax >> 8) & 0xff);
    return SOUND_ERROR_NO_ERRORS;
}

Boolean SoundMixerSetMasterVolume(word left, word right)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) ((right & 0xff) << 8 | (left & 0xff));
    return (Boolean) Mixer(DRE_SOUND_MIXER_SET_MASTER_VOLUME, &r);
}

/* DRE_SOUND_MIXER_GET_CAPABILITIES: ax sources, dx effects */
word SoundMixerGetSourceCount(void)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    return Mixer(DRE_SOUND_MIXER_GET_CAPABILITIES, &r) ? 0 : (word) r.DR_ax;
}

word SoundMixerGetEffectCount(void)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    return Mixer(DRE_SOUND_MIXER_GET_CAPABILITIES, &r) ? 0 : (word) r.DR_dx;
}

word SoundMixerMapSource(StdSourceType type, word *source,
			 StdSourceBehavior *behavior)
{
    DriverRegs r;
    word err;

    memset(&r, 0, sizeof(r));
    r.DR_ax = type;
    err = Mixer(DRE_SOUND_MIXER_MAP_SOURCE, &r);
    if (!err) {
	if (source)
	    *source = (word) r.DR_cx;
	if (behavior)
	    *behavior = (StdSourceBehavior) r.DR_bx;
    }
    return err;
}

word SoundMixerMapEffect(StdEffectType type, word *effect,
			 StdEffectBehavior *behavior)
{
    DriverRegs r;
    word err;

    memset(&r, 0, sizeof(r));
    r.DR_ax = type;
    err = Mixer(DRE_SOUND_MIXER_MAP_EFFECT, &r);
    if (!err) {
	if (effect)
	    *effect = (word) r.DR_cx;
	if (behavior)
	    *behavior = (StdEffectBehavior) r.DR_bx;
    }
    return err;
}

static word GetPair(word fn, word which, MixerLevel *left,
		    MixerLevel *right)
{
    DriverRegs r;
    word err;

    memset(&r, 0, sizeof(r));
    r.DR_cx = which;
    err = Mixer(fn, &r);
    if (!err)
	Levels(&r, left, right);
    return err;
}

static word SetPair(word fn, word which, MixerLevel left, MixerLevel right)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = which;
    r.DR_ax = (word) (right << 8 | left);
    return Mixer(fn, &r);
}

word SoundMixerGetSourceVolume(word source, MixerLevel *left,
			       MixerLevel *right)
{
    return GetPair(DRE_SOUND_MIXER_GET_SOURCE_VOLUME, source, left, right);
}

word SoundMixerSetSourceVolume(word source, MixerLevel left,
			       MixerLevel right)
{
    return SetPair(DRE_SOUND_MIXER_SET_SOURCE_VOLUME, source, left, right);
}

word SoundMixerGetEffectLevel(word effect, MixerLevel *left,
			      MixerLevel *right)
{
    return GetPair(DRE_SOUND_MIXER_GET_EFFECT_LEVEL, effect, left, right);
}

word SoundMixerSetEffectLevel(word effect, MixerLevel left,
			      MixerLevel right)
{
    return SetPair(DRE_SOUND_MIXER_SET_EFFECT_LEVEL, effect, left, right);
}
