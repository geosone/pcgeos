/*
 * soundDriver.c -- the sounds of the sound library: FM songs and notes,
 * music streams and sample (DAC) streams (soundDriver.asm, which made
 * them the functions of the library's own driver).
 *
 * The block of a sound is a SoundControl (Internal/soundCtl.h).  An FM
 * song plays from memory, a note is a song of its own in the block, a
 * music stream plays what is written to its stream; all three play in
 * the timer routine (soundResident.c).  A sample stream gives the sound
 * driver's DAC a stream the samples are written to.
 *
 * Port32:
 *   - a sound's semaphores in the block are the kernel's; the asm set
 *     the "stream drained" one (SSS_activeReaderSem) to 1 and woke its
 *     queue: SetDrained;
 *   - SoundFreeMusic of a sound still playing silences it first (the asm
 *     left its voices to a freed block);
 *   - SoundDisableSampleStream leaves the stream drained, as it was
 *     before SoundEnableSampleStream (the asm left the semaphore taken,
 *     so that a later write that waited for a change of format would
 *     wait for good);
 *   - SoundFreeSampleStream that leaves the block to a waiting writer
 *     lets go of the sound's mutex (the asm kept it, and the writer's
 *     own free waited for it for good);
 *   - no DMA page check for the stream of a DAC (there are no pages).
 */
#include "soundLocal.h"
#include <string.h>

/* the sizes of the events in a song (SoundLibDriverWriteEvent), by
 * SoundStreamEvent / 2 and GeneralEvent / 2; 0: see the general one.
 * Port32: the general events with a handle are two bytes longer. */
static const word eventBufferSize[] = { 8, 4, 8, 0, 4, 4, 4 };
static const word generalEventBufferSize[] = { 4, 4, 6, 6, 12, 8 };

static word EventSize(const byte *p)
{
    word ev, gen;

    memcpy(&ev, p, sizeof(ev));
    if ((ev & 1) || ev / 2 >= sizeof(eventBufferSize) / sizeof(word))
	return 0;
    if (eventBufferSize[ev / 2])
	return eventBufferSize[ev / 2];
    memcpy(&gen, p + 2, sizeof(gen));
    if ((gen & 1) || gen / 2 >= sizeof(generalEventBufferSize) / sizeof(word))
	return 0;
    return generalEventBufferSize[gen / 2];
}

static Boolean IsEndOfSong(const byte *p)
{
    word ev, gen;

    memcpy(&ev, p, sizeof(ev));
    memcpy(&gen, p + 2, sizeof(gen));
    return ev == SSE_GENERAL && gen == GE_END_OF_SONG;
}

static SoundControl *Lock(MemHandle mh)
{
    return (SoundControl *) MemLock(mh);
}

/* the "stream drained" semaphore free again, everyone waiting on it
 * woken (the asm: value 1, ThreadWakeUpQueue) */
static void SetDrained(Semaphore *s)
{
    ghal_crit_enter();
    KSemVAll(s);
    s->Sem_value = 1;
    ghal_crit_leave();
}

static Boolean StreamCall(DriverStrategy *strat, word fn, DriverRegs *r)
{
    return strat(fn, r);
}

static void InitVoices(SoundControl *sc, word voices)
{
    word i;

    for (i = 0; i < voices; i++) {
	sc->SC_voice[i].SVS_physicalVoice = NO_VOICE;
	sc->SC_voice[i].SVS_instrument = DEFAULT_INSTRUMENT;
    }
}

/* the mutex, owned as the block is */
static SemaphoreHandle NewMutex(MemHandle mh)
{
    SemaphoreHandle s = ThreadAllocSem(1);

    HandleModifyOwner(s, MemOwner(mh));
    return s;
}

/* --------------------------------------------------------------- FM */

/* SoundLibDriverInitSimpleFM: song NULL: right after the voices in the
 * block (a note, or a block of the caller's) */
void SoundInitSimpleFM(SoundControl *sc, MemHandle mh, word voices,
		       const byte *song)
{
    SoundSimpleStatus *ss = &sc->SC_position.SSS_simple;
    SoundFMStatus *fm = &sc->SC_format.SFS_fm;

    sc->SC_status.SBS_blockHandle = mh;
    sc->SC_status.SBS_ID = SOUND_ID;
    sc->SC_status.SBS_type = ST_SIMPLE_FM;
    fm->SFMS_timerHandle = 0;
    fm->SFMS_timerID = 0;
    fm->SFMS_timeRemaining = 0;
    fm->SFMS_voicesUsed = voices;
    ss->SSS_songBuffer = song ? song :
	(const byte *) sc + SOUND_CONTROL_SIZE(voices);
    ss->SSS_songPointer = ss->SSS_songBuffer;
    ss->SSS_songHandle = 0;
    InitVoices(sc, voices);
    sc->SC_status.SBS_mutExSem = NewMutex(mh);
    sc->SC_status.SBS_slot = SoundSlotAlloc(sc);
}

/* a block for a sound of n voices and extra bytes, locked; 0 if none */
static MemHandle AllocControl(word voices, word extra, SoundControl **sc)
{
    MemHandle mh = MemAlloc((word) (SOUND_CONTROL_SIZE(voices) + extra),
			    HF_SWAPABLE | HF_SHARABLE,
			    HAF_ZERO_INIT | HAF_LOCK);

    *sc = mh ? (SoundControl *) MemDeref(mh) : NULL;
    return mh;
}

word SoundAllocMusic(const word *song, word voices, MemHandle *control)
{
    SoundControl *sc;
    MemHandle mh;

    if (!voices)
	voices = 1;		/* (EC: CAN_NOT_ALLOCATE_CONTROL_WITH_NO_VOICES) */
    mh = AllocControl(voices, 0, &sc);
    *control = mh;
    if (!mh)
	return SOUND_ERROR_OUT_OF_MEMORY;
    SoundInitSimpleFM(sc, mh, voices, (const byte *) song);
    MemUnlock(mh);
    return SOUND_ERROR_NO_ERRORS;
}

void SoundInitMusic(MemHandle mh, word voices)
{
    SoundControl *sc = Lock(mh);

    if (!sc)
	return;			/* (EC: CONTROL_BLOCK_IN_DISCARDED_RESOURCE) */
    SoundInitSimpleFM(sc, mh, voices, NULL);
    MemUnlock(mh);
}

/* SoundLibDriverSilenceVoices: the timer stopped (and the locks of the
 * end of the song given back), every voice silenced and given up */
void SoundSilenceVoices(SoundControl *sc)
{
    SoundFMStatus *fm = &sc->SC_format.SFS_fm;
    TimerHandle th;
    word id, i;

    ghal_crit_enter();
    th = fm->SFMS_timerHandle;
    id = fm->SFMS_timerID;
    fm->SFMS_timerHandle = 0;
    fm->SFMS_timerID = 0;
    fm->SFMS_timeRemaining = 0;
    if (th) {
	(void) TimerStop(th, id);
	if (sc->SC_status.SBS_EOS & EOSF_LMEM_UNLOCK)
	    MemUnlock(OptrToHandle(sc->SC_position.SSS_simple.
				   SSS_songHandle));
	if (sc->SC_status.SBS_EOS & EOSF_UNLOCK)
	    MemUnlock(sc->SC_status.SBS_blockHandle);
    }
    ghal_crit_leave();
    for (i = 0; i < fm->SFMS_voicesUsed; i++) {
	word phys;

	ghal_crit_enter();
	phys = sc->SC_voice[i].SVS_physicalVoice;
	if (phys != NO_VOICE) {
	    DriverRegs r;

	    sc->SC_voice[i].SVS_physicalVoice = NO_VOICE;
	    memset(&r, 0, sizeof(r));
	    r.DR_cx = phys;
	    (void) SoundSynth(DRE_SOUND_VOICE_SILENCE, &r);
	    SoundVoiceDeactivate(phys);
	    SoundVoiceFree(phys);
	}
	ghal_crit_leave();
    }
}

/* SoundCheckExclusiveSimpleFM: the block locked (for the play) and its
 * mutex taken; a sound still playing (locked before) silenced */
static SoundControl *CheckExclusiveSimpleFM(MemHandle mh)
{
    SoundControl *sc = Lock(mh);

    if (!sc)
	return NULL;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    if (MGI_LOCK_COUNT(MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT)) != 1)
	SoundSilenceVoices(sc);
    return sc;
}

/* SoundStartSimpleFM: the mutex given back, the first events played */
static void StartSimpleFM(SoundControl *sc)
{
    ghal_crit_enter();
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    SoundHandleTimerEvent(sc);
    ghal_crit_leave();
}

word SoundPlayMusic(MemHandle mh, word priority, word tempo,
		    EndOfSongFlags flags)
{
    SoundControl *sc;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = CheckExclusiveSimpleFM(mh);
    if (sc) {
	sc->SC_status.SBS_priority = priority;
	sc->SC_format.SFS_fm.SFMS_tempo = tempo;
	sc->SC_position.SSS_simple.SSS_songPointer =
	    sc->SC_position.SSS_simple.SSS_songBuffer;
	sc->SC_status.SBS_EOS = flags & ~EOSF_LMEM_UNLOCK;
	StartSimpleFM(sc);
    }
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundPlayMusicLMem(MemHandle mh, word priority, word tempo,
			EndOfSongFlags flags)
{
    SoundControl *sc;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = CheckExclusiveSimpleFM(mh);
    if (sc) {
	SoundSimpleStatus *ss = &sc->SC_position.SSS_simple;
	byte *blk;

	sc->SC_status.SBS_priority = priority;
	sc->SC_format.SFS_fm.SFMS_tempo = tempo;
	sc->SC_status.SBS_EOS = flags | EOSF_LMEM_UNLOCK;
	blk = MemLock(OptrToHandle(ss->SSS_songHandle));
	ss->SSS_songBuffer = blk ?
	    (const byte *) LMemDeref(ss->SSS_songHandle) : NULL;
	ss->SSS_songPointer = ss->SSS_songBuffer;
	if (ss->SSS_songBuffer)
	    StartSimpleFM(sc);
	else
	    ThreadVSem(sc->SC_status.SBS_mutExSem);
    }
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundStopMusic(MemHandle mh)
{
    SoundControl *sc;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = Lock(mh);
    if (sc) {
	ThreadPSem(sc->SC_status.SBS_mutExSem);
	SoundSilenceVoices(sc);
	ThreadVSem(sc->SC_status.SBS_mutExSem);
	MemUnlock(mh);
    }
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundStopMusicLMem(MemHandle mh)
{
    return SoundStopMusic(mh);
}

word SoundReallocMusic(MemHandle mh, const word *song)
{
    SoundControl *sc;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = Lock(mh);
    if (sc) {
	sc->SC_position.SSS_simple.SSS_songBuffer = (const byte *) song;
	MemUnlock(mh);
    }
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundReallocMusicLMem(MemHandle mh, optr song)
{
    SoundControl *sc;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = Lock(mh);
    if (sc) {
	sc->SC_position.SSS_simple.SSS_songHandle = song;
	MemUnlock(mh);
    }
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

/* SoundLibDriverFreeSimple */
word SoundFreeSimple(MemHandle mh)
{
    SoundControl *sc = Lock(mh);

    if (!sc)
	return SOUND_ERROR_NO_ERRORS;
    SoundSilenceVoices(sc);			/* Port32 */
    SoundSlotFree(sc->SC_status.SBS_slot);
    ThreadFreeSem(sc->SC_status.SBS_mutExSem);
    MemFree(mh);
    return SOUND_ERROR_NO_ERRORS;
}

void SoundFreeMusic(MemHandle mh)
{
    (void) SoundFreeSimple(mh);
}

/* ------------------------------------------------------------- notes */

/* a StandardNoteBuffer (soundConstant.def): change, voice on, delta,
 * voice off, end of song */
enum {
    SNB_CHANGE_EVENT, SNB_CHANGE_VOICE, SNB_PATCH, SNB_TABLE,
    SNB_DELTA_TYPE_ONE, SNB_DELTA_TIME_ONE,
    SNB_VOICE_ON, SNB_VOICE_ON_VOICE, SNB_FREQUENCY, SNB_ATTACK,
    SNB_DELTA_TYPE, SNB_DELTA_TIME,
    SNB_VOICE_OFF, SNB_VOICE_OFF_VOICE, SNB_DELTA_TYPE_TWO,
    SNB_DELTA_TIME_TWO, SNB_GENERAL, SNB_END_OF_SONG,
    SNB_WORDS
};

static const word oneNoteSong[SNB_WORDS] = {
    SSE_CHANGE, 0, 0, 0, SSDTT_MSEC, 0,
    SSE_VOICE_ON, 0, 0, 0, 0, 0,
    SSE_VOICE_OFF, 0, SSDTT_MSEC, 0,
    SSE_GENERAL, GE_END_OF_SONG
};

static word *NoteOf(SoundControl *sc)
{
    return (word *) ((byte *) sc + SOUND_CONTROL_SIZE(1));
}

static void SetNote(word *n, InstrumentPatch instrument, word table,
		    word freq, word vol, word deltaType, word duration)
{
    n[SNB_PATCH] = (word) instrument;
    n[SNB_TABLE] = table;
    n[SNB_FREQUENCY] = freq;
    n[SNB_ATTACK] = vol;
    n[SNB_DELTA_TYPE] = deltaType;
    n[SNB_DELTA_TIME] = duration;
}

word SoundAllocMusicNote(InstrumentPatch instrument, word instTable,
			 word frequency, word volume,
			 SoundStreamDeltaTimeType deltaType, word duration,
			 MemHandle *control)
{
    SoundControl *sc;
    MemHandle mh;

    *control = 0;
    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    mh = AllocControl(1, sizeof(oneNoteSong), &sc);
    *control = mh;
    if (!mh) {
	SoundExitLibrary();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    SoundInitSimpleFM(sc, mh, 1, NULL);
    memcpy(NoteOf(sc), oneNoteSong, sizeof(oneNoteSong));
    SetNote(NoteOf(sc), instrument, instTable, frequency, volume,
	    (word) deltaType, duration);
    MemUnlock(mh);
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundPlayMusicNote(MemHandle mh, SoundPriority priority, word tempo,
			EndOfSongFlags flags)
{
    return SoundPlayMusic(mh, (word) priority, tempo, flags);
}

word SoundStopMusicNote(MemHandle mh)
{
    return SoundStopMusic(mh);
}

word SoundReallocMusicNote(MemHandle mh, word freq, word vol,
			   SoundStreamDeltaTimeType timer, word durat,
			   InstrumentPatch instrument, word instTable)
{
    SoundControl *sc;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = Lock(mh);
    if (sc) {
	SetNote(NoteOf(sc), instrument, instTable, freq, vol, (word) timer,
		durat);
	MemUnlock(mh);
    }
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

void SoundFreeMusicNote(MemHandle mh)
{
    (void) SoundFreeSimple(mh);
}

/* ----------------------------------------------------- music streams */

static void InitStreamFM(SoundControl *sc, MemHandle mh, word priority,
			 word voices, word tempo)
{
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;
    SoundFMStatus *fm = &sc->SC_format.SFS_fm;

    sc->SC_status.SBS_blockHandle = mh;
    sc->SC_status.SBS_type = ST_STREAM_FM;
    sc->SC_status.SBS_ID = SOUND_ID;
    sc->SC_status.SBS_priority = priority;
    sc->SC_status.SBS_EOS = 0;
    fm->SFMS_tempo = tempo;
    fm->SFMS_timerHandle = 0;
    fm->SFMS_timerID = 0;
    fm->SFMS_timeRemaining = 0;
    fm->SFMS_voicesUsed = voices;
    ss->SSS_dataRemaining = 0;
    ss->SSS_dataOnStream = 0;
    ss->SSS_streamToken = 0;
    ss->SSS_buffer = NULL;
    ss->SSS_streamState = 0;
    InitVoices(sc, voices);
    sc->SC_status.SBS_mutExSem = NewMutex(mh);
    KSemInit(&ss->SSS_writerSem, 1);
    KSemInit(&ss->SSS_dataSem, 0);
    KSemInit(&ss->SSS_activeReaderSem, 1);
    sc->SC_status.SBS_slot = SoundSlotAlloc(sc);
}

/* a stream of size bytes owned by the library; 0 if none */
static StreamToken CreateStream(DriverStrategy *strat, word size)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = size;
    r.DR_bx = GeodeGetCodeProcessHandle();
    r.DR_cx = HF_FIXED;
    if (StreamCall(strat, DR_STREAM_CREATE, &r))
	return 0;
    return (StreamToken) r.DR_bx;
}

static void DestroyStream(DriverStrategy *strat, StreamToken token)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_DISCARD;
    r.DR_bx = token;
    (void) StreamCall(strat, DR_STREAM_DESTROY, &r);
}

word SoundAllocMusicStream(SoundStreamSize streamSize,
			   SoundPriority priority, word voices, word tempo,
			   MemHandle *control)
{
    SoundControl *sc;
    MemHandle mh;
    StreamToken st;

    *control = 0;
    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    if (!voices)
	voices = 1;
    mh = AllocControl(voices, 0, &sc);
    if (!mh) {
	SoundExitLibrary();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    InitStreamFM(sc, mh, (word) priority, voices, tempo);
    st = CreateStream(StreamStrategy, streamSize);
    if (!st) {
	SoundSlotFree(sc->SC_status.SBS_slot);
	ThreadFreeSem(sc->SC_status.SBS_mutExSem);
	MemFree(mh);
	SoundExitLibrary();
	return SOUND_ERROR_UNABLE_TO_ALLOCATE_STREAM;
    }
    sc->SC_position.SSS_stream.SSS_streamToken = st;
    MemUnlock(mh);
    *control = mh;
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

/* SoundStartPlayToStreamFM: locked, mutex and writer taken, the thread
 * at a high priority; its old priority back */
static SoundControl *StartPlayToStreamFM(MemHandle mh, word *oldPrio)
{
    SoundControl *sc = Lock(mh);

    if (!sc)
	return NULL;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    (void) KSemP(&sc->SC_position.SSS_stream.SSS_writerSem, SEM_FOREVER);
    *oldPrio = TGI_PRIORITY(ThreadGetInfo(0, TGIT_PRIORITY_AND_USAGE));
    ThreadModify(0, PRIORITY_HIGH, TMF_BASE_PRIO);
    return sc;
}

static void EndPlayToStreamFM(SoundControl *sc, word oldPrio)
{
    MemHandle mh = sc->SC_status.SBS_blockHandle;

    ThreadModify(0, oldPrio, TMF_BASE_PRIO);
    KSemV(&sc->SC_position.SSS_stream.SSS_writerSem);
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    MemUnlock(mh);
}

/* SoundActivateStreamReader: the block locked for the reader (once), the
 * first events played */
static void ActivateStreamReader(SoundControl *sc)
{
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;

    if (!(ss->SSS_streamState & SSST_LOCKED)) {
	(void) MemLock(sc->SC_status.SBS_blockHandle);
	ss->SSS_streamState |= SSST_LOCKED;
    }
    ghal_crit_enter();
    ss->SSS_streamState |= SSST_ACTIVE;
    SoundHandleTimerEvent(sc);
    ghal_crit_leave();
}

/* SoundLibDriverWriteEvent: one event onto the stream (blocking); TRUE
 * when the song is written (*left bytes left, 0: up to the end of the
 * song) */
static Boolean WriteEvent(SoundControl *sc, const byte **p, sword *left,
			  Boolean sized)
{
    word size = EventSize(*p);
    Boolean eos = IsEndOfSong(*p);
    DriverRegs r;

    if (!size)
	return TRUE;			/* (EC: SOUND_BAD_EVENT_COMMAND) */
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_BLOCK;
    r.DR_bx = sc->SC_position.SSS_stream.SSS_streamToken;
    r.DR_cx = size;
    r.DR_dsSi = (void *) *p;
    (void) StreamStrategy(DR_STREAM_WRITE, &r);
    *p += size;
    if (sized) {
	*left = (sword) (*left - size);
	return *left <= 0;
    }
    return eos;
}

/* an event more on the stream; the reader on if it was off */
static void EventWritten(SoundControl *sc)
{
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;
    Boolean active;

    ghal_crit_enter();
    ss->SSS_dataOnStream++;
    active = (ss->SSS_streamState & SSST_ACTIVE) != 0;
    ghal_crit_leave();
    if (!active)
	ActivateStreamReader(sc);
}

word SoundPlayToMusicStream(MemHandle mh, const word *song, word size)
{
    SoundControl *sc;
    const byte *p = (const byte *) song;
    sword left = (sword) size;
    word oldPrio;
    Boolean done;

    sc = StartPlayToStreamFM(mh, &oldPrio);
    if (!sc)
	return SOUND_ERROR_STREAM_DESTROYED;
    do {
	done = WriteEvent(sc, &p, &left, size != 0);
	EventWritten(sc);
    } while (!done);
    EndPlayToStreamFM(sc, oldPrio);
    return SOUND_ERROR_NO_ERRORS;
}

word SoundPlayToMusicStreamNB(MemHandle mh, const word *song, word size,
			      word *bytesWritten)
{
    SoundControl *sc;
    const byte *p = (const byte *) song;
    sword left = (sword) size;
    word oldPrio, written = 0, err = SOUND_ERROR_NO_ERRORS;
    Boolean done;

    sc = StartPlayToStreamFM(mh, &oldPrio);
    if (!sc) {
	if (bytesWritten)
	    *bytesWritten = 0;
	return SOUND_ERROR_STREAM_DESTROYED;
    }
    do {
	/* SoundCanWriteEventToStream */
	DriverRegs r;
	word need = EventSize(p);

	memset(&r, 0, sizeof(r));
	r.DR_ax = (word) STREAM_WRITE;
	r.DR_bx = sc->SC_position.SSS_stream.SSS_streamToken;
	(void) StreamStrategy(DR_STREAM_QUERY, &r);
	if ((word) r.DR_ax < need) {
	    err = SOUND_ERROR_STREAM_FULL;
	    break;
	}
	written = (word) (written + need);
	done = WriteEvent(sc, &p, &left, size != 0);
	EventWritten(sc);
    } while (!done);
    EndPlayToStreamFM(sc, oldPrio);
    if (bytesWritten)
	*bytesWritten = written;
    return err;
}

word SoundStopMusicStream(MemHandle mh)
{
    SoundControl *sc;
    SoundStreamStatus *ss;
    DriverRegs r;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = Lock(mh);
    if (!sc) {
	SoundExitLibrary();
	return SOUND_ERROR_NO_ERRORS;
    }
    ss = &sc->SC_position.SSS_stream;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    SoundSilenceVoices(sc);
    memset(&r, 0, sizeof(r));
    r.DR_bx = ss->SSS_streamToken;
    (void) StreamStrategy(DR_STREAM_FLUSH, &r);
    ghal_crit_enter();
    ss->SSS_dataOnStream = 0;
    ss->SSS_dataRemaining = 0;
    ss->SSS_activeReaderSem.Sem_value = 0;
    ss->SSS_streamState &= ~SSST_ACTIVE;
    ghal_crit_leave();
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    MemUnlock(mh);
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

void SoundFreeMusicStream(MemHandle mh)
{
    SoundControl *sc = Lock(mh);

    if (!sc)
	return;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    SoundSilenceVoices(sc);			/* Port32 */
    if (sc->SC_position.SSS_stream.SSS_streamToken)
	DestroyStream(StreamStrategy,
		      sc->SC_position.SSS_stream.SSS_streamToken);
    SoundSlotFree(sc->SC_status.SBS_slot);
    ThreadFreeSem(sc->SC_status.SBS_mutExSem);
    MemFree(mh);
}

/* ------------------------------------------------------ sample streams */

word SoundAllocSampleStream(MemHandle *control)
{
    SoundControl *sc;
    SoundStreamStatus *ss;
    MemHandle mh;

    *control = 0;
    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    mh = AllocControl(1, 0, &sc);
    if (!mh) {
	SoundExitLibrary();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    /* SoundLibDriverInitDAC */
    ss = &sc->SC_position.SSS_stream;
    sc->SC_status.SBS_blockHandle = mh;
    sc->SC_status.SBS_mutExSem = NewMutex(mh);
    sc->SC_status.SBS_ID = SOUND_ID;
    sc->SC_status.SBS_type = ST_STREAM_DAC;
    sc->SC_status.SBS_EOS = 0;
    sc->SC_voice[0].SVS_physicalVoice = NO_VOICE;
    sc->SC_format.SFS_dac.SDACS_rate = 0;
    sc->SC_format.SFS_dac.SDACS_format = 0xffff;
    sc->SC_format.SFS_dac.SDACS_manufactID = 0xffff;
    ss->SSS_streamToken = 0;
    ss->SSS_dataRemaining = 0;
    ss->SSS_dataOnStream = 0;
    KSemInit(&ss->SSS_writerSem, 1);
    KSemInit(&ss->SSS_activeReaderSem, 1);
    KSemInit(&ss->SSS_dataSem, 0);
    ss->SSS_buffer = NULL;
    ss->SSS_streamState = 0;
    sc->SC_status.SBS_slot = SoundSlotAlloc(sc);
    MemUnlock(mh);
    *control = mh;
    SoundExitLibrary();
    return SOUND_ERROR_NO_ERRORS;
}

static Boolean SetRateAndFormat(word dac, word manufact, word format,
				word rate, word *size)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = dac;
    r.DR_ax = manufact;
    r.DR_bx = format;
    r.DR_dx = rate;
    ghal_crit_enter();
    (void) SoundSynth(DRE_SOUND_DAC_SET_SAMPLE_RATE_AND_FORMAT, &r);
    ghal_crit_leave();
    if (size)
	*size = (word) r.DR_cx;
    return (word) r.DR_dx != 0;
}

/* SoundLibDriverGetDACAndStreamForSound: a DAC for the sound at its
 * priority, set to the format, a stream to it; 0 or the error */
static word GetDACAndStream(SoundControl *sc, word manufact, word format)
{
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;
    word dac, size = 0;
    StreamToken st;
    DriverRegs r;

    dac = SoundDACGetFree(sc->SC_status.SBS_priority);
    if (dac == NO_VOICE)
	return SOUND_ERROR_HARDWARE_NOT_AVAILABLE;
    sc->SC_voice[0].SVS_physicalVoice = dac;
    SoundDACAssign(dac, sc, 0);
    memset(&r, 0, sizeof(r));
    r.DR_cx = dac;
    (void) SoundSynth(DRE_SOUND_DAC_FLUSH_DAC, &r);
    (void) SoundSynth(DRE_SOUND_DAC_DETTACH_FROM_STREAM, &r);
    if (!SetRateAndFormat(dac, manufact, format,
			  sc->SC_format.SFS_dac.SDACS_rate, &size)) {
	sc->SC_voice[0].SVS_physicalVoice = NO_VOICE;
	SoundDACFree(dac);
	return SOUND_ERROR_HARDWARE_DOESNT_SUPPORT_FORMAT;
    }
    st = CreateStream(soundStreamStrategy, size ? size : SSS_MEDIUM);
    if (!st) {
	sc->SC_voice[0].SVS_physicalVoice = NO_VOICE;
	SoundDACFree(dac);
	return SOUND_ERROR_UNABLE_TO_ALLOCATE_STREAM;
    }
    memset(&r, 0, sizeof(r));
    r.DR_cx = dac;
    r.DR_ax = st;
    r.DR_bx = st;
    if (SoundSynth(DRE_SOUND_DAC_ATTACH_TO_STREAM, &r)) {
	DestroyStream(soundStreamStrategy, st);
	return SOUND_ERROR_FAILED_ATTACH_TO_HARDWARE;
    }
    ss->SSS_streamToken = st;
    /* told whenever there is room (threshold 0) */
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_WRITE;
    r.DR_bx = st;
    r.DR_cx = 0;
    (void) StreamCall(soundStreamStrategy, DR_STREAM_SET_THRESHOLD, &r);
    memset(&r, 0, sizeof(r));
    r.DR_ax = (SNE_DATA << SNT_EVENT_OFFSET) | SNM_ROUTINE;
    r.DR_bx = st;
    r.DR_farPtr = (void *) SoundWriterNotification;
    r.DR_bp = sc->SC_status.SBS_slot;
    (void) StreamCall(soundStreamStrategy, DR_STREAM_SET_NOTIFY, &r);
    SoundDACActivate(dac);
    return 0;
}

word SoundEnableSampleStream(MemHandle mh, SoundPriority priority,
			     word rate, word manufacturerID,
			     SampleFormat sampleFormat)
{
    SoundControl *sc;
    word err;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = Lock(mh);			/* until it is disabled */
    if (!sc) {
	SoundExitLibrary();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    sc->SC_status.SBS_priority = (word) priority;
    sc->SC_format.SFS_dac.SDACS_rate = rate;
    sc->SC_format.SFS_dac.SDACS_manufactID = manufacturerID;
    sc->SC_format.SFS_dac.SDACS_format = sampleFormat;
    sc->SC_position.SSS_stream.SSS_streamState |= SSST_ACTIVE;
    err = GetDACAndStream(sc, manufacturerID, sampleFormat);
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    SoundExitLibrary();
    return err;
}

/* SoundLibDriverWriterNotification: room on the stream of a DAC: more of
 * the buffer written; the writer woken when it is all on the stream;
 * the stream marked drained when the DAC read it all.  From the reader
 * of the stream (the driver), maybe interrupt code. */
void _pascal SoundWriterNotification(word data, word value, word side)
{
    SoundControl *sc = SoundSlot(data);
    SoundStreamStatus *ss;
    StreamData *sd;

    (void) value;
    (void) side;
    if (!sc || !sc->SC_position.SSS_stream.SSS_streamToken)
	return;
    ss = &sc->SC_position.SSS_stream;
    sd = StreamDataOf(ss->SSS_streamToken);
    ghal_crit_enter();
    if (ss->SSS_dataRemaining) {
	DriverRegs r;
	word n;

	memset(&r, 0, sizeof(r));
	r.DR_ax = STREAM_NO_BLOCK;
	r.DR_bx = ss->SSS_streamToken;
	r.DR_cx = ss->SSS_dataRemaining;
	r.DR_dsSi = (void *) ss->SSS_buffer;
	n = StreamCall(soundStreamStrategy, DR_STREAM_WRITE, &r) ? 0 :
	    (word) r.DR_cx;
	ss->SSS_dataRemaining = (word) (ss->SSS_dataRemaining - n);
	ss->SSS_buffer += n;
    } else if (ss->SSS_buffer) {
	ss->SSS_buffer = NULL;
	KSemV(&ss->SSS_dataSem);	/* all of it is on the stream */
    }
    if (sd->SD_reader.SSD_sem.Sem_value <= 0)
	SetDrained(&ss->SSS_activeReaderSem);
    sd->SD_writer.SSD_data.SN_ack = 0;
    sd->SD_state &= ~SS_WDATA;
    ghal_crit_leave();
}

/* SoundLibDriverWriteToDAC: the samples onto the stream; a new format
 * first waits for the stream to drain; returns when all is on it */
static word WriteToDAC(SoundControl *sc, const byte *sample, word size,
		       const SampleFormatDescription *fmt)
{
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;
    SoundDACStatus *dac = &sc->SC_format.SFS_dac;
    StreamData *sd;
    DriverRegs r;
    word room, n;

    if (fmt->SFD_rate != dac->SDACS_rate ||
	fmt->SFD_manufact != dac->SDACS_manufactID ||
	((fmt->SFD_format ^ dac->SDACS_format) & ~SMID_REFERENCE) ||
	(fmt->SFD_format & SMID_REFERENCE) ||
	!(fmt->SFD_playFlags & DACPF_CATENATE)) {
	(void) KSemP(&ss->SSS_activeReaderSem, SEM_FOREVER);
	dac->SDACS_manufactID = fmt->SFD_manufact;
	dac->SDACS_format = fmt->SFD_format;
	dac->SDACS_rate = fmt->SFD_rate;
	if (!SetRateAndFormat(sc->SC_voice[0].SVS_physicalVoice,
			      fmt->SFD_manufact, fmt->SFD_format,
			      fmt->SFD_rate, NULL))
	    return SOUND_ERROR_HARDWARE_DOESNT_SUPPORT_FORMAT;
    }
    if (!size)
	return 0;			/* (EC: NO_DATA_TO_STREAM) */
    sd = StreamDataOf(ss->SSS_streamToken);
    ghal_crit_enter();
    ss->SSS_dataRemaining = size;
    ss->SSS_buffer = sample;
    ss->SSS_activeReaderSem.Sem_value = 0;	/* the reader is on */
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_WRITE;
    r.DR_bx = ss->SSS_streamToken;
    (void) StreamCall(soundStreamStrategy, DR_STREAM_QUERY, &r);
    room = (word) r.DR_ax;
    n = room < size ? room : size;
    ss->SSS_buffer += n;
    ss->SSS_dataRemaining = (word) (ss->SSS_dataRemaining - n);
    sd->SD_state |= SS_WDATA;
    ghal_crit_leave();
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_NO_BLOCK;
    r.DR_bx = ss->SSS_streamToken;
    r.DR_cx = n;
    r.DR_dsSi = (void *) sample;
    if (n)
	(void) StreamCall(soundStreamStrategy, DR_STREAM_WRITE, &r);
    sd->SD_state &= ~SS_WDATA;
    StreamWriteDataNotify(sd);
    (void) KSemP(&ss->SSS_dataSem, SEM_FOREVER);
    return 0;
}

static void FreeDAC(MemHandle mh);

word SoundPlayToSampleStream(MemHandle mh, const word *sample, word size,
			     const SampleFormatDescription *format)
{
    SoundControl *sc;
    SoundStreamStatus *ss;
    word err;

    if (!SoundEnterLibrary())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = (SoundControl *) MemDeref(mh);	/* locked while enabled */
    ss = &sc->SC_position.SSS_stream;
    if (!(ss->SSS_streamState & SSST_ACTIVE)) {
	SoundExitLibrary();
	return SOUND_ERROR_DAC_UNATTACHED;
    }
    (void) KSemP(&ss->SSS_writerSem, SEM_FOREVER);
    if (ss->SSS_streamState & SSST_DESTROYING) {
	/* freed while we waited: the last to come frees it */
	if (ss->SSS_writerSem.Sem_queue)
	    KSemV(&ss->SSS_writerSem);
	else {
	    KSemV(&ss->SSS_writerSem);
	    FreeDAC(mh);
	}
	SoundExitLibrary();
	return SOUND_ERROR_STREAM_DESTROYED;
    }
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    err = 0;
    if (sc->SC_voice[0].SVS_physicalVoice == NO_VOICE) {
	/* the DAC was taken away: another */
	sc->SC_format.SFS_dac.SDACS_rate = format->SFD_rate;
	err = GetDACAndStream(sc, format->SFD_manufact, format->SFD_format);
    }
    if (!err)
	err = WriteToDAC(sc, (const byte *) sample, size, format);
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    KSemV(&ss->SSS_writerSem);
    SoundExitLibrary();
    return err;
}

void SoundDisableSampleStream(MemHandle mh)
{
    SoundControl *sc = (SoundControl *) MemDeref(mh);
    SoundStreamStatus *ss = &sc->SC_position.SSS_stream;
    StreamToken st;
    DriverRegs r;
    word dac;

    if (!(ss->SSS_streamState & SSST_ACTIVE))
	return;
    (void) KSemP(&ss->SSS_writerSem, SEM_FOREVER);
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    ghal_crit_enter();
    dac = sc->SC_voice[0].SVS_physicalVoice;
    sc->SC_voice[0].SVS_physicalVoice = NO_VOICE;
    if (dac != NO_VOICE)
	SoundDACDeactivate(dac);
    ghal_crit_leave();
    (void) KSemP(&ss->SSS_activeReaderSem, SEM_FOREVER);	/* drained */
    if (dac != NO_VOICE) {
	memset(&r, 0, sizeof(r));
	r.DR_cx = dac;
	(void) SoundSynth(DRE_SOUND_DAC_DETTACH_FROM_STREAM, &r);
    }
    ghal_crit_enter();
    ss->SSS_streamState &= ~SSST_ACTIVE;
    st = ss->SSS_streamToken;
    ss->SSS_streamToken = 0;
    ghal_crit_leave();
    if (st)
	DestroyStream(soundStreamStrategy, st);
    if (dac != NO_VOICE)
	SoundDACFree(dac);
    SetDrained(&ss->SSS_activeReaderSem);		/* Port32 */
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    KSemV(&ss->SSS_writerSem);
    MemUnlock(mh);
}

/* SoundLibDriverFreeDAC */
static void FreeDAC(MemHandle mh)
{
    SoundControl *sc = Lock(mh);
    SoundStreamStatus *ss;

    if (!sc)
	return;
    ss = &sc->SC_position.SSS_stream;
    (void) KSemP(&ss->SSS_writerSem, SEM_FOREVER);
    ss->SSS_streamState = SSST_DESTROYING;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    if (ss->SSS_writerSem.Sem_queue) {
	/* a writer waits: it frees the block */
	ThreadVSem(sc->SC_status.SBS_mutExSem);		/* Port32 */
	KSemV(&ss->SSS_writerSem);
	MemUnlock(mh);
	return;
    }
    SoundSlotFree(sc->SC_status.SBS_slot);
    ThreadFreeSem(sc->SC_status.SBS_mutExSem);
    MemFree(mh);
}

void SoundFreeSampleStream(MemHandle mh)
{
    FreeDAC(mh);
}

/* -------------------------------------------------------- ownership */

static void ChangeOwner(MemHandle mh, GeodeHandle owner)
{
    SoundControl *sc;

    HandleModifyOwner(mh, owner);
    sc = Lock(mh);
    if (!sc)
	return;
    HandleModifyOwner(sc->SC_status.SBS_mutExSem, owner);
    ghal_crit_enter();
    if (sc->SC_status.SBS_type != ST_STREAM_DAC &&
	sc->SC_format.SFS_fm.SFMS_timerHandle)
	HandleModifyOwner(sc->SC_format.SFS_fm.SFMS_timerHandle, owner);
    ghal_crit_leave();
    MemUnlock(mh);
}

void SoundChangeOwnerSimple(MemHandle mh, MemHandle owner)
{
    ChangeOwner(mh, (GeodeHandle) owner);
}

void SoundChangeOwnerStream(MemHandle mh, MemHandle owner)
{
    ChangeOwner(mh, (GeodeHandle) owner);
}
