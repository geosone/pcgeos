/*
 * hostSynth.c -- the voices and DACs of the host sound driver, mixed in
 * the GHAL_IRQ_AUDIO interrupt.
 *
 * An FM voice is two operators, modulator and carrier, played from the
 * Sound Blaster driver's instruments (SBI, hostInstruments.h) the way
 * the OPL2 of the card plays them: frequency multiple, the waveforms
 * (sine, half sine, absolute sine, quarter sine), the envelope (attack,
 * decay to the sustain level, held there or released on, release) at the
 * chip's rates, total level, tremolo and vibrato, the modulator's
 * feedback, and either FM (the modulator moves the carrier's phase) or
 * the two added.  The volume of a note sets the carrier's total level,
 * as the asm driver did.  Not modelled: key scaling (KSL, KSR).
 *
 * A DAC reads 8-bit unsigned PCM (mono, or stereo taken as mono) from
 * its stream at its rate, interpolated to the mix's rate; the writer
 * is told when there is room (StreamWriteDataNotify), and once when the
 * stream ran empty.
 *
 * No libm (a geode imports only what the kernel exports): the sine and
 * attenuation tables are computed at the start by rotation and repeated
 * multiplication.
 */
#include "hostSound.h"
#include <string.h>

#define SINE_BITS	10
#define SINE_SIZE	(1 << SINE_BITS)
#define ATT_STEPS	1024		/* 0.09375 dB each: 96 dB */
#define ATT_DB_STEP	0.09375f
#define ENV_MAX		96.0f		/* dB: silent */

static float sineTable[SINE_SIZE];
static float attTable[ATT_STEPS + 1];	/* linear gain of i steps */

typedef enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE }
    EnvStage;

typedef struct {
    dword	phase;			/* 2^32 a cycle */
    float	mult;
    byte	wave;
    Boolean	am, vib, egt;
    float	tl;			/* dB */
    float	sl;			/* dB */
    float	attackK, decayStep, releaseStep;	/* per sample */
    EnvStage	stage;
    float	env;			/* dB of attenuation */
} Operator;

typedef struct {
    Operator	op[2];			/* modulator, carrier */
    byte	feedback;		/* 0..7 */
    Boolean	additive;
    float	fb1, fb2;		/* the modulator's last outputs */
    float	freq;			/* Hz */
    byte	carScaling;
} Voice;

static Voice voices[HS_VOICES];
HostDAC hostDACs[HS_DACS];
word hostMasterLeft = MIXER_LVL_MAX, hostMasterRight = MIXER_LVL_MAX;
static dword lfoPhase;			/* samples */

static const float multTable[16] = {
    0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15
};

void HostSynthInit(void)
{
    double c = 0.9999811752826011, s = 0.006135884649154475;  /* 2pi/1024 */
    double x = 1, y = 0, g = 1, f = 0.9892646717677409;  /* -0.09375 dB */
    int i;

    for (i = 0; i < SINE_SIZE; i++) {
	double nx;

	sineTable[i] = (float) y;
	nx = x * c - y * s;
	y = x * s + y * c;
	x = nx;
    }
    for (i = 0; i <= ATT_STEPS; i++) {
	attTable[i] = (float) g;
	g *= f;
    }
    attTable[ATT_STEPS] = 0;
    memset(voices, 0, sizeof(voices));
    for (i = 0; i < HS_VOICES; i++)
	HostVoiceEnvelope((word) i, 0);
}

static float Gain(float db)
{
    int i;

    if (db >= ENV_MAX)
	return 0;
    if (db <= 0)
	return 1;
    i = (int) (db / ATT_DB_STEP);
    return attTable[i];
}

/* the OPL2's times: attack 0 -> full, decay/release full -> -96 dB */
static float AttackMs(int r)
{
    float t = 2826.24f;

    while (--r > 0)
	t *= 0.5f;
    return t;
}

static float DecayMs(int r)
{
    float t = 39280.64f;

    while (--r > 0)
	t *= 0.5f;
    return t;
}

static void SetOperator(Operator *o, byte timbre, byte scaling, byte ad,
			byte sr, byte wave)
{
    int ar = ad >> 4, dr = ad & 15, sl = sr >> 4, rr = sr & 15;

    o->mult = multTable[timbre & 15];
    o->am = (timbre & 0x80) != 0;
    o->vib = (timbre & 0x40) != 0;
    o->egt = (timbre & 0x20) != 0;
    o->tl = (float) (scaling & 0x3f) * 0.75f;
    o->sl = sl == 15 ? 93.0f : (float) sl * 3.0f;
    o->wave = (byte) (wave & 3);
    /* attack: e' = -(e / 8 + 0.75) k reaches 0 from 96 dB in 22.67 / k */
    if (ar == 0)
	o->attackK = 0;
    else if (ar == 15)
	o->attackK = -1;			/* at once */
    else
	o->attackK = 22.67f / (AttackMs(ar) * HS_RATE / 1000.0f);
    o->decayStep = dr ? ENV_MAX / (DecayMs(dr) * HS_RATE / 1000.0f) : 0;
    o->releaseStep = rr ? ENV_MAX / (DecayMs(rr) * HS_RATE / 1000.0f) : 0;
}

void HostVoiceEnvelope(word voice, word patch)
{
    const unsigned char *p;
    Voice *v;

    if (voice >= HS_VOICES)
	return;
    if (patch >= hostInstrumentCount)
	patch = 0;
    p = hostInstruments[patch];
    v = &voices[voice];
    SetOperator(&v->op[0], p[SBI_MOD_TIMBRE], p[SBI_MOD_SCALING],
		p[SBI_MOD_ATTACK], p[SBI_MOD_SUSTAIN], p[SBI_MOD_WAVE]);
    SetOperator(&v->op[1], p[SBI_CAR_TIMBRE], p[SBI_CAR_SCALING],
		p[SBI_CAR_ATTACK], p[SBI_CAR_SUSTAIN], p[SBI_CAR_WAVE]);
    v->carScaling = p[SBI_CAR_SCALING];
    v->feedback = (byte) ((p[SBI_FEEDBACK] >> 1) & 7);
    v->additive = (p[SBI_FEEDBACK] & 1) != 0;
}

void HostVoiceOn(word voice, word freq, word volume)
{
    Voice *v;
    int i;

    if (voice >= HS_VOICES)
	return;
    v = &voices[voice];
    v->freq = (float) freq;
    /* the carrier's total level from the volume (SBDVoiceOn) */
    v->op[1].tl = (float) ((255 - (volume >> 8)) >> 2) * 0.75f;
    for (i = 0; i < 2; i++) {
	Operator *o = &v->op[i];

	o->phase = 0;
	if (o->attackK < 0) {
	    o->env = 0;
	    o->stage = ENV_DECAY;
	} else {
	    if (o->stage == ENV_OFF)
		o->env = ENV_MAX;
	    o->stage = ENV_ATTACK;
	}
    }
    v->fb1 = v->fb2 = 0;
}

void HostVoiceOff(word voice)
{
    int i;

    if (voice >= HS_VOICES)
	return;
    for (i = 0; i < 2; i++)
	if (voices[voice].op[i].stage != ENV_OFF)
	    voices[voice].op[i].stage = ENV_RELEASE;
}

void HostVoiceSilence(word voice)
{
    int i;

    if (voice >= HS_VOICES)
	return;
    for (i = 0; i < 2; i++) {
	voices[voice].op[i].stage = ENV_OFF;
	voices[voice].op[i].env = ENV_MAX;
    }
}

/* one sample of the envelope */
static void EnvStep(Operator *o)
{
    switch (o->stage) {
    case ENV_ATTACK:
	o->env -= (o->env * 0.125f + 0.75f) * o->attackK;
	if (o->env <= 0) {
	    o->env = 0;
	    o->stage = ENV_DECAY;
	}
	break;
    case ENV_DECAY:
	o->env += o->decayStep;
	if (o->env >= o->sl) {
	    o->env = o->sl;
	    o->stage = o->egt ? ENV_SUSTAIN : ENV_RELEASE;
	}
	break;
    case ENV_SUSTAIN:
	break;
    case ENV_RELEASE:
	o->env += o->releaseStep;
	if (o->env >= ENV_MAX) {
	    o->env = ENV_MAX;
	    o->stage = ENV_OFF;
	}
	break;
    case ENV_OFF:
	break;
    }
}

/* the waveform at phase (cycles in 2^32) moved by mod cycles */
static float Wave(byte w, dword phase, float mod)
{
    dword p = phase + (dword) (sdword) (mod * 4294967296.0f);
    unsigned i = (unsigned) (p >> (32 - SINE_BITS));
    float s = sineTable[i];

    switch (w) {
    case 1:			/* half sine */
	return i < SINE_SIZE / 2 ? s : 0;
    case 2:			/* absolute sine */
	return s < 0 ? -s : s;
    case 3:			/* quarter sine, twice a cycle */
	return (i & (SINE_SIZE / 2 - 1)) < SINE_SIZE / 4 ? (s < 0 ? -s : s)
	    : 0;
    default:
	return s;
    }
}

/* the LFOs: tremolo 1 dB at 3.7 Hz, vibrato 7 cents at 6.1 Hz */
static float Tremolo(void)
{
    dword p = (dword) ((float) lfoPhase * 3.7f / HS_RATE * 4294967296.0f);

    return (sineTable[p >> (32 - SINE_BITS)] + 1) * 0.5f;	/* dB */
}

static float Vibrato(void)
{
    dword p = (dword) ((float) lfoPhase * 6.1f / HS_RATE * 4294967296.0f);

    return 1 + sineTable[p >> (32 - SINE_BITS)] * 0.00405f;	/* 7 cents */
}

static void MixVoice(Voice *v, int32_t *mix, unsigned n, dword lfo0)
{
    static const float fbCycles[8] = {
	0, 1.0f / 32, 1.0f / 16, 1.0f / 8, 1.0f / 4, 1.0f / 2, 1, 2
    };
    Operator *m = &v->op[0], *c = &v->op[1];
    float scale = 4294967296.0f / HS_RATE;
    unsigned k;

    for (k = 0; k < n; k++) {
	float trem = 0, vib = 1, mo, co, fb = 0;

	lfoPhase = lfo0 + k;
	if (m->am || c->am)
	    trem = Tremolo();
	if (m->vib || c->vib)
	    vib = Vibrato();
	if (v->feedback)
	    fb = (v->fb1 + v->fb2) * 0.5f * fbCycles[v->feedback];
	mo = Wave(m->wave, m->phase, fb) *
	    Gain(m->env + m->tl + (m->am ? trem : 0));
	v->fb2 = v->fb1;
	v->fb1 = mo;
	if (v->additive)
	    co = Wave(c->wave, c->phase, 0) *
		Gain(c->env + c->tl + (c->am ? trem : 0)) + mo;
	else
	    co = Wave(c->wave, c->phase, mo * 4) *
		Gain(c->env + c->tl + (c->am ? trem : 0));
	mix[k] += (int32_t) (co * 6000.0f);
	m->phase += (dword) (v->freq * m->mult * (m->vib ? vib : 1) * scale);
	c->phase += (dword) (v->freq * c->mult * (c->vib ? vib : 1) * scale);
	EnvStep(m);
	EnvStep(c);
    }
}

/* the next input sample of a DAC; FALSE if the stream is empty */
static Boolean DacNext(HostDAC *d, StreamData *sd)
{
    byte b, b2;

    if (StreamGetByteNB(sd, &b))
	return FALSE;
    if (d->HD_stereo && !StreamGetByteNB(sd, &b2))
	b = (byte) (((unsigned) b + b2) / 2);
    d->HD_prev = d->HD_cur;
    d->HD_cur = (sword) (((int) b - 128) << 8);
    return TRUE;
}

static void MixDAC(HostDAC *d, int32_t *mix, unsigned n)
{
    StreamData *sd;
    Boolean any = FALSE, empty = FALSE;
    unsigned k;

    if (!d->HD_stream || !d->HD_rate)
	return;
    sd = StreamDataOf(d->HD_stream);
    for (k = 0; k < n; k++) {
	int32_t f;

	while (d->HD_pos >= 0x10000) {
	    if (!DacNext(d, sd)) {
		empty = TRUE;
		break;
	    }
	    any = TRUE;
	    d->HD_pos -= 0x10000;
	}
	if (empty) {
	    d->HD_prev = d->HD_cur = 0;
	    d->HD_pos = 0x10000;
	    break;
	}
	f = (int32_t) d->HD_pos;
	mix[k] += d->HD_prev + (((d->HD_cur - d->HD_prev) * f) >> 16);
	d->HD_pos += d->HD_step;
    }
    if (any) {
	d->HD_empty = FALSE;
	StreamWriteDataNotify(sd);		/* room */
    } else if (empty && !d->HD_empty) {
	d->HD_empty = TRUE;
	StreamWriteDataNotify(sd);		/* drained */
    }
}

void HostMix(int32_t *mix, unsigned n)
{
    dword lfo0 = lfoPhase;
    int i;

    /* heard is the carrier, and the modulator when the two are added */
    for (i = 0; i < HS_VOICES; i++)
	if (voices[i].op[1].stage != ENV_OFF ||
	    (voices[i].additive && voices[i].op[0].stage != ENV_OFF))
	    MixVoice(&voices[i], mix, n, lfo0);
    lfoPhase = lfo0 + n;
    for (i = 0; i < HS_DACS; i++)
	MixDAC(&hostDACs[i], mix, n);
}
