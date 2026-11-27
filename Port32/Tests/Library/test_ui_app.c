/*
 * test_ui_app.c -- the UI as a process and the life of an application
 * (Library/User: UserClass, GenProcessClass, GenApplicationClass,
 * UserLoadApplication): the test boots the UI as the kernel does
 * (BootLoadUI); its process starts the system with the specific UI
 * tspui (Geode/tspui.goc), a screen on the memory video driver vidmem
 * whose root window is the test's, its own application and field, the
 * system field, and the application uiapp (Geode/uiapp.goc, a
 * GenProcessClass process) of "[ui] execOnStartup".  The process geode
 * uahost (Geode/uahost.goc) checks the system and launches uiapp
 * through the UI; uiapp reports the steps of its process to uahost.
 *
 * The steps:
 *   - booted: the flow object, the specific UI, the system with a
 *     screen (registered by the specific UI: the pointer on its root
 *     window), the UI's own field and application, the system field,
 *     the mouse driver; uiapp of execOnStartup on the default field,
 *     the UI its parent;
 *   1. the system checked; uiapp quit by the user: it exits, the UI
 *      hears of it and tells the field and GCNSLT_APPLICATION;
 *   2. uiapp launched through the UI with an acknowledgement;
 *   3. it is up (the field's lists, its state, the acknowledgement);
 *      quit by the user: the quit levels, the detach, the close, the
 *      shutdown, the final detach; it exits;
 *   4. launched again, not on top;
 *   5. up; the UI's process detached: the system detaches the fields
 *      (uiapp's process detached, not quit), then the UI as an
 *      application; the UI lets go of the system and ends its process,
 *      the caller acked;
 *   6. the library stays without its process and system.
 */
#include "kernelC.h"
#include <Internal/videoDr.h>
#include <Internal/im.h>
#include <geosdriver.h>
#include <initfile.h>
#include <sysstats.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern const unsigned char ui_geode[], uahost_geode[], uiapp_geode[],
			   tspui_geode[], vidmem_geode[];
extern const unsigned long ui_geode_size, uahost_geode_size,
			   uiapp_geode_size, tspui_geode_size,
			   vidmem_geode_size;

/* as uiapp.goh */
enum {
    UAE_ATTACH, UAE_OPEN_APP, UAE_ACTIVE_ATTACH, UAE_ACTIVE_DETACH,
    UAE_STARTUP_ATTACH, UAE_STARTUP_SHUTDOWN, UAE_DETACH, UAE_CLOSE_APP,
    UAE_REAL_DETACH, UAE_FINAL_DETACH, UAE_QUIT, UAE_QUIT_ACK, UAE_COUNT
};

/* as uahost.goc */
typedef struct {
    GeodeHandle	UAH_process;
    word	UAH_attached;
    word	UAH_checks, UAH_fails, UAH_failLine;
    word	UAH_events[UAE_COUNT];
    word	UAH_lastData[UAE_COUNT];
    word	UAH_loads;
    GeodeHandle	UAH_loaded;
    word	UAH_loadError, UAH_loadID;
    GeodeHandle	UAH_app;
    word	UAH_acks;
    word	UAH_started, UAH_exited;
    GeodeHandle	UAH_lastExited;
    word	UAH_failLines[8];
    word	UAH_iacp[6];
} UAHostState;

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
    word		TSS_appStartups;
    word		TSS_appAttaches;
    word		TSS_appOpenCompletes;
    word		TSS_appToTop;
    word		TSS_appBusy, TSS_appNotBusy;
    word		TSS_appDetachPending;
    word		TSS_appCloseCompletes;
    word		TSS_appShutdowns;
    word		TSS_appIgnoreInput;
    word		TSS_screenBuilds;
    word		TSS_ptrShows, TSS_ptrHides;
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

static UAHostState *st;

static void Events(void)
{
    int i;

    printf("events:");
    for (i = 0; i < UAE_COUNT; i++)
	printf(" %u", st->UAH_events[i]);
    printf("\n");
}

static GeodeHandle Find(const char *name)
{
    return GeodeFind(name, GEODE_NAME_SIZE, 0, 0);
}

int main(void)
{
    GeodeHandle host, sp, ui;
    GeodeLoadError e;
    TSState *ts;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uahost", uahost_geode, uahost_geode_size);
    GeodeRegisterImage("uiapp", uiapp_geode, uiapp_geode_size);
    GeodeRegisterImage("tspui", tspui_geode, tspui_geode_size);
    GeodeRegisterImage("vidmem", vidmem_geode, vidmem_geode_size);
    InitFileWriteString("ui", "generic", "ui");
    InitFileWriteString("ui", "specific", "tspui");
    InitFileWriteString("ui", "execOnStartup", "uiapp");
    InitFileWriteString("screen 0", "driver", "vidmem");
    InitFileWriteString("screen 0", "device", "memory");
    /* no file system here: no clipboard file */
    InitFileWriteBoolean("ui", "noClipboard", TRUE);

    /* tspui has the UI as a library: a library only, its process ends */
    sp = GeodeUseLibrary("tspui", 0, 0, NULL);
    CHECK(sp != 0);
    if (!sp)
	return 1;
    WAIT_FOR(!(GeodeGetInfo(Find("ui      "), GGIT_ATTRIBUTES, NULL) &
	       GA_PROCESS));
    CHECK(!(GeodeGetInfo(Find("ui      "), GGIT_ATTRIBUTES, NULL) &
	    GA_PROCESS));
    CHECK(!SysGetInfo(SGIT_UI_PROCESS));
    GeodeFreeLibrary(sp);
    CHECK(!Find("ui      ") && !Find("tspui   "));

    /* booted: the system started, uiapp of execOnStartup up */
    BootLoadUI();
    ui = (GeodeHandle) SysGetInfo(SGIT_UI_PROCESS);
    CHECK(ui != 0 && ui == Find("ui      "));
    WAIT_FOR(Find("tspui   "));
    sp = GeodeUseLibrary("tspui", 0, 0, NULL);
    ts = ProcGetLibraryEntry(sp, 6);
    WAIT_FOR(Find("uiapp   ") && ts->TSS_appOpenCompletes == 2 &&
	     ts->TSS_appNotBusy == 2);
    printf("booted: screens %u, opens %u\n", ts->TSS_screenBuilds,
	   ts->TSS_appOpenCompletes);
    CHECK(ts->TSS_screenBuilds == 1);
    CHECK(ts->TSS_sysBuilds[2] >= 1);
    CHECK(GeodeGetDefaultDriver(GDDT_VIDEO) == Find("vidmem  "));
    /* the screen's root window, the pointer hidden, then shown by the
     * flow object */
    CHECK(ts->TSS_root != 0);
    CHECK(ts->TSS_ptrHides >= 1 && ts->TSS_ptrShows >= 1);
    CHECK(Find("uiapp   ") != 0);
    /* the UI's application and uiapp: started, attached, opened */
    CHECK(ts->TSS_appStartups == 2 && ts->TSS_appAttaches == 2);
    CHECK(ts->TSS_appToTop == 2);

    host = GeodeLoad("uahost", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(host != 0);
    if (!host) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(host, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(host, 1);
    WAIT_FOR(st->UAH_attached);

    /* 1: the system; uiapp quit: the UI hears of the exit */
    step(1);
    WAIT_FOR(!Find("uiapp   ") && st->UAH_exited);
    CHECK(!Find("uiapp   "));
    CHECK(st->UAH_exited == 1 && st->UAH_lastExited != 0);
    CHECK(st->UAH_events[UAE_ATTACH] == 0);	/* not ours to hear */

    /* 2, 3: through the UI; up, acknowledged, quit */
    step(2);
    WAIT_FOR(ts->TSS_appOpenCompletes == 3 && ts->TSS_appNotBusy == 3 &&
	     st->UAH_loads && st->UAH_events[UAE_ACTIVE_ATTACH]);
    CHECK(st->UAH_events[UAE_ATTACH] == 1 && st->UAH_events[UAE_OPEN_APP] == 1);
    CHECK(st->UAH_events[UAE_STARTUP_ATTACH] == 1 &&
	  st->UAH_events[UAE_ACTIVE_ATTACH] == 1);
    CHECK(ts->TSS_appToTop == 3);
    CHECK(st->UAH_started >= 1);
    step(3);
    WAIT_FOR(!Find("uiapp   ") && st->UAH_exited == 2);
    CHECK(!Find("uiapp   ") && st->UAH_exited == 2);
    CHECK(st->UAH_lastExited == st->UAH_app);
    Events();
    CHECK(st->UAH_events[UAE_QUIT] == 5 && st->UAH_events[UAE_QUIT_ACK] == 5);
    CHECK(st->UAH_events[UAE_DETACH] == 1 &&
	  st->UAH_events[UAE_ACTIVE_DETACH] == 1);
    CHECK(st->UAH_events[UAE_CLOSE_APP] == 1 &&
	  st->UAH_events[UAE_REAL_DETACH] == 1 &&
	  st->UAH_events[UAE_FINAL_DETACH] == 1);
    CHECK(st->UAH_events[UAE_STARTUP_SHUTDOWN] == 1);

    /* 7 to 9: launched by IACP for a connection to its token; a
     * message over the connection, the client leaves, quit */
    step(7);
    WAIT_FOR(ts->TSS_appOpenCompletes == 4 && ts->TSS_appNotBusy == 4);
    CHECK(ts->TSS_appOpenCompletes == 4);
    /* (it has no host to report its steps to: the launch block is
     * IACP's) */
    CHECK(st->UAH_events[UAE_OPEN_APP] == 1);
    step(8);
    WAIT_FOR(st->UAH_iacp[4]);
    /* (the application hears that we left, and leaves too) */
    { int k; for (k = 0; k < 30; k++) TimerSleep(1); }
    step(9);
    WAIT_FOR(!Find("uiapp   ") && st->UAH_exited == 3);
    CHECK(!Find("uiapp   ") && st->UAH_exited == 3);
    printf("IACP: connection %u, servers %u, the application's %u (%u in "
	   "application mode), completed %u, left %u\n", st->UAH_iacp[0],
	   st->UAH_iacp[1], st->UAH_iacp[2], st->UAH_iacp[3],
	   st->UAH_iacp[4], st->UAH_iacp[5]);

    /* 4, 5: again, not on top; the UI detached */
    step(4);
    WAIT_FOR(ts->TSS_appOpenCompletes == 5 && ts->TSS_appNotBusy == 5 &&
	     st->UAH_events[UAE_OPEN_APP] == 2);
    CHECK(ts->TSS_appToTop == 4);
    CHECK(st->UAH_events[UAE_OPEN_APP] == 2);
    step(5);
    WAIT_FOR(st->UAH_acks && !(GeodeGetInfo(ui, GGIT_ATTRIBUTES, NULL) &
			       GA_PROCESS));
    CHECK(st->UAH_acks == 1);
    CHECK(!Find("uiapp   ") && st->UAH_exited == 4);
    CHECK(!(GeodeGetInfo(ui, GGIT_ATTRIBUTES, NULL) & GA_PROCESS));
    Events();
    /* uiapp detached, not quit */
    CHECK(st->UAH_events[UAE_QUIT] == 5 && st->UAH_events[UAE_DETACH] == 2);
    CHECK(st->UAH_events[UAE_CLOSE_APP] == 2 &&
	  st->UAH_events[UAE_FINAL_DETACH] == 2);
    /* the UI's application and the four uiapps shut down */
    CHECK(ts->TSS_appShutdowns == 5);
    CHECK(ts->TSS_fieldDetaches == 2);

    /* 6: the library stays */
    step(6);
    printf("geode checks: %u, failed %u (first at uahost.goc line %u)\n",
	   st->UAH_checks, st->UAH_fails, st->UAH_failLine);
    {
	int i;

	for (i = 0; i < st->UAH_fails && i < 8; i++)
	    printf("  failed: uahost.goc line %u\n", st->UAH_failLines[i]);
    }
    CHECK(st->UAH_checks >= 40 && !st->UAH_fails);

    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(host, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!Find("uahost  "));
    CHECK(!Find("uahost  "));
    GeodeFreeLibrary(sp);
    CHECK(!Find("ui      ") && !Find("tspui   "));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
