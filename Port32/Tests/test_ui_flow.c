/*
 * test_ui_flow.c -- the UI library's flow object (FlowClass,
 * Library/User/User/userFlowInput.goc) between the kernel's input
 * manager, the window system and two geodes, through the process geode
 * uiflow (Geode/uiflow.goc).
 *
 * The test boots the kernel (the input manager's thread), opens a root
 * window on a video driver of its own and sends the input manager what
 * the drivers would: pointer moves, buttons, the wheel, keys.  The
 * geode makes the flow object, which grabs the input and the window
 * system's pointer messages, and opens window A (its own) and B (the ui
 * library's).  The steps (checked in the geode):
 *   1. the flow object made; the screen set (the driver shows the
 *      pointer); the input map's flags;
 *   2. the pointer into A: A's geode told, the pointer events;
 *   3. select pressed: the mouse grabbed for A;
 *   4. dragged into B: still A's, B may not be used meanwhile;
 *   5. released: B is the implied geode;
 *   6. <Ctrl> button 2 in B (copy), button 0 meanwhile ("other");
 *   7. button 1 (features);
 *   8. a double press in B, none for the first press in A; the
 *      keyboard grabbed for A (its thread's priority);
 *   9. keys to A, <Shift> constrains the mouse; the keyboard forced to
 *      B, asked for and released;
 *  10. B system modal: A may not be used;
 *  11. not modal any more;
 *  12. input held up;
 *  13. let go: the moves as one event;
 *  14. the mouse bumped;
 *  15. the wheel; a notification, the trespassing check, no-ink
 *      windows;
 *  16. the active function ended;
 *  17. its release is "other"; the end.
 */
#include "kernelC.h"
#include "ghal.h"
#include "IM/imInt.h"
#include <input.h>
#include <timer.h>
#include <win.h>
#include <Internal/videoDr.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern const unsigned char ui_geode[], uiflow_geode[];
extern const unsigned long ui_geode_size, uiflow_geode_size;

/* as uiflow.goc */
typedef struct {
    WindowHandle	UFS_root;
    void		*UFS_strategy;
    GeodeHandle		UFS_process;
    word		UFS_attached;
    word		UFS_checks, UFS_fails, UFS_failLine;
    word		UFS_failLines[8];
    word		UFS_n;
    struct {
	word		UL_who;
	Message		UL_msg;
	sword		UL_x, UL_y;
	word		UL_state;
	optr		UL_od;
	WindowHandle	UL_win;
    } UFS_log[128];
    /* (more, for the geode only) */
} UFState;

static UFState *st;

/* UF_DEBUG set: the log before each step */
static void Dump(word n)
{
    int i;

    if (!getenv("UF_DEBUG"))
	return;
    printf("before step %u:\n", n);
    for (i = 0; i < st->UFS_n && i < 128; i++)
	printf("  %d: who %u msg %lu x %d y %d state %04x od %llx win %x\n", i,
	       st->UFS_log[i].UL_who, (unsigned long) st->UFS_log[i].UL_msg,
	       st->UFS_log[i].UL_x, st->UFS_log[i].UL_y,
	       st->UFS_log[i].UL_state,
	       (unsigned long long) st->UFS_log[i].UL_od,
	       (unsigned) st->UFS_log[i].UL_win);
}

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define WAIT_FOR(cond) \
    do { int n_; for (n_ = 0; n_ < 500 && !(cond); n_++) TimerSleep(1); \
    } while (0)

static void (*stepFn)(word);
static void Dump(word n);
static void step(word n)
{
    if (n)
	Dump(n);
    stepFn(n);
}

static void ToIM(Message m, word cx, word dx, word bp)
{
    IMParams p;

    p.IMP_cx = cx;
    p.IMP_dx = dx;
    p.IMP_bp = bp;
    p.IMP_si = 0;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(ImInfoInputProcess(), 0), m,
		 &p, m == IM_META_KBD_CHAR ? 3 * sizeof(word) : sizeof(p),
		 NULL, 0);
}

/* the input manager has done what it was sent */
static void Sync(void)
{
    word r;

    CObjMessageP(MF_CALL, ConstructOptr(ImInfoInputProcess(), 0),
		 MSG_IM_GET_SCREEN_SAVER_DELAY, NULL, 0, &r, sizeof(r));
}

/* what the input manager sent has gone through the flow object to the
 * geodes (each round: the input manager, then the process's queue) */
static void Settle(void)
{
    int i;

    for (i = 0; i < 4; i++) {
	Sync();
	step(0);
    }
}

static void PtrAt(sword x, sword y)
{
    ToIM(MSG_IM_PTR_CHANGE, (word) x, (word) y, PI_absX | PI_absY);
    Sync();
}

/* the time of the presses: far apart, unless given */
static word now = 1000;

static void PressAt(word button, Boolean press, word t)
{
    ToIM(MSG_IM_BUTTON_CHANGE, t, 0,
	 (word) (button | (press ? BI_PRESS : 0)));
    Sync();
}

static void Press(word button, Boolean press)
{
    now += 1000;
    PressAt(button, press, now);
}

static void Key(word ch, word flags)
{
    ToIM(IM_META_KBD_CHAR, ch, flags, 0);
    Sync();
}

/* the video driver of the root window */
static int showPtrs;

static Boolean Strategy(word function, DriverRegs *r)
{
    switch (function) {
    case DR_VID_SHOWPTR:
	showPtrs++;
	return FALSE;
    case DR_VID_MOVEPTR:
    case DR_VID_CHECK_UNDER:
    case DR_VID_INFO_UNDER:
	r->DR_ax = r->DR_di = 0;	/* no save under areas */
	return FALSE;
    case DR_VID_SAVE_UNDER:
	return TRUE;			/* refused */
    default:
	return FALSE;
    }
}

int main(int argc, char **argv)
{
    char top[260], *slash;
    GeodeLoadError e;
    GeodeHandle app;
    WindowHandle root;
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    int i;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GUFTOP");
    else
	strcpy(top, "GUFTOP");
    ghal_fs_mkdir(top);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    CHECK(ImInfoInputProcess() != 0);
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uiflow", uiflow_geode, uiflow_geode_size);
    /* no drags by time: the test is not that fast on every host */
    im.dragTime = 30000;
    ImSetDoubleClick(100, 1000);

    root = WinOpen(0, 0, 0, WCF_PLAIN | CMT_CLOSEST, C_DARK_GRAY, 0, 0,
		   WPF_ROOT, 0, 0, NULL, 0, 0, 639, 479);
    WinSetInfo(root, WIT_STRATEGY, (optr) (uintptr_t) (void *) Strategy);
    ImSetPtrWin(root, 0, 0);

    app = GeodeLoad("uiflow", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(app != 0);
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(app, 0);
    stepFn = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->UFS_attached);
    CHECK(st->UFS_attached == 1 && st->UFS_process == app);
    st->UFS_root = root;
    st->UFS_strategy = (void *) Strategy;

    step(1);
    CHECK(showPtrs == 1);
    PtrAt(150, 150);
    Settle();
    step(2);
    Press(0, TRUE);
    Settle();
    step(3);
    PtrAt(350, 150);
    PtrAt(352, 150);
    Settle();
    step(4);
    Press(0, FALSE);
    Settle();
    step(5);
    im.shiftState = SS_LCTRL;
    Press(2, TRUE);
    im.shiftState = 0;
    Press(0, TRUE);
    Press(0, FALSE);
    Press(2, FALSE);
    Settle();
    step(6);
    Press(1, TRUE);
    Press(1, FALSE);
    Settle();
    step(7);
    now += 1000;
    PressAt(0, TRUE, now);
    PressAt(0, FALSE, now + 1);
    PressAt(0, TRUE, now + 10);
    PressAt(0, FALSE, now + 11);
    PtrAt(150, 150);
    PressAt(0, TRUE, now + 20);
    PressAt(0, FALSE, now + 21);
    Settle();
    step(8);
    Key('a', CF_FIRST_PRESS);
    Key((CS_CONTROL << 8) | VC_LSHIFT, CF_FIRST_PRESS | CF_STATE_KEY);
    Key((CS_CONTROL << 8) | VC_LSHIFT, CF_RELEASE | CF_STATE_KEY);
    Settle();
    step(9);
    Settle();
    step(10);
    PtrAt(160, 160);
    Press(0, TRUE);
    Press(0, FALSE);
    Settle();
    step(11);
    Settle();
    step(12);
    PtrAt(170, 170);
    PtrAt(171, 171);
    PtrAt(172, 172);
    Press(0, TRUE);
    Press(0, FALSE);
    Settle();
    step(13);
    Settle();
    step(14);
    Settle();
    ToIM(MSG_IM_MOUSE_WHEEL_VERTICAL, 0, 0xff00, 0);
    Settle();
    step(15);
    Settle();
    Press(0, TRUE);
    Settle();
    step(16);
    Press(0, FALSE);
    Settle();
    step(17);

    printf("geode checks: %u, failed %u (first at uiflow.goc line %u)\n",
	   st->UFS_checks, st->UFS_fails, st->UFS_failLine);
    for (i = 0; i < st->UFS_fails && i < 8; i++)
	printf("  failed: uiflow.goc line %u\n", st->UFS_failLines[i]);
    CHECK(st->UFS_checks >= 90 && !st->UFS_fails);

    /* detach: the process goes, and with it the library */
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("uiflow  ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("uiflow  ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0));
    WinClose(root);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
