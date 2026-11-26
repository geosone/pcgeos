/*
 * sysNotification.c -- notifications to subsystems that may not be
 * loaded yet (after sysNotification.asm: SysSendNotification,
 * SysHookNotification, SysUnhookNotification, SysIgnoreNotification).
 *
 * Each SysSubsystemType (a multiple of 9) has an entry: hooked (the
 * routine that gets the notifications), ignored (they are dropped), or
 * neither, when the notifications wait in a queue of the kernel until
 * the subsystem hooks them (and gets them all, in order) or is ignored
 * (and they are thrown away, with the memory blocks they carry).
 *
 * A notification is four words (dwords in Port32, so a handle fits:
 * system.h); the SNF_WORD<n>_MEM bits of its type say which are memory
 * handles: those blocks become sharable and the
 * kernel's (owner 0) until the subsystem has them.  The routine gets the
 * subsystem, the type and the four words (the C convention of system.h;
 * DOS16 also had a register one for asm routines).  The entry's lock is
 * not held while the routine runs, as in the asm: notifications sent
 * meanwhile go to the end of the queue being delivered.
 *
 * Not for interrupt code, as in DOS16.
 */
#include "kernelC.h"
#include "sysVariable.h"
#include "sysConstant.h"

#include <system.h>
#include <object.h>
#include <geode.h>
#include <string.h>

static SemaphoreHandle tableSem;

/* SysPNotification: the entry, locked */
static SubsystemNotificationElement *Lock(SysSubsystemType subsys)
{
    EC_CHECK((word) subsys % 9 == 0 && (word) subsys <= SST_SOCKET,
	     NOT_VALID_MEMBER_OF_ENUMERATED_TYPE);
    if (!tableSem) {
	SemaphoreHandle s = ThreadAllocSem(1);
	ghal_crit_enter();
	if (!tableSem) {
	    tableSem = s;
	    s = 0;
	}
	ghal_crit_leave();
	if (s)
	    ThreadFreeSem(s);
    }
    ThreadPSem(tableSem);
    return &sysNotificationTable[(word) subsys / 9];
}

static void Unlock(void)
{
    ThreadVSem(tableSem);
}

/* the words of a notification that are memory handles */
static void ForBlocks(SysNotificationType notif, const dword *w,
		      Boolean free)
{
    int i;

    for (i = 0; i < 4; i++)
	if (notif & (SNF_WORD1_MEM >> i) && w[i]) {
	    if (free)
		MemFree((MemHandle) w[i]);
	    else {
		MemModifyFlags((MemHandle) w[i], HF_SHARABLE, 0);
		HandleModifyOwner((MemHandle) w[i], 0);
	    }
	}
}

/* the next queued notification: FALSE if none */
static Boolean Next(QueueHandle q, SysNotificationType *notif, dword *w)
{
    EventHandle ev;
    const void *p;
    dword size;

    if (!GeodeInfoQueue(q))
	return FALSE;
    ev = QueueGetMessage(q);
    *notif = (SysNotificationType) ObjGetMessageInfo(ev, NULL);
    p = KernelEventParams(ev, &size);
    memset(w, 0, 4 * sizeof(dword));
    memcpy(w, p, size < 4 * sizeof(dword) ? size : 4 * sizeof(dword));
    ObjFreeMessage(ev);
    return TRUE;
}

void SysSendNotification(SysSubsystemType subsys, SysNotificationType notif,
			 dword word1, dword word2, dword word3, dword word4)
{
    SubsystemNotificationElement *e = Lock(subsys);
    dword w[4];

    w[0] = word1;
    w[1] = word2;
    w[2] = word3;
    w[3] = word4;
    ForBlocks(notif, w, FALSE);
    switch (e->state) {
    case SNE_HOOKED: {
	SysNotifyRoutine *r = e->routine;
	Unlock();
	r(subsys, notif, word1, word2, word3, word4);
	return;
    }
    case SNE_IGNORED:
	break;
    case SNE_QUEUE:
	if (!e->queue) {
	    e->queue = GeodeAllocQueue();
	    HandleModifyOwner(e->queue, 0);
	}
	CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(e->queue, 0),
		     (Message) notif, w, sizeof(w), NULL, 0);
	break;
    }
    Unlock();
}

/* SysHookNotificationInternal: TRUE if hooked (FALSE: hooked or ignored
 * already); what waits is delivered first */
Boolean SysHookNotification(SysSubsystemType subsys,
			    SysNotificationRoutine *routine)
{
    SubsystemNotificationElement *e = Lock(subsys);
    SysNotificationType notif;
    dword w[4];

    if (e->state != SNE_QUEUE) {
	Unlock();
	return FALSE;
    }
    if (e->queue) {
	/* the entry keeps the queue while we deliver, so what comes in
	 * meanwhile is queued behind */
	while (Next(e->queue, &notif, w)) {
	    Unlock();
	    routine(subsys, notif, w[0], w[1], w[2], w[3]);
	    e = Lock(subsys);
	}
	GeodeFreeQueue(e->queue);
	e->queue = 0;
    }
    e->state = SNE_HOOKED;
    e->routine = routine;
    Unlock();
    return TRUE;
}

/* only the routine that has it unhooks; notifications wait again */
void SysUnhookNotification(SysSubsystemType subsys,
			   SysNotificationRoutine *routine)
{
    SubsystemNotificationElement *e = Lock(subsys);

    if (e->state == SNE_HOOKED && e->routine == routine) {
	e->state = SNE_QUEUE;
	e->routine = NULL;
    }
    Unlock();
}

/* TRUE if ignored now (FALSE: hooked); what waits is dropped */
Boolean SysIgnoreNotification(SysSubsystemType subsys)
{
    SubsystemNotificationElement *e = Lock(subsys);
    SysNotificationType notif;
    dword w[4];

    if (e->state == SNE_HOOKED) {
	Unlock();
	return FALSE;
    }
    if (e->queue) {
	while (Next(e->queue, &notif, w))
	    ForBlocks(notif, w, TRUE);
	GeodeFreeQueue(e->queue);
	e->queue = 0;
    }
    e->state = SNE_IGNORED;
    Unlock();
    return TRUE;
}
