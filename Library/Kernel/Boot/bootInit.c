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
 *      the initial path SP_TOP (FileSetInitialPath); GEOS_ACT.IVE in
 *      the top, made here and deleted by a clean end: if it is there
 *      already, the last run did not end cleanly (SCF_CRASHED, InitSys);
 *   3. the primary file system driver and its drives (LoadFSDriver,
 *      FSDLoadPrimary); the init file is read on first use;
 *   4. the standard paths of [paths] (GetPaths of the loader);
 *   5. the input manager's thread (InitIM), which runs once the
 *      drivers are loaded;
 *      the keyboard driver (LoadKeyboardDriver), from
 *      SP_KEYBOARD_DRIVERS, the default GDDT_KEYBOARD driver;
 *      the UI (LoadUI), from SP_SYSTEM;
 *   6. a geode named on the command line (ProcessCommandLine): what is
 *      left of the tail after the switches ("/x"), loaded as a process
 *      from SP_SYSTEM, the path LoadUI left.
 *
 * DOS16 then turned the boot thread into the scheduler's idle loop
 * (Dispatch).  Here the host schedules the threads; KernelRun keeps the
 * boot thread waiting until no process is left or GEOS ends (EndGeos,
 * bootBoot.c), and returns the exit flags.
 *
 * The UI: "[ui] generic" names its geode, as in DOS16; if it does not
 * load, the default ui.geo is tried and its failure is fatal
 * (CANNOT_LOAD_UI).  Without the key, no UI is loaded yet: there is no
 * C UI library to be the default.
 *
 * The keyboard driver: "[keyboard] driver" names it, as in DOS16; if it
 * does not load, kbd.geo is tried and its failure is fatal
 * (CANNOT_LOAD_KEYBOARD_DRIVER).  Without the key, none is loaded yet:
 * a host without keys (the tests) needs none.
 *
 * Not yet: the drivers (memory, power, mouse, video, font),
 * fonts, graphics and windows, patches, the log
 * file, documentIsTop, FSDInitComplete.
 */
#include "kernelC.h"

#include <initfile.h>
#include <system.h>
#include <driver.h>
#include <Internal/kbdDr.h>
#include <string.h>

GeodeHandle kernelUIHandle;
ThreadHandle kernelBootThread;

/* InitSys: GEOS_ACT.IVE in the top tells of a start that did not end
 * cleanly; it stays there until a clean end (EndGeos) deletes it */
static void ActiveFile(void)
{
    FileHandle fh;

    FileGetAttributes(KERNEL_ACTIVE_FILE);
    if (ThreadGetError() == 0) {
	kernelSysConfig |= SCF_CRASHED;
	return;
    }
    fh = FileCreate(KERNEL_ACTIVE_FILE, FILE_CREATE_ONLY | FCF_NATIVE |
					FILE_ACCESS_RW, 0);
    if (fh)
	FileClose(fh, TRUE);
}

/* LoadDriver of bootInitfile.asm: a driver from a standard path */
static GeodeHandle LoadDriver(const char *name, StandardPath sp,
			      word major, word minor)
{
    GeodeLoadError e;
    GeodeHandle gh;

    FilePushDir();
    FileSetStandardPath(sp);
    gh = GeodeUseDriver(name, major, minor, &e);
    FilePopDir();
    return gh;
}

static void LoadKeyboardDriver(void)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    word n;
    GeodeHandle gh;

    if (InitFileReadStringBuffer("keyboard", "driver", name, sizeof(name),
				 &n))
	return;
    gh = LoadDriver(name, SP_KEYBOARD_DRIVERS, KEYBOARD_PROTO_MAJOR,
		    KEYBOARD_PROTO_MINOR);
    if (!gh)
	gh = LoadDriver("kbd.geo", SP_KEYBOARD_DRIVERS, KEYBOARD_PROTO_MAJOR,
			KEYBOARD_PROTO_MINOR);
    if (!gh)
	KernelFatal(CANNOT_LOAD_KEYBOARD_DRIVER);
    GeodeSetDefaultDriver(GDDT_KEYBOARD, gh);
}

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

    kernelBootThread = KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    FileSetTopHostPath(topHostPath);
    FileSetStandardPath(SP_TOP);
    ActiveFile();
    FSDLoadPrimary();
    FileInitStdPaths();
    KernelImInit();
    LoadKeyboardDriver();
    KernelImStart();
    LoadUI();
    gh = ProcessCommandLine(cmdTail, &e);
    if (err)
	*err = e;
    return gh;
}

word KernelRun(void)
{
    GeodeHandle gh;

    while (!KernelEnded()) {
	GeodeLock();
	gh = GeodeFindLow("", 0, GA_PROCESS, 0);
	GeodeUnlock();
	if (!gh)
	    break;
	TimerSleep(6);
    }
    return kernelExitFlags;
}
