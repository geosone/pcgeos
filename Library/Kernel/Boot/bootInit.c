/*
 * bootInit.c -- starting the kernel (after InitGeos and
 * ProcessCommandLine of bootInit.asm, LoadUI of bootInitfile.asm).
 *
 * The program that runs the kernel (Loader/main.c, as the loader of DOS16
 * GEOS) calls KernelBoot with the host directory of the GEOS top and the
 * command tail.  KernelBoot does what InitGeos did, as far as the C
 * kernel goes:
 *
 *   1. the first thread and the timer (InitThread, InitTimer);
 *   2. the top directory as drive C (the loader's top level path) and
 *      the initial path SP_TOP (FileSetInitialPath);
 *   3. the primary file system driver and its drives (LoadFSDriver,
 *      FSDLoadPrimary); the init file is read on first use;
 *   4. the standard paths of [paths] (GetPaths of the loader);
 *   5. the UI (LoadUI), from SP_SYSTEM;
 *   6. a geode named on the command line (ProcessCommandLine): what is
 *      left of the tail after the switches ("/x"), loaded as a process
 *      from SP_SYSTEM, the path LoadUI left.
 *
 * DOS16 then turned the boot thread into the scheduler's idle loop
 * (Dispatch).  Here the host schedules the threads; KernelRun keeps the
 * boot thread waiting until no process is left, and returns.
 *
 * The UI: "[ui] generic" names its geode, as in DOS16; if it does not
 * load, the default ui.geo is tried and its failure is fatal
 * (CANNOT_LOAD_UI).  Without the key, no UI is loaded yet: there is no
 * C UI library to be the default.
 *
 * Not yet: the drivers (memory, power, keyboard, mouse, video, font),
 * the input manager, fonts, graphics and windows, patches, the log
 * file, documentIsTop, FSDInitComplete.
 */
#include "kernelC.h"

#include <initfile.h>
#include <string.h>

GeodeHandle kernelUIHandle;

static void LoadUI(void)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    word n;
    GeodeLoadError e;

    FileSetStandardPath(SP_SYSTEM);
    if (InitFileReadStringBuffer("ui", "generic", name, sizeof(name), &n))
	return;
    kernelUIHandle = GeodeLoad(name, 0, 0, 0, 0, &e);
    if (!kernelUIHandle)
	kernelUIHandle = GeodeLoad("ui.geo", 0, 0, 0, 0, &e);
    if (!kernelUIHandle)
	KernelFatal(CANNOT_LOAD_UI);
}

/* skip the switches, then load the rest of the tail as a process */
static GeodeHandle ProcessCommandLine(const char *tail, GeodeLoadError *err)
{
    const char *p = tail;

    *err = GLE_OK;
    if (!p)
	return 0;
    for (;;) {
	while (*p == ' ' || *p == '\t')
	    p++;
	if (*p != '/')
	    break;
	while (*p && *p != ' ' && *p != '\t')
	    p++;
    }
    if (!*p)
	return 0;
    return GeodeLoad(p, GA_PROCESS, 0, PRIORITY_STANDARD, 0, err);
}

GeodeHandle KernelBoot(const char *topHostPath, const char *cmdTail,
		       GeodeLoadError *err)
{
    GeodeLoadError e;
    GeodeHandle gh;

    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    FileSetTopHostPath(topHostPath);
    FileSetStandardPath(SP_TOP);
    FSDLoadPrimary();
    FileInitStdPaths();
    LoadUI();
    gh = ProcessCommandLine(cmdTail, &e);
    if (err)
	*err = e;
    return gh;
}

void KernelRun(void)
{
    GeodeHandle gh;

    for (;;) {
	GeodeLock();
	gh = GeodeFindLow("", 0, GA_PROCESS, 0);
	GeodeUnlock();
	if (!gh)
	    break;
	TimerSleep(6);
    }
}
