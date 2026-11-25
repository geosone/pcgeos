/*
 * test_hostsnd.c -- the host sound driver (Driver/Sound/Host) under the
 * sound library, heard through the tap of its ghal_audio (ctest:
 * GEOS_AUDIO=null, nothing reaches a loudspeaker; the test also sets
 * that itself):
 *   - the driver's voices and DACs, its stream paused while silent;
 *   - a note: sound while it plays, at its pitch (autocorrelation),
 *     louder for a louder note, silence after its release, and the
 *     stream paused again after a second of silence;
 *   - a drum (a percussion patch) and a chord of three voices;
 *   - the master volume: off silences;
 *   - a sample stream: 8-bit PCM at 8000 Hz, a square wave of 200 Hz
 *     for half a second, resampled: its pitch and length.
 */
#define _GNU_SOURCE
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <sound.h>
#include <Internal/soundDrv.h>
#include <heap.h>
#include <thread.h>
#include <timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char sound_geode[], stream_geode[], hostsnd_geode[];
extern const unsigned long sound_geode_size, stream_geode_size,
    hostsnd_geode_size;

#define HOSTSND_GET_AUDIO	0x7e00
#define RATE			44100

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';
static GeodeHandle snd;

/* the library's routines by entry number (Library/Sound/sound32.gp) */
#define E(n, f)	((__typeof__(&f)) ProcGetLibraryEntry(snd, (n)))
#define S_AllocMusicNote	E(9, SoundAllocMusicNote)
#define S_PlayMusicNote		E(10, SoundPlayMusicNote)
#define S_FreeMusicNote		E(13, SoundFreeMusicNote)
#define S_AllocSampleStream	E(20, SoundAllocSampleStream)
#define S_FreeSampleStream	E(21, SoundFreeSampleStream)
#define S_PlayToSampleStream	E(22, SoundPlayToSampleStream)
#define S_EnableSampleStream	E(23, SoundEnableSampleStream)
#define S_DisableSampleStream	E(24, SoundDisableSampleStream)
#define S_SynthDriverInfo	E(35, SoundSynthDriverInfo)
#define S_SampleDriverInfo	E(36, SoundSampleDriverInfo)
#define S_MixerSetMaster	E(102, SoundMixerSetMasterVolume)

static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    sprintf(path, "%s%c%s", top, sep, rel);
    for (p = path; *p; p++)
	if (*p == '/')
	    *p = sep;
    if (!size) {
	ghal_fs_mkdir(path);
	return;
    }
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, data, size, &done);
    CHECK(done == size);
    ghal_fs_close(f);
}

/* ------------------------------------------------------------ the tap */

#define CAP_MAX	(RATE * 4)
static int16_t cap[CAP_MAX];
static volatile size_t capN;
static volatile int capOn;

static void Tap(void *ctx, const int16_t *f, size_t n)
{
    size_t i;

    (void) ctx;
    if (!capOn)
	return;
    for (i = 0; i < n && capN < CAP_MAX; i++)
	cap[capN++] = f[i];
}

static void Record(void)
{
    capOn = 0;
    capN = 0;
    capOn = 1;
}

/* root mean square of cap[a..b) */
static double Rms(size_t a, size_t b)
{
    double s = 0;
    size_t i;

    if (b > capN)
	b = capN;
    if (b <= a)
	return 0;
    for (i = a; i < b; i++)
	s += (double) cap[i] * cap[i];
    return __builtin_sqrt(s / (double) (b - a));
}

/* the period in samples of cap[a..a+len) by autocorrelation, lags
 * lo..hi */
static int Period(size_t a, size_t len, int lo, int hi)
{
    double best = 0;
    int lag, bestLag = 0;
    size_t i;

    if (a + len + (size_t) hi > capN)
	return 0;
    for (lag = lo; lag <= hi; lag++) {
	double s = 0;

	for (i = 0; i < len; i++)
	    s += (double) cap[a + i] * cap[a + i + (size_t) lag];
	if (s > best) {
	    best = s;
	    bestLag = lag;
	}
    }
    return bestLag;
}

/* the first sample from a on louder than lvl, or capN */
static size_t Onset(size_t a, int lvl)
{
    size_t i;

    for (i = a; i < capN; i++)
	if (cap[i] > lvl || cap[i] < -lvl)
	    return i;
    return capN;
}

static void Wait(unsigned ticks)
{
    TimerSleep(ticks);
}

static void PlayNote(InstrumentPatch inst, word freq, word vol, word ms,
		     MemHandle *mh)
{
    CHECK(S_AllocMusicNote(inst, IT_STANDARD_TABLE, freq, vol,
			      SSDTT_MSEC, ms, mh) == SOUND_ERROR_NO_ERRORS);
    CHECK(S_PlayMusicNote(*mh, SP_STANDARD, 1, UNLOCK_ON_EOS) ==
	  SOUND_ERROR_NO_ERRORS);
}

int main(int argc, char **argv)
{
    const char *ini = "[sound]\r\nsynthDriver = hostsnd.geo\r\n";
    char *slash;
    GeodeLoadError e;
    GeodeHandle drv;
    DriverRegs r;
    ghal_audio *au;
    MemHandle mh, mh2, mh3;
    word voices = 0;
    SupportedEnvelopeFormat fmt;
    SoundDriverCapability cap0 = 0;
    size_t on;
    int per;
    double loud, soft;
    uint64_t p1;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("GEOS_AUDIO", "null", 1);		/* never heard */
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (slash) {
	sep = *slash;
	slash[1] = 0;
    } else
	top[0] = 0;
    strcat(top, "GHSTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/SOUND", NULL, 0);
    Put("SYSTEM/stream.geo", stream_geode, stream_geode_size);
    Put("SYSTEM/sound.geo", sound_geode, sound_geode_size);
    Put("SYSTEM/SOUND/hostsnd.geo", hostsnd_geode, hostsnd_geode_size);
    Put("geos.ini", ini, strlen(ini));
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_SYSTEM);
    snd = GeodeUseLibrary("sound.geo", 0, 0, &e);
    CHECK(snd != 0);
    drv = GeodeFind("hostsnd ", GEODE_NAME_SIZE, 0, 0);
    CHECK(drv != 0);
    if (!snd || !drv) {
	printf("sound.geo or hostsnd.geo not loaded (%d)\n", (int) e);
	return 1;
    }
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(GeodeInfoDriver(drv), HOSTSND_GET_AUDIO, &r));
    au = r.DR_farPtr;
    CHECK(au != NULL);
    if (!au)
	return 1;
    printf("backend %s\n", ghal_audio_backend(au));
    CHECK(!strcmp(ghal_audio_backend(au), "null"));
    ghal_audio_set_tap(au, Tap, NULL);

    S_SynthDriverInfo(&voices, &fmt, &cap0);
    CHECK(voices == 16 && fmt == SEF_SBI_FORMAT);
    S_SampleDriverInfo(&voices, &cap0);
    CHECK(voices == 2);

    /* silent so far: paused */
    Wait(10);
    CHECK(ghal_audio_played(au) == 0);

    /* a flute's A: 440 Hz */
    Record();
    PlayNote(IP_FLUTE, 440, DYNAMIC_FF, 400, &mh);
    Wait(45);
    on = Onset(0, 200);
    printf("note: onset %u, captured %u\n", (unsigned) on, (unsigned) capN);
    CHECK(on < RATE / 10);
    loud = Rms(on + RATE / 10, on + RATE * 3 / 10);
    per = Period(on + RATE / 10, 4096, 60, 200);
    printf("note: rms %.0f, period %d (%.1f Hz)\n", loud, per,
	   per ? (double) RATE / per : 0.0);
    CHECK(loud > 500);
    CHECK(per >= 98 && per <= 103);		/* 44100 / 440 = 100.2 */
    /* released: silence within a second */
    Wait(60);
    CHECK(Rms(capN - RATE / 10, capN) < 20);
    S_FreeMusicNote(mh);
    /* and paused after a second of it */
    Wait(90);
    p1 = ghal_audio_played(au);
    Wait(20);
    CHECK(ghal_audio_played(au) == p1);

    /* softer is softer */
    Record();
    PlayNote(IP_FLUTE, 440, DYNAMIC_PP, 400, &mh);
    Wait(30);
    on = Onset(0, 30);
    soft = Rms(on + RATE / 10, on + RATE * 3 / 10);
    printf("soft: rms %.0f\n", soft);
    CHECK(soft > 20 && soft < loud * 0.5);
    Wait(80);
    S_FreeMusicNote(mh);

    /* a snare drum and a chord */
    Record();
    PlayNote(IP_ACOUSTIC_SNARE, 200, DYNAMIC_FF, 100, &mh);
    Wait(20);
    CHECK(Rms(0, capN) > 100);
    S_FreeMusicNote(mh);
    Record();
    PlayNote(IP_ACOUSTIC_GRAND_PIANO, 262, DYNAMIC_F, 300, &mh);
    PlayNote(IP_ACOUSTIC_GRAND_PIANO, 330, DYNAMIC_F, 300, &mh2);
    PlayNote(IP_ACOUSTIC_GRAND_PIANO, 392, DYNAMIC_F, 300, &mh3);
    Wait(15);
    {
	double chord = Rms(Onset(0, 200), capN);
	printf("chord: rms %.0f\n", chord);
	CHECK(chord > 500);
    }
    Wait(60);
    S_FreeMusicNote(mh);
    S_FreeMusicNote(mh2);
    S_FreeMusicNote(mh3);

    /* the master volume off: nothing */
    CHECK(S_MixerSetMaster(0, 0) == 0);
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(GeodeInfoDriver(drv),
		      DRE_SOUND_MIXER_GET_MASTER_VOLUME, &r));
    CHECK((word) r.DR_ax == 0);
    Record();
    PlayNote(IP_FLUTE, 440, DYNAMIC_FF, 200, &mh);
    Wait(20);
    /* (after what was in the ring: 50 ms) */
    CHECK(capN > RATE / 5 && Rms(RATE / 10, capN) < 1);
    Wait(40);
    S_FreeMusicNote(mh);
    CHECK(S_MixerSetMaster(MIXER_LVL_MAX, MIXER_LVL_MAX) == 0);

    /* a sample stream: 200 Hz square wave, 8000 Hz, half a second */
    {
	static byte sq[4000];
	SampleFormatDescription d;
	size_t i, a, b;

	for (i = 0; i < sizeof(sq); i++)
	    sq[i] = (byte) ((i / 20) % 2 ? 0x80 + 64 : 0x80 - 64);
	CHECK(S_AllocSampleStream(&mh) == 0);
	CHECK(S_EnableSampleStream(mh, SP_STANDARD, 8000, 0,
				      DACSF_8_BIT_PCM << SMID_FORMAT_OFFSET)
	      == 0);
	Record();
	d.SFD_manufact = 0;
	d.SFD_format = DACSF_8_BIT_PCM << SMID_FORMAT_OFFSET;
	d.SFD_rate = 8000;
	d.SFD_playFlags = DACPF_CATENATE;
	CHECK(S_PlayToSampleStream(mh, (const word *) sq, sizeof(sq), &d)
	      == 0);
	Wait(50);
	a = Onset(0, 4000);
	for (b = capN; b > a && cap[b - 1] > -4000 && cap[b - 1] < 4000; b--)
	    ;
	per = Period(a + 1000, 4096, 150, 300);
	printf("samples: %u to %u (%u), period %d\n", (unsigned) a,
	       (unsigned) b, (unsigned) (b - a), per);
	CHECK(b - a > RATE / 2 - 400 && b - a < RATE / 2 + 400);
	CHECK(per >= 218 && per <= 223);	/* 44100 / 200 = 220.5 */
	S_DisableSampleStream(mh);
	S_FreeSampleStream(mh);
    }

    printf("underruns %llu\n",
	   (unsigned long long) ghal_audio_underruns(au));
    GeodeFreeLibrary(snd);
    printf("%s\n", fails ? "FAILED" : "hostsnd: ok");
    return fails != 0;
}
