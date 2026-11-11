/*
 * objectShutdown.c -- detaching and quitting objects (after
 * objectShutdown.asm).
 *
 * Detach: an object that must wait for others before it is detached
 * calls ObjInitDetach (DETACH_DATA: an ack count, starting at 1, the
 * caller ID and ack optr, and the message that completes it),
 * ObjIncDetach for each message whose MSG_META_ACK it waits for, and
 * ObjEnableDetach when it is ready.  MSG_META_ACK counts down; at 0 the
 * object gets MSG_META_DETACH_COMPLETE (or SHUTDOWN_COMPLETE), and
 * MetaClass sends MSG_META_ACK (SHUTDOWN_ACK) to the ack optr through
 * the queue.  An object that never called ObjInitDetach acks at once
 * when MSG_META_DETACH reaches MetaClass.  Under Port32 the complete
 * messages carry the caller ID and ack optr as parameters (metaC.goh);
 * the asm kernel passed them in registers.
 *
 * Quit: MSG_META_QUIT is acked with MSG_META_QUIT_ACK.  An object with
 * an active list (MGCNLT_ACTIVE_LIST in its TEMP_META_GCN lists) sends
 * MSG_META_QUIT to each object on it in turn, each time it gets the
 * ack, keeping the ones it has asked in TEMP_META_QUIT_LIST, and ends
 * with MSG_META_FINISH_QUIT to itself.
 */
#include "kernelC.h"

#include <string.h>

typedef struct {			/* MSG_META_ACK, DETACH, ... */
    word	callerID;
    optr	ackOD;
} AckParams;

typedef struct {			/* MSG_META_QUIT_ACK */
    word	quitLevel;
    word	abortFlag;
} QuitAckParams;

static int IsObject(optr o)
{
    return OptrToChunk(o) != 0 &&
	   HandleEntryOf(OptrToHandle(o))->HE_type == HT_MEM;
}

void ObjInitDetach(Message msg, optr obj, word callerID, optr ackOD)
{
    Message complete;
    DetachDataEntry *d;

    if (msg == KMSG_META_DETACH)
	complete = KMSG_META_DETACH_COMPLETE;
    else if (msg == KMSG_META_APP_SHUTDOWN)
	complete = KMSG_META_SHUTDOWN_COMPLETE;
    else
	KernelFatal(BAD_DETACH_MSG);
    d = ObjVarFindData(obj, KVD_DETACH_DATA);
    if (d) {				/* from a superclass: once more */
	if (d->DDE_completeMsg != complete)
	    KernelFatal(OBJ_SHUTDOWN_OR_DETACH_WHILE_OTHER_STILL_ACTIVE);
	d->DDE_ackCount++;
	return;
    }
    d = ObjVarAddData(obj, KVD_DETACH_DATA, sizeof(DetachDataEntry));
    d->DDE_ackCount = 1;		/* until ObjEnableDetach */
    d->DDE_callerID = callerID;
    d->DDE_ackOD = ackOD;
    d->DDE_completeMsg = (word) complete;
}

void ObjIncDetach(optr obj)
{
    DetachDataEntry *d = ObjVarFindData(obj, KVD_DETACH_DATA);

    if (!d)
	KernelFatal(OBJ_DETACH_NOT_STARTED);
    d->DDE_ackCount++;
}

/* one ack fewer; at 0 the object gets its complete message */
void ObjEnableDetach(optr obj)
{
    DetachDataEntry *d = ObjVarFindData(obj, KVD_DETACH_DATA);
    AckParams p;
    Message complete;

    if (!d)
	KernelFatal(OBJ_DETACH_NOT_STARTED);
    if (--d->DDE_ackCount)
	return;
    complete = d->DDE_completeMsg;
    p.callerID = d->DDE_callerID;
    p.ackOD = d->DDE_ackOD;
    ObjVarDeleteData(obj, KVD_DETACH_DATA);
    CObjMessageP(0, obj, complete, &p, sizeof(p), NULL, 0);
}

/* MSG_META_ACK, MSG_META_SHUTDOWN_ACK: an object detaching counts it
 * (other acks, such as a thread's exit, need nothing here) */
void MetaAck(void *pself, optr oself, Message m, void *params, void *reply)
{
    (void) pself; (void) m; (void) params; (void) reply;
    if (IsObject(oself) && ObjVarFindData(oself, KVD_DETACH_DATA))
	ObjEnableDetach(oself);
}

/* the ack to whoever asked, through the queue (a little flush time) */
static void Completed(optr oself, void *params, Message ack)
{
    AckParams *p = params, q;

    if (!p)
	return;
    q.callerID = p->callerID;
    q.ackOD = oself;
    CObjMessageP(MF_FORCE_QUEUE, p->ackOD, ack, &q, sizeof(q), NULL, 0);
}

void MetaDetachComplete(void *pself, optr oself, Message m, void *params,
			void *reply)
{
    (void) pself; (void) m; (void) reply;
    Completed(oself, params, KMSG_META_ACK);
}

void MetaShutdownComplete(void *pself, optr oself, Message m, void *params,
			  void *reply)
{
    (void) pself; (void) m; (void) reply;
    Completed(oself, params, KMSG_META_SHUTDOWN_ACK);
}

/* MSG_META_DETACH reaching MetaClass: ack now, unless a subclass called
 * ObjInitDetach (then the last MSG_META_ACK does it) */
void MetaDetach(void *pself, optr oself, Message m, void *params,
		void *reply)
{
    (void) pself; (void) m; (void) reply;
    if (!IsObject(oself) || !ObjVarFindData(oself, KVD_DETACH_DATA))
	Completed(oself, params, KMSG_META_ACK);
}

void MetaAppShutdown(void *pself, optr oself, Message m, void *params,
		     void *reply)
{
    (void) pself; (void) m; (void) reply;
    if (!IsObject(oself) || !ObjVarFindData(oself, KVD_DETACH_DATA))
	Completed(oself, params, KMSG_META_SHUTDOWN_ACK);
}

/* MSG_META_QUIT (MSG_META_QUIT_OBJECT: the optr to ack) */
void MetaQuit(void *pself, optr oself, Message m, void *params, void *reply)
{
    QuitAckParams q = { 0, 0 };

    (void) pself; (void) m; (void) reply;
    if (!params || !IsObject(oself))
	return;				/* nobody to ack */
    CObjMessageP(MF_FORCE_QUEUE, *(optr *) params, KMSG_META_QUIT_ACK, &q,
		 sizeof(q), NULL, 0);
}

static void FinishQuit(optr oself, Boolean abort)
{
    CObjMessageP(MF_CALL, oself, KMSG_META_FINISH_QUIT, &abort,
		 sizeof(abort), NULL, 0);
}

/* the object's active list, 0 if none */
static ChunkHandle ActiveList(optr oself)
{
    TempMetaGCNData *g = ObjVarFindData(oself, KVD_TEMP_META_GCN);

    if (!g)
	return 0;
    return GCNListFindListInBlock(MANUFACTURER_ID_GEOWORKS,
				  MGCNLT_ACTIVE_LIST, OptrToHandle(oself),
				  g->TMGCND_listOfLists, FALSE);
}

static Boolean InList(optr list, optr item)
{
    word i, n = ChunkArrayGetCount(list);

    for (i = 0; i < n; i++)
	if (!memcmp(ChunkArrayElementToPtr(list, i, NULL), &item,
		    sizeof(item)))
	    return TRUE;
    return FALSE;
}

static void EndQuitList(optr oself, ChunkHandle quit)
{
    LMemFree(ConstructOptr(OptrToHandle(oself), quit));
    ObjVarDeleteData(oself, KVD_TEMP_META_QUIT_LIST);
}

/* MSG_META_QUIT_ACK: MSG_META_QUIT to the next object on the active list
 * not asked yet, or MSG_META_FINISH_QUIT */
void MetaQuitAck(void *pself, optr oself, Message m, void *params,
		 void *reply)
{
    QuitAckParams *p = params;
    MemHandle mh = OptrToHandle(oself);
    ChunkHandle *qv, quit, active;
    word i;

    (void) pself; (void) m; (void) reply;
    if (!IsObject(oself))
	return;
    if (p && p->abortFlag) {
	FinishQuit(oself, TRUE);
	return;
    }
    active = ActiveList(oself);
    qv = ObjVarFindData(oself, KVD_TEMP_META_QUIT_LIST);
    if (qv) {
	quit = *qv;
    } else {
	if (!active) {
	    FinishQuit(oself, FALSE);
	    return;
	}
	quit = ChunkArrayCreateAt(ConstructOptr(mh, 0), sizeof(optr), 0, 0);
	if (!quit)
	    KernelFatal(LMEM_CANNOT_ALLOCATE);
	qv = ObjVarAddData(oself, KVD_TEMP_META_QUIT_LIST,
			   sizeof(ChunkHandle));
	*qv = quit;
    }
    /* objects added to the active list meanwhile are asked too */
    for (i = 0; active && i < ChunkArrayGetCount(ConstructOptr(mh, active));
	 i++) {
	optr item, *e;
	memcpy(&item, ChunkArrayElementToPtr(ConstructOptr(mh, active), i,
					     NULL), sizeof(item));
	if (InList(ConstructOptr(mh, quit), item))
	    continue;
	e = ChunkArrayAppend(ConstructOptr(mh, quit), 0);
	memcpy(e, &item, sizeof(item));
	CObjMessageP(MF_FORCE_QUEUE, item, KMSG_META_QUIT, &oself,
		     sizeof(oself), NULL, 0);
	return;				/* until its ack */
    }
    EndQuitList(oself, quit);
    FinishQuit(oself, FALSE);
}

/* MSG_META_FINISH_QUIT: in a quit sequence, go on (or stop on abort) */
void MetaFinishQuit(void *pself, optr oself, Message m, void *params,
		    void *reply)
{
    ChunkHandle *qv;
    QuitAckParams q = { 0, 0 };

    (void) pself; (void) m; (void) reply;
    if (!IsObject(oself))
	return;
    qv = ObjVarFindData(oself, KVD_TEMP_META_QUIT_LIST);
    if (!qv)
	return;
    if (params && *(Boolean *) params)
	EndQuitList(oself, *qv);
    else
	CObjMessageP(MF_CALL, oself, KMSG_META_QUIT_ACK, &q, sizeof(q), NULL,
		     0);
}
