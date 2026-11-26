/*
 * soundCommon.c -- the sound library's entry, its driver, exclusive
 * access and what the API routines share (soundCommon.asm;
 * the C stubs of soundC.asm are in soundC.c).
 *
 * At its attach the library loads the sound driver named in geos.ini
 * ([sound] synthDriver, in SYSTEM\SOUND), else standard.geo, and asks it
 * what it can do; without one every sound routine returns
 * SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED, as if another thread had
 * exclusive access for good (the asm did the same).
 *
 * Exclusive access: SoundGetExclusive waits until no library routine
 * runs and keeps the others out until SoundReleaseExclusive; a routine
 * entered meanwhile returns SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED.
 *
 * Port32: the table of sounds (SoundSlot) is new; the UI is found by
 * name on a new client (the asm loaded "ui.geo" on the first client
 * thread, which the UI is when it starts).
 */
#include "soundLocal.h"
#include <geode.h>
#include <file.h>
#include <initfile.h>
#include <library.h>
#include <string.h>

DriverStrategy *soundSynthStrategy;
GeodeHandle soundSynthHandle;
DriverStrategy *soundStreamStrategy;
word driverVoices, driverDACs;
SupportedInstrumentFormat driverVoiceFormat;
SoundDriverCapability driverCapability;
SoundDriverDACCapability driverDACCapability;
word libraryAccess, exclusiveAccess;
GeodeHandle userInterfaceHandle;
Semaphore exclusiveSemaphore = { 1, 0 };
Semaphore librarySemaphore = { 1, 0 };

/* ------------------------------------------------- the table of sounds */

#define SOUND_SLOTS_GROW	64

static SoundControl **slots;
static word slotCount;
static MemHandle slotBlock;		/* fixed, the library's */

word SoundSlotAlloc(SoundControl *sc)
{
    word i;
    SoundControl **grown;
    MemHandle mh, old;

    for (;;) {
	ghal_crit_enter();
	for (i = 0; i < slotCount; i++)
	    if (!slots[i]) {
		slots[i] = sc;
		ghal_crit_leave();
		return (word) (i + 1);
	    }
	ghal_crit_leave();
	if (slotCount > 0xffff - SOUND_SLOTS_GROW)
	    return 0;
	mh = MemAlloc((word) ((slotCount + SOUND_SLOTS_GROW) *
			      sizeof(SoundControl *)),
		      HF_FIXED | HF_SHARABLE, HAF_ZERO_INIT);
	if (!mh)
	    return 0;
	HandleModifyOwner(mh, GeodeGetCodeProcessHandle());
	grown = (SoundControl **) MemDeref(mh);
	ghal_crit_enter();
	if (slotCount)
	    memcpy(grown, slots, slotCount * sizeof(SoundControl *));
	old = slotBlock;
	slots = grown;
	slotBlock = mh;
	slotCount = (word) (slotCount + SOUND_SLOTS_GROW);
	ghal_crit_leave();
	if (old)
	    MemFree(old);
    }
}

void SoundSlotFree(word slot)
{
    ghal_crit_enter();
    if (slot && slot <= slotCount)
	slots[slot - 1] = NULL;
    ghal_crit_leave();
}

SoundControl *SoundSlot(word slot)
{
    SoundControl *sc = NULL;

    ghal_crit_enter();
    if (slot && slot <= slotCount)
	sc = slots[slot - 1];
    ghal_crit_leave();
    return sc;
}

/* ----------------------------------------------------------- the driver */

Boolean SoundSynth(word function, DriverRegs *r)
{
    if (!soundSynthStrategy)
	return TRUE;
    return soundSynthStrategy(function, r);
}

/* SoundGetDrivers: FALSE if there is none */
static Boolean GetDrivers(void)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    GeodeLoadError err;
    GeodeHandle gh = 0;
    DriverInfoStruct *dis;
    word size = 0;

    FilePushDir();
    if (!FileSetCurrentPath(SP_SYSTEM, "SOUND")) {
	FilePopDir();
	return FALSE;
    }
    if (!InitFileReadStringBuffer("sound", "synthDriver", name,
				  sizeof(name), &size) && size)
	gh = GeodeUseDriver(name, 0, 0, &err);
    if (!gh)
	gh = GeodeUseDriver("standard.geo", SOUND_PROTO_MAJOR,
			    SOUND_PROTO_MINOR, &err);
    FilePopDir();
    if (!gh)
	return FALSE;
    dis = GeodeInfoDriver(gh);
    soundSynthStrategy = DriverStrategyOf(dis);
    soundSynthHandle = gh;
    return TRUE;
}

/* SoundAttachLibrary: FALSE if there is no driver (everything refused) */
static Boolean Attach(void)
{
    DriverRegs r;

    if (!GetDrivers()) {
	(void) KSemP(&exclusiveSemaphore, SEM_FOREVER);
	exclusiveAccess = 1;
	return FALSE;
    }
    memset(&r, 0, sizeof(r));
    (void) SoundSynth(DRE_SOUND_QUERY_DEVICE_CAPABILITY, &r);
    driverVoices = (word) r.DR_ax;
    driverVoiceFormat = (word) r.DR_bx;
    driverCapability = (word) r.DR_cx;
    driverDACs = (word) r.DR_dx;
    driverDACCapability = (word) r.DR_di;
    soundStreamStrategy = r.DR_farPtr ? (DriverStrategy *) r.DR_farPtr :
	StreamStrategy;
    if (!SoundVoiceInitialize()) {
	ghal_crit_enter();
	exclusiveSemaphore.Sem_value = 0;
	exclusiveAccess = 1;
	ghal_crit_leave();
	return FALSE;
    }
    return TRUE;
}

Boolean _pascal SoundEntry(LibraryCallType ty, GeodeHandle client)
{
    (void) client;
    switch (ty) {
    case LCT_ATTACH:
	(void) Attach();
	break;
    case LCT_DETACH:
	SoundVoiceDetach();
	break;
    case LCT_NEW_CLIENT:
    case LCT_NEW_CLIENT_THREAD:
	if (!userInterfaceHandle)
	    userInterfaceHandle = GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0);
	break;
    default:
	break;
    }
    return FALSE;
}

/* ---------------------------------------------------- exclusive access */

void SoundGetExclusive(void)
{
    (void) KSemP(&exclusiveSemaphore, SEM_FOREVER);
    ghal_crit_enter();
    exclusiveAccess++;
    ghal_crit_leave();
    (void) KSemP(&librarySemaphore, SEM_FOREVER);
}

/* TRUE if another thread has it */
Boolean SoundGetExclusiveNB(void)
{
    if (KSemP(&exclusiveSemaphore, 0) != SE_NO_ERROR)
	return TRUE;
    ghal_crit_enter();
    exclusiveAccess++;
    ghal_crit_leave();
    if (KSemP(&librarySemaphore, 0) != SE_NO_ERROR) {
	ghal_crit_enter();
	exclusiveAccess--;
	ghal_crit_leave();
	KSemV(&exclusiveSemaphore);
	return TRUE;
    }
    return FALSE;
}

void SoundReleaseExclusive(void)
{
    KSemV(&librarySemaphore);
    ghal_crit_enter();
    exclusiveAccess--;
    ghal_crit_leave();
    KSemV(&exclusiveSemaphore);
}

/* ---------------------------------------------------------- driver info */

void SoundSynthDriverInfo(word *voices, SupportedEnvelopeFormat *format,
			  SoundDriverCapability *capability)
{
    if (voices)
	*voices = driverVoices;
    if (format)
	*format = (SupportedEnvelopeFormat) driverVoiceFormat;
    if (capability)
	*capability = driverCapability;
}

void SoundSampleDriverInfo(word *voices, SoundDriverCapability *capability)
{
    if (voices)
	*voices = driverDACs;
    if (capability)
	*capability = driverDACCapability;
}

/* the sizes of the events in a song (SoundLibDriverWriteEvent), by
 * SoundStreamEvent / 2 and GeneralEvent / 2; 0: see the general one.
 * Port32: the general events with a handle are two bytes longer. */
static const word eventBufferSize[] = { 8, 4, 8, 0, 4, 4, 4 };
static const word generalEventBufferSize[] = { 4, 4, 6, 6, 12, 8 };

word EventSize(const byte *p)
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
    SoundControl *sc = SoundLockControl(mh);

    if (!sc)
	return;			/* (EC: CONTROL_BLOCK_IN_DISCARDED_RESOURCE) */
    SoundInitSimpleFM(sc, mh, voices, NULL);
    MemUnlock(mh);
}

word SoundPlayMusic(MemHandle mh, word priority, word tempo,
		    EndOfSongFlags flags)
{
    SoundControl *sc;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundCheckExclusiveSimpleFM(mh);
    if (sc) {
	sc->SC_status.SBS_priority = priority;
	sc->SC_format.SFS_fm.SFMS_tempo = tempo;
	sc->SC_position.SSS_simple.SSS_songPointer =
	    sc->SC_position.SSS_simple.SSS_songBuffer;
	sc->SC_status.SBS_EOS = flags & ~EOSF_LMEM_UNLOCK;
	SoundStartSimpleFM(sc);
    }
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundPlayMusicLMem(MemHandle mh, word priority, word tempo,
			EndOfSongFlags flags)
{
    SoundControl *sc;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundCheckExclusiveSimpleFM(mh);
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
	    SoundStartSimpleFM(sc);
	else
	    ThreadVSem(sc->SC_status.SBS_mutExSem);
    }
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundStopMusic(MemHandle mh)
{
    SoundControl *sc;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundLockControl(mh);
    if (sc) {
	ThreadPSem(sc->SC_status.SBS_mutExSem);
	SoundSilenceVoices(sc);
	ThreadVSem(sc->SC_status.SBS_mutExSem);
	MemUnlock(mh);
    }
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundReallocMusic(MemHandle mh, const word *song)
{
    SoundControl *sc;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundLockControl(mh);
    if (sc) {
	sc->SC_position.SSS_simple.SSS_songBuffer = (const byte *) song;
	MemUnlock(mh);
    }
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundReallocMusicLMem(MemHandle mh, optr song)
{
    SoundControl *sc;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundLockControl(mh);
    if (sc) {
	sc->SC_position.SSS_simple.SSS_songHandle = song;
	MemUnlock(mh);
    }
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

void SoundFreeMusic(MemHandle mh)
{
    (void) SoundFreeSimple(mh);
}

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
    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    mh = AllocControl(1, sizeof(oneNoteSong), &sc);
    *control = mh;
    if (!mh) {
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    SoundInitSimpleFM(sc, mh, 1, NULL);
    memcpy(NoteOf(sc), oneNoteSong, sizeof(oneNoteSong));
    SetNote(NoteOf(sc), instrument, instTable, frequency, volume,
	    (word) deltaType, duration);
    MemUnlock(mh);
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundReallocMusicNote(MemHandle mh, word freq, word vol,
			   SoundStreamDeltaTimeType timer, word durat,
			   InstrumentPatch instrument, word instTable)
{
    SoundControl *sc;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundLockControl(mh);
    if (sc) {
	SetNote(NoteOf(sc), instrument, instTable, freq, vol, (word) timer,
		durat);
	MemUnlock(mh);
    }
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}
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
    SoundInitVoices(sc, voices);
    sc->SC_status.SBS_mutExSem = SoundNewMutex(mh);
    KSemInit(&ss->SSS_writerSem, 1);
    KSemInit(&ss->SSS_dataSem, 0);
    KSemInit(&ss->SSS_activeReaderSem, 1);
    sc->SC_status.SBS_slot = SoundSlotAlloc(sc);
}

void DestroyStream(DriverStrategy *strat, StreamToken token)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_DISCARD;
    r.DR_bx = token;
    (void) SoundStreamCall(strat, DR_STREAM_DESTROY, &r);
}

word SoundAllocMusicStream(SoundStreamSize streamSize,
			   SoundPriority priority, word voices, word tempo,
			   MemHandle *control)
{
    SoundControl *sc;
    MemHandle mh;
    StreamToken st;

    *control = 0;
    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    if (!voices)
	voices = 1;
    mh = AllocControl(voices, 0, &sc);
    if (!mh) {
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    InitStreamFM(sc, mh, (word) priority, voices, tempo);
    st = SoundLibDriverCreateStream(StreamStrategy, streamSize);
    if (!st) {
	SoundSlotFree(sc->SC_status.SBS_slot);
	ThreadFreeSem(sc->SC_status.SBS_mutExSem);
	MemFree(mh);
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_UNABLE_TO_ALLOCATE_STREAM;
    }
    sc->SC_position.SSS_stream.SSS_streamToken = st;
    MemUnlock(mh);
    *control = mh;
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

static void EndPlayToStreamFM(SoundControl *sc, word oldPrio)
{
    MemHandle mh = sc->SC_status.SBS_blockHandle;

    ThreadModify(0, oldPrio, TMF_BASE_PRIO);
    KSemV(&sc->SC_position.SSS_stream.SSS_writerSem);
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    MemUnlock(mh);
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
	SoundActivateStreamReader(sc);
}

word SoundPlayToMusicStream(MemHandle mh, const word *song, word size)
{
    SoundControl *sc;
    const byte *p = (const byte *) song;
    sword left = (sword) size;
    word oldPrio;
    Boolean done;

    sc = SoundStartPlayToStreamFM(mh, &oldPrio);
    if (!sc)
	return SOUND_ERROR_STREAM_DESTROYED;
    do {
	done = SoundLibDriverWriteEvent(sc, &p, &left, size != 0);
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

    sc = SoundStartPlayToStreamFM(mh, &oldPrio);
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
	done = SoundLibDriverWriteEvent(sc, &p, &left, size != 0);
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

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundLockControl(mh);
    if (!sc) {
	SoundLibDriverExitLibraryRoutine();
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
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

void SoundFreeMusicStream(MemHandle mh)
{
    SoundControl *sc = SoundLockControl(mh);

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
word SoundAllocSampleStream(MemHandle *control)
{
    SoundControl *sc;
    SoundStreamStatus *ss;
    MemHandle mh;

    *control = 0;
    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    mh = AllocControl(1, 0, &sc);
    if (!mh) {
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    /* SoundLibDriverInitDAC */
    ss = &sc->SC_position.SSS_stream;
    sc->SC_status.SBS_blockHandle = mh;
    sc->SC_status.SBS_mutExSem = SoundNewMutex(mh);
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
    SoundLibDriverExitLibraryRoutine();
    return SOUND_ERROR_NO_ERRORS;
}

word SoundEnableSampleStream(MemHandle mh, SoundPriority priority,
			     word rate, word manufacturerID,
			     SampleFormat sampleFormat)
{
    SoundControl *sc;
    word err;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = SoundLockControl(mh);			/* until it is disabled */
    if (!sc) {
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_OUT_OF_MEMORY;
    }
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    sc->SC_status.SBS_priority = (word) priority;
    sc->SC_format.SFS_dac.SDACS_rate = rate;
    sc->SC_format.SFS_dac.SDACS_manufactID = manufacturerID;
    sc->SC_format.SFS_dac.SDACS_format = sampleFormat;
    sc->SC_position.SSS_stream.SSS_streamState |= SSST_ACTIVE;
    err = SoundLibDriverGetDACAndStreamForSound(sc, manufacturerID, sampleFormat);
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    SoundLibDriverExitLibraryRoutine();
    return err;
}

word SoundPlayToSampleStream(MemHandle mh, const word *sample, word size,
			     const SampleFormatDescription *format)
{
    SoundControl *sc;
    SoundStreamStatus *ss;
    word err;

    if (!SoundLibDriverEnterLibraryRoutine())
	return SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED;
    sc = (SoundControl *) MemDeref(mh);	/* locked while enabled */
    ss = &sc->SC_position.SSS_stream;
    if (!(ss->SSS_streamState & SSST_ACTIVE)) {
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_DAC_UNATTACHED;
    }
    (void) KSemP(&ss->SSS_writerSem, SEM_FOREVER);
    if (ss->SSS_streamState & SSST_DESTROYING) {
	/* freed while we waited: the last to come frees it */
	if (ss->SSS_writerSem.Sem_queue)
	    KSemV(&ss->SSS_writerSem);
	else {
	    KSemV(&ss->SSS_writerSem);
	    SoundLibDriverFreeDAC(mh);
	}
	SoundLibDriverExitLibraryRoutine();
	return SOUND_ERROR_STREAM_DESTROYED;
    }
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    err = 0;
    if (sc->SC_voice[0].SVS_physicalVoice == NO_VOICE) {
	/* the DAC was taken away: another */
	sc->SC_format.SFS_dac.SDACS_rate = format->SFD_rate;
	err = SoundLibDriverGetDACAndStreamForSound(sc, format->SFD_manufact, format->SFD_format);
    }
    if (!err)
	err = SoundLibDriverWriteToDAC(sc, (const byte *) sample, size, format);
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    KSemV(&ss->SSS_writerSem);
    SoundLibDriverExitLibraryRoutine();
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
    SoundSetDrained(&ss->SSS_activeReaderSem);		/* Port32 */
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    KSemV(&ss->SSS_writerSem);
    MemUnlock(mh);
}

void SoundFreeSampleStream(MemHandle mh)
{
    SoundLibDriverFreeDAC(mh);
}

void SoundChangeOwnerStream(MemHandle mh, MemHandle owner)
{
    SoundChangeOwnerBlock(mh, (GeodeHandle) owner);
}
