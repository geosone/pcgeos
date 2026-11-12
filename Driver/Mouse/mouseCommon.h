/*
 * mouseCommon.h -- the common part of the C mouse drivers
 * (mouseCommon.c, after mouseCommon.asm) and what the device part of a
 * driver provides.
 *
 * As in DOS16, a driver is the common code plus a device part.  The
 * device part defines the driver table (MOUSE_DRIVER_TABLE), its
 * MouseDev* routines and MouseTestDevice/MouseSetDevice, and reports
 * the state of the mouse with MouseSendEvents; the common part sends
 * MSG_IM_PTR_CHANGE and MSG_IM_BUTTON_CHANGE to the input manager,
 * accelerates relative motion and handles the DR_MOUSE_* functions.
 */
#ifndef MOUSECOMMON_H
#define MOUSECOMMON_H

#include <geos.h>
#include <driver.h>
#include <Internal/mouseDr.h>
#include <Internal/im.h>
#include <geosdriver.h>
#include <object.h>
#include <input.h>

/* MouseButtonBits: the state MouseSendEvents gets; 1 = up (historical) */
#define MOUSE_B3	0x08
#define MOUSE_B0	0x04
#define MOUSE_B1	0x02
#define MOUSE_B2	0x01
#define MOUSE_BUTTONS_UP (MOUSE_B3 | MOUSE_B0 | MOUSE_B1 | MOUSE_B2)

Boolean MouseStrategy(word function, DriverRegs *r);

/* the driver table of a mouse driver */
#define MOUSE_DRIVER_TABLE(buttons, flags) \
    MouseDriverInfo DriverTable = { \
	{ { DRIVER_STRATEGY(MouseStrategy), DA_HAS_EXTENDED_INFO, \
	    DRIVER_TYPE_INPUT }, 0 }, \
	(buttons), 0, 0, (flags) }

/*
 * The state of the mouse: the buttons (MouseButtonBits), and the motion:
 * a change (relative) or the position (absolute: PI_absX/PI_absY to the
 * input manager, no acceleration).  Sends what changed, the motion
 * first.  Interrupt context is fine.
 */
void MouseSendEvents(byte buttons, sword x, sword y, Boolean absolute);
/* the wheel: < 0 up, > 0 down (MouseSendWheelEventNative) */
void MouseSendWheel(sbyte turns);

/* the device part */
Boolean MouseDevInit(void);			/* carry */
Boolean MouseDevExit(void);
word MouseTestDevice(DriverRegs *r);		/* DevicePresent */
Boolean MouseSetDevice(DriverRegs *r);

#endif /* MOUSECOMMON_H */
