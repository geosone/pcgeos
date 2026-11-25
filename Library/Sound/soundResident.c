/*
 * soundResident.c -- the player of FM sounds (soundResident.asm).
 *
 * A sound plays in its timer routine: it reads the events of the song
 * (from memory, or from the stream of a music stream) up to the next
 * delta time, turns voices on and off through the sound driver, and
 * starts the timer for the next.  Everything here runs inside
 * ghal_crit: in the timer routine (interrupt code) or, for the first
 * events, from SoundStartSimpleFM and SoundActivateStreamReader.
 *
 * Port32: the timer's data is the sound's place in the library's table
 * (SoundSlot); a handle in a song is two words (sound.h); at the end of
 * a song that is to be destroyed the UI is sent
 * MSG_USER_FREE_SOUND_HANDLE straight away (the asm went through a
 * one-tick timer to get out of the interrupt code; the kernel sends
 * MF_FORCE_QUEUE messages from interrupt code from a reserve).
 */
#include "soundLocal.h"
#include <string.h>

/* SoundReadNextWord */
static word ReadNextWord(SoundControl *sc)
{
    word w = 0;

    if (sc->SC_status.SBS_type == ST_SIMPLE_FM) {
	SoundSimpleStatus *ss = &sc->SC_position.SSS_simple;

	memcpy(&w, ss->SSS_songPointer, sizeof(w));
	ss->SSS_songPointer += sizeof(w);
    } else {
	StreamData *sd = StreamDataOf(sc->SC_position.SSS_stream.
				      SSS_streamToken);
	byte b[2] = { 0, 0 };

	(void) StreamGetByteNB(sd, &b[0]);
	(void) StreamGetByteNB(sd, &b[1]);
	StreamWriteDataNotify(sd);
	memcpy(&w, b, sizeof(w));
    }
    return w;
}

static void Synth(word fn, word cx)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = cx;
    (void) SoundSynth(fn, &r);
}

/* SoundVoiceOn */
static void EvVoiceOn(SoundControl *sc)
{
    word voice = ReadNextWord(sc), phys, freq, vol;
    SoundVoiceStatus *vs = &sc->SC_voice[voice];
    DriverRegs r;

    phys = vs->SVS_physicalVoice;
    if (phys != NO_VOICE) {
	/* ours still: off, and to the end of its priority */
	Synth(DRE_SOUND_VOICE_OFF, phys);
	SoundVoiceDeactivate(phys);
	SoundVoiceActivate(phys, sc->SC_status.SBS_priority);
    } else {
	phys = SoundVoiceGetFree(sc->SC_status.SBS_priority);
	if (phys == NO_VOICE) {
	    (void) ReadNextWord(sc);
	    (void) ReadNextWord(sc);
	    vs->SVS_physicalVoice = NO_VOICE;
	    return;
	}
	SoundVoiceAssign(phys, sc, voice);
	SoundVoiceActivate(phys, sc->SC_status.SBS_priority);
	memset(&r, 0, sizeof(r));
	r.DR_bx = (word) (vs->SVS_instrument >> 16);
	r.DR_si = (word) vs->SVS_instrument;
	r.DR_cx = phys;
	(void) SoundSynth(DRE_SOUND_SET_ENVELOPE, &r);
    }
    freq = ReadNextWord(sc);
    vol = ReadNextWord(sc);
    memset(&r, 0, sizeof(r));
    r.DR_ax = freq;
    r.DR_bx = vol;
    r.DR_cx = phys;
    (void) SoundSynth(DRE_SOUND_VOICE_ON, &r);
}

/* SoundVoiceOff: the voice stays the sound's, but goes first */
static void EvVoiceOff(SoundControl *sc)
{
    word phys = sc->SC_voice[ReadNextWord(sc)].SVS_physicalVoice;

    if (phys == NO_VOICE)
	return;
    Synth(DRE_SOUND_VOICE_OFF, phys);
    SoundVoiceDeactivate(phys);
    SoundVoiceActivate(phys, SOUND_PRIORITY_REUSE);
}

/* SoundChange: a new instrument; the voice is given up */
static void EvChange(SoundControl *sc)
{
    SoundVoiceStatus *vs = &sc->SC_voice[ReadNextWord(sc)];
    word patch = ReadNextWord(sc), table = ReadNextWord(sc), phys;

    vs->SVS_instrument = (dword) table << 16 | patch;
    phys = vs->SVS_physicalVoice;
    vs->SVS_physicalVoice = NO_VOICE;
    if (phys == NO_VOICE)
	return;
    Synth(DRE_SOUND_VOICE_OFF, phys);
    SoundVoiceDeactivate(phys);
    SoundVoiceFree(phys);
}

static Handle ReadHandle(SoundControl *sc)
{
    word lo = ReadNextWord(sc);

    return (Handle) ((dword) ReadNextWord(sc) << 16 | lo);
}

/* SoundGeneral: TRUE at the end of the song */
static Boolean EvGeneral(SoundControl *sc)
{
    word i, msg, chunk;
    Handle h;

    switch (ReadNextWord(sc)) {
    case GE_NO_EVENT:
	break;
    case GE_END_OF_SONG:
	/* the voices given up (not silenced, as in the asm) */
	for (i = 0; i < sc->SC_format.SFS_fm.SFMS_voicesUsed; i++) {
	    word phys = sc->SC_voice[i].SVS_physicalVoice;

	    sc->SC_voice[i].SVS_physicalVoice = NO_VOICE;
	    if (phys != NO_VOICE) {
		SoundVoiceDeactivate(phys);
		SoundVoiceFree(phys);
	    }
	}
	return TRUE;
    case GE_SET_PRIORITY:
	sc->SC_status.SBS_priority = ReadNextWord(sc);
	break;
    case GE_SET_TEMPO:
	sc->SC_format.SFS_fm.SFMS_tempo = ReadNextWord(sc);
	break;
    case GE_SEND_NOTIFICATION:
	msg = ReadNextWord(sc);
	h = ReadHandle(sc);
	chunk = ReadNextWord(sc);
	(void) TimerStart(TIMER_EVENT_ONE_SHOT, ConstructOptr(h, chunk), 1,
			  msg, 0, NULL);
	break;
    case GE_V_SEMAPHORE:
	h = ReadHandle(sc);
	if (h)
	    ThreadVSem(h);
	break;
    default:
	break;				/* (EC: SOUND_BAD_EVENT_COMMAND) */
    }
    return FALSE;
}

static void StartTimer(SoundControl *sc, TimerType type, word time)
{
    SoundFMStatus *fm = &sc->SC_format.SFS_fm;

    fm->SFMS_timerHandle = TimerStart(type,
				      TimerRoutineOptr(SoundTimerRoutine),
				      time, sc->SC_status.SBS_slot, 0,
				      &fm->SFMS_timerID);
}

/* SoundResetTimer: FALSE if the delta is 0 (the next event right away) */
static Boolean ResetTimer(SoundControl *sc, word type)
{
    word t = ReadNextWord(sc);
    dword ms;

    if (!t)
	return FALSE;
    switch (type) {
    case SSDTT_TEMPO:
	ms = (dword) t * sc->SC_format.SFS_fm.SFMS_tempo;
	sc->SC_format.SFS_fm.SFMS_timeRemaining = (word) (ms >> 16);
	StartTimer(sc, TIMER_MS_ROUTINE_ONE_SHOT, (word) ms);
	break;
    case SSDTT_TICKS:
	StartTimer(sc, TIMER_ROUTINE_ONE_SHOT, t);
	break;
    default:				/* SSDTT_MSEC */
	StartTimer(sc, TIMER_MS_ROUTINE_ONE_SHOT, t);
	break;
    }
    return TRUE;
}

/* the end of a simple song: unlock, destroy as the flags say */
static void EndOfSong(SoundControl *sc)
{
    byte eos = sc->SC_status.SBS_EOS;
    MemHandle mh = sc->SC_status.SBS_blockHandle;

    sc->SC_format.SFS_fm.SFMS_timerID = 0;
    sc->SC_format.SFS_fm.SFMS_timerHandle = 0;
    if (eos & EOSF_LMEM_UNLOCK)
	MemUnlock(OptrToHandle(sc->SC_position.SSS_simple.SSS_songHandle));
    if (eos & EOSF_UNLOCK)
	MemUnlock(mh);
    if (eos & EOSF_DESTROY)
	SoundSendFreeToUI(mh);
}

/* the stream ran dry: the reader off; the block unlocked unless a voice
 * still sounds */
static void ReadFromEmptyStream(SoundControl *sc)
{
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;
    word i;

    ss->SSS_streamState &= ~SSST_ACTIVE;
    for (i = 0; i < sc->SC_format.SFS_fm.SFMS_voicesUsed; i++)
	if (sc->SC_voice[i].SVS_physicalVoice != NO_VOICE)
	    return;
    ss->SSS_streamState &= ~SSST_LOCKED;
    MemUnlock(sc->SC_status.SBS_blockHandle);
}

void SoundHandleTimerEvent(SoundControl *sc)
{
    SoundFMStatus *fm = &sc->SC_format.SFS_fm;

    ghal_crit_enter();
    if (fm->SFMS_timeRemaining) {
	/* a long tempo delta: another 65535 ms */
	fm->SFMS_timeRemaining--;
	StartTimer(sc, TIMER_MS_ROUTINE_ONE_SHOT, 65535);
	ghal_crit_leave();
	return;
    }
    for (;;) {
	word ev;

	if (sc->SC_status.SBS_type != ST_SIMPLE_FM) {
	    if (!sc->SC_position.SSS_stream.SSS_dataOnStream) {
		ReadFromEmptyStream(sc);
		break;
	    }
	    sc->SC_position.SSS_stream.SSS_dataOnStream--;
	}
	ev = ReadNextWord(sc);
	if (ev == SSE_VOICE_ON)
	    EvVoiceOn(sc);
	else if (ev == SSE_VOICE_OFF)
	    EvVoiceOff(sc);
	else if (ev == SSE_CHANGE)
	    EvChange(sc);
	else if (ev == SSE_GENERAL) {
	    if (EvGeneral(sc) && sc->SC_status.SBS_type == ST_SIMPLE_FM) {
		EndOfSong(sc);
		break;
	    }
	} else if (ev == SSDTT_MSEC || ev == SSDTT_TICKS ||
		   ev == SSDTT_TEMPO) {
	    if (ResetTimer(sc, ev))
		break;
	} else
	    break;			/* (EC: SOUND_BAD_EVENT_COMMAND) */
    }
    ghal_crit_leave();
}

void SoundTimerRoutine(word data, dword tick)
{
    SoundControl *sc = SoundSlot(data);

    (void) tick;
    if (sc)
	SoundHandleTimerEvent(sc);
}
