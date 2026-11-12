/*
 * test_mouse.c -- the C host mouse driver (Driver/Mouse: mouseCommon.c,
 * Host/hostMouse.c) in the C kernel, through the input manager.
 *
 * The test boots, loads hostmse.geo from SP_MOUSE_DRIVERS as a driver,
 * makes a queue the output of the input manager (ImGrabInput) and posts
 * the pointer's HID events as a window system does:
 *   - an absolute position, then relative motion (MSG_META_MOUSE_PTR);
 *   - the buttons: primary, secondary (GEOS button 2), middle (1), a
 *     press after motion in the same batch at the new position;
 *   - the wheel (up: < 0);
 *   - acceleration (DR_MOUSE_SET/GET_ACCELERATION), the combine mode;
 *   - DRE_TEST_DEVICE, the driver table, DR_MOUSE_CHANGE_OUTPUT (the
 *     raw MSG_IM_* messages to a queue of our own);
 *   - DR_EXIT: no more events.
 */
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <Internal/mouseDr.h>
#include <Internal/im.h>
#include <input.h>
#include <timer.h>

#include <stdio.h>
#include <string.h>

extern const unsigned char hostmse_geode[];
extern const unsigned long hostmse_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';
static QueueHandle q;
static DriverInfoStruct *dis;

static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    sprintf(path, "%s%c%s", top, sep, rel);
    for (p = path; *p; p++)
	if (*p == '/')
	    *p = sep;
    if (!size) {
	ghal_fs_mkdir(path);
	return;
    }
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, data, size, &done);
    CHECK(done == size);
    ghal_fs_close(f);
}

static void Post(word page, word usage, int value, Boolean absolute)
{
    ghal_input_event e;

    memset(&e, 0, sizeof(e));
    e.page = page;
    e.usage = usage;
    e.value = value;
    e.source = GHAL_INPUT_SOURCE_POINTER;
    e.flags = absolute ? GHAL_INPUT_ABSOLUTE : 0;
    CHECK(ghal_input_post(&e, 1) == GHAL_OK);
}

/* a batch, as a window system posts it: several events, one interrupt */
static void PostXY(int x, int y, Boolean absolute)
{
    ghal_input_event e[2];

    memset(e, 0, sizeof(e));
    e[0].page = e[1].page = GHAL_HID_PAGE_DESKTOP;
    e[0].usage = GHAL_HID_X;
    e[1].usage = GHAL_HID_Y;
    e[0].value = x;
    e[1].value = y;
    e[0].source = e[1].source = GHAL_INPUT_SOURCE_POINTER;
    e[0].flags = e[1].flags = absolute ? GHAL_INPUT_ABSOLUTE : 0;
    CHECK(ghal_input_post(e, 2) == GHAL_OK);
}

static Message Get(word *w)
{
    EventHandle ev;
    const word *p;
    dword size;
    Message m;
    int n;

    for (n = 0; n < 300 && !GeodeInfoQueue(q); n++)
	TimerSleep(1);
    if (!GeodeInfoQueue(q))
	return 0;
    ev = QueueGetMessage(q);
    m = ObjGetMessageInfo(ev, NULL);
    p = KernelEventParams(ev, &size);
    memset(w, 0, 3 * sizeof(word));
    memcpy(w, p, size < 3 * sizeof(word) ? size : 3 * sizeof(word));
    ObjFreeMessage(ev);
    return m;
}

/* the next event; pointer events that do not move are skipped */
static sword lastX = -1000, lastY = -1000;
static Message Out(word *w)
{
    Message m;

    for (;;) {
	m = Get(w);
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

int main(int argc, char **argv)
{
    char *slash;
    GeodeLoadError e;
    GeodeHandle drv;
    MouseDriverInfo *mdi;
    DriverRegs r;
    word w[3];

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GMSTOP");
    } else
	strcpy(top, "GMSTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/MOUSE", NULL, 0);
    Put("SYSTEM/MOUSE/hostmse.geo", hostmse_geode, hostmse_geode_size);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    q = GeodeAllocQueue();
    CHECK(ImGrabInput(ConstructOptr(q, 0)));

    FileSetStandardPath(SP_MOUSE_DRIVERS);
    drv = GeodeUseDriver("hostmse.geo", MOUSE_DRIVER_PROTO_MAJOR,
			 MOUSE_DRIVER_PROTO_MINOR, &e);
    CHECK(drv != 0);
    if (!drv) {
	printf("GeodeLoadError %d\n", (int) e);
	return 1;
    }
    dis = GeodeInfoDriver(drv);
    mdi = (MouseDriverInfo *) dis;
    CHECK(dis->DIS_driverType == DRIVER_TYPE_INPUT &&
	  (dis->DIS_driverAttributes & DA_HAS_EXTENDED_INFO));
    CHECK(mdi->MDI_numButtons == 3 && mdi->MDI_xRes == 0);
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DRE_TEST_DEVICE, &r) && r.DR_ax == DP_PRESENT);
    CHECK(!DriverCall(dis, DRE_SET_DEVICE, &r));

    /* a position, then a change */
    PostXY(100, 50, TRUE);
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 100 && w[1] == 50);
    PostXY(3, -2, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 103 && w[1] == 48);

    /* the primary button: GEOS button 0 */
    Post(GHAL_HID_PAGE_BUTTON, 1, 1, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  (w[2] & 0xff) == (BI_PRESS | BI_B0_DOWN | 0));
    Post(GHAL_HID_PAGE_BUTTON, 1, 0, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && (w[2] & 0xff) == 0);
    /* secondary: the right button, 2; the middle one: 1 */
    Post(GHAL_HID_PAGE_BUTTON, 2, 1, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  (w[2] & 0xff) == (BI_PRESS | BI_B2_DOWN | 2));
    Post(GHAL_HID_PAGE_BUTTON, 2, 0, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && (w[2] & 0xff) == 2);
    Post(GHAL_HID_PAGE_BUTTON, 3, 1, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON &&
	  (w[2] & 0xff) == (BI_PRESS | BI_B1_DOWN | 1));
    Post(GHAL_HID_PAGE_BUTTON, 3, 0, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && (w[2] & 0xff) == 1);

    /* motion and a press in one batch: the press where the pointer went */
    {
	ghal_input_event ev[3];
	memset(ev, 0, sizeof(ev));
	ev[0].page = ev[1].page = GHAL_HID_PAGE_DESKTOP;
	ev[0].usage = GHAL_HID_X;
	ev[1].usage = GHAL_HID_Y;
	ev[0].value = 200;
	ev[1].value = 150;
	ev[0].flags = ev[1].flags = GHAL_INPUT_ABSOLUTE;
	ev[2].page = GHAL_HID_PAGE_BUTTON;
	ev[2].usage = 1;
	ev[2].value = 1;
	ev[0].source = ev[1].source = ev[2].source =
	    GHAL_INPUT_SOURCE_POINTER;
	CHECK(ghal_input_post(ev, 3) == GHAL_OK);
    }
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 200 && w[1] == 150);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && w[0] == 200 && w[1] == 150 &&
	  (w[2] & BI_PRESS));
    Post(GHAL_HID_PAGE_BUTTON, 1, 0, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_BUTTON && !(w[2] & BI_PRESS));

    /* the wheel: away from the user is up */
    Post(GHAL_HID_PAGE_DESKTOP, GHAL_HID_WHEEL, 1, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_WHEEL_VERTICAL &&
	  (sbyte) (w[2] & 0xff) == -1);
    Post(GHAL_HID_PAGE_DESKTOP, GHAL_HID_WHEEL, -2, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_WHEEL_VERTICAL &&
	  (sbyte) (w[2] & 0xff) == 2);

    /* acceleration: past 5 pixels, twice as far */
    memset(&r, 0, sizeof(r));
    r.DR_cx = 5;
    r.DR_dx = 2;
    CHECK(!DriverCall(dis, DR_MOUSE_SET_ACCELERATION, &r));
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DR_MOUSE_GET_ACCELERATION, &r) &&
	  r.DR_cx == 5 && r.DR_dx == 2);
    TimerSleep(MOUSE_ACCEL_THRESH + 1);	/* a motion of its own */
    PostXY(10, 0, FALSE);
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 215 && w[1] == 150);
    TimerSleep(MOUSE_ACCEL_THRESH + 1);
    PostXY(-3, 0, FALSE);			/* under the threshold */
    CHECK(Out(w) == IM_META_MOUSE_PTR && w[0] == 212);
    memset(&r, 0, sizeof(r));
    r.DR_cx = MCM_NO_COMBINE;
    CHECK(!DriverCall(dis, DR_MOUSE_SET_COMBINE_MODE, &r));
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DR_MOUSE_GET_COMBINE_MODE, &r) &&
	  r.DR_cx == MCM_NO_COMBINE);
    CHECK(DriverCall(dis, DR_MOUSE_SET_RATE, &r));	/* cannot */

    /* the raw messages to an output of our own */
    {
	QueueHandle raw = GeodeAllocQueue(), keep = q;
	memset(&r, 0, sizeof(r));
	r.DR_bx = raw;
	CHECK(!DriverCall(dis, DR_MOUSE_CHANGE_OUTPUT, &r) &&
	      r.DR_bx == ImInfoInputProcess());
	PostXY(7, 8, TRUE);
	Post(GHAL_HID_PAGE_BUTTON, 1, 1, FALSE);
	q = raw;
	CHECK(Get(w) == MSG_IM_PTR_CHANGE && w[0] == 7 && w[1] == 8 &&
	      (w[2] & (PI_absX | PI_absY)) == (PI_absX | PI_absY));
	CHECK(Get(w) == MSG_IM_BUTTON_CHANGE);
	q = keep;
	memset(&r, 0, sizeof(r));
	r.DR_bx = ImInfoInputProcess();
	DriverCall(dis, DR_MOUSE_CHANGE_OUTPUT, &r);
	Post(GHAL_HID_PAGE_BUTTON, 1, 0, FALSE);
	GeodeFreeQueue(raw);
    }

    /* freed: no more events */
    GeodeFreeDriver(drv);
    {
	sword x0, y0, x1, y1;
	TimerSleep(20);			/* what is under way, and the drag
					 * timer's refresh */
	ImGetMousePos(&x0, &y0);
	PostXY(1, 1, FALSE);
	Post(GHAL_HID_PAGE_BUTTON, 1, 1, FALSE);
	TimerSleep(5);
	ImGetMousePos(&x1, &y1);
	CHECK(x0 == x1 && y0 == y1);
	CHECK(!(ImGetButtonState() & BI_B0_DOWN));
    }
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
