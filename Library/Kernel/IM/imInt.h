/*
 * imInt.h -- the variables of the C input manager (after imVariable.def)
 * and what its files share.
 */
#ifndef IMINT_H
#define IMINT_H

#include <Internal/im.h>
#include <Internal/kbdDr.h>
#include <geosdriver.h>
#include <string.h>

#define IM_MAX_BUTTONS			4
#define IM_NUM_PTR_IMAGE_LEVELS		10	/* PIL_SYSTEM ... PIL_DEFAULT */
#define IM_DEFAULT_DOUBLE_CLICK_TIME	20	/* ticks */
#define IM_DEFAULT_DOUBLE_CLICK_DISTANCE 12	/* pixels */
#define IM_DEFAULT_DRAG_TIME		14
#define IM_DEFAULT_DRAG_DISTANCE	10

/* ButtonPressState: the last press of a button */
typedef struct {
    dword	BP_pressTime;
    sword	BP_pressX, BP_pressY;
} ImButtonPress;

typedef struct {
    ThreadHandle	imThread;
    optr		outputOD;	/* ImGrabInput */
    DriverInfoStruct	*kbdDriver;	/* the default keyboard driver */
    /* the pointer */
    sword		pointerX, pointerY;	/* raw position */
    sword		displayX, displayY;	/* after the combination */
    sword		changeX, changeY;	/* the last relative change */
    sword		drawnX, drawnY;		/* sent to the output */
    Boolean		haveScreen;		/* ImSetScreenLimits */
    sword		screenXMin, screenYMin, screenXMax, screenYMax;
    Boolean		ptrSent;
    word		lastSentBp;
    Boolean		drawnValid;		/* drawnX, drawnY */
    /* imPtr.c: the root window and its driver, the pointer images */
    WindowHandle	pointerWin;
    void		*ptrStrategy;
    optr		ptrImages[IM_NUM_PTR_IMAGE_LEVELS];
    optr		curPtrImage;
    Boolean		curPtrValid;
    byte		ptrFlags;	/* PtrFlags (imPtrFlags) */
    Boolean		hidingPtr;	/* PS_HIDING_PTR */
    /* the buttons */
    byte		buttonState;	/* BI_B*_DOWN */
    byte		dragState;	/* armed for a drag */
    byte		shiftState;	/* of the keyboard */
    Boolean		leftHanded;
    word		doubleClickTime, doubleClickDistance;
    word		dragTime, dragDistance;
    ImButtonPress	bps[IM_MAX_BUTTONS];
    Boolean		delayedRelease;
    ImEvent		delayedEvent;
    /* ImConstrainMouse */
    Boolean		constraining, perturbInstalled;
    byte		constrainButtons;
    sword		constrainXMin, constrainYMin;
    sword		constrainXMax, constrainYMax;
    optr		constrainOD;
    /* <Ctrl><Alt><Del> ([input] rebootOnReset, quickShutdownOnReset) */
    Boolean		rebootOnReset, alreadyReset;
} ImVars;

extern ImVars im;

void ImLockChain(void);
void ImUnlockChain(void);
void ImProcessUserInput(ImEvent *ev);
void ImInitMonitors(void);
byte ImPtrPerturbMonitor(Monitor *mon, ImEvent *ev);
/* imPtr.c: move the pointer in the driver; the save under areas
 * under it */
word ImCallMovePtr(sword x, sword y);

#endif /* IMINT_H */
