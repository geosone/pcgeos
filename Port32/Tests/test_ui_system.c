/*
 * test_ui_system.c -- the system, screen and field classes of the UI
 * library (Library/User/Gen: GenSystemClass, GenScreenClass,
 * GenFieldClass, UserCallSystem) through the process geode uisys
 * (Geode/uisys.goc) and the specific UI library tspui (Geode/tspui.goc),
 * which [ui] specific names.
 *
 * The video driver is the memory video driver vidmem (the default video
 * driver); [ui] tvMode and xScreenSize change its display type and page.
 * The steps:
 *   1. the system object: usable, enabled, its specific UI;
 *   2. a screen: the system's screen child and default screen, its
 *      video driver and the UI's display type, built by a message;
 *   3. two fields attached: their options ([uiFeatures], [myField]), the
 *      system's first children, built with the system, the categories,
 *      MSG_GEN_SYSTEM_FOREACH_FIELD past another child;
 *   4. applications started on a field: its lists, the process now the
 *      UI's, moved, removed, exited; no focus in the field;
 *   5. MSG_GEN_CALL_SYSTEM, UserCallSystem and TO_SYS_FOCUS;
 *   6. the system detached: the fields detach (the specific UI's part,
 *      the system's exclusives, the applications), leave the system,
 *      notify; the caller acked;
 *   7. the specific parts shrunk, the system object gone, the specific
 *      UI freed.
 */
#include "kernelC.h"
#include <Internal/videoDr.h>
#include <initfile.h>

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], uisys_geode[], tspui_geode[],
			   vidmem_geode[];
extern const unsigned long ui_geode_size, uisys_geode_size,
			   tspui_geode_size, vidmem_geode_size;

/* as uisys.goc */
typedef struct {
    GeodeHandle	USS_process;
    word	USS_attached;
    word	USS_checks, USS_fails, USS_failLine;
    word	USS_acks;
    word	USS_fieldDetached;
    word	USS_noFocus;
    word	USS_shutdownFlags;
    word	USS_failLines[8];
} USState;

/* as tspui.goh */
typedef struct {
    WindowHandle	TSS_root;
    WindowHandle	TSS_win;
    word		TSS_builds[3];
    word		TSS_draws[3];
    word		TSS_greyDraws;
    word		TSS_monikers;
    word		TSS_buttons;
    word		TSS_sysBuilds[3];
    word		TSS_fieldAttaches;
    word		TSS_orders;
    word		TSS_aboutToDetach;
    word		TSS_fieldDetaches;
    word		TSS_sysExcls;
    word		TSS_busy, TSS_notBusy;
    word		TSS_sysFocusEvents;
} TSState;

static int fails;

static void Check(int ok, int line, const char *what)
{
    if (!ok) {
	fails++;
	printf("FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(c) Check(!!(c), __LINE__, #c)

#define WAIT_FOR(cond) \
    do { int n_; for (n_ = 0; n_ < 500 && !(cond); n_++) TimerSleep(1); \
    } while (0)

#define GEODE	0x2340

int main(void)
{
    GeodeHandle app, sp, vm;
    GeodeLoadError e;
    USState *st;
    TSState *ts;
    VideoDriverInfo *vdi;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uisys", uisys_geode, uisys_geode_size);
    GeodeRegisterImage("tspui", tspui_geode, tspui_geode_size);
    GeodeRegisterImage("vidmem", vidmem_geode, vidmem_geode_size);
    InitFileWriteString("ui", "specific", "tspui");
    InitFileWriteBoolean("ui", "tvMode", TRUE);
    InitFileWriteInteger("ui", "xScreenSize", 320);
    InitFileWriteInteger("uiFeatures", "launchModel", 1);	/* UILM_SINGLE_INSTANCE */
    InitFileWriteInteger("uiFeatures", "interfaceLevel", 2);	/* UIIL_INTERMEDIATE */
    InitFileWriteInteger("myField", "launchModel", 3);	/* UILM_GURU */
    InitFileWriteInteger("myField", "launchLevel", 1);	/* UIIL_BEGINNING */

    vm = GeodeUseDriver("vidmem", VIDEO_PROTO_MAJOR, VIDEO_PROTO_MINOR, &e);
    CHECK(vm != 0);
    if (!vm) {
	printf("GeodeUseDriver error %d\n", (int) e);
	return 1;
    }
    GeodeSetDefaultDriver(GDDT_VIDEO, vm);
    vdi = (VideoDriverInfo *) GeodeInfoDriver(vm);

    app = GeodeLoad("uisys", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(app != 0);
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(app, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->USS_attached);
    CHECK(st->USS_attached == 1 && st->USS_process == app);

    /* 1: the system object loads the specific UI */
    CHECK(!GeodeFind("tspui   ", GEODE_NAME_SIZE, 0, 0));
    step(1);
    sp = GeodeUseLibrary("tspui", 0, 0, NULL);
    CHECK(sp != 0);
    if (!sp)
	return 1;
    ts = ProcGetLibraryEntry(sp, 6);
    CHECK(!ts->TSS_sysBuilds[0] && !ts->TSS_sysBuilds[1] &&
	  !ts->TSS_sysBuilds[2]);

    /* 2: the screen; FakeScreen's page width */
    step(2);
    CHECK(ts->TSS_sysBuilds[1] == 1 && !ts->TSS_sysBuilds[2]);
    CHECK(vdi->VDI_pageW == 320);

    /* 3: the fields, built with the system: MSG_GEN_LOAD_OPTIONS builds
     * the first field, which the system (not built) shrinks when it adds
     * it; its attach builds it again, with the system */
    step(3);
    CHECK(ts->TSS_sysBuilds[0] == 3 && ts->TSS_sysBuilds[2] == 1);
    CHECK(ts->TSS_fieldAttaches == 3);

    /* 4: the applications; the field busy while they start */
    step(4);
    CHECK(ts->TSS_busy == 2 && ts->TSS_notBusy == 2);
    WAIT_FOR(st->USS_noFocus);
    CHECK(st->USS_noFocus == 1);

    /* 5: through the system */
    step(5);
    CHECK(ts->TSS_busy == 3 && ts->TSS_notBusy == 3);
    CHECK(ts->TSS_sysFocusEvents == 1);

    /* 6: detached */
    step(6);
    WAIT_FOR(st->USS_acks);
    CHECK(st->USS_acks == 1);
    CHECK(ts->TSS_fieldDetaches == 2 && ts->TSS_orders == 2 &&
	  ts->TSS_aboutToDetach == 2 && ts->TSS_sysExcls == 2);
    CHECK(st->USS_fieldDetached == 1 && !st->USS_shutdownFlags);

    /* 7: gone */
    step(7);
    GeodeFreeLibrary(sp);
    CHECK(!GeodeFind("tspui   ", GEODE_NAME_SIZE, 0, 0));

    printf("geode checks: %u, failed %u (first at uisys.goc line %u)\n",
	   st->USS_checks, st->USS_fails, st->USS_failLine);
    {
	int i;

	for (i = 0; i < st->USS_fails && i < 8; i++)
	    printf("  failed: uisys.goc line %u\n", st->USS_failLines[i]);
    }
    CHECK(st->USS_checks >= 50 && !st->USS_fails);

    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("uisys   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("uisys   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0));
    GeodeFreeDriver(vm);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
