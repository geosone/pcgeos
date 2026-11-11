/*
 * objectMeta.c -- MetaClass, the root of all classes, and freeing
 * objects and object blocks (after objectMeta.asm).
 *
 * The kernel's classes are written by hand in the Port32 ClassStruct
 * format: message table, thunks (GocMethodThunk), no handler types.  A
 * thunk gets the parameters as goc -cg packs them (a structure of the
 * message's parameters in order) and the reply buffer.  Messages to a
 * thread or process reach MetaClass with pself NULL.
 *
 * Not yet: detach and quit (objectShutdown.asm), GCN lists, help and
 * options messages, MSG_META_OBJ_FLUSH_INPUT_QUEUE.  Without the input
 * manager, freeing an object or a block flushes only the queue of the
 * thread that runs it (the final message goes through that queue).
 */
#include "kernelC.h"

#include <string.h>

/* TravelOption (metaC.goh) */
enum { TO_NULL, TO_SELF, TO_OBJ_BLOCK_OUTPUT, TO_PROCESS };

static int IsObject(optr o)
{
    return OptrToChunk(o) != 0 &&
	   HandleEntryOf(OptrToHandle(o))->HE_type == HT_MEM;
}

/* the class of an object or of the thread a message went to */
static ClassStruct *ClassOfDest(optr o)
{
    if (IsObject(o))
	return ObjClassOf(o);
    return ThreadOf(KernelThisThread())->T_class;
}

static void MetaGetClass(void *pself, optr oself, Message m, void *params,
			 void *reply)
{
    (void) pself; (void) m; (void) params;
    *(ClassStruct **) reply = ClassOfDest(oself);
}

static void MetaIsObjectInClass(void *pself, optr oself, Message m,
				void *params, void *reply)
{
    ClassStruct *of = *(ClassStruct **) params;

    (void) pself; (void) m;
    *(Boolean *) reply = IsObject(oself)
			 ? ObjIsObjectInClass(oself, of)
			 : ObjIsClassADescendant(ClassOfDest(oself), of);
}

static void MetaGetOptr(void *pself, optr oself, Message m, void *params,
			void *reply)
{
    (void) pself; (void) m; (void) params;
    *(optr *) reply = oself;
}

static void MetaBlockFree(void *pself, optr oself, Message m, void *params,
			  void *reply)
{
    (void) pself; (void) m; (void) params; (void) reply;
    ObjFreeObjBlock(OptrToHandle(oself));
}

static void MetaObjFree(void *pself, optr oself, Message m, void *params,
			void *reply)
{
    (void) pself; (void) m; (void) params; (void) reply;
    /* the in-use count goes down in MSG_META_FINAL_OBJ_FREE */
    ObjIncInUseCount(OptrToHandle(oself));
    CObjMessageP(MF_FORCE_QUEUE, oself, KMSG_META_FINAL_OBJ_FREE, NULL, 0,
		 NULL, 0);
}

static void MetaFinalObjFree(void *pself, optr oself, Message m,
			     void *params, void *reply)
{
    (void) pself; (void) m; (void) params; (void) reply;
    ObjDecInUseCount(OptrToHandle(oself));
    ObjFreeChunk(oself);
}

static void MetaDecBlockRefCount(void *pself, optr oself, Message m,
				 void *params, void *reply)
{
    struct { MemHandle block1; MemHandle block2; } *p = params;

    (void) pself; (void) oself; (void) m; (void) reply;
    if (p->block1)
	MemDecRefCount(p->block1);
    if (p->block2)
	MemDecRefCount(p->block2);
}

static void MetaSetFlags(void *pself, optr oself, Message m, void *params,
			 void *reply)
{
    struct { ChunkHandle ch; ObjChunkFlags set; ObjChunkFlags clear; } *p =
	params;

    (void) pself; (void) m; (void) reply;
    ObjSetFlags(ConstructOptr(OptrToHandle(oself), p->ch), p->set,
		p->clear);
}

static void MetaGetFlags(void *pself, optr oself, Message m, void *params,
			 void *reply)
{
    ChunkHandle ch = *(ChunkHandle *) params;

    (void) pself; (void) m;
    *(word *) reply = ObjGetFlags(ConstructOptr(OptrToHandle(oself), ch));
}

static void MetaDispatchEvent(void *pself, optr oself, Message m,
			      void *params, void *reply)
{
    struct { EventHandle event; MessageFlags flags; } *p = params;

    (void) pself; (void) oself; (void) m; (void) reply;
    CMessageDispatchP(p->event, p->flags, NULL, 0);
}

/* Deliver a classed event: to this object if it is of the event's class
 * (none: any), to the block's output or the process, or nowhere. */
static void MetaSendClassedEvent(void *pself, optr oself, Message m,
				 void *params, void *reply)
{
    struct { EventHandle event; word whereTo; } *p = params;
    EventHandle ev = p->event;
    optr dest;
    ClassStruct *cls;

    (void) pself; (void) m;
    switch (p->whereTo) {
    case TO_NULL:
	break;
    case TO_OBJ_BLOCK_OUTPUT: {
	optr out = IsObject(oself) ? ObjBlockGetOutput(OptrToHandle(oself))
				   : 0;
	if (OptrToHandle(out)) {
	    struct { EventHandle event; word whereTo; } q;
	    q.event = ev;
	    q.whereTo = TO_SELF;
	    CObjMessageP(0, out, KMSG_META_SEND_CLASSED_EVENT, &q, sizeof(q),
			 NULL, 0);
	    return;
	}
	break;
    }
    case TO_PROCESS: {
	Handle proc = GeodeResourceHandle(
	    (GeodeHandle) MemGetInfo(OptrToHandle(oself), MGIT_OWNER_OR_VM_FILE_HANDLE),
	    GOCR_PROCESS_RES);
	if (proc) {
	    MessageSetDestination(ev, ConstructOptr(proc, 0));
	    CMessageDispatchP(ev, 0, NULL, 0);
	    return;
	}
	break;
    }
    default:
	ObjGetMessageInfo(ev, &dest);
	cls = (ClassStruct *) (uintptr_t) dest;
	if (!cls || (IsObject(oself) ? ObjIsObjectInClass(oself, cls)
			: ObjIsClassADescendant(ClassOfDest(oself), cls))) {
	    MessageSetDestination(ev, oself);
	    CMessageDispatchP(ev, MF_CALL, reply, 0);
	    return;
	}
	break;
    }
    ObjFreeMessage(ev);
}

/* ---- variable data ---- */

static void MetaAddVarData(void *pself, optr oself, Message m, void *params,
			   void *reply)
{
    struct { word dataType; word dataSize; void *data; } *p = params;
    void *d;

    (void) pself; (void) m; (void) reply;
    d = ObjVarAddData(oself, p->dataType, p->dataSize);
    if (p->data && p->dataSize)
	memcpy(d, p->data, p->dataSize);
    ObjMarkDirty(oself);
}

/* reply TRUE if it was not there (carry, as the asm kernel) */
static void MetaDeleteVarData(void *pself, optr oself, Message m,
			      void *params, void *reply)
{
    (void) pself; (void) m;
    *(Boolean *) reply = !ObjVarDeleteData(oself, *(word *) params);
}

/* MetaClass has no data to make: 0 (ObjVarDerefData fails) */
static void MetaInitializeVarData(void *pself, optr oself, Message m,
				  void *params, void *reply)
{
    (void) pself; (void) oself; (void) m; (void) params;
    *(word *) reply = 0;
}

/* copy out the data: the bytes copied, or 0xffff if not there */
static void MetaGetVarData(void *pself, optr oself, Message m, void *params,
			   void *reply)
{
    struct { word dataType; word bufSize; void *buf; } *p = params;
    void *d = ObjVarFindData(oself, p->dataType);
    word n;

    (void) pself; (void) m;
    if (!d) {
	*(word *) reply = 0xffff;
	return;
    }
    n = VarDataSizePtr(d);
    if (n > p->bufSize)
	n = p->bufSize;
    memcpy(p->buf, d, n);
    *(word *) reply = n;
}

static void MetaSetObjBlockOutput(void *pself, optr oself, Message m,
				  void *params, void *reply)
{
    (void) pself; (void) m; (void) reply;
    ObjBlockSetOutput(OptrToHandle(oself), *(optr *) params);
}

static void MetaGetObjBlockOutput(void *pself, optr oself, Message m,
				  void *params, void *reply)
{
    (void) pself; (void) m; (void) params;
    *(optr *) reply = ObjBlockGetOutput(OptrToHandle(oself));
}

static const Message metaMessages[] = {
    KMSG_META_GET_CLASS,
    KMSG_META_IS_OBJECT_IN_CLASS,
    KMSG_META_GET_OPTR,
    KMSG_META_BLOCK_FREE,
    KMSG_META_OBJ_FREE,
    KMSG_META_FINAL_OBJ_FREE,
    KMSG_META_DEC_BLOCK_REF_COUNT,
    KMSG_META_SET_FLAGS,
    KMSG_META_GET_FLAGS,
    KMSG_META_DISPATCH_EVENT,
    KMSG_META_SEND_CLASSED_EVENT,
    KMSG_META_ADD_VAR_DATA,
    KMSG_META_DELETE_VAR_DATA,
    KMSG_META_INITIALIZE_VAR_DATA,
    KMSG_META_GET_VAR_DATA,
    KMSG_META_SET_OBJ_BLOCK_OUTPUT,
    KMSG_META_GET_OBJ_BLOCK_OUTPUT
};

static GocMethodThunk *const metaMethods[] = {
    MetaGetClass,
    MetaIsObjectInClass,
    MetaGetOptr,
    MetaBlockFree,
    MetaObjFree,
    MetaFinalObjFree,
    MetaDecBlockRefCount,
    MetaSetFlags,
    MetaGetFlags,
    MetaDispatchEvent,
    MetaSendClassedEvent,
    MetaAddVarData,
    MetaDeleteVarData,
    MetaInitializeVarData,
    MetaGetVarData,
    MetaSetObjBlockOutput,
    MetaGetObjBlockOutput
};

ClassStruct MetaClass = {
    NULL,					/* the root */
    0,						/* no master levels */
    sizeof(metaMessages) / sizeof(metaMessages[0]),
    sizeof(ClassStruct *),			/* MetaBase */
    NULL, NULL,					/* nothing to relocate */
    CLASSF_NEVER_SAVED,
    1,						/* messages 0-8191 */
    metaMessages,
    (MessageMethod *const *) metaMethods,
    NULL,
    NULL
};

/* ---------------------------------------------------------------------
 * Freeing objects and blocks
 * ------------------------------------------------------------------- */

/* Free an object's chunk; an object of a resource keeps its handle with
 * no data and is marked dirty (so the state file knows it is gone). */
void ObjFreeChunk(optr o)
{
    if (ObjGetFlags(o) & OCF_IN_RESOURCE) {
	ObjSetFlags(o, OCF_DIRTY, 0);
	LMemReAlloc(o, 0);
    } else {
	LMemFree(o);
    }
}

/*
 * Free an object block once nothing uses it: with a non-zero in-use
 * count it is only marked LMF_AUTO_FREE (ObjDecInUseCount comes back);
 * otherwise the thread that runs it frees it after the messages it has
 * queued (MSG_PROCESS_FINAL_BLOCK_FREE).
 */
void ObjFreeObjBlock(MemHandle mh)
{
    ObjLMemBlockHeader *h = ObjLockObjBlock(mh);
    ThreadHandle exec;

    if (h->OLMBH_inUseCount) {
	if (h->OLMBH_header.LMBH_flags & LMF_AUTO_FREE)
	    KernelFatal(NESTED_FREE_OBJ_BLOCK);
	h->OLMBH_header.LMBH_flags |= LMF_AUTO_FREE;
	MemUnlock(mh);
	return;
    }
    if (h->OLMBH_header.LMBH_flags & LMF_DEATH_COUNT)
	KernelFatal(NESTED_FREE_OBJ_BLOCK);
    h->OLMBH_header.LMBH_flags++;		/* death count */
    MemUnlock(mh);
    exec = (ThreadHandle) MemGetInfo(mh, MGIT_EXEC_THREAD);
    if (!exec)
	exec = KernelThisThread();
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(exec, 0),
		 KMSG_PROCESS_FINAL_BLOCK_FREE, &mh, sizeof(mh), NULL, 0);
}
