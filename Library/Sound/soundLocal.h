/*
 * soundLocal.h -- the sound library in C (Port32): its data and the
 * routines its files share.  The asm is soundManager.asm and the files
 * it includes; the C files lie beside them:
 *
 *   soundCommon.c		the library entry, the drivers, exclusive
 *				access, the API (soundCommon.asm, soundC.asm)
 *   soundResident.c		the player: the timer routine reading the
 *				events (soundResident.asm)
 *   soundVoiceAllocation.c	the voice and DAC manager
 *   soundDriver.c		what the library's driver functions did
 *				(soundDriver.asm): alloc, play, stop, free
 *				of FM sounds, music streams, sample streams
 *   soundMixer.c		the mixer (soundMixer.asm)
 *
 * Port32 (Port32/ABI.md section 9.12): the library's own driver side
 * (DR_SOUND_*, which nobody used but the library) is gone, its functions
 * are called directly; interrupts off is ghal_crit; the player runs in
 * the timer routine, interrupt code (the kernel lets it start timers
 * there); a sound is found from a word of data through SoundSlot.
 */
#ifndef SOUNDLOCAL_H
#define SOUNDLOCAL_H

#include <geos.h>
#include <geosdriver.h>
#include <heap.h>
#include <sem.h>
#include <thread.h>
#include <timer.h>
#include <ghal.h>
#include <sound.h>
#include <Internal/soundDrv.h>
#include <Internal/soundCtl.h>
#include <Internal/strDrInt.h>

/* --- soundCommon.c */
extern DriverStrategy *soundSynthStrategy;	/* the sound driver */
extern GeodeHandle soundSynthHandle;
extern DriverStrategy *soundStreamStrategy;	/* for DAC streams */
extern word driverVoices, driverDACs;
extern SupportedInstrumentFormat driverVoiceFormat;
extern SoundDriverCapability driverCapability;
extern SoundDriverDACCapability driverDACCapability;
extern word libraryAccess, exclusiveAccess;
extern GeodeHandle userInterfaceHandle;
extern Semaphore exclusiveSemaphore, librarySemaphore;

/* call the sound driver; TRUE: carry */
extern Boolean SoundSynth(word function, DriverRegs *r);
/* SoundCallLibraryDriverRoutine: FALSE if another thread has exclusive
 * access (SOUND_ERROR_EXCLUSIVE_ACCESS_GRANTED) */
extern Boolean SoundLibDriverEnterLibraryRoutine(void);
extern void SoundLibDriverExitLibraryRoutine(void);

/* the table of sounds (a timer routine's or notification's data is the
 * place + 1) */
extern word SoundSlotAlloc(SoundControl *sc);	/* 0: full */
extern void SoundSlotFree(word slot);
extern SoundControl *SoundSlot(word slot);

/* --- soundUI.goc: MSG_USER_FREE_SOUND_HANDLE to the UI (from interrupt
 * code too); nothing without a UI */
extern void SoundSendFreeToUI(MemHandle mh);

/* --- soundResident.c */
/* the next event(s) of a sound, inside ghal_crit (SoundHandleTimerEvent) */
extern void SoundHandleTimerEvent(SoundControl *sc);
extern void SoundTimerRoutine(word data, dword tick);

/* --- soundVoiceAllocation.c (inside ghal_crit) */
extern Boolean SoundVoiceInitialize(void);	/* FALSE: no memory */
extern void SoundVoiceDetach(void);
/* a voice for priority, taken from its owner if need be; NO_VOICE if
 * none */
extern word SoundVoiceGetFree(word priority);
extern void SoundVoiceFree(word voice);
extern void SoundVoiceActivate(word voice, word priority);
extern Boolean SoundVoiceDeactivate(word voice);	/* TRUE: not active */
extern void SoundVoiceAssign(word voice, SoundControl *owner,
			     word streamVoice);
extern word SoundDACGetFree(word priority);
extern void SoundDACFree(word dac);
extern void SoundDACActivate(word dac);
extern Boolean SoundDACDeactivate(word dac);
extern void SoundDACAssign(word dac, SoundControl *owner, word streamVoice);

/* --- soundDriver.c */
extern word SoundFreeSimple(MemHandle mh);
extern void SoundSilenceVoices(SoundControl *sc);
extern void SoundInitSimpleFM(SoundControl *sc, MemHandle mh, word voices,
			      const byte *song);
extern void _pascal SoundWriterNotification(word data, word value,
					    word side);

#define SOUND_PRIORITY_REUSE	0xffff	/* a voice off: first to go */

/* soundDriver.c and soundC.c */
void SoundChangeOwnerBlock(MemHandle mh, GeodeHandle owner);

/* soundDriver.c and soundCommon.c */
SoundControl *SoundLockControl(MemHandle mh);
void SoundSetDrained(Semaphore *s);
Boolean SoundStreamCall(DriverStrategy *strat, word fn, DriverRegs *r);
void SoundInitVoices(SoundControl *sc, word voices);
SemaphoreHandle SoundNewMutex(MemHandle mh);

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

/* soundDriver.c: the library driver's routines soundCommon.c calls */
SoundControl *SoundCheckExclusiveSimpleFM(MemHandle mh);
void SoundStartSimpleFM(SoundControl *sc);
SoundControl *SoundStartPlayToStreamFM(MemHandle mh, word *oldPrio);
void SoundActivateStreamReader(SoundControl *sc);
Boolean SoundLibDriverWriteEvent(SoundControl *sc, const byte **p, sword *left,
	Boolean sized);
StreamToken SoundLibDriverCreateStream(DriverStrategy *strat, word size);
word SoundLibDriverGetDACAndStreamForSound(SoundControl *sc, word manufact, word format);
word SoundLibDriverWriteToDAC(SoundControl *sc, const byte *sample, word size,
	const SampleFormatDescription *fmt);
void SoundLibDriverFreeDAC(MemHandle mh);
/* soundCommon.c: event sizes and streams, for soundDriver.c too */
word EventSize(const byte *p);
void DestroyStream(DriverStrategy *strat, StreamToken token);

#endif /* SOUNDLOCAL_H */
