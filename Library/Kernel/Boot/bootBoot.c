/*
 * bootBoot.c -- ending GEOS (after EndGeos of bootBoot.asm, with
 * RemoveGeodes of geodesUtils.asm and ExitFile of fileList.asm).
 *
 * EndGeos, as DOS16 did before it went back to DOS:
 *   1. clears the file change list (GCNSLT_FILE_SYSTEM), whose objects
 *      will not answer any more;
 *   2. gives DR_EXIT to the drivers that are not system drivers
 *      (RemoveGeodes);
 *   3. closes the files still open (ExitFile);
 *   4. gives DR_EXIT to the system drivers (ExitSystemDrivers).
 * Then it lets KernelRun return the exit flags (KEF_*) and the reason
 * to the program, which ends the host process.  On any thread but the
 * boot thread, EndGeos does not return (DOS16 never returned): the
 * thread waits until the program ends.  On the boot thread it returns,
 * and the next KernelRun returns at once.
 *
 * Not yet: the GEOS_ACT.IVE file that tells the next start of a dirty
 * exit, and ExitFSD.
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

#define END_MESSAGE_SIZE 160
#define GCNSLT_FILE_SYSTEM_LIST 0	/* gcnlist.goh */

word kernelExitFlags;
static Boolean ended;		/* EndGeos has begun */
static volatile Boolean done;	/* and finished */
static char endMessage[END_MESSAGE_SIZE];
static KSem never = { 0, 0 };

static int CollectFile(Handle h, HandleEntry *he, void *ctx)
{
    FileHandle *files = ctx;
    word i;

    if (he->HE_type != SIG_FILE)
	return 0;
    for (i = 0; files[i]; i++)
	;
    if (i < 63)
	files[i] = (FileHandle) h;
    return 0;
}

/* RemoveGeodes: DR_EXIT to the drivers that are not system drivers */
static void ExitDrivers(void)
{
    GeodeHandle drivers[64], gh;
    word n = 0;

    GeodeLock();
    for (gh = GeodeFindLow("", 0, 0, 0); gh; gh = GeodeCoreOf(gh)->GC_next)
	if ((GeodeCoreOf(gh)->GC_attrs & (GA_DRIVER | GA_SYSTEM)) == GA_DRIVER
	    && n < 64)
	    drivers[n++] = gh;
    while (n)
	GeodeExitDriver(drivers[--n]);
    GeodeUnlock();
}

void EndGeos(word flags, const char *reason)
{
    FileHandle files[64];
    word i;

    ghal_crit_enter();
    if (ended) {
	ghal_crit_leave();
	if (KernelThisThread() != kernelBootThread)
	    KSemP(&never, KSEM_FOREVER);
	return;
    }
    ended = TRUE;
    ghal_crit_leave();

    kernelExitFlags |= flags;
    if (reason) {
	strncpy(endMessage, reason, sizeof(endMessage) - 1);
	endMessage[sizeof(endMessage) - 1] = 0;
    }
    GCNListClearList(MANUFACTURER_ID_GEOWORKS, GCNSLT_FILE_SYSTEM_LIST);
    ExitDrivers();
    memset(files, 0, sizeof(files));
    HandleEnum(CollectFile, files);
    for (i = 0; files[i]; i++)
	FileClose(files[i], TRUE);
    GeodeExitSystemDrivers();
    done = TRUE;
    if (KernelThisThread() != kernelBootThread)
	KSemP(&never, KSEM_FOREVER);
}

Boolean KernelEnded(void)
{
    return done;
}

const char *KernelExitMessage(void)
{
    return endMessage;
}
