/*
 * hostSound.c -- the host sound driver (hostsnd.geo): the sound driver
 * of the sound library on the host's audio (ghal_audio, PipeWire on
 * Linux), as the Sound Blaster driver is on the card.
 *
 * HS_VOICES FM voices with the Sound Blaster's instruments and HS_DACS
 * DACs for sample streams (hostSynth.c), mixed in the GHAL_IRQ_AUDIO
 * interrupt into one mono stream of HS_RATE.  The stream plays only
 * while something sounds: the first note or attached DAC starts it,
 * the interrupt pauses it after a second of silence.
 *
 * The functions are the asm's (Internal/soundDrv.h); they run inside
 * ghal_crit, as the library calls some of them from interrupt code.
 * The instrument tables of the caller's own (DR_bx not
 * IT_STANDARD_TABLE: a far pointer in the asm) are not supported: the
 * patch is taken from the standard table.  DACs take 8-bit PCM only
 * (no ADPCM), mono or stereo.
 */
#include "hostSound.h"
#include <string.h>

#define MIX_CHUNK	512
#define IDLE_FRAMES	HS_RATE			/* a second */

static ghal_audio *audio;
static ghal_irq_source *audioIrq;
static Boolean playing;
static dword idle;

/* on: from the next interrupt on (inside ghal_crit) */
static void Wake(void)
{
    idle = 0;
    if (audio && !playing) {
	playing = TRUE;
	ghal_audio_pause(audio, 0);
    }
}

static Boolean DacsActive(void)
{
    int i;

    for (i = 0; i < HS_DACS; i++)
	if (hostDACs[i].HD_stream && !hostDACs[i].HD_empty)
	    return TRUE;
    return FALSE;
}

/* GHAL_IRQ_AUDIO: the ring refilled */
static void AudioInterrupt(void *ctx)
{
    static int32_t mix[MIX_CHUNK];
    int16_t out[MIX_CHUNK];
    size_t room;
    int32_t gain;

    (void) ctx;
    if (!audio)
	return;
    gain = (int32_t) ((hostMasterLeft + hostMasterRight) & 0xff);
    while ((room = ghal_audio_room(audio)) > 0) {
	unsigned n = room > MIX_CHUNK ? MIX_CHUNK : (unsigned) room, k;

	memset(mix, 0, n * sizeof(int32_t));
	HostMix(mix, n);
	Boolean heard = FALSE;

	for (k = 0; k < n; k++) {
	    int32_t s = mix[k] * gain / (2 * MIXER_LVL_MAX);

	    out[k] = (int16_t) (s > 32767 ? 32767 : s < -32768 ? -32768 : s);
	    if (out[k] > 1 || out[k] < -1)
		heard = TRUE;
	}
	(void) ghal_audio_write(audio, out, n);
	/* silent is what is not heard (a long release is, long after a
	 * voice ended) */
	if (heard || DacsActive())
	    idle = 0;
	else
	    idle += n;
    }
    if (playing && idle >= IDLE_FRAMES) {
	playing = FALSE;
	ghal_audio_pause(audio, 1);
    }
}

static Boolean Init(void)
{
    HostSynthInit();
    memset(hostDACs, 0, sizeof(hostDACs));
    if (ghal_audio_open(HS_RATE, 1, HS_RATE / 20, &audio) != GHAL_OK) {
	audio = NULL;
	return TRUE;
    }
    if (ghal_irq_attach(GHAL_IRQ_AUDIO, AudioInterrupt, NULL, &audioIrq) !=
	GHAL_OK) {
	ghal_audio_close(audio);
	audio = NULL;
	return TRUE;
    }
    ghal_log(GHAL_LOG_INFO, "hostsnd: %u Hz on %s", (unsigned) HS_RATE,
	     ghal_audio_backend(audio));
    return FALSE;
}

static void Exit(void)
{
    if (audioIrq)
	ghal_irq_detach(audioIrq);
    audioIrq = NULL;
    if (audio)
	ghal_audio_close(audio);
    audio = NULL;
    playing = FALSE;
}

/* --------------------------------------------------------------- DACs */

static HostDAC *Dac(const DriverRegs *r)
{
    return (word) r->DR_cx < HS_DACS ? &hostDACs[(word) r->DR_cx] : NULL;
}

/* DR_dx: the rate (0: not supported), DR_cx: the stream's size */
static void CheckFormat(DriverRegs *r, Boolean set)
{
    HostDAC *d = Dac(r);
    word fmt = (word) (((word) r->DR_bx & SMID_FORMAT) >> SMID_FORMAT_OFFSET);
    word rate = (word) r->DR_dx;

    if (!d || r->DR_ax != 0 || (fmt & ~DACSF_STEREO) != DACSF_8_BIT_PCM ||
	rate < 2000 || rate > 48000) {
	r->DR_dx = 0;
	r->DR_cx = SSS_MEDIUM;
	return;
    }
    /* a fifth of a second (stereo: twice the bytes) */
    r->DR_cx = (word) ((rate / 5 + 255) & ~255u) * ((fmt & DACSF_STEREO) ?
						       2u : 1u);
    if (set) {
	d->HD_rate = rate;
	d->HD_stereo = (fmt & DACSF_STEREO) != 0;
	d->HD_step = (dword) (((uint64_t) rate << 16) / HS_RATE);
    }
}

static Boolean Strategy(word fn, DriverRegs *r)
{
    HostDAC *d;
    Boolean err = FALSE;

    switch (fn) {
    case DR_INIT:
	return Init();
    case DR_EXIT:
	Exit();
	return FALSE;
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case HOSTSND_GET_AUDIO:
	r->DR_farPtr = audio;
	return FALSE;
    case DRE_SOUND_QUERY_DEVICE_CAPABILITY:
	r->DR_ax = HS_VOICES;
	r->DR_dx = HS_DACS;
	r->DR_bx = SEF_SBI_FORMAT;
	r->DR_cx = SDWFC_SELECT | SDEC_ADSR;
	r->DR_di = SDDACC_INT;
	r->DR_farPtr = NULL;
	return FALSE;
    }
    ghal_crit_enter();
    switch (fn) {
    case DRE_SOUND_SET_ENVELOPE:
	HostVoiceEnvelope((word) r->DR_cx, (word) r->DR_si);
	break;
    case DRE_SOUND_VOICE_ON:
	HostVoiceOn((word) r->DR_cx, (word) r->DR_ax, (word) r->DR_bx);
	Wake();
	break;
    case DRE_SOUND_VOICE_OFF:
	HostVoiceOff((word) r->DR_cx);
	break;
    case DRE_SOUND_VOICE_SILENCE:
	HostVoiceSilence((word) r->DR_cx);
	break;
    case DRE_SOUND_DAC_ATTACH_TO_STREAM:
	d = Dac(r);
	if (!d || d->HD_stream)
	    err = TRUE;
	else {
	    d->HD_stream = (StreamToken) r->DR_ax;
	    d->HD_pos = 0x20000;		/* two samples first */
	    d->HD_prev = d->HD_cur = 0;
	    d->HD_empty = FALSE;
	    Wake();
	}
	break;
    case DRE_SOUND_DAC_DETTACH_FROM_STREAM:
	d = Dac(r);
	if (d)
	    d->HD_stream = 0;
	break;
    case DRE_SOUND_DAC_SET_SAMPLE_RATE_AND_FORMAT:
	CheckFormat(r, TRUE);
	break;
    case DRE_SOUND_DAC_CHECK_SAMPLE_RATE_AND_FORMAT:
	CheckFormat(r, FALSE);
	break;
    case DRE_SOUND_DAC_RESET_REFERENCE_BYTE:
	break;
    case DRE_SOUND_DAC_FLUSH_DAC:
	d = Dac(r);
	if (d) {
	    d->HD_pos = 0x20000;
	    d->HD_prev = d->HD_cur = 0;
	}
	break;
    case DRE_SOUND_DAC_IS_DAC_EMPTY:
	d = Dac(r);
	err = d && d->HD_stream &&
	    StreamDataOf(d->HD_stream)->SD_reader.SSD_sem.Sem_value > 0;
	break;
    case DRE_SOUND_MIXER_GET_MASTER_VOLUME:
	r->DR_ax = (word) (hostMasterRight << 8 | hostMasterLeft);
	break;
    case DRE_SOUND_MIXER_SET_MASTER_VOLUME:
	hostMasterLeft = (word) (r->DR_ax & 0xff);
	hostMasterRight = (word) ((r->DR_ax >> 8) & 0xff);
	if (hostMasterLeft > MIXER_LVL_MAX)
	    hostMasterLeft = 0;		/* MIXER_LVL_OFF */
	if (hostMasterRight > MIXER_LVL_MAX)
	    hostMasterRight = 0;
	r->DR_ax = (word) (hostMasterRight << 8 | hostMasterLeft);
	break;
    case DRE_SOUND_MIXER_GET_CAPABILITIES:
	r->DR_ax = 0;			/* no sources but the master */
	r->DR_dx = 0;
	break;
    default:
	err = TRUE;
	r->DR_ax = SOUND_ERROR_MIXER_UNKNOWN_DEVICE;
	break;
    }
    ghal_crit_leave();
    return err;
}

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(Strategy), DA_CHARACTER, DRIVER_TYPE_SOUND
};
