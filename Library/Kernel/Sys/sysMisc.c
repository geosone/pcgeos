/*
 * sysMisc.c -- SysShutdown (after SysShutdown of sysMisc.asm and
 * _SysShutdown of sysC.asm).
 *
 *	SysShutdown(SST_CLEAN, optr ackOD, Message ackMsg)
 *	SysShutdown(SST_SUSPEND, optr ackOD, Message ackMsg)
 *	SysShutdown(SST_CLEAN_REBOOT, optr ackOD, Message ackMsg)
 *	SysShutdown(SST_POWER_OFF, optr ackOD, Message ackMsg)
 *	SysShutdown(SST_DIRTY, const char *reason)	(NULL: no reason)
 *	SysShutdown(SST_FINAL, const char *reason)
 *	SysShutdown(SST_CONFIRM_END, Boolean allowShutdown)
 *	SysShutdown(SST_CLEAN_FORCED / SST_PANIC / SST_REBOOT / SST_RESTART /
 *		    SST_CONFIRM_START)
 *
 * A clean shutdown or suspend asks the objects on the system GCN list
 * GCNSLT_SHUTDOWN_CONTROL with MSG_META_CONFIRM_SHUTDOWN(confirmed,
 * type).  Each answers with SST_CONFIRM_START (which gives it the right
 * to ask the user, one at a time; TRUE if someone refused already) and
 * SST_CONFIRM_END (FALSE refuses).  When the last answer is in, ackOD
 * gets ackMsg with { word confirmed }; without an ackOD the UI gets
 * MSG_META_DETACH if all agreed.  Only one clean shutdown or suspend
 * runs at a time: another one returns TRUE.
 *
 * SST_CLEAN_FORCED detaches the UI, which comes back with SST_FINAL;
 * without a UI it ends at once.  SST_DIRTY, SST_FINAL, SST_PANIC and
 * SST_REBOOT end GEOS (EndGeos, bootBoot.c): they return only to the boot
 * thread.
 *
 * SST_RESTART returns TRUE: the C kernel cannot reload itself (DOS16
 * returned the same when loader.exe could not be found).  Not yet: the
 * task driver (EF_RUN_DOS).
 */
#include "kernelC.h"
#include <system.h>

#include <stdarg.h>
#include <string.h>

#define SST_POWER_OFF_TYPE	(SST_CLEAN_REBOOT + 1)	/* system.def */
#define KMSG_META_CONFIRM_SHUTDOWN	702		/* gcnlist.goh */
#define GCNSLT_SHUTDOWN_CONTROL_LIST	14
#define GCNSCT_SUSPEND_TYPE	0
#define GCNSCT_SHUTDOWN_TYPE	1

static KSem broadcastSem = { 1, 0 };	/* shutdownBroadcastSem */
static KSem confirmSem = { 1, 0 };	/* shutdownConfirmSem */
static dword confirmCount;		/* shutdownConfirmCount */
static Boolean shutdownOK = TRUE;
static optr ackOD;
static Message ackMsg;

typedef struct {
    word	confirmed;
    int		type;		/* GCNShutdownControlType */
} ConfirmShutdownParams;

typedef struct {
    word	callerID;
    optr	ackOD;
} DetachParams;

static void SendAck(Boolean ok)
{
    word confirmed = ok;
    DetachParams dp;

    if (OptrToHandle(ackOD)) {
	CObjMessageP(0, ackOD, ackMsg, &confirmed, sizeof(confirmed), NULL, 0);
	return;
    }
    /* nobody to tell: the UI detaches if all agreed */
    if (ok && kernelUIHandle) {
	dp.callerID = 0;
	dp.ackOD = 0;
	CObjMessageP(0, ConstructOptr(kernelUIHandle, 0), KMSG_META_DETACH,
		     &dp, sizeof(dp), NULL, 0);
    }
}

static Boolean ConfirmEnd(Boolean allow)
{
    Boolean ok;

    if (!allow)
	shutdownOK = FALSE;
    KSemV(&confirmSem);
    KSemP(&broadcastSem, KSEM_FOREVER);
    if (confirmCount && !--confirmCount) {
	ok = shutdownOK;
	shutdownOK = TRUE;
	SendAck(ok);
    }
    KSemV(&broadcastSem);
    return FALSE;
}

static Boolean CleanOrSuspend(int gcnType, optr od, Message msg)
{
    ConfirmShutdownParams p;
    EventHandle ev;
    word n;

    KSemP(&broadcastSem, KSEM_FOREVER);
    if (confirmCount) {
	KSemV(&broadcastSem);
	return TRUE;
    }
    ackOD = od;
    ackMsg = msg;
    /* 1, plus 10000 against answers that come in before the count of the
     * list is known */
    confirmCount = 10001;
    KSemV(&broadcastSem);

    p.confirmed = 0;
    p.type = gcnType;
    CObjMessageP(MF_RECORD, 0, KMSG_META_CONFIRM_SHUTDOWN, &p, sizeof(p),
		 &ev, sizeof(ev));
    n = GCNListSend(MANUFACTURER_ID_GEOWORKS, GCNSLT_SHUTDOWN_CONTROL_LIST,
		    ev, 0, 0);
    KSemP(&broadcastSem, KSEM_FOREVER);
    confirmCount = confirmCount + n - 10000;
    KSemV(&broadcastSem);
    /* our own answer: we allow it */
    KSemP(&confirmSem, KSEM_FOREVER);
    return ConfirmEnd(TRUE);
}

Boolean SysShutdown(SysShutdownType type, ...)
{
    va_list ap;
    optr od = 0;
    Message msg = 0;
    const char *reason = NULL;
    Boolean allow = TRUE;
    DetachParams dp;

    va_start(ap, type);
    switch ((int) type) {
    case SST_CLEAN:
    case SST_SUSPEND:
    case SST_CLEAN_REBOOT:
    case SST_POWER_OFF_TYPE:
	od = va_arg(ap, optr);
	msg = (Message) va_arg(ap, unsigned int);
	break;
    case SST_DIRTY:
    case SST_FINAL:
	reason = va_arg(ap, const char *);
	break;
    case SST_CONFIRM_END:
	allow = (Boolean) va_arg(ap, int);
	break;
    default:
	break;
    }
    va_end(ap);

    switch ((int) type) {
    case SST_DIRTY:
	EndGeos(KEF_DIRTY, reason);
	return FALSE;
    case SST_FINAL:
	EndGeos(0, reason);
	return FALSE;
    case SST_PANIC:
	EndGeos(KEF_DIRTY, NULL);
	return FALSE;
    case SST_REBOOT:
	EndGeos(KEF_RESET, NULL);
	return FALSE;
    case SST_RESTART:
	return TRUE;
    case SST_CLEAN_FORCED:
	if (!kernelUIHandle) {
	    EndGeos(0, NULL);
	    return FALSE;
	}
	dp.callerID = 0;
	dp.ackOD = 0;
	CObjMessageP(0, ConstructOptr(kernelUIHandle, 0), KMSG_META_DETACH,
		     &dp, sizeof(dp), NULL, 0);
	return FALSE;
    case SST_POWER_OFF_TYPE:
	kernelExitFlags |= KEF_POWER_OFF;
	return CleanOrSuspend(GCNSCT_SHUTDOWN_TYPE, od, msg);
    case SST_CLEAN_REBOOT:
	kernelExitFlags |= KEF_RESET;
	return CleanOrSuspend(GCNSCT_SHUTDOWN_TYPE, od, msg);
    case SST_CLEAN:
	return CleanOrSuspend(GCNSCT_SHUTDOWN_TYPE, od, msg);
    case SST_SUSPEND:
	return CleanOrSuspend(GCNSCT_SUSPEND_TYPE, od, msg);
    case SST_CONFIRM_START:
	KSemP(&confirmSem, KSEM_FOREVER);
	if (shutdownOK)
	    return FALSE;
	ConfirmEnd(FALSE);
	return TRUE;
    case SST_CONFIRM_END:
	return ConfirmEnd(allow);
    default:
	KernelFatal(BAD_SHUTDOWN_TYPE);
	return TRUE;
    }
}
