/*
 * mouseCommon.c -- the common code of the C mouse drivers (after
 * mouseCommon.asm: MouseStrategy, MouseInit, MouseExit, MouseSuspend,
 * MouseUnsuspend, MouseSetAcceleration ..., MouseSendEvents,
 * MouseAccelerate, MouseSendButtonEvent, MousePreparePtrEvent,
 * MouseSendWheelEventNative).
 *
 * MouseInit takes the input manager as the output and reads [input]:
 *   mouseAccelThreshold   pixels per report before acceleration
 *   mouseAccelMultiplier  factor past the threshold
 *   mouseNaturalScroll    the wheel the other way round
 *
 * DOS16 folded a pointer change into the last one in the input
 * manager's queue (MouseCombineEvent, MF_CUSTOM), as the combine mode
 * said.  The C kernel has no custom combining; the changes are sent as
 * they come and the mode is only kept.  The input manager combines what
 * it sends on (ABI.md 13j).
 *
 * Not yet: ImSetPtrFlags (MOUSE_PTR_FLAGS), calibration, the coordinate
 * buffer escapes, wheel keys (MOUSE_HAS_WHEEL_KEYS), DR_MOUSE_SET_RATE
 * (as MOUSE_CANT_SET_RATE).
 */
#include "mouseCommon.h"

#include <initfile.h>
#include <timer.h>

static Boolean mouseOn;
static Handle mouseOutputHandle;
static byte mouseButtons = MOUSE_BUTTONS_UP;
static word mouseAccelThreshold = 5;
static word mouseAccelMultiplier = 1;
static Boolean mouseNaturalScroll;
static MouseCombineMode mouseCombineMode = MCM_COMBINE;
/* MousePreparePtrEvent */
static word mouseLastTime;
static sword mouseRawDeltaX, mouseRawDeltaY;
static sword mouseAccDeltaX, mouseAccDeltaY;
/* absolute devices */
static sword mouseLastX = -1, mouseLastY = -1;

static const char inputCategory[] = "input";

static void ToIM(Message m, word cx, word dx, word bp)
{
    IMParams p;

    if (!mouseOutputHandle)
	return;
    p.IMP_cx = cx;
    p.IMP_dx = dx;
    p.IMP_bp = bp;
    p.IMP_si = 0;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(mouseOutputHandle, 0), m,
		 &p, sizeof(p), NULL, 0);
}

/* MouseAccelerate: past the threshold, the motion is multiplied */
static sword MouseAccelerate(sword v)
{
    sword t = (sword) mouseAccelThreshold;

    if (v < 0)
	t = (sword) -t;
    if ((v - t < 0) != (v < 0) || v - t == 0)
	return v;			/* not past the threshold */
    return (sword) ((v - t) * (sword) mouseAccelMultiplier + t);
}

/* MousePreparePtrEvent: motion close in time is accelerated together */
static void MousePreparePtrEvent(sword *cx, sword *cy, word time)
{
    sword a;

    if ((word) ((time - mouseLastTime) & PI_time) <= MOUSE_ACCEL_THRESH) {
	mouseRawDeltaX = (sword) (mouseRawDeltaX + *cx);
	mouseRawDeltaY = (sword) (mouseRawDeltaY + *cy);
	a = MouseAccelerate(mouseRawDeltaX);
	*cx = (sword) (a - mouseAccDeltaX);
	mouseAccDeltaX = a;
	a = MouseAccelerate(mouseRawDeltaY);
	*cy = (sword) (a - mouseAccDeltaY);
	mouseAccDeltaY = a;
    } else {
	mouseLastTime = time;
	mouseRawDeltaX = *cx;
	mouseRawDeltaY = *cy;
	*cx = mouseAccDeltaX = MouseAccelerate(*cx);
	*cy = mouseAccDeltaY = MouseAccelerate(*cy);
    }
}

/* MouseSendButtonEvent: button b now down or up, with the time */
static void MouseSendButtonEvent(byte b, Boolean down)
{
    dword now = TimerGetCount();

    ToIM(MSG_IM_BUTTON_CHANGE, (word) now, (word) (now >> 16),
	 (word) (b | (down ? BI_PRESS : 0)));
}

void MouseSendEvents(byte buttons, sword x, sword y, Boolean absolute)
{
    word time;
    byte changed;

    if (absolute ? (x != mouseLastX || y != mouseLastY) : (x || y)) {
	time = (word) (TimerGetCount() & PI_time);
	if (absolute) {
	    mouseLastX = x;
	    mouseLastY = y;
	    ToIM(MSG_IM_PTR_CHANGE, (word) x, (word) y,
		 (word) (time | PI_absX | PI_absY));
	} else {
	    MousePreparePtrEvent(&x, &y, time);
	    ToIM(MSG_IM_PTR_CHANGE, (word) x, (word) y, time);
	}
    }
    buttons &= MOUSE_BUTTONS_UP;
    changed = (byte) (buttons ^ mouseButtons);
    if (!changed)
	return;
    mouseButtons = buttons;
    if (changed & MOUSE_B0)
	MouseSendButtonEvent(0, !(buttons & MOUSE_B0));
    if (changed & MOUSE_B1)
	MouseSendButtonEvent(1, !(buttons & MOUSE_B1));
    if (changed & MOUSE_B2)
	MouseSendButtonEvent(2, !(buttons & MOUSE_B2));
    if (changed & MOUSE_B3)
	MouseSendButtonEvent(3, !(buttons & MOUSE_B3));
}

void MouseSendWheel(sbyte turns)
{
    if (!turns)
	return;
    if (mouseNaturalScroll)
	turns = (sbyte) -turns;
    ToIM(MSG_IM_MOUSE_WHEEL_VERTICAL, 0, (word) ((byte) turns << 8), 0);
}

static void ReadInteger(const char *key, word *v)
{
    word w;

    if (!InitFileReadInteger(inputCategory, key, &w))
	*v = w;
}

static Boolean MouseInit(void)
{
    Boolean b;

    if (mouseOn)
	return TRUE;			/* ALREADY_INITIALIZED */
    mouseOutputHandle = ImInfoInputProcess();
    mouseButtons = MOUSE_BUTTONS_UP;
    mouseLastX = mouseLastY = -1;
    ReadInteger("mouseAccelThreshold", &mouseAccelThreshold);
    ReadInteger("mouseAccelMultiplier", &mouseAccelMultiplier);
    if (!InitFileReadBoolean(inputCategory, "mouseNaturalScroll", &b))
	mouseNaturalScroll = b ? TRUE : FALSE;
    if (MouseDevInit())
	return TRUE;
    mouseOn = TRUE;
    return FALSE;
}

static Boolean MouseExit(void)
{
    if (!mouseOn)
	return TRUE;			/* ALREADY_EXITED */
    if (MouseDevExit())
	return TRUE;
    mouseOn = FALSE;
    return FALSE;
}

Boolean MouseStrategy(word function, DriverRegs *r)
{
    static const byte noRates[1] = { 0 };
    Handle old;

    switch (function) {
    case DR_INIT:
	return MouseInit();
    case DR_EXIT:
	return MouseExit();
    case DR_SUSPEND:			/* MouseSuspend */
	return MouseExit();
    case DR_UNSUSPEND:
	return MouseInit();
    case DRE_TEST_DEVICE:
	r->DR_ax = MouseTestDevice(r);
	return r->DR_ax == (word) DP_INVALID_DEVICE;
    case DRE_SET_DEVICE:
	return MouseSetDevice(r);
    case DR_MOUSE_SET_RATE:
	return TRUE;			/* MOUSE_CANT_SET_RATE */
    case DR_MOUSE_GET_RATES:
	r->DR_esDi = (void *) noRates;
	r->DR_cx = 0;
	return FALSE;
    case DR_MOUSE_SET_ACCELERATION:
	mouseAccelThreshold = (word) r->DR_cx;
	mouseAccelMultiplier = (word) r->DR_dx;
	return FALSE;
    case DR_MOUSE_GET_ACCELERATION:
	r->DR_cx = mouseAccelThreshold;
	r->DR_dx = mouseAccelMultiplier;
	return FALSE;
    case DR_MOUSE_SET_COMBINE_MODE:
	mouseCombineMode = (MouseCombineMode) r->DR_cx;
	return FALSE;
    case DR_MOUSE_GET_COMBINE_MODE:
	r->DR_cx = mouseCombineMode;
	return FALSE;
    case DR_MOUSE_CHANGE_OUTPUT:
	old = mouseOutputHandle;
	mouseOutputHandle = (Handle) r->DR_bx;
	r->DR_bx = old;
	return FALSE;
    default:
	/* calibration: no device of this kind */
	return TRUE;
    }
}
