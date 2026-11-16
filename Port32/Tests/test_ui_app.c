/*
 * test_ui_app.c -- the life of an application (Library/User:
 * GenProcessClass, GenApplicationClass): the process geode uahost
 * (Geode/uahost.goc) makes the UI's system object, a screen and a field
 * and launches the application uiapp (Geode/uiapp.goc, a GenProcessClass
 * process) on the field; the specific UI tspui (Geode/tspui.goc) builds
 * them and counts what the generic application tells it; uiapp reports
 * the steps of its process to uahost.  The kernel tells uahost, the
 * parent process, when uiapp exits.
 *
 * The steps:
 *   1. the system, the screen and the field;
 *   2. uiapp launched: the process attaches, the application starts up
 *      on the field, opens in application mode (usable, on the field's
 *      lists, its startup and active lists told, brought to the top);
 *   3. it is up: the field's lists, its state;
 *   4. the user quits it: the quit levels, the detach (the active list),
 *      the close, the shutdown (the startup list, off the field), the
 *      final detach; the process exits, uahost hears of it;
 *   5. off the field; launched again, not on top;
 *   6. up again; the system detaches: the field detaches the process
 *      (not a quit), which exits, and the field and system detach;
 *   7. the specific parts shrunk, the system object gone, the specific
 *      UI freed.
 */
#include "kernelC.h"
#include <Internal/videoDr.h>
#include <initfile.h>

#include <stdio.h>
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
    word	UAH_exits;
    GeodeHandle	UAH_app;
    word	UAH_acks;
    word	UAH_fieldDetached;
    word	UAH_failLines[8];
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

int main(void)
{
    GeodeHandle host, sp, vm;
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
    InitFileWriteString("ui", "specific", "tspui");

    vm = GeodeUseDriver("vidmem", VIDEO_PROTO_MAJOR, VIDEO_PROTO_MINOR, &e);
    CHECK(vm != 0);
    if (!vm) {
	printf("GeodeUseDriver error %d\n", (int) e);
	return 1;
    }
    GeodeSetDefaultDriver(GDDT_VIDEO, vm);

    host = GeodeLoad("uahost", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(host != 0);
    if (!host) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(host, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(host, 1);
    WAIT_FOR(st->UAH_attached);
    CHECK(st->UAH_attached == 1 && st->UAH_process == host);

    /* 1: the system, screen and field */
    step(1);
    sp = GeodeUseLibrary("tspui", 0, 0, NULL);
    CHECK(sp != 0);
    if (!sp)
	return 1;
    ts = ProcGetLibraryEntry(sp, 6);
    CHECK(ts->TSS_fieldAttaches == 1);

    /* 2: launched; up once the open is complete and the busy state is
     * through the queue */
    step(2);
    CHECK(st->UAH_app != 0);
    WAIT_FOR(ts->TSS_appOpenCompletes && ts->TSS_appNotBusy);
    CHECK(st->UAH_events[UAE_ATTACH] == 1 && st->UAH_events[UAE_OPEN_APP] == 1);
    CHECK(st->UAH_lastData[UAE_OPEN_APP] == 0);		/* AppAttachFlags */
    CHECK(st->UAH_events[UAE_STARTUP_ATTACH] == 1 &&
	  st->UAH_lastData[UAE_STARTUP_ATTACH] == 1);	/* with the ALB */
    CHECK(st->UAH_events[UAE_ACTIVE_ATTACH] == 1);
    /* built at the startup, shrunk and built again by the attach */
    CHECK(ts->TSS_builds[2] == 2 && ts->TSS_appStartups == 1 &&
	  ts->TSS_appAttaches == 1);
    CHECK(ts->TSS_appOpenCompletes == 1 && ts->TSS_appToTop == 1);
    CHECK(ts->TSS_appBusy == 1 && ts->TSS_appNotBusy == 1);
    CHECK(ts->TSS_busy == ts->TSS_notBusy && ts->TSS_busy >= 3);

    /* 3: up */
    step(3);

    /* 4: quit: the process goes, the host hears of it */
    step(4);
    WAIT_FOR(!GeodeFind("uiapp   ", GEODE_NAME_SIZE, 0, 0) && st->UAH_exits);
    CHECK(!GeodeFind("uiapp   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(st->UAH_exits == 1);
    Events();
    /* QL_BEFORE_UI .. QL_AFTER_DETACH, each acked */
    CHECK(st->UAH_events[UAE_QUIT] == 5 && st->UAH_events[UAE_QUIT_ACK] == 5);
    CHECK(st->UAH_events[UAE_DETACH] == 1 &&
	  st->UAH_events[UAE_ACTIVE_DETACH] == 1);
    CHECK(st->UAH_events[UAE_CLOSE_APP] == 1 &&
	  st->UAH_events[UAE_REAL_DETACH] == 1 &&
	  st->UAH_events[UAE_FINAL_DETACH] == 1);
    CHECK(st->UAH_events[UAE_STARTUP_SHUTDOWN] == 1);
    CHECK(ts->TSS_appIgnoreInput == 1 && ts->TSS_appDetachPending == 1 &&
	  ts->TSS_appCloseCompletes == 1 && ts->TSS_appShutdowns == 1);

    /* 5, 6: again, not on top; detached with the system */
    step(5);
    WAIT_FOR(ts->TSS_appOpenCompletes == 2 && ts->TSS_appNotBusy == 2);
    CHECK(ts->TSS_appOpenCompletes == 2 && ts->TSS_appToTop == 1);
    CHECK(st->UAH_events[UAE_OPEN_APP] == 2);
    step(6);
    WAIT_FOR(st->UAH_acks && st->UAH_exits == 2);
    CHECK(st->UAH_acks == 1 && st->UAH_exits == 2);
    CHECK(st->UAH_fieldDetached == 1);
    CHECK(!GeodeFind("uiapp   ", GEODE_NAME_SIZE, 0, 0));
    Events();
    /* a detach, not a quit */
    CHECK(st->UAH_events[UAE_QUIT] == 5 && st->UAH_events[UAE_DETACH] == 2);
    CHECK(st->UAH_events[UAE_ACTIVE_DETACH] == 2 &&
	  st->UAH_events[UAE_CLOSE_APP] == 2 &&
	  st->UAH_events[UAE_FINAL_DETACH] == 2);
    CHECK(ts->TSS_appIgnoreInput == 1 && ts->TSS_appShutdowns == 2);

    /* 7: gone */
    step(7);
    GeodeFreeLibrary(sp);
    CHECK(!GeodeFind("tspui   ", GEODE_NAME_SIZE, 0, 0));

    printf("geode checks: %u, failed %u (first at uahost.goc line %u)\n",
	   st->UAH_checks, st->UAH_fails, st->UAH_failLine);
    {
	int i;

	for (i = 0; i < st->UAH_fails && i < 8; i++)
	    printf("  failed: uahost.goc line %u\n", st->UAH_failLines[i]);
    }
    CHECK(st->UAH_checks >= 25 && !st->UAH_fails);

    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(host, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("uahost  ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("uahost  ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0));
    GeodeFreeDriver(vm);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
