/*
 * test_kernel_im.c -- the input manager (Library/Kernel/IM; Port32/ABI.md
 * section 13j).
 *
 * The test boots the kernel (the input manager's thread), makes a queue
 * the output (ImGrabInput) and sends the input manager what pointer
 * drivers send: MSG_IM_PTR_CHANGE, MSG_IM_BUTTON_CHANGE,
 * MSG_IM_MOUSE_WHEEL_VERTICAL.  It reads what comes out:
 *   - relative and absolute moves summed into MSG_META_MOUSE_PTR,
 *     ImGetMousePos;
 *   - presses and releases (MSG_META_MOUSE_BUTTON) with the button
 *     state, ImGetButtonState, the double press, left-handed buttons;
 *   - a drag by distance (MSG_META_MOUSE_DRAG), and the drag a release
 *     finds lost (the drag, then the release held back);
 *   - the wheel; MSG_META_KBD_CHAR passed on;
 *   - ImConstrainMouse with MSG_META_LEAVE_CONSTRAIN, the screen limits;
 *   - monitors of its own: one that makes two events of one
 *     (MF_MORE_TO_DO), one that eats events, ImRemoveMonitor;
 *   - ImBumpMouse;
 *   - the screen saver: the delay, an installed saver blanked and
 *     brought back by input, its notifications, SysDisableAPO.
 */
#include "kernelC.h"
#include "ghal.h"
#include "IM/imInt.h"
#include <input.h>
#include <timer.h>
#include <system.h>

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static QueueHandle q, qc;
static sword lastX, lastY;		/* the pointer the output knows */

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

/* the next event of queue qh, waiting for the IM's thread */
static Message Get(QueueHandle qh, word *w)
{
    EventHandle ev;
    const word *p;
    dword size;
    Message m;
    int n;

    for (n = 0; n < 300 && !GeodeInfoQueue(qh); n++)
	TimerSleep(1);
    if (!GeodeInfoQueue(qh))
	return 0;
    ev = QueueGetMessage(qh);
    m = ObjGetMessageInfo(ev, NULL);
    p = KernelEventParams(ev, &size);
    memset(w, 0, 3 * sizeof(word));
    memcpy(w, p, size < 3 * sizeof(word) ? size : 3 * sizeof(word));
    ObjFreeMessage(ev);
    return m;
}

/* the next output event; pointer events that do not move (the refresh
 * of the drag timer and of ImForcePtrMethod) are skipped */
static Message Out(word *w)
{
    Message m;

    for (;;) {
	m = Get(q, w);
	if (m != IM_META_MOUSE_PTR || (sword) w[0] != lastX ||
	    (sword) w[1] != lastY)
	    break;
    }
    if (m == IM_META_MOUSE_PTR || m == IM_META_MOUSE_DRAG) {
	lastX = (sword) w[0];
	lastY = (sword) w[1];
    }
    return m;
}

/* a message to the input manager, the reply word */
static word ToIMCall(Message m, const void *p, word size)
{
    word r = 0xffff;

    CObjMessageP(MF_CALL, ConstructOptr(ImInfoInputProcess(), 0), m,
		 p, size, &r, sizeof(r));
    return r;
}

static word Now(void)
{
    return (word) (TimerGetCount() & PI_time);
}

/* a monitor that makes "xy" of each MSG_META_KBD_CHAR 'm' */
static int twoState;
static byte TwoMonitor(Monitor *mon, ImEvent *ev)
{
    (void) mon;
    if (ev->IE_message == IM_META_KBD_CHAR && ev->IE_cx == 'm') {
	ev->IE_cx = 'x';
	twoState = 1;
	return MF_DATA | MF_MORE_TO_DO;
    }
    if (ev->IE_message == 0 && twoState) {
	ev->IE_message = IM_META_KBD_CHAR;
	ev->IE_cx = 'y';
	ev->IE_dx = 0;
	ev->IE_bp = 0;
	twoState = 0;
	return MF_DATA;
    }
    return MF_DATA;
}

/* a monitor that eats 'e' */
static byte EatMonitor(Monitor *mon, ImEvent *ev)
{
    (void) mon;
    if (ev->IE_message == IM_META_KBD_CHAR && ev->IE_cx == 'e')
	return 0;
    return MF_DATA;
}

int main(int argc, char **argv)
{
    char top[260], *slash;
    GeodeLoadError e;
    Monitor two, eat;
    word w[3], t;
    sword x, y;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GIMTOP");
    else
	strcpy(top, "GIMTOP");
    ghal_fs_mkdir(top);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    CHECK(ImInfoInputProcess() != 0);
    q = GeodeAllocQueue();
    qc = GeodeAllocQueue();
    CHECK(ImGrabInput(ConstructOptr(q, 0)));
    /* no drags by time here: the test is not that fast on every host */
    im.dragTime = 30000;

    /* moves: relative, then absolute */
    ToIM(MSG_IM_PTR_CHANGE, 10, 5, Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 10 && w[1] == 5 &&
	  (w[2] & 0xff) == 0);
    ToIM(MSG_IM_PTR_CHANGE, (word) -3, 2, Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 7 && w[1] == 7);
    ToIM(MSG_IM_PTR_CHANGE, 100, 50, PI_absX | PI_absY | Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 100 && w[1] == 50);
    ImGetMousePos(&x, &y);
    CHECK(x == 100 && y == 50);

    /* button 0: press, a move too small for a drag, release */
    t = 1000;
    ToIM(MSG_IM_BUTTON_CHANGE, t, 0, BI_PRESS | 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && w[0] == 100 && w[1] == 50 &&
	  (w[2] & 0xff) == (BI_PRESS | BI_B0_DOWN));
    CHECK(ImGetButtonState() == BI_B0_DOWN);
    ToIM(MSG_IM_PTR_CHANGE, 3, 0, Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 103 &&
	  (w[2] & 0xff) == BI_B0_DOWN);
    /* a drag: more than dragDistance from the press */
    ToIM(MSG_IM_PTR_CHANGE, 20, 0, Now());
    CHECK(Out(w) == IM_META_MOUSE_DRAG && w[0] == 123 &&
	  (w[2] & 0xff) == (BI_B0_DOWN | 0));
    ToIM(MSG_IM_PTR_CHANGE, 1, 0, Now());	/* once per press */
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 124);
    ToIM(MSG_IM_BUTTON_CHANGE, t + 30, 0, 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && (w[2] & 0xff) == 0);
    CHECK(ImGetButtonState() == 0);

    /* a double press: soon again at the same place */
    ToIM(MSG_IM_BUTTON_CHANGE, t + 100, 0, BI_PRESS | 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  !((w[2] & 0xff) & BI_DOUBLE_PRESS));
    ToIM(MSG_IM_BUTTON_CHANGE, t + 102, 0, 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON);
    ToIM(MSG_IM_BUTTON_CHANGE, t + 105, 0, BI_PRESS | 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  (w[2] & 0xff) == (BI_PRESS | BI_DOUBLE_PRESS | BI_B0_DOWN));
    ToIM(MSG_IM_BUTTON_CHANGE, t + 107, 0, 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON);
    /* too late for a double press */
    ToIM(MSG_IM_BUTTON_CHANGE, t + 200, 0, BI_PRESS | 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  !((w[2] & 0xff) & BI_DOUBLE_PRESS));
    ToIM(MSG_IM_BUTTON_CHANGE, t + 201, 0, 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON);

    /* the lost drag: button 1 held past dragTime, released before a
     * move came; the drag first, then the release */
    im.dragTime = 14;
    ToIM(MSG_IM_BUTTON_CHANGE, 2000, 0, BI_PRESS | 1);
    ToIM(MSG_IM_BUTTON_CHANGE, 2020, 0, 1);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  (w[2] & 0xff) == (BI_PRESS | BI_B1_DOWN | 1));
    CHECK(Out(w) == IM_META_MOUSE_DRAG && (w[2] & 0xff) == (BI_B1_DOWN | 1));
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && (w[2] & 0xff) == 1);
    im.dragTime = 30000;

    /* left-handed: button 0 is button 2 */
    im.leftHanded = TRUE;
    ToIM(MSG_IM_BUTTON_CHANGE, 3000, 0, BI_PRESS | 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  (w[2] & 0xff) == (BI_PRESS | BI_B2_DOWN | 2));
    ToIM(MSG_IM_BUTTON_CHANGE, 3001, 0, 0);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && (w[2] & 0xff) == 2);
    im.leftHanded = FALSE;

    /* the wheel: up (dh < 0) at the pointer */
    ToIM(MSG_IM_MOUSE_WHEEL_VERTICAL, 0, 0xff00, 0);
    CHECK(Out(w) == IM_META_MOUSE_WHEEL_VERTICAL && w[0] == 124 &&
	  w[1] == 50 && (sbyte) (w[2] & 0xff) == -1);

    /* keys pass through */
    ToIM(IM_META_KBD_CHAR, 'k', CF_FIRST_PRESS, 0);
    CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'k' &&
	  w[1] == CF_FIRST_PRESS);

    /* monitors of our own: two events of one, one eaten */
    CHECK(ImAddMonitor(&two, ML_DRIVER, TwoMonitor) == ImInfoInputProcess());
    ImAddMonitor(&eat, ML_DRIVER, EatMonitor);
    ToIM(IM_META_KBD_CHAR, 'm', 0, 0);
    ToIM(IM_META_KBD_CHAR, 'e', 0, 0);
    ToIM(IM_META_KBD_CHAR, 'z', 0, 0);
    CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'x');
    CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'y');
    CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'z');
    ImRemoveMonitor(&eat, MF_REMOVE_IMMEDIATE);
    ImRemoveMonitor(&two, MF_REMOVE_IMMEDIATE);
    ToIM(IM_META_KBD_CHAR, 'm', 0, 0);
    ToIM(IM_META_KBD_CHAR, 'e', 0, 0);
    CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'm');
    CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'e');

    /* constrained to (0,0)-(50,40): the pointer at (124,50) comes in,
     * the constrain object learns it tried to leave */
    ImConstrainMouse(0, 0, 50, 40, 0, ConstructOptr(qc, 0));
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 50 && w[1] == 40);
    CHECK(Get(qc, w) == IM_META_LEAVE_CONSTRAIN &&
	  w[0] == (PTR_LEAVE_RIGHT | PTR_LEAVE_BOTTOM));
    ToIM(MSG_IM_PTR_CHANGE, (word) -60, 0, Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 0 && w[1] == 40);
    CHECK(Get(qc, w) == IM_META_LEAVE_CONSTRAIN && w[0] == PTR_LEAVE_LEFT);
    ImUnconstrainMouse();
    ToIM(MSG_IM_PTR_CHANGE, 70, 30, PI_absX | PI_absY | Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 70 && w[1] == 30);

    /* the screen: the pointer stays on it */
    ImSetScreenLimits(0, 0, 639, 479);
    ToIM(MSG_IM_PTR_CHANGE, 1000, (word) -5, PI_absX | PI_absY | Now());
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 639 && w[1] == 0);
    ImGetMousePos(&x, &y);
    CHECK(x == 639 && y == 0);

    /* without the grab, nothing goes out */
    ImReleaseInput(ConstructOptr(q, 0));
    ToIM(IM_META_KBD_CHAR, 'q', 0, 0);
    TimerSleep(5);
    while (GeodeInfoQueue(q)) {
	Get(q, w);
	CHECK(w[0] != 'q');
    }
    CHECK(ImGrabInput(ConstructOptr(q, 0)));

    /* ImBumpMouse: the mark, then the pointer moved */
    ImPtrJump(100, 100);
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 100 && w[1] == 100);
    ImBumpMouse(0x6000, 5, -3);
    CHECK(Out(w) == 0x6000 && w[0] == 5 && w[1] == (word) -3);
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 105 && w[1] == 97);

    /* the screen saver */
    {
	IMScreenSaverParams sp;
	word min = 2;
	optr saver = ConstructOptr(qc, 0);

	while (GeodeInfoQueue(q))
	    Get(q, w);
	while (GeodeInfoQueue(qc))
	    Get(qc, w);
	CHECK(ToIMCall(MSG_IM_GET_SCREEN_SAVER_DELAY, NULL, 0) == 15);
	(void) ToIMCall(MSG_IM_SET_SCREEN_SAVER_DELAY, &min, sizeof(min));
	CHECK(ToIMCall(MSG_IM_GET_SCREEN_SAVER_DELAY, NULL, 0) == 2);
	CHECK(im.ssCurCount == 8);
	CHECK(GCNListAdd(saver, MANUFACTURER_ID_GEOWORKS, 72));
	sp.IMSSP_od = saver;
	sp.IMSSP_beginMsg = 0x5000;
	sp.IMSSP_endMsg = 0x5001;
	(void) ToIMCall(MSG_IM_INSTALL_SCREEN_SAVER, &sp, sizeof(sp));
	(void) ToIMCall(MSG_IM_ENABLE_SCREEN_SAVER, NULL, 0);
	CHECK(Get(qc, w) == KMSG_META_NOTIFY && w[0] ==
	      MANUFACTURER_ID_GEOWORKS && w[1] == 170 && w[2] == SSS_ENABLED);
	/* blanked: the saver's begin message, then the notification */
	(void) ToIMCall(MSG_IM_ACTIVATE_SCREEN_SAVER, NULL, 0);
	CHECK(Get(qc, w) == 0x5000);
	CHECK(Get(qc, w) == KMSG_META_NOTIFY &&
	      w[2] == (SSS_ENABLED | SSS_ACTIVE));
	/* a pointer event that does not move is not the user */
	ToIM(MSG_IM_PTR_CHANGE, 0, 0, Now());
	(void) ToIMCall(MSG_IM_GET_SCREEN_SAVER_DELAY, NULL, 0);
	CHECK(!GeodeInfoQueue(qc) && (im.ssState & SSS_ACTIVE));
	/* a key is: back, no notification */
	ToIM(IM_META_KBD_CHAR, 'k', 0, 0);
	CHECK(Get(qc, w) == 0x5001);
	CHECK(Out(w) == IM_META_KBD_CHAR && w[0] == 'k');
	CHECK(!(im.ssState & SSS_ACTIVE) && im.ssCurCount == 8);
	/* a countdown counts */
	(void) ToIMCall(MSG_IM_SCREEN_SAVER_COUNTDOWN, NULL, 0);
	CHECK(im.ssCurCount == 7);
	/* busy: not blanked, the count stays at one */
	SysDisableAPO();
	im.ssCurCount = 1;
	(void) ToIMCall(MSG_IM_SCREEN_SAVER_COUNTDOWN, NULL, 0);
	CHECK(im.ssCurCount == 1 && !(im.ssState & SSS_ACTIVE));
	SysEnableAPO();
	(void) ToIMCall(MSG_IM_SCREEN_SAVER_COUNTDOWN, NULL, 0);
	CHECK(Get(qc, w) == 0x5000);
	CHECK(Get(qc, w) == KMSG_META_NOTIFY);
	/* disabling while blank: back, two notifications */
	(void) ToIMCall(MSG_IM_DISABLE_SCREEN_SAVER, NULL, 0);
	CHECK(Get(qc, w) == 0x5001);
	CHECK(Get(qc, w) == KMSG_META_NOTIFY && w[2] == SSS_ENABLED);
	CHECK(Get(qc, w) == KMSG_META_NOTIFY && w[2] == 0);
	CHECK(im.ssState == 0 && im.ssTimer == 0);
	(void) ToIMCall(MSG_IM_REMOVE_SCREEN_SAVER, &sp, sizeof(sp));
	CHECK(im.ssOD == 0);
	CHECK(GCNListRemove(saver, MANUFACTURER_ID_GEOWORKS, 72));
    }

    GeodeFreeQueue(qc);
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
