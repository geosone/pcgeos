/*
 * imScreenSaver.c -- the screen saver of the input manager (after
 * imScreenSaver.asm, MaybeUnblankScreens of imMonitors.asm and
 * SysEnableAPO/SysDisableAPO of sysMisc.asm).
 *
 * Once enabled (MSG_IM_ENABLE_SCREEN_SAVER), a continual timer sends the
 * input manager MSG_IM_SCREEN_SAVER_COUNTDOWN every 15 seconds; each one
 * counts down from the delay (MSG_IM_SET_SCREEN_SAVER_DELAY, in minutes;
 * 15 at the start).  At zero the screens are blanked: the installed saver
 * (MSG_IM_INSTALL_SCREEN_SAVER) gets its begin message, or without one
 * the pointer's video driver gets DR_VID_SCREEN_OFF.  Input from the user
 * starts the count again and, if the screens are blank, brings them back
 * (the end message, or DR_VID_SCREEN_ON).  A pointer event that does not
 * move counts as the program's, not the user's.
 *
 * While an application is busy (SysDisableAPO), the count stays at one.
 * Enabling, disabling, blanking and unblanking with the messages go out
 * to GCNSLT_SCREEN_SAVER_NOTIFICATIONS (MSG_META_NOTIFY,
 * GWNT_SCREEN_SAVER_STATUS_NOTIFICATION, the ScrSaverStatus); input that
 * unblanks does not, as in the asm.
 *
 * Not yet: the power driver (DR_POWER_LONG_TERM_IDLE), which may turn
 * the machine off instead; the delay through SysGetInfo.
 */
#include "kernelC.h"
#include "imInt.h"

#include <input.h>
#include <timer.h>
#include <system.h>
#include <Internal/videoDr.h>

#define IM_DEFAULT_SCREEN_SAVER_COUNT		15	/* minutes */
#define INTERVAL_BETWEEN_SCREEN_SAVER_COUNTDOWN_EVENTS (15 * 60) /* ticks */
#define GCNSLT_SCREEN_SAVER_NOTIFICATIONS	72	/* gcnlist.goh */
/* GWNT_SCREEN_SAVER_STATUS_NOTIFICATION of geoworks.h, which clashes
 * with kernelC.h */
#define IM_GWNT_SCREEN_SAVER_STATUS		170

/* InitScreenSaver */
void ImInitScreenSaver(void)
{
    im.ssMaxCount = IM_DEFAULT_SCREEN_SAVER_COUNT * 4;
    im.screenSaverDelay = IM_DEFAULT_SCREEN_SAVER_COUNT * 4;
}

void SysDisableAPO(void)
{
    ghal_crit_enter();
    im.disableAPOCount++;
    ghal_crit_leave();
    if (!im.disableAPOCount)
	KernelFatal(DISABLE_APO_COUNT_OVERFLOW);
}

void SysEnableAPO(void)
{
    ghal_crit_enter();
    if (!im.disableAPOCount) {
	ghal_crit_leave();
	KernelFatal(DISABLE_APO_COUNT_OVERFLOW);
    }
    im.disableAPOCount--;
    ghal_crit_leave();
}

static void CallVideo(word function)
{
    DriverStrategy *s = (DriverStrategy *) im.ptrStrategy;
    DriverRegs r;

    if (!s)
	return;
    memset(&r, 0, sizeof(r));
    (void) s(function, &r);
}

/* SendSaverNotification */
static void SendSaverNotification(void)
{
    struct { word manuf, type, data; } p;
    EventHandle ev;

    p.manuf = MANUFACTURER_ID_GEOWORKS;
    p.type = IM_GWNT_SCREEN_SAVER_STATUS;
    p.data = im.ssState;
    CObjMessageP(MF_RECORD, 0, KMSG_META_NOTIFY, &p, sizeof(p), &ev,
		 sizeof(ev));
    (void) GCNListSend(MANUFACTURER_ID_GEOWORKS,
		       GCNSLT_SCREEN_SAVER_NOTIFICATIONS, ev, 0,
		       GCNLSF_FORCE_QUEUE);
}

/* ObjMessageScreenSaverOD: no parameters, to the end of its queue */
static void ToSaver(Message m)
{
    CObjMessageP(MF_FORCE_QUEUE, im.ssOD, m, NULL, 0, NULL, 0);
}

/* BlankScreens */
static void BlankScreens(void)
{
    if (OptrToHandle(im.ssOD))
	ToSaver(im.ssBeginMsg);
    else
	CallVideo(DR_VID_SCREEN_OFF);
}

/* UnBlankScreens */
static void UnBlankScreens(void)
{
    if (OptrToHandle(im.ssOD))
	ToSaver(im.ssEndMsg);
    else
	CallVideo(DR_VID_SCREEN_ON);
}

/* MaybeUnblankScreens: input from the user; FALSE if the screens were
 * blank (and are back now) */
Boolean ImMaybeUnblankScreens(void)
{
    im.ssCurCount = im.ssMaxCount;
    if (!(im.ssState & SSS_ACTIVE))
	return TRUE;
    UnBlankScreens();
    im.ssState &= (byte) ~SSS_ACTIVE;
    return FALSE;
}

/* IMSaverCountdownReachedZero */
static void ReachedZero(void)
{
    if (im.disableAPOCount) {
	im.ssCurCount++;	/* an application is busy: again next time */
	return;
    }
    BlankScreens();
    im.ssState |= SSS_ACTIVE;
    SendSaverNotification();
}

/* IMSaverCountdown */
static void Countdown(void)
{
    if (im.ssCurCount)
	im.ssCurCount--;
    if (!im.ssCurCount)
	ReachedZero();
}

/* IMSaverDeactivate */
static void Deactivate(void)
{
    word old = im.ssCurCount;

    im.ssCurCount = im.ssMaxCount;
    if (old)
	return;			/* was not active */
    UnBlankScreens();
    im.ssState &= (byte) ~SSS_ACTIVE;
    SendSaverNotification();
}

/* the IMClass methods of the screen saver */
void ImScreenSaverMethod(void *pself, optr oself, Message m, void *params,
			 void *reply)
{
    const IMScreenSaverParams *sp = params;

    (void) pself; (void) oself;
    switch (m) {
    case MSG_IM_SCREEN_SAVER_COUNTDOWN:
	Countdown();
	break;
    case MSG_IM_SCREEN_SAVER_COUNTDOWN_REACHED_ZERO:
	ReachedZero();
	break;
    case MSG_IM_ACTIVATE_SCREEN_SAVER:
	if (!(im.ssState & SSS_ACTIVE)) {
	    im.ssCurCount = 1;	/* count down a tick: that does it */
	    Countdown();
	}
	break;
    case MSG_IM_DEACTIVATE_SCREEN_SAVER:
	Deactivate();
	break;
    case MSG_IM_ENABLE_SCREEN_SAVER:
	if (im.ssState & SSS_ENABLED)
	    break;
	im.ssTimer = TimerStart(TIMER_EVENT_CONTINUAL,
				ConstructOptr(im.imThread, 0),
				INTERVAL_BETWEEN_SCREEN_SAVER_COUNTDOWN_EVENTS,
				MSG_IM_SCREEN_SAVER_COUNTDOWN,
				INTERVAL_BETWEEN_SCREEN_SAVER_COUNTDOWN_EVENTS,
				&im.ssTimerID);
	im.ssCurCount = im.ssMaxCount;
	im.ssState |= SSS_ENABLED;
	SendSaverNotification();
	break;
    case MSG_IM_DISABLE_SCREEN_SAVER:
	if (!(im.ssState & (SSS_ENABLED | SSS_ACTIVE)))
	    break;
	if (im.ssState & SSS_ACTIVE)
	    Deactivate();
	(void) TimerStop(im.ssTimer, im.ssTimerID);
	im.ssTimer = 0;
	im.ssState &= (byte) ~SSS_ENABLED;
	SendSaverNotification();
	break;
    case MSG_IM_GET_SCREEN_SAVER_DELAY:
	if (reply)
	    *(word *) reply = (word) (im.ssMaxCount >> 2);
	break;
    case MSG_IM_SET_SCREEN_SAVER_DELAY:
	/* minutes to counts of 15 seconds; the count starts again, so
	 * that the screens are not blanked before the new delay */
	im.ssMaxCount = im.screenSaverDelay = (word) (*(const word *) params
						      << 2);
	im.ssCurCount = im.ssMaxCount;
	break;
    case MSG_IM_INSTALL_SCREEN_SAVER:
    case MSG_IM_REMOVE_SCREEN_SAVER:
	if (im.ssState & SSS_ACTIVE)
	    UnBlankScreens();
	if (m == MSG_IM_INSTALL_SCREEN_SAVER) {
	    im.ssOD = sp->IMSSP_od;
	    im.ssBeginMsg = sp->IMSSP_beginMsg;
	    im.ssEndMsg = sp->IMSSP_endMsg;
	} else if (im.ssOD == sp->IMSSP_od)
	    im.ssOD = 0;
	if (im.ssState & SSS_ACTIVE)
	    BlankScreens();	/* the new saver */
	break;
    }
}
