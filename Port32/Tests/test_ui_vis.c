/*
 * test_ui_vis.c -- the visual classes of the UI library (Library/User/Vis:
 * VisClass, VisCompClass) in a window, through the process geode uivis
 * (Geode/uivis.goc).
 *
 * The root window is on a fake video driver that records the rectangles
 * (DR_VID_RECT).  The process's win group opens a window on it; the steps:
 *   1. the tree (parents, children, queries up the tree, a classed
 *      event), made visible: opened, realized, exposed and drawn;
 *   2. one child's image invalid: only it is drawn again, at its place in
 *      the window, in its colour;
 *   3. mouse events to the child under the point, the gadget exclusive;
 *   4. a child removed: closed, its area washed with the window's colour;
 *   5. added again: opened by the update and drawn;
 *   6. not visible: the branch closed, the window gone.
 */
#include "kernelC.h"
#include "Win/winInt.h"
#include "IM/imInt.h"
#include <Internal/videoDr.h>

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], uivis_geode[];
extern const unsigned long ui_geode_size, uivis_geode_size;

/* as uivis.goc */
typedef struct {
    WindowHandle	UVS_root;
    GeodeHandle		UVS_process;
    word		UVS_attached;
    word		UVS_checks, UVS_fails, UVS_failLine;
    word		UVS_drawsA, UVS_drawsB;
    word		UVS_selectsA, UVS_selectsB;
    word		UVS_lostA, UVS_lostB;
    word		UVS_pings;
    WindowHandle	UVS_win;
} UVState;

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

/* the fake driver: the rectangles drawn */
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

/* rectangles of the colour drawn since the last reset */
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
    ThreadHandle me;
    GeodeHandle app;
    GeodeLoadError e;
    WindowHandle root;
    UVState *st;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    word last;

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    me = KernelThreadInit(PRIORITY_STANDARD);
    (void) me;
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uivis", uivis_geode, uivis_geode_size);

    root = WinOpen(0, 0, 0, WCF_PLAIN | CMT_CLOSEST, C_DARK_GRAY, 0, 0,
		   WPF_ROOT, 0, 0, NULL, 0, 0, 639, 479);
    WinSetInfo(root, WIT_STRATEGY, (optr) (uintptr_t) (void *) Strategy);

    app = GeodeLoad("uivis", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(app != 0);
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(app, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->UVS_attached);
    CHECK(st->UVS_attached == 1 && st->UVS_process == app);
    st->UVS_root = root;

    /* 1: opened and drawn (the window at 10, 10; A at 5, 5 in it) */
    nRects = 0;
    step(1);
    CHECK(st->UVS_win != 0 && st->UVS_pings == 1);
    WAIT_FOR(st->UVS_drawsA && st->UVS_drawsB);
    CHECK(st->UVS_drawsA == 1 && st->UVS_drawsB == 1);
    CHECK(Count(C_WHITE) >= 1);
    CHECK(Within(C_RED, 15, 15, 65, 55));
    CHECK(Within(C_BLUE, 110, 30, 160, 70));

    /* 2: A again, not B */
    nRects = 0;
    step(2);
    WAIT_FOR(st->UVS_drawsA == 2);
    TimerSleep(5);
    CHECK(st->UVS_drawsA == 2 && st->UVS_drawsB == 1);
    CHECK(Count(C_RED) == 1 && !Count(C_BLUE));

    /* 3: mouse and gadget exclusive (checked in the geode) */
    step(3);

    /* 4: B removed: its area washed white, nothing drawn */
    nRects = 0;
    step(4);
    WAIT_FOR(Count(C_WHITE));
    TimerSleep(5);
    CHECK(Count(C_WHITE) >= 1 && Within(C_WHITE, 110, 30, 160, 70));
    CHECK(st->UVS_drawsB == 1 && !Count(C_BLUE));

    /* 5: B back */
    nRects = 0;
    step(5);
    WAIT_FOR(st->UVS_drawsB == 2);
    CHECK(st->UVS_drawsB == 2 && Within(C_BLUE, 110, 30, 160, 70));

    /* 6: closed: the root washed where the window was */
    nRects = 0;
    last = st->UVS_drawsA;
    step(6);
    WAIT_FOR(Count(C_DARK_GRAY));
    CHECK(Count(C_DARK_GRAY) >= 1 && st->UVS_drawsA == last);

    printf("geode checks: %u, failed %u (first at uivis.goc line %u)\n",
	   st->UVS_checks, st->UVS_fails, st->UVS_failLine);
    CHECK(st->UVS_checks >= 30 && !st->UVS_fails);

    /* detach: the process goes, and with it the library */
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("uivis   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("uivis   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0));
    WinClose(root);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
