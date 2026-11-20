/*
 * test_ui_gen.c -- the generic classes of the UI library (Library/User/Gen:
 * GenClass, the generic tree, GenApplicationClass) built by a specific
 * UI, through the process geode uigen (Geode/uigen.goc) and the specific
 * UI library tspui (Geode/tspui.goc), which [ui] specific names.
 *
 * The root window is on a fake video driver that records the rectangles
 * (DR_VID_RECT).  The steps:
 *   1. the generic tree without specific parts: parents and children,
 *      usable and enabled, monikers, size hints, the keyboard
 *      accelerator's search path, upward queries and classed events (one
 *      to the display itself resolves its variant: the specific UI is
 *      loaded, the application and the display grown);
 *   2. the display made usable: the specific UI loaded, the
 *      application, the display and its triggers built into its classes,
 *      the window opened and the triggers drawn (the one not enabled
 *      grey);
 *   3. the trigger enabled: drawn again, black;
 *   4. the display not enabled and enabled again, a new moniker;
 *   5. a trigger added and made usable: built and drawn;
 *   6. not usable again: unbuilt;
 *   7. the display not usable: the window closed;
 *   8. the specific parts shrunk, the specific UI freed.
 */
#include "kernelC.h"
#include "Win/winInt.h"
#include <Internal/videoDr.h>
#include <initfile.h>

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], uigen_geode[], tspui_geode[];
extern const unsigned long ui_geode_size, uigen_geode_size,
			   tspui_geode_size;

/* as uigen.goc */
typedef struct {
    GeodeHandle	UGS_process;
    word	UGS_attached;
    word	UGS_checks, UGS_fails, UGS_failLine;
    word	UGS_pings;
    word	UGS_failLines[8];
} UGState;

/* as tspui.goh (the part this test uses) */
typedef struct {
    WindowHandle	TSS_root;
    WindowHandle	TSS_win;
    word		TSS_builds[3];
    word		TSS_draws[3];
    word		TSS_greyDraws;
    word		TSS_monikers;
    word		TSS_buttons;
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

#define MAX_RECTS 64
static struct { Rectangle r; dword color; } rects[MAX_RECTS];
static int nRects;

static Boolean Strategy(word function, DriverRegs *r)
{
    if (function == DR_VID_RECT) {
	GState *gs = r->DR_dsSi;
	const CommonAttr *ca = (const CommonAttr *) ((byte *) gs + r->DR_si);
	if (nRects < MAX_RECTS) {
	    rects[nRects].r.R_left = (sword) r->DR_ax;
	    rects[nRects].r.R_top = (sword) r->DR_bx;
	    rects[nRects].r.R_right = (sword) r->DR_cx;
	    rects[nRects].r.R_bottom = (sword) r->DR_dx;
	    rects[nRects].color = ca->CA_colorIndex;
	}
	nRects++;
	return FALSE;
    }
    if (function == DR_VID_CHECK_UNDER || function == DR_VID_INFO_UNDER)
	r->DR_ax = r->DR_di = 0;
    if (function == DR_VID_MOVEPTR)
	r->DR_ax = 0;
    return function == DR_VID_SAVE_UNDER;
}

static int Count(dword color)
{
    int i, n = 0;

    for (i = 0; i < nRects && i < MAX_RECTS; i++)
	if (rects[i].color == color)
	    n++;
    return n;
}

/* the last one of the colour within (l, t, r, b) */
static int Within(dword color, sword l, sword t, sword r, sword b)
{
    int i;

    for (i = (nRects < MAX_RECTS ? nRects : MAX_RECTS) - 1; i >= 0; i--)
	if (rects[i].color == color)
	    return rects[i].r.R_left >= l && rects[i].r.R_top >= t &&
		   rects[i].r.R_right <= r && rects[i].r.R_bottom <= b;
    return 0;
}

#define GEODE	0x2340

int main(void)
{
    GeodeHandle app, sp;
    GeodeLoadError e;
    WindowHandle root;
    UGState *st;
    TSState *ts;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uigen", uigen_geode, uigen_geode_size);
    GeodeRegisterImage("tspui", tspui_geode, tspui_geode_size);
    InitFileWriteString("ui", "specific", "tspui");

    root = WinOpen(0, 0, 0, WCF_PLAIN | CMT_CLOSEST, C_DARK_GRAY, 0, 0,
		   WPF_ROOT, 0, 0, NULL, 0, 0, 639, 479);
    WinSetInfo(root, WIT_STRATEGY, (optr) (uintptr_t) (void *) Strategy);

    app = GeodeLoad("uigen", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(app != 0);
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(app, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->UGS_attached);
    CHECK(st->UGS_attached == 1 && st->UGS_process == app);

    /* 1: the specific UI loaded only by the classed event to the display */
    CHECK(!GeodeFind("tspui   ", GEODE_NAME_SIZE, 0, 0));
    step(1);
    CHECK(GeodeFind("tspui   ", GEODE_NAME_SIZE, 0, 0) != 0);

    /* 2: built and opened (the window at 20, 20; the triggers 50 apart) */
    sp = GeodeUseLibrary("tspui", 0, 0, NULL);
    CHECK(sp != 0);
    if (!sp)
	return 1;
    ts = ProcGetLibraryEntry(sp, 6);
    ts->TSS_root = root;
    nRects = 0;
    step(2);
    CHECK(ts->TSS_builds[2] == 1 && ts->TSS_builds[1] == 2 &&
	  ts->TSS_builds[0] == 2 && ts->TSS_buttons == 2);
    CHECK(ts->TSS_win != 0);
    WAIT_FOR(ts->TSS_draws[0] && ts->TSS_draws[1]);
    CHECK(ts->TSS_draws[0] >= 1 && ts->TSS_draws[1] >= 1);
    CHECK(ts->TSS_greyDraws >= 1);
    CHECK(Within(C_LIGHT_GRAY, 30, 30, 70, 50));
    CHECK(Within(C_BLACK, 80, 30, 120, 50));

    /* 3: the first trigger black */
    nRects = 0;
    step(3);
    WAIT_FOR(Count(C_BLACK));
    CHECK(Within(C_BLACK, 30, 30, 70, 50));

    /* 4: grey and black again; the moniker told */
    nRects = 0;
    step(4);
    WAIT_FOR(Count(C_BLACK) >= 2);
    CHECK(Count(C_LIGHT_GRAY) >= 2 && Count(C_BLACK) >= 2);
    CHECK(ts->TSS_monikers >= 1);

    /* 5: the third trigger at 120, 10 in the window */
    nRects = 0;
    step(5);
    CHECK(ts->TSS_builds[0] == 3 && ts->TSS_buttons == 3);
    WAIT_FOR(ts->TSS_draws[2]);
    CHECK(ts->TSS_draws[2] >= 1 && Within(C_BLACK, 130, 30, 170, 50));

    /* 6, 7: unbuilt, closed: the root washed where the window was */
    step(6);
    nRects = 0;
    step(7);
    WAIT_FOR(Count(C_DARK_GRAY));
    CHECK(!ts->TSS_win && Count(C_DARK_GRAY) >= 1);

    /* 9: GenInteraction, GenDisplay, GenPrimary; the size hints */
    step(9);

    /* 10: GenView and GenContent, generic data, linked views */
    step(10);

    /* 8: the UI's use of the specific UI ends; ours too */
    step(8);
    GeodeFreeLibrary(sp);
    CHECK(!GeodeFind("tspui   ", GEODE_NAME_SIZE, 0, 0));

    printf("geode checks: %u, failed %u (first at uigen.goc line %u)\n",
	   st->UGS_checks, st->UGS_fails, st->UGS_failLine);
    {
	int i;

	for (i = 0; i < st->UGS_fails && i < 8; i++)
	    printf("  failed: uigen.goc line %u\n", st->UGS_failLines[i]);
    }
    CHECK(st->UGS_checks >= 70 && !st->UGS_fails);

    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("uigen   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("uigen   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0));
    WinClose(root);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
