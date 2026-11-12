/*
 * hostMouse.c -- the device part of the host mouse driver (with
 * Driver/Mouse/mouseCommon.c).  No asm original: DOS16 had a driver per
 * mouse hardware (PS2, serial, bus, the BIOS through int 33h in
 * GenMouse); on a host the pointer of the window system or the console
 * comes from ghal_input (CInclude/ghal.h), whatever device it is.
 *
 * The GHAL_IRQ_INPUT handler takes the pointer's HID events:
 *   page 0x01 X, Y   a position (GHAL_INPUT_ABSOLUTE, a window) or a
 *                    change (a relative device); sent as such
 *   page 0x09 1..4   buttons: 1 primary (GEOS 0), 2 secondary (GEOS 2,
 *                    the right one), 3 middle (GEOS 1), 4 (GEOS 3)
 *   page 0x01 wheel  +1 away from the user: up (< 0 for GEOS)
 * Motion is sent before a button change that follows it, so a press
 * happens where the pointer was.  Pan (the horizontal wheel) is not
 * passed on: GEOS has no message for it.
 */
#include "../mouseCommon.h"

#include <ghal.h>

MOUSE_DRIVER_TABLE(3, 0);

static ghal_irq_source *irq;
static byte buttons = MOUSE_BUTTONS_UP;

static const byte hidButton[5] = {
    0, MOUSE_B0, MOUSE_B2, MOUSE_B1, MOUSE_B3
};

/* the pointer's events, as the mouse interrupt */
static void HostMouseHandler(void *ctx)
{
    ghal_input_event e;
    sword x = 0, y = 0;
    Boolean moved = FALSE, absolute = FALSE;
    sbyte wheel = 0;

    (void) ctx;
    while (ghal_input_read(GHAL_INPUT_POINTER, &e)) {
	if (e.page == GHAL_HID_PAGE_DESKTOP &&
	    (e.usage == GHAL_HID_X || e.usage == GHAL_HID_Y)) {
	    Boolean abs = (e.flags & GHAL_INPUT_ABSOLUTE) != 0;
	    if (moved && abs != absolute) {
		MouseSendEvents(buttons, x, y, absolute);
		moved = FALSE;
	    }
	    if (!moved) {
		absolute = abs;
		if (absolute) {
		    ImGetMousePos(&x, &y);	/* the axis not given */
		} else
		    x = y = 0;
	    }
	    if (e.usage == GHAL_HID_X)
		x = (sword) (absolute ? e.value : x + e.value);
	    else
		y = (sword) (absolute ? e.value : y + e.value);
	    moved = TRUE;
	} else if (e.page == GHAL_HID_PAGE_BUTTON && e.usage >= 1 &&
		   e.usage <= 4) {
	    if (moved) {
		MouseSendEvents(buttons, x, y, absolute);
		moved = FALSE;
	    }
	    if (e.value)
		buttons &= (byte) ~hidButton[e.usage];
	    else
		buttons |= hidButton[e.usage];
	    MouseSendEvents(buttons, 0, 0, FALSE);
	} else if (e.page == GHAL_HID_PAGE_DESKTOP &&
		   e.usage == GHAL_HID_WHEEL)
	    wheel = (sbyte) (wheel - e.value);
    }
    if (moved)
	MouseSendEvents(buttons, x, y, absolute);
    MouseSendWheel(wheel);
}

Boolean MouseDevInit(void)
{
    buttons = MOUSE_BUTTONS_UP;
    if (ghal_irq_attach(GHAL_IRQ_INPUT, HostMouseHandler, NULL, &irq) !=
	GHAL_OK) {
	irq = NULL;
	return TRUE;
    }
    return FALSE;
}

Boolean MouseDevExit(void)
{
    if (irq)
	ghal_irq_detach(irq);
    irq = NULL;
    return FALSE;
}

/* the host always has its pointer */
word MouseTestDevice(DriverRegs *r)
{
    (void) r;
    return DP_PRESENT;
}

Boolean MouseSetDevice(DriverRegs *r)
{
    (void) r;
    return FALSE;
}
