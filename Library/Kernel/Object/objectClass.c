/*
 * objectClass.c -- calling methods (after objectClass.asm and the master
 * part routines of objectUtils.asm).
 *
 * An object is a chunk that starts with its class (MetaBase) and one
 * word per master level: the offset of that master part, 0 while it is
 * not built.  A class's Class_masterOffset is the offset of that word
 * (0: no master level, pself is the chunk).  The search for a method
 * walks from the object's class up:
 *
 *  - a master part that is not built yet is built when a class of its
 *    level is reached (ObjInitializeMaster: grow, MSG_META_INITIALIZE);
 *  - MSG_META_INITIALIZE is not inherited across a master class;
 *  - above a variant master class the superclass is the class stored at
 *    the start of its master part; while that is 0 the object is asked
 *    with MSG_META_RESOLVE_VARIANT_SUPERCLASS (ResolveVariant).
 *
 * Methods are called through their thunks (Port32/ABI.md section 5).
 * ObjInstantiate makes objects (the master parts are built on demand).
 * Messages to a thread, queue or process (chunk 0) go to the class the
 * running thread was attached with (ThreadAttachToQueue).
 */
#include "kernelC.h"

#include <string.h>

static byte *Obj(optr o)
{
    return LMemDeref(o);
}

static word *MasterWord(optr o, word masterOffset)
{
    return (word *) (Obj(o) + masterOffset);
}

static ClassStruct *ClassOfObj(optr o)
{
    return *(ClassStruct **) Obj(o);
}

static int CallClass(ClassStruct *cls, optr oself, int isObj, Message msg,
		     const void *params, void *reply);

void ObjInitializeMaster(optr o, ClassStruct *cls)
{
    ObjResizeMaster(o, cls->Class_masterOffset, cls->Class_instanceSize);
    CallClass(cls, o, 1, KMSG_META_INITIALIZE, NULL, NULL);
}

/* the superclass of a variant master class `cls' for this object */
ClassStruct *ObjResolveVariant(optr o, ClassStruct *cls)
{
    ClassStruct **slot;
    ClassStruct *super;
    struct { word masterOffset; } p;

    p.masterOffset = cls->Class_masterOffset;
    slot = (ClassStruct **) (Obj(o) + *MasterWord(o, cls->Class_masterOffset));
    if (*slot)
	return *slot;
    super = NULL;
    CallClass(ClassOfObj(o), o, 1, KMSG_META_RESOLVE_VARIANT_SUPERCLASS, &p,
	      &super);
    if (!super)
	KernelFatal(VARIANT_SUPERCLASS_NOT_RESOLVED);
    slot = (ClassStruct **) (Obj(o) + *MasterWord(o, cls->Class_masterOffset));
    *slot = super;
    return super;
}

/* ObjCallMethodTable: TRUE if a method handled it */
static int CallClass(ClassStruct *cls, optr oself, int isObj, Message msg,
		     const void *params, void *reply)
{
    byte scratch[256];
    void *rp = reply ? reply : scratch;

    while (cls) {
	word i;
	if (isObj && cls->Class_masterOffset &&
	    !*MasterWord(oself, cls->Class_masterOffset))
	    ObjInitializeMaster(oself, cls);
	for (i = 0; i < cls->Class_methodCount; i++)
	    if (cls->Class_messages[i] == msg) {
		void *pself = NULL;
		if (isObj) {
		    pself = Obj(oself);
		    if (cls->Class_masterOffset)
			pself = (byte *) pself +
				*MasterWord(oself, cls->Class_masterOffset);
		}
		((GocMethodThunk *) cls->Class_methods[i])
		    (pself, oself, msg, (void *) params, rp);
		return 1;
	    }
	if (cls->Class_flags & CLASSF_MASTER_CLASS) {
	    if (msg == KMSG_META_INITIALIZE)
		return 0;		/* not inherited across masters */
	    if (cls->Class_superClass == OBJ_VARIANT_SUPER) {
		if (!isObj)
		    return 0;
		cls = ObjResolveVariant(oself, cls);
		continue;
	    }
	}
	cls = cls->Class_superClass;
    }
    return 0;
}

/* the class a thread, queue or process message goes to */
static ClassStruct *ThreadClass(void)
{
    return kernelCurrentThread ? ThreadOf(kernelCurrentThread)->T_class
			       : NULL;
}

/* Call the method for `msg' of `dest', run by the current thread. */
void KernelDeliver(optr dest, Message msg, const void *params, void *reply)
{
    Handle h = OptrToHandle(dest);
    HandleEntry *he = HandleEntryOf(h);

    if (OptrToChunk(dest) == 0 || he->HE_type != HT_MEM) {
	CallClass(ThreadClass(), dest, 0, msg, params, reply);
	return;
    }
    MemLock(h);
    CallClass(ClassOfObj(dest), dest, 1, msg, params, reply);
    MemUnlock(h);
}

/* @callsuper: start above `superOf' (the class of the calling method) */
void *CObjCallSuperP(ClassStruct *superOf, optr dest, Message message,
		     const void *params, dword paramSize,
		     void *reply, dword replySize)
{
    Handle h = OptrToHandle(dest);
    int isObj = OptrToChunk(dest) != 0 &&
		HandleEntryOf(h)->HE_type == HT_MEM;
    ClassStruct *cls;

    (void) paramSize;
    (void) replySize;
    if (isObj)
	MemLock(h);
    cls = superOf->Class_superClass;
    if (cls == OBJ_VARIANT_SUPER)
	cls = isObj ? ObjResolveVariant(dest, superOf) : NULL;
    if (!(superOf->Class_flags & CLASSF_MASTER_CLASS) ||
	message != KMSG_META_INITIALIZE)
	CallClass(cls, dest, isObj, message, params, reply);
    if (isObj)
	MemUnlock(h);
    return reply;
}

/* The object with the master part at `masterOffset' (0: the chunk). */
void *ObjDeref(optr obj, word masterOffset)
{
    byte *p = Obj(obj);

    return masterOffset ? p + *(word *) (p + masterOffset) : p;
}

void *ObjDeref1(optr obj)
{
    return ObjDeref(obj, sizeof(ClassStruct *));
}

void *ObjDeref2(optr obj)
{
    return ObjDeref(obj, sizeof(ClassStruct *) + sizeof(word));
}

/* TRUE if `cls' is `of' or below it (variants: the static chain only) */
Boolean ObjIsClassADescendant(ClassStruct *cls, ClassStruct *of)
{
    for (; cls && cls != OBJ_VARIANT_SUPER; cls = cls->Class_superClass)
	if (cls == of)
	    return TRUE;
    return FALSE;
}

/* the next class up for object `o' (0: none or an unresolved variant) */
ClassStruct *ObjSuperOf(optr o, ClassStruct *cls)
{
    word mo;

    if (cls->Class_superClass != OBJ_VARIANT_SUPER)
	return cls->Class_superClass;
    mo = cls->Class_masterOffset;
    if (!OptrToChunk(o) || !*MasterWord(o, mo))
	return NULL;
    return *(ClassStruct **) (Obj(o) + *MasterWord(o, mo));
}

/* ClassInClassCommon: is the object's class `of' or below it? */
Boolean ObjIsObjectInClass(optr obj, ClassStruct *of)
{
    ClassStruct *cls;

    for (cls = ClassOfObj(obj); cls; cls = ObjSuperOf(obj, cls)) {
	if (of->Class_masterOffset > cls->Class_masterOffset)
	    return FALSE;
	if (cls == of)
	    return TRUE;
    }
    return FALSE;
}

/*
 * A new object of class `cls' in object block `block'.  With master
 * parts only the base structure is allocated (the parts are built when
 * a message reaches them); without, the whole instance, and the object
 * gets MSG_META_INITIALIZE.  A block run by another thread asks that
 * thread (MSG_PROCESS_INSTANTIATE).
 */
optr ObjInstantiate(MemHandle block, ClassStruct *cls)
{
    ThreadHandle exec = (ThreadHandle) MemGetInfo(block, MGIT_EXEC_THREAD);
    ChunkHandle ch;
    word size;
    byte *p;

    if (exec && exec != KernelThisThread()) {
	struct { MemHandle block; ClassStruct *cls; } params;
	ChunkHandle reply = 0;
	params.block = block;
	params.cls = cls;
	CObjMessageP(MF_CALL, ConstructOptr(exec, 0),
		     KMSG_PROCESS_INSTANTIATE, &params, sizeof(params),
		     &reply, sizeof(reply));
	return ConstructOptr(block, reply);
    }
    size = cls->Class_masterOffset
	   ? (word) (cls->Class_masterOffset + sizeof(word))
	   : cls->Class_instanceSize;
    MemLock(block);
    ch = LMemAlloc(block, size);
    p = LMemDeref(ConstructOptr(block, ch));
    memset(p, 0, size);
    *(ClassStruct **) p = cls;
    ObjSetFlags(ConstructOptr(block, ch), OCF_IS_OBJECT | OCF_DIRTY, 0);
    if (!cls->Class_masterOffset)
	CallClass(cls, ConstructOptr(block, ch), 1, KMSG_META_INITIALIZE,
		  NULL, NULL);
    MemUnlock(block);
    return ConstructOptr(block, ch);
}

/* the header of the blocks of ObjInstantiateForThread */
typedef struct {
    ObjLMemBlockHeader	OOLMBH_header;
    MemHandle		OOLMBH_dynamicObjBlock;	/* next block */
} OIFTObjLMemBlockHeader;

#define DESIRED_OBJECT_BLOCK_SIZE	0x1000

/* A new object run by `thread' (0: this one), in a block of its own
 * that is not saved to state. */
optr ObjInstantiateForThread(ThreadHandle thread, ClassStruct *cls)
{
    KThread *t;
    MemHandle mh;

    if (thread && thread != KernelThisThread()) {
	optr reply = 0;
	CObjMessageP(MF_CALL, ConstructOptr(thread, 0),
		     KMSG_PROCESS_INSTANTIATE_FOR_THREAD, &cls, sizeof(cls),
		     &reply, sizeof(reply));
	return reply;
    }
    t = ThreadOf(KernelThisThread());
    for (mh = t->T_dynamicObjBlock; mh; ) {
	OIFTObjLMemBlockHeader *h = MemLock(mh);
	word used = (word) (h->OOLMBH_header.OLMBH_header.LMBH_blockSize -
			    h->OOLMBH_header.OLMBH_header.LMBH_totalFree);
	MemHandle next = h->OOLMBH_dynamicObjBlock;
	MemUnlock(mh);
	if (used < DESIRED_OBJECT_BLOCK_SIZE)
	    return ObjInstantiate(mh, cls);
	mh = next;
    }
    mh = MemAllocLMem(LMEM_TYPE_OBJ_BLOCK, sizeof(OIFTObjLMemBlockHeader));
    MemModifyOtherInfo(mh, KernelThisThread());
    ((OIFTObjLMemBlockHeader *) MemLock(mh))->OOLMBH_dynamicObjBlock =
	t->T_dynamicObjBlock;
    MemUnlock(mh);
    t->T_dynamicObjBlock = mh;
    return ObjInstantiate(mh, cls);
}
