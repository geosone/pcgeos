/*
 * test_sound.c -- the sound library (Library/Sound) in the C kernel with
 * the test driver tsnddrv (Port32/Tests/Geode/tsnddrv.c), which logs
 * what it is told; the test reads the DAC's stream as the hardware would.
 *   - the driver of "[sound] synthDriver", its voices and DACs;
 *   - a song: envelopes, voices on and off at their delta times (ms,
 *     ticks, tempo), the voices given back and the block unlocked at the
 *     end; a semaphore V'd by the song (a handle in two words);
 *   - a note, reallocated and played again;
 *   - stopping a song: the voice silenced, nothing more;
 *   - priorities: a more urgent sound takes a voice of a less urgent one,
 *     a less urgent one gets none;
 *   - exclusive access keeps the routines out;
 *   - a music stream in two writes, one to the end of the song; a
 *     non-blocking write into a full stream;
 *   - a sample stream: an unsupported format; the samples through a
 *     stream smaller than they are, read by the "DAC"; disabled, freed;
 *   - the master volume.
 */
#define _GNU_SOURCE
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <sound.h>
#include <Internal/soundDrv.h>
#include <Internal/strDrInt.h>
#include <heap.h>
#include <thread.h>
#include <timer.h>
#include "tsnddrv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char sound_geode[], stream_geode[], tsnddrv_geode[];
extern const unsigned long sound_geode_size, stream_geode_size,
    tsnddrv_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';

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

static GeodeHandle snd, strm;
static TsndLog *tlog;

/* the library's routines by entry number (sound32.gp) */
#define E(n, f)	((__typeof__(&f)) ProcGetLibraryEntry(snd, (n)))
#define S_GetExclusive		E(0, SoundGetExclusive)
#define S_GetExclusiveNB	E(1, SoundGetExclusiveNB)
#define S_ReleaseExclusive	E(2, SoundReleaseExclusive)
#define S_AllocMusic		E(3, SoundAllocMusic)
#define S_PlayMusic		E(4, SoundPlayMusic)
#define S_StopMusic		E(5, SoundStopMusic)
#define S_FreeMusic		E(7, SoundFreeMusic)
#define S_AllocMusicNote	E(9, SoundAllocMusicNote)
#define S_ReallocMusicNote	E(12, SoundReallocMusicNote)
#define S_AllocMusicStream	E(14, SoundAllocMusicStream)
#define S_StopMusicStream	E(15, SoundStopMusicStream)
#define S_FreeMusicStream	E(16, SoundFreeMusicStream)
#define S_PlayToMusicStream	E(17, SoundPlayToMusicStream)
#define S_AllocSampleStream	E(20, SoundAllocSampleStream)
#define S_FreeSampleStream	E(21, SoundFreeSampleStream)
#define S_PlayToSampleStream	E(22, SoundPlayToSampleStream)
#define S_EnableSampleStream	E(23, SoundEnableSampleStream)
#define S_DisableSampleStream	E(24, SoundDisableSampleStream)
#define S_VMGetFree		E(25, SoundVoiceManagerGetFree)
#define S_VMFree		E(26, SoundVoiceManagerFree)
#define S_SynthDriverInfo	E(35, SoundSynthDriverInfo)
#define S_SampleDriverInfo	E(36, SoundSampleDriverInfo)
#define S_PlayToMusicStreamNB	E(95, SoundPlayToMusicStreamNB)
#define S_MixerGetMaster	E(101, SoundMixerGetMasterVolume)
#define S_MixerSetMaster	E(102, SoundMixerSetMasterVolume)

/* the n-th logged call of fn from `from' on; -1 if none */
static int Find(word fn, int from, int n)
{
    int i;

    for (i = from; i < tlog->TL_count; i++)
	if (tlog->TL_ev[i].TE_fn == fn && n-- == 0)
	    return i;
    return -1;
}

/* wait (up to 2 s) for a call of fn after `from' */
static int WaitFor(word fn, int from, int n)
{
    int i, t;

    for (t = 0; t < 120; t++) {
	if ((i = Find(fn, from, n)) >= 0)
	    return i;
	TimerSleep(1);
    }
    return -1;
}

static word Locks(MemHandle mh)
{
    return MGI_LOCK_COUNT(MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT));
}

static dword Tick(int i)
{
    return tlog->TL_ev[i].TE_tick;
}

static void Song(void)
{
    static const word song[] = {
	ChangeEnvelope(1, IP_FLUTE, IT_STANDARD_TABLE),
	VoiceOn(0, MIDDLE_C, DYNAMIC_F),
	DeltaMS(50),
	VoiceOn(1, MIDDLE_E, DYNAMIC_MF),
	DeltaTick(3),
	VoiceOff(0),
	DeltaTempo(QUARTER),		/* 32 64ths of 2 ms */
	VoiceOff(1),
	General(GE_END_OF_SONG)
    };
    MemHandle mh;
    int s = tlog->TL_count, on0, on1, off0, off1, env;
    word t;

    CHECK(S_AllocMusic(song, 2, &mh) == SOUND_ERROR_NO_ERRORS && mh);
    CHECK(Locks(mh) == 0);
    CHECK(S_PlayMusic(mh, SP_STANDARD, 2, UNLOCK_ON_EOS) ==
	  SOUND_ERROR_NO_ERRORS);
    /* the first events at once: voice 0 on physical voice 0 */
    env = Find(DRE_SOUND_SET_ENVELOPE, s, 0);
    on0 = Find(DRE_SOUND_VOICE_ON, s, 0);
    CHECK(env >= 0 && on0 == env + 1);
    CHECK(env >= 0 && tlog->TL_ev[env].TE_cx == 0 &&
	  tlog->TL_ev[env].TE_si == DEFAULT_INSTRUMENT &&
	  tlog->TL_ev[env].TE_bx == IT_STANDARD_TABLE);
    CHECK(on0 >= 0 && tlog->TL_ev[on0].TE_ax == MIDDLE_C &&
	  tlog->TL_ev[on0].TE_bx == DYNAMIC_F && tlog->TL_ev[on0].TE_cx == 0);
    CHECK(Find(DRE_SOUND_VOICE_ON, s, 1) < 0);
    CHECK(Locks(mh) == 1);
    /* 50 ms later voice 1 with the flute */
    on1 = WaitFor(DRE_SOUND_VOICE_ON, s, 1);
    CHECK(on1 > 0 && tlog->TL_ev[on1].TE_ax == MIDDLE_E &&
	  tlog->TL_ev[on1].TE_cx == 1);
    CHECK(on1 > 0 && tlog->TL_ev[on1 - 1].TE_fn == DRE_SOUND_SET_ENVELOPE &&
	  tlog->TL_ev[on1 - 1].TE_si == IP_FLUTE);
    CHECK(on1 > 0 && Tick(on1) - Tick(on0) >= 3 &&
	  Tick(on1) - Tick(on0) <= 8);
    off0 = WaitFor(DRE_SOUND_VOICE_OFF, s, 0);
    CHECK(off0 > 0 && tlog->TL_ev[off0].TE_cx == 0 &&
	  Tick(off0) - Tick(on1) >= 3 && Tick(off0) - Tick(on1) <= 8);
    off1 = WaitFor(DRE_SOUND_VOICE_OFF, s, 1);
    CHECK(off1 > 0 && tlog->TL_ev[off1].TE_cx == 1 &&
	  Tick(off1) - Tick(off0) >= 4 && Tick(off1) - Tick(off0) <= 10);
    /* the end: unlocked, the voices back (0 and 1 after 2 and 3) */
    for (t = 0; t < 30 && Locks(mh); t++)
	TimerSleep(1);
    CHECK(Locks(mh) == 0);
    S_GetExclusive();
    ghal_crit_enter();
    CHECK(S_VMGetFree(SP_STANDARD) == 2);
    S_VMFree(2);
    ghal_crit_leave();
    S_ReleaseExclusive();
    S_FreeMusic(mh);
}

static void VSemaphore(void)
{
    SemaphoreHandle sem = ThreadAllocSem(0);
    word song[] = {
	DeltaTick(2),
	GeneralVSemaphore(sem),
	General(GE_END_OF_SONG)
    };
    MemHandle mh;

    CHECK(S_AllocMusic(song, 1, &mh) == SOUND_ERROR_NO_ERRORS);
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, UNLOCK_ON_EOS) == 0);
    CHECK(ThreadPTimedSem(sem, 60) == SE_NO_ERROR);
    TimerSleep(1);
    S_FreeMusic(mh);
    ThreadFreeSem(sem);
}

static void Note(void)
{
    MemHandle mh;
    int s = tlog->TL_count, on, off;

    CHECK(S_AllocMusicNote(IP_TRUMPET, IT_STANDARD_TABLE, LOW_A,
			   DYNAMIC_MF, SSDTT_TICKS, 4, &mh) == 0 && mh);
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, UNLOCK_ON_EOS) == 0);
    on = Find(DRE_SOUND_VOICE_ON, s, 0);
    CHECK(on > 0 && tlog->TL_ev[on].TE_ax == LOW_A &&
	  tlog->TL_ev[on].TE_bx == DYNAMIC_MF);
    CHECK(on > 0 && tlog->TL_ev[on - 1].TE_fn == DRE_SOUND_SET_ENVELOPE &&
	  tlog->TL_ev[on - 1].TE_si == IP_TRUMPET);
    off = WaitFor(DRE_SOUND_VOICE_OFF, s, 0);
    CHECK(off > 0 && Tick(off) - Tick(on) >= 4 && Tick(off) - Tick(on) <= 8);
    TimerSleep(2);
    CHECK(Locks(mh) == 0);
    /* again, higher and with the flute */
    s = tlog->TL_count;
    CHECK(S_ReallocMusicNote(mh, MIDDLE_A, DYNAMIC_P, SSDTT_MSEC, 20,
			     IP_FLUTE, IT_STANDARD_TABLE) == 0);
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, UNLOCK_ON_EOS) == 0);
    on = Find(DRE_SOUND_VOICE_ON, s, 0);
    CHECK(on > 0 && tlog->TL_ev[on].TE_ax == MIDDLE_A &&
	  tlog->TL_ev[on - 1].TE_si == IP_FLUTE);
    CHECK(WaitFor(DRE_SOUND_VOICE_OFF, s, 0) > 0);
    TimerSleep(2);
    S_FreeMusic(mh);
}

static void Stop(void)
{
    static const word song[] = {
	VoiceOn(0, LOW_A, DYNAMIC_F),
	DeltaMS(5000),
	VoiceOff(0),
	General(GE_END_OF_SONG)
    };
    MemHandle mh;
    int s = tlog->TL_count, n;

    CHECK(S_AllocMusic(song, 1, &mh) == 0);
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, UNLOCK_ON_EOS) == 0);
    CHECK(Find(DRE_SOUND_VOICE_ON, s, 0) >= 0);
    TimerSleep(2);
    CHECK(S_StopMusic(mh) == 0);
    CHECK(Find(DRE_SOUND_VOICE_SILENCE, s, 0) >= 0);
    CHECK(Locks(mh) == 0);
    n = tlog->TL_count;
    TimerSleep(10);
    CHECK(tlog->TL_count == n);
    /* played again: from the start (the voice it had is free) */
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, UNLOCK_ON_EOS) == 0);
    CHECK(Find(DRE_SOUND_VOICE_ON, n, 0) >= 0);
    S_FreeMusic(mh);			/* silenced first */
    CHECK(Find(DRE_SOUND_VOICE_SILENCE, n, 0) >= 0);
}

static void Priorities(void)
{
    static const word four[] = {
	VoiceOn(0, LOW_C, DYNAMIC_P), VoiceOn(1, LOW_E, DYNAMIC_P),
	VoiceOn(2, LOW_G, DYNAMIC_P), VoiceOn(3, MIDDLE_C, DYNAMIC_P),
	DeltaMS(3000),
	General(GE_END_OF_SONG)
    };
    MemHandle bg, urgent, lazy;
    int s = tlog->TL_count;

    CHECK(S_AllocMusic(four, 4, &bg) == 0);
    CHECK(S_PlayMusic(bg, SP_BACKGROUND, 1, UNLOCK_ON_EOS) == 0);
    CHECK(Find(DRE_SOUND_VOICE_ON, s, 3) >= 0);
    s = tlog->TL_count;
    CHECK(S_AllocMusicNote(IP_FLUTE, 0, HIGH_C, DYNAMIC_F, SSDTT_TICKS, 30,
			   &urgent) == 0);
    CHECK(S_PlayMusic(urgent, SP_GAME, 1, UNLOCK_ON_EOS) == 0);
    CHECK(Find(DRE_SOUND_VOICE_ON, s, 0) >= 0);	/* took one */
    s = tlog->TL_count;
    CHECK(S_AllocMusicNote(IP_FLUTE, 0, HIGH_E, DYNAMIC_F, SSDTT_TICKS, 30,
			   &lazy) == 0);
    CHECK(S_PlayMusic(lazy, SP_BACKGROUND + 5, 1, UNLOCK_ON_EOS) == 0);
    CHECK(Find(DRE_SOUND_VOICE_ON, s, 0) < 0);	/* none for it */
    S_FreeMusic(lazy);
    S_FreeMusic(urgent);
    S_FreeMusic(bg);
}

static void Exclusive(void)
{
    static const word song[] = { General(GE_END_OF_SONG) };
    MemHandle mh;

    CHECK(S_AllocMusic(song, 1, &mh) == 0);
    S_GetExclusive();
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, 0) ==
	  SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED);
    CHECK(S_GetExclusiveNB() == TRUE);		/* taken */
    S_ReleaseExclusive();
    CHECK(S_PlayMusic(mh, SP_STANDARD, 1, 0) == 0);
    CHECK(S_GetExclusiveNB() == FALSE);
    S_ReleaseExclusive();
    S_FreeMusic(mh);
}

static void MusicStream(void)
{
    static const word first[] = {
	VoiceOn(0, LOW_A, DYNAMIC_F), DeltaTick(2), VoiceOff(0),
	DeltaTick(1)
    };
    static const word second[] = {
	VoiceOn(0, MIDDLE_A, DYNAMIC_F), DeltaTick(1), VoiceOff(0),
	General(GE_END_OF_SONG),
	VoiceOn(0, HIGH_A, DYNAMIC_F)		/* after the end: not sent */
    };
    word slow[6 * 40], written = 0, i;
    MemHandle mh;
    int s = tlog->TL_count, on;

    CHECK(S_AllocMusicStream(SSS_ONE_SHOT, SP_STANDARD, 1, 4, &mh) == 0 &&
	  mh);
    CHECK(S_PlayToMusicStream(mh, first, sizeof(first)) == 0);
    on = Find(DRE_SOUND_VOICE_ON, s, 0);
    CHECK(on >= 0 && tlog->TL_ev[on].TE_ax == LOW_A);
    CHECK(S_PlayToMusicStream(mh, second, 0) == 0);
    CHECK(WaitFor(DRE_SOUND_VOICE_OFF, s, 1) > 0);
    on = Find(DRE_SOUND_VOICE_ON, s, 1);
    CHECK(on > 0 && tlog->TL_ev[on].TE_ax == MIDDLE_A);
    TimerSleep(3);
    CHECK(Find(DRE_SOUND_VOICE_ON, s, 2) < 0);

    /* more than the stream holds while it plays slowly */
    for (i = 0; i < 40; i++) {
	slow[i * 6] = SSE_VOICE_ON;
	slow[i * 6 + 1] = 0;
	slow[i * 6 + 2] = LOW_C;
	slow[i * 6 + 3] = DYNAMIC_P;
	slow[i * 6 + 4] = SSDTT_TICKS;
	slow[i * 6 + 5] = 30;
    }
    CHECK(S_PlayToMusicStreamNB(mh, slow, sizeof(slow), &written) ==
	  SOUND_ERROR_STREAM_FULL);
    CHECK(written > 0 && written < sizeof(slow));
    s = tlog->TL_count;
    CHECK(S_StopMusicStream(mh) == 0);
    CHECK(Find(DRE_SOUND_VOICE_SILENCE, s, 0) >= 0);
    S_FreeMusicStream(mh);
}

/* the DAC: reads its stream until it has dacWant bytes */
static byte dacGot[512];
static volatile word dacGotN, dacWant;
static SemaphoreHandle dacDone;

static word Dac(word v)
{
    StreamData *sd;
    word idle = 0;

    (void) v;
    while (dacGotN < dacWant && idle < 300) {
	byte b;
	Boolean any = FALSE;

	sd = tlog->TL_dacStream ? StreamDataOf(tlog->TL_dacStream) : NULL;
	while (sd && dacGotN < dacWant && !StreamGetByteNB(sd, &b)) {
	    dacGot[dacGotN++] = b;
	    any = TRUE;
	}
	if (sd)				/* room: the library writes more */
	    ((__typeof__(&StreamWriteDataNotify))
	     ProcGetLibraryEntry(strm, 2))(sd);
	if (!any) {
	    idle++;
	    TimerSleep(1);
	}
    }
    ThreadVSem(dacDone);
    return 0;
}

static void SampleStream(void)
{
    SampleFormatDescription d;
    byte sample[300];
    MemHandle mh;
    word i;
    int s = tlog->TL_count, att;

    CHECK(S_AllocSampleStream(&mh) == 0 && mh);
    CHECK(S_EnableSampleStream(mh, SP_STANDARD, TSND_DAC_RATE, 0,
			       DACSF_2_TO_1_ADPCM << SMID_FORMAT_OFFSET) ==
	  SOUND_ERROR_HARDWARE_DOESNT_SUPPORT_FORMAT);
    CHECK(tlog->TL_dacStream == 0);
    S_DisableSampleStream(mh);
    S_FreeSampleStream(mh);

    CHECK(S_AllocSampleStream(&mh) == 0 && mh);
    CHECK(S_EnableSampleStream(mh, SP_STANDARD, TSND_DAC_RATE, 0,
			       DACSF_8_BIT_PCM << SMID_FORMAT_OFFSET) == 0);
    att = Find(DRE_SOUND_DAC_ATTACH_TO_STREAM, s, 0);
    CHECK(att > 0 && tlog->TL_ev[att].TE_cx == 0 && tlog->TL_dacStream);
    CHECK(att > 0 &&
	  tlog->TL_ev[att - 1].TE_fn ==
	  DRE_SOUND_DAC_SET_SAMPLE_RATE_AND_FORMAT);
    for (i = 0; i < sizeof(sample); i++)
	sample[i] = (byte) (i * 7 + 3);
    dacWant = sizeof(sample);
    dacGotN = 0;
    dacDone = ThreadAllocSem(0);
    CHECK(ThreadCreate(PRIORITY_STANDARD, 0, Dac, 2048,
		       GeodeGetProcessHandle()) != 0);
    d.SFD_manufact = 0;
    d.SFD_format = DACSF_8_BIT_PCM << SMID_FORMAT_OFFSET;
    d.SFD_rate = TSND_DAC_RATE;
    d.SFD_playFlags = DACPF_CATENATE;
    CHECK(S_PlayToSampleStream(mh, (const word *) sample, sizeof(sample),
			       &d) == 0);
    CHECK(ThreadPTimedSem(dacDone, 400) == SE_NO_ERROR);
    CHECK(dacGotN == sizeof(sample) && !memcmp(dacGot, sample,
					       sizeof(sample)));
    ThreadFreeSem(dacDone);
    s = tlog->TL_count;
    S_DisableSampleStream(mh);
    CHECK(Find(DRE_SOUND_DAC_DETTACH_FROM_STREAM, s, 0) >= 0);
    CHECK(tlog->TL_dacStream == 0);
    S_FreeSampleStream(mh);
}

int main(int argc, char **argv)
{
    const char *ini = "[sound]\r\nsynthDriver = tsnddrv.geo\r\n";
    char *slash;
    GeodeLoadError e;
    GeodeHandle drv;
    DriverRegs r;
    word voices = 0, l = 0, rt = 0;
    SupportedEnvelopeFormat fmt;
    SoundDriverCapability cap = 0;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	slash[1] = 0;
    } else
	top[0] = 0;
    strcat(top, "GSNTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/SOUND", NULL, 0);
    Put("SYSTEM/stream.geo", stream_geode, stream_geode_size);
    Put("SYSTEM/sound.geo", sound_geode, sound_geode_size);
    Put("SYSTEM/SOUND/tsnddrv.geo", tsnddrv_geode, tsnddrv_geode_size);
    Put("geos.ini", ini, strlen(ini));
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_SYSTEM);
    snd = GeodeUseLibrary("sound.geo", 0, 0, &e);
    CHECK(snd != 0);
    if (!snd) {
	printf("sound.geo: GeodeLoadError %d\n", (int) e);
	return 1;
    }
    strm = GeodeFind("stream  ", GEODE_NAME_SIZE, 0, 0);
    CHECK(strm != 0);
    drv = GeodeFind("tsnddrv ", GEODE_NAME_SIZE, 0, 0);
    CHECK(drv != 0);
    if (!drv)
	return 1;
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(GeodeInfoDriver(drv), TSND_GET_LOG, &r));
    tlog = r.DR_farPtr;

    S_SynthDriverInfo(&voices, &fmt, &cap);
    CHECK(voices == TSND_VOICES && cap == (SDWFC_SELECT | SDEC_ADSR));
    S_SampleDriverInfo(&voices, &cap);
    CHECK(voices == TSND_DACS && cap == SDDACC_INT);

    Song();
    VSemaphore();
    Note();
    Stop();
    Priorities();
    Exclusive();
    MusicStream();
    SampleStream();

    CHECK(S_MixerSetMaster(100, 50) == 0);
    CHECK(S_MixerGetMaster(&l, &rt) == 0 && l == 100 && rt == 50);

    printf("%d driver calls\n", (int) tlog->TL_count);
    GeodeFreeLibrary(snd);
    printf("%s\n", fails ? "FAILED" : "sound: ok");
    return fails != 0;
}
