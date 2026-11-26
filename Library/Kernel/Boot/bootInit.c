/*
 * bootInit.c -- starting the kernel (after InitGeos and
 * ProcessCommandLine of bootInit.asm, LoadUI of bootInitfile.asm; the
 * drivers of bootInitfile.asm are in bootInitfile.c).
 *
 * The program that runs the kernel (Loader/main.c, as the loader of DOS16
 * GEOS) calls KernelBoot with the host directory of the GEOS top and the
 * command tail.  KernelBoot does what InitGeos did, as far as the C
 * kernel goes:
 *
 *   1. the first thread and the timer (InitThread, InitTimer);
 *      [system] hostRole: guest (default on linux) or session (the
 *      default on DOS, where GEOS is the system), where GEOS is the
 *      desktop and manages the host through GAPI (the clock, ...): the
 *      host may then ask the user for a password, and the log goes to
 *      the host's system log (the journal);
 *   2. the top directory as drive C (the loader's top level path) and
 *      the initial path SP_TOP (FileSetInitialPath); GEOS_ACT.IVE in
 *      the top, made here and deleted by a clean end: if it is there
 *      already, the last run did not end cleanly (SCF_CRASHED, InitSys);
 *   3. the primary file system driver and its drives (LoadFSDriver,
 *      FSDLoadPrimary); the init file is read on first use;
 *   4. the standard paths of [paths] (GetPaths of the loader); the
 *      power management driver of [system] power, from
 *      SP_POWER_DRIVERS (LoadPowerDriver), the default
 *      GDDT_POWER_MANAGEMENT driver; device power notifications are
 *      ignored unless it hooked them; the
 *      fonts of SP_FONT, the font drivers of [system] font and the
 *      default font (GrInitFonts, GrLoadFontDrivers,
 *      GrInitDefaultFont); the memory video driver vidmem.geo of
 *      SP_VIDEO_DRIVERS (LoadMemVid), unless [system] noVidMem;
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
 * (CANNOT_LOAD_UI).  Without the key, no UI is loaded yet.  The UI's
 * handle (SGIT_UI_PROCESS) is set before its process runs, so that the
 * UI knows at its attach that it is the system's UI (BootLoadUI).
 *
 * The keyboard driver: "[keyboard] driver" names it, as in DOS16; if it
 * does not load, kbd.geo is tried and its failure is fatal
 * (CANNOT_LOAD_KEYBOARD_DRIVER).  Without the key, none is loaded yet:
 * a host without keys (the tests) needs none.
 *
 * The power driver: without "[system] power" there is none (DOS16
 * looked for a Poqet then); one that does not load is not fatal, as in
 * DOS16.  On a host, the host power driver hostpwr.geo.
 *
 * The memory drivers of "[system] memory" (EMS, XMS, extended memory)
 * have no place in Port32: the host gives the memory.  The mouse and
 * the screen video driver are the UI's to load, as in DOS16.
 *
 * Not yet: patches, the log file, documentIsTop, FSDInitComplete.
 */
#include "kernelC.h"
#include "gapi.h"

#include <initfile.h>
#include <system.h>
#include <driver.h>
#include <Internal/kbdDr.h>
#include <Internal/videoDr.h>
#include <Internal/powerDr.h>
#include <string.h>

GeodeHandle kernelUIHandle;
Boolean kernelLoadingUI;
ThreadHandle kernelBootThread;

/* InitSys: GEOS_ACT.IVE in the top tells of a start that did not end
 * cleanly; it stays there until a clean end (EndGeos) deletes it */
static void ActiveFile(void)
{
    FileHandle fh;

    FileGetAttributes(KERNEL_ACTIVE_FILE);
    if (ThreadGetError() == 0) {
	sysConfig |= SCF_CRASHED;
	return;
    }
    fh = FileCreate(KERNEL_ACTIVE_FILE, FILE_CREATE_ONLY | FCF_NATIVE |
					FILE_ACCESS_RW, 0);
    if (fh)
	FileClose(fh, TRUE);
}

/* LoadMemVid: the memory video driver, which draws into bitmaps
 * (GrCreateBitmap), unless "[system] noVidMem = true".  DOS16 found it
 * fatal when it was missing (KR-25); here a GEOS without it (the tests)
 * has no bitmaps. */
static void LoadMemVid(void)
{
    Boolean no = FALSE;
    GeodeHandle gh;

    if (!InitFileReadBoolean("system", "noVidMem", &no) && no)
	return;
    gh = BootLoadDriver("vidmem.geo", SP_VIDEO_DRIVERS, VIDEO_PROTO_MAJOR,
		    VIDEO_PROTO_MINOR);
    if (gh)
	GeodeSetDefaultDriver(GDDT_MEMORY_VIDEO, gh);
}

/* LoadPowerDriver */
static void LoadPowerDriver(void)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    word n;
    GeodeHandle gh;

    if (!InitFileReadStringBuffer("system", "power", name, sizeof(name),
				  &n)) {
	gh = BootLoadDriver(name, SP_POWER_DRIVERS, POWER_PROTO_MAJOR,
			POWER_PROTO_MINOR);
	if (gh)
	    GeodeSetDefaultDriver(GDDT_POWER_MANAGEMENT, gh);
    }
    /* a driver that wants them has hooked them */
    (void) SysIgnoreNotification(SST_DEVICE_POWER);
}

/* the geode loader sets kernelUIHandle to the first process it starts
 * while kernelLoadingUI is set: the UI's (it imports no library with a
 * process), before its thread runs */
static GeodeHandle LoadUIGeode(const char *name)
{
    GeodeLoadError e;
    GeodeHandle gh;

    kernelUIHandle = 0;
    kernelLoadingUI = TRUE;
    gh = GeodeLoad(name, 0, 0, 0, 0, &e);
    kernelLoadingUI = FALSE;
    kernelUIHandle = gh;
    return gh;
}

void BootLoadUI(void)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    word n;

    FileSetStandardPath(SP_SYSTEM);
    if (InitFileReadStringBuffer("ui", "generic", name, sizeof(name), &n))
	return;
    if (!LoadUIGeode(name) && !LoadUIGeode("ui.geo"))
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
    LoadPowerDriver();
    LocalInit();
    /* [system] hostRole: "guest" (default) leaves the host alone;
     * "session" (GEOS is the user's desktop) manages it through GAPI */
    {
	char role[16];
	word n;
	if (InitFileReadStringBuffer("system", "hostRole", role,
				     sizeof(role), &n))
	    kernelHostSession = (Boolean) gapi_host_is_geos();
	else
	    kernelHostSession = !strcmp(role, "session");
	if (kernelHostSession) {
	    /* the host may ask the user (polkit); the log goes to the
	     * host's system log (the journal) */
	    gapi_set_interactive(TRUE);
	    (void) gapi_log_to_system();
	}
    }
    GrInitFonts();
    GrLoadFontDrivers();
    GrInitDefaultFont();
    LoadMemVid();
    KernelImInit();
    BootLoadKeyboardDriver();
    KernelImStart();
    BootLoadUI();
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
