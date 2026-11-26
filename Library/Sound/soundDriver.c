/*
 * soundDriver.c -- the sounds of the sound library: FM songs and notes,
 * music streams and sample (DAC) streams (soundDriver.asm, which made
 * them the functions of the library's own driver; the API routines that
 * make and play them are in soundCommon.c and soundC.c, after their asm
 * files).
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
#include <geode.h>
#include <file.h>
#include <initfile.h>
#include <library.h>

SoundControl *SoundLockControl(MemHandle mh)
{
    return (SoundControl *) MemLock(mh);
}

/* the "stream drained" semaphore free again, everyone waiting on it
 * woken (the asm: value 1, ThreadWakeUpQueue) */
void SoundSetDrained(Semaphore *s)
{
    ghal_crit_enter();
    KSemVAll(s);
    s->Sem_value = 1;
    ghal_crit_leave();
}

Boolean SoundStreamCall(DriverStrategy *strat, word fn, DriverRegs *r)
{
    return strat(fn, r);
}

void SoundInitVoices(SoundControl *sc, word voices)
{
    word i;

    for (i = 0; i < voices; i++) {
	sc->SC_voice[i].SVS_physicalVoice = NO_VOICE;
	sc->SC_voice[i].SVS_instrument = DEFAULT_INSTRUMENT;
    }
}

/* the mutex, owned as the block is */
SemaphoreHandle SoundNewMutex(MemHandle mh)
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
    SoundInitVoices(sc, voices);
    sc->SC_status.SBS_mutExSem = SoundNewMutex(mh);
    sc->SC_status.SBS_slot = SoundSlotAlloc(sc);
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

/* SoundLibDriverFreeSimple */
word SoundFreeSimple(MemHandle mh)
{
    SoundControl *sc = SoundLockControl(mh);

    if (!sc)
	return SOUND_ERROR_NO_ERRORS;
    SoundSilenceVoices(sc);			/* Port32 */
    SoundSlotFree(sc->SC_status.SBS_slot);
    ThreadFreeSem(sc->SC_status.SBS_mutExSem);
    MemFree(mh);
    return SOUND_ERROR_NO_ERRORS;
}

/* ------------------------------------------------------------- notes */

/* ----------------------------------------------------- music streams */

/* ------------------------------------------------------ sample streams */

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
	n = SoundStreamCall(soundStreamStrategy, DR_STREAM_WRITE, &r) ? 0 :
	    (word) r.DR_cx;
	ss->SSS_dataRemaining = (word) (ss->SSS_dataRemaining - n);
	ss->SSS_buffer += n;
    } else if (ss->SSS_buffer) {
	ss->SSS_buffer = NULL;
	KSemV(&ss->SSS_dataSem);	/* all of it is on the stream */
    }
    if (sd->SD_reader.SSD_sem.Sem_value <= 0)
	SoundSetDrained(&ss->SSS_activeReaderSem);
    sd->SD_writer.SSD_data.SN_ack = 0;
    sd->SD_state &= ~SS_WDATA;
    ghal_crit_leave();
}

/* -------------------------------------------------------- ownership */

void SoundChangeOwnerBlock(MemHandle mh, GeodeHandle owner)
{
    SoundControl *sc;

    HandleModifyOwner(mh, owner);
    sc = SoundLockControl(mh);
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

/* SoundLibDriverEnterLibraryRoutine */
Boolean SoundLibDriverEnterLibraryRoutine(void)
{
    ghal_crit_enter();
    if (exclusiveAccess) {
	ghal_crit_leave();
	return FALSE;
    }
    librarySemaphore.Sem_value = 0;	/* a routine runs */
    libraryAccess++;
    ghal_crit_leave();
    return TRUE;
}

/* SoundLibDriverExitLibraryRoutine */
void SoundLibDriverExitLibraryRoutine(void)
{
    ghal_crit_enter();
    if (--libraryAccess == 0)
	KSemV(&librarySemaphore);
    ghal_crit_leave();
}

/* SoundCheckExclusiveSimpleFM: the block locked (for the play) and its
 * mutex taken; a sound still playing (locked before) silenced */
SoundControl *SoundCheckExclusiveSimpleFM(MemHandle mh)
{
    SoundControl *sc = SoundLockControl(mh);

    if (!sc)
	return NULL;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    if (MGI_LOCK_COUNT(MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT)) != 1)
	SoundSilenceVoices(sc);
    return sc;
}

/* SoundStartSimpleFM: the mutex given back, the first events played */
void SoundStartSimpleFM(SoundControl *sc)
{
    ghal_crit_enter();
    ThreadVSem(sc->SC_status.SBS_mutExSem);
    SoundHandleTimerEvent(sc);
    ghal_crit_leave();
}

/* SoundStartPlayToStreamFM: locked, mutex and writer taken, the thread
 * at a high priority; its old priority back */
SoundControl *SoundStartPlayToStreamFM(MemHandle mh, word *oldPrio)
{
    SoundControl *sc = SoundLockControl(mh);

    if (!sc)
	return NULL;
    ThreadPSem(sc->SC_status.SBS_mutExSem);
    (void) KSemP(&sc->SC_position.SSS_stream.SSS_writerSem, SEM_FOREVER);
    *oldPrio = TGI_PRIORITY(ThreadGetInfo(0, TGIT_PRIORITY_AND_USAGE));
    ThreadModify(0, PRIORITY_HIGH, TMF_BASE_PRIO);
    return sc;
}

/* SoundActivateStreamReader: the block locked for the reader (once), the
 * first events played */
void SoundActivateStreamReader(SoundControl *sc)
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

static Boolean IsEndOfSong(const byte *p)
{
    word ev, gen;

    memcpy(&ev, p, sizeof(ev));
    memcpy(&gen, p + 2, sizeof(gen));
    return ev == SSE_GENERAL && gen == GE_END_OF_SONG;
}

/* SoundLibDriverWriteEvent: one event onto the stream (blocking); TRUE
 * when the song is written (*left bytes left, 0: up to the end of the
 * song) */
Boolean SoundLibDriverWriteEvent(SoundControl *sc, const byte **p, sword *left,
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
word SoundLibDriverGetDACAndStreamForSound(SoundControl *sc, word manufact, word format)
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
    st = SoundLibDriverCreateStream(soundStreamStrategy, size ? size : SSS_MEDIUM);
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
    (void) SoundStreamCall(soundStreamStrategy, DR_STREAM_SET_THRESHOLD, &r);
    memset(&r, 0, sizeof(r));
    r.DR_ax = (SNE_DATA << SNT_EVENT_OFFSET) | SNM_ROUTINE;
    r.DR_bx = st;
    r.DR_farPtr = (void *) SoundWriterNotification;
    r.DR_bp = sc->SC_status.SBS_slot;
    (void) SoundStreamCall(soundStreamStrategy, DR_STREAM_SET_NOTIFY, &r);
    SoundDACActivate(dac);
    return 0;
}

/* SoundLibDriverWriteToDAC: the samples onto the stream; a new format
 * first waits for the stream to drain; returns when all is on it */
word SoundLibDriverWriteToDAC(SoundControl *sc, const byte *sample, word size,
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
    (void) SoundStreamCall(soundStreamStrategy, DR_STREAM_QUERY, &r);
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
	(void) SoundStreamCall(soundStreamStrategy, DR_STREAM_WRITE, &r);
    sd->SD_state &= ~SS_WDATA;
    StreamWriteDataNotify(sd);
    (void) KSemP(&ss->SSS_dataSem, SEM_FOREVER);
    return 0;
}

/* SoundLibDriverFreeDAC */
void SoundLibDriverFreeDAC(MemHandle mh)
{
    SoundControl *sc = SoundLockControl(mh);
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

/* a stream of size bytes owned by the library; 0 if none */
StreamToken SoundLibDriverCreateStream(DriverStrategy *strat, word size)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = size;
    r.DR_bx = GeodeGetCodeProcessHandle();
    r.DR_cx = HF_FIXED;
    if (SoundStreamCall(strat, DR_STREAM_CREATE, &r))
	return 0;
    return (StreamToken) r.DR_bx;
}

