/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	im.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/im.def: the messages of the input
 *	manager, its monitor chain and its routines (Library/Kernel/IM).
 *
 *	IMClass is a subclass of ProcessClass (first message 8192), so
 *	its messages start at 8704 (goc and Esp give a subclass 512).
 *	Port32: the parameters of these register messages are the
 *	structures below (Port32/ABI.md section 5).
 *
 ***********************************************************************/
#ifndef __IM_H
#define __IM_H

#include <geos.h>

#define IM_FIRST_MESSAGE	8704
#define MSG_IM_BUTTON_CHANGE	(IM_FIRST_MESSAGE + 0)
#define MSG_IM_PTR_CHANGE	(IM_FIRST_MESSAGE + 1)
#define MSG_IM_PRESSURE_CHANGE	(IM_FIRST_MESSAGE + 2)
#define MSG_IM_DIRECTION_CHANGE	(IM_FIRST_MESSAGE + 3)
#define MSG_IM_KBD_SCAN		(IM_FIRST_MESSAGE + 4)
#define MSG_IM_MOUSE_WHEEL_VERTICAL (IM_FIRST_MESSAGE + 23)

/* MSG_META_KBD_CHAR (MetaInputMessages, inputC.goh), which the
 * keyboard driver returns from DR_KBD_XLATE_SCAN; for C code that is
 * not goc (ctest kernel.obj checks it against goc) */
#define IM_META_KBD_CHAR	119
/* the other MetaInputMessages the input manager sends (checked the
 * same way) */
#define IM_META_MOUSE_BUTTON	117
#define IM_META_MOUSE_PTR	118
#define IM_META_MOUSE_DRAG	123
#define IM_META_MOUSE_WHEEL_VERTICAL 124
#define IM_META_LEAVE_CONSTRAIN	22

/*
 * MSG_IM_KBD_SCAN: cx = scan code (high byte the extension, 0xe0 or
 * 0xe1, low byte the code with 0x80 on a release), dx, bp = 0,
 * si = driver handle (0); laid out as IMParams
 */
typedef struct {
    word	IMKS_scanCode;
    word	IMKS_dx;
    word	IMKS_bp;
    Handle	IMKS_driver;
} IMKbdScanParams;

/*
 * The parameters of every input message, to the input manager and from
 * it to the output (MSG_META_KBD_CHAR, MSG_META_MOUSE_*): the registers
 * cx, dx, bp of DOS16, then si (the driver or window handle), which
 * goc's messages leave out.
 */
typedef struct {
    word	IMP_cx;
    word	IMP_dx;
    word	IMP_bp;
    Handle	IMP_si;
} IMParams;

/* MSG_IM_PTR_CHANGE: bp is PtrInfo */
#define PI_absX		0x8000		/* x is absolute */
#define PI_absY		0x4000		/* y is absolute */
#define PI_time		0x3fff		/* low 14 bits of the tick count */

/* MSG_META_LEAVE_CONSTRAIN: cx, the edges the pointer tried to leave
 * (metaC.goh) */
#ifndef PTR_LEAVE_LEFT
#define PTR_LEAVE_LEFT		0x01
#define PTR_LEAVE_TOP		0x02
#define PTR_LEAVE_RIGHT		0x04
#define PTR_LEAVE_BOTTOM	0x08
#endif

/* the levels of the monitor chain */
#define ML_DRIVER	20		/* extra processing by drivers */
#define ML_COMBINE	40		/* input combination */
#define ML_PTR_PERTURB	60		/* pointer perturbations */
#define ML_INK		80		/* ink capture */
#define ML_OUTPUT	100		/* final output */

/* MonitorFlags */
#define MF_REMOVE_WHEN_EMPTY	0x80
#define MF_REMOVE_IMMEDIATE	0x40
#define MF_IN_MONITOR		0x04
#define MF_MORE_TO_DO		0x02
#define MF_DATA			0x01

/*
 * An event in the monitor chain: the message (0 when the chain asks a
 * monitor that said MF_MORE_TO_DO for its next event) and the registers.
 */
typedef struct {
    Message	IE_message;
    word	IE_cx;
    word	IE_dx;
    word	IE_bp;
    Handle	IE_si;
} ImEvent;

/*
 * A monitor: its routine gets each event of its level and returns
 * MonitorFlags (MF_DATA: *ev is passed on; MF_MORE_TO_DO: call again for
 * more).  The structure belongs to the caller of ImAddMonitor and must
 * stay until ImRemoveMonitor.
 */
typedef struct _Monitor Monitor;
typedef byte ImMonitorRoutine(Monitor *mon, ImEvent *ev);
struct _Monitor {
    ImMonitorRoutine	*M_monRoutine;
    sbyte		M_priority;
    byte		M_flags;
    Monitor		*M_prevMonitor;
    Monitor		*M_nextMonitor;
    SemaphoreHandle	M_semMonitor;	/* ImRemoveMonitor waits on it */
};

/* add a monitor at a level; returns the input manager's thread */
extern ThreadHandle ImAddMonitor(Monitor *mon, byte level,
				 ImMonitorRoutine *routine);
/* remove it (MF_REMOVE_IMMEDIATE or MF_REMOVE_WHEN_EMPTY); never from
 * inside a monitor */
extern void ImRemoveMonitor(Monitor *mon, byte flags);
extern ThreadHandle ImInfoInputProcess(void);
/* the output of the input manager (the UI's flow object): TRUE if it was
 * free */
extern Boolean ImGrabInput(optr od);
extern void ImReleaseInput(optr od);
extern void ImSetDoubleClick(word time, word distance);
extern void ImInfoDoubleClick(word *time, word *distance);
/* the pointer position on the screen (no window: no transformation) */
extern void ImGetMousePos(sword *x, sword *y);
/* the buttons down (ButtonInfo B*_DOWN) */
extern byte ImGetButtonState(void);
extern void ImPtrJump(sword x, sword y);
extern void ImForcePtrMethod(void);
/* keep the pointer in a rectangle; buttons 0: always, else only while
 * one of these ButtonInfo bits is down; LEAVE_CONSTRAIN goes to od */
extern void ImConstrainMouse(sword left, sword top, sword right,
			     sword bottom, byte buttons, optr od);
extern void ImUnconstrainMouse(void);
/* Port32: the limits of the pointer without a root window
 * (ImSetPtrWin sets them from its bounds) */
extern void ImSetScreenLimits(sword left, sword top, sword right,
			      sword bottom);
/* the root window the pointer is on and where on it: the pointer's
 * limits are its bounds, its video driver draws the pointer */
extern void ImSetPtrWin(WindowHandle root, sword x, sword y);
/* the pointer image of a level (PtrImageLevel, win.h): an optr to a
 * PointerDef chunk, or 0 for none (ConstructOptr(0, PIV_UPDATE) loads
 * the current one again); the first level with an image is shown, the
 * video driver's default if none */
extern void ImSetPtrImage(word level, optr ptrDef);
/* ImSetPtrFlags: set, then clear PtrFlags; returns the old ones */
typedef ByteFlags PtrFlags;
#define PF_DISEMBODIED_PTR			0x80
#define PF_HIDE_PTR_IF_NOT_OF_ALWAYS_SHOW_TYPE	0x40
extern PtrFlags ImSetPtrFlags(PtrFlags set, PtrFlags clear);
extern PtrFlags ImGetPtrFlags(void);

#endif /* __IM_H */
