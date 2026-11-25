/*
 * tsnddrv.c -- a sound driver for the sound library's test
 * (Port32/Tests/Library/test_sound.c): four FM voices and one DAC that
 * do nothing but log every call with the tick (tsnddrv.h).  The DAC
 * takes 8-bit PCM at 8000 Hz from a stream of 64 bytes; the test reads
 * the stream, as the hardware would.
 */
#include <geos.h>
#include <geode.h>
#include <timer.h>
#include <sound.h>
#include <geosdriver.h>
#include <Internal/soundDrv.h>
#include "tsnddrv.h"

static TsndLog tsndLog;

static void Log(word fn, const DriverRegs *r)
{
    TsndEvent *e;

    if (tsndLog.TL_count >= TSND_EVENTS)
	return;
    e = &tsndLog.TL_ev[tsndLog.TL_count++];
    e->TE_fn = fn;
    e->TE_ax = (word) r->DR_ax;
    e->TE_bx = (word) r->DR_bx;
    e->TE_cx = (word) r->DR_cx;
    e->TE_si = (word) r->DR_si;
    e->TE_tick = TimerGetCount();
}

static Boolean TsndStrategy(word function, DriverRegs *r)
{
    switch (function) {
    case DR_INIT:
    case DR_EXIT:
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case TSND_GET_LOG:
	r->DR_farPtr = &tsndLog;
	return FALSE;
    case DRE_SOUND_QUERY_DEVICE_CAPABILITY:
	r->DR_ax = TSND_VOICES;
	r->DR_dx = TSND_DACS;
	r->DR_bx = SEF_NO_FORMAT;
	r->DR_cx = SDWFC_SELECT | SDEC_ADSR;
	r->DR_di = SDDACC_INT;
	r->DR_farPtr = NULL;		/* the stream driver's */
	return FALSE;
    case DRE_SOUND_VOICE_ON:
    case DRE_SOUND_VOICE_OFF:
    case DRE_SOUND_VOICE_SILENCE:
    case DRE_SOUND_SET_ENVELOPE:
    case DRE_SOUND_DAC_DETTACH_FROM_STREAM:
    case DRE_SOUND_DAC_FLUSH_DAC:
	Log(function, r);
	if (function == DRE_SOUND_DAC_DETTACH_FROM_STREAM)
	    tsndLog.TL_dacStream = 0;
	return FALSE;
    case DRE_SOUND_DAC_ATTACH_TO_STREAM:
	Log(function, r);
	if (r->DR_cx >= TSND_DACS || tsndLog.TL_dacStream)
	    return TRUE;
	tsndLog.TL_dacStream = (StreamToken) r->DR_ax;
	return FALSE;
    case DRE_SOUND_DAC_SET_SAMPLE_RATE_AND_FORMAT:
    case DRE_SOUND_DAC_CHECK_SAMPLE_RATE_AND_FORMAT:
	Log(function, r);
	if (r->DR_ax != 0 ||
	    ((r->DR_bx & SMID_FORMAT) >> SMID_FORMAT_OFFSET) !=
	    DACSF_8_BIT_PCM)
	    r->DR_dx = 0;
	else
	    r->DR_dx = TSND_DAC_RATE;
	r->DR_cx = TSND_DAC_STREAM;
	return FALSE;
    case DRE_SOUND_DAC_IS_DAC_EMPTY:
	return FALSE;
    case DRE_SOUND_MIXER_GET_MASTER_VOLUME:
	r->DR_ax = (word) (tsndLog.TL_right << 8 | tsndLog.TL_left);
	return FALSE;
    case DRE_SOUND_MIXER_SET_MASTER_VOLUME:
	tsndLog.TL_left = (word) (r->DR_ax & 0x7f);
	tsndLog.TL_right = (word) ((r->DR_ax >> 8) & 0x7f);
	r->DR_ax = (word) (tsndLog.TL_right << 8 | tsndLog.TL_left);
	return FALSE;
    case DRE_SOUND_MIXER_GET_CAPABILITIES:
	r->DR_ax = 1;
	r->DR_dx = 0;
	return FALSE;
    default:
	return TRUE;
    }
}

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(TsndStrategy), DA_CHARACTER, DRIVER_TYPE_SOUND
};
