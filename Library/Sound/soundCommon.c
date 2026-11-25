/*
 * soundCommon.c -- the sound library's entry, its driver, exclusive
 * access and what the API routines share (soundCommon.asm, soundC.asm).
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

/* SoundLibDriverEnterLibraryRoutine */
Boolean SoundEnterLibrary(void)
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
void SoundExitLibrary(void)
{
    ghal_crit_enter();
    if (--libraryAccess == 0)
	KSemV(&librarySemaphore);
    ghal_crit_leave();
}

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
