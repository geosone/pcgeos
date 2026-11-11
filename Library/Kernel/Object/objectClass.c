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
 * Messages to a thread, queue or process (chunk 0) go to the class the
 * running thread was attached with (ThreadAttachToQueue).
 */
#include "kernelC.h"

#define VARIANT_SUPER	((ClassStruct *) 1)	/* GOCR_VARIANT_SUPERCLASS */

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

/* Resize the master part whose offset word is at `masterOffset' to
 * `size' bytes; the later parts move (objectUtils.asm). */
void ObjResizeMaster(optr o, word masterOffset, word size)
{
    word last = ClassOfObj(o)->Class_masterOffset;
    word start, end, cur, at;
    sword delta;

    if (masterOffset >= last) {
	/* the last part: the class's instance size (variable data may
	 * follow); not built: the end of the base structure */
	start = *MasterWord(o, masterOffset);
	end = start ? (word) (start + ClassOfObj(o)->Class_instanceSize)
		    : (word) (last + sizeof(word));
    } else {
	/* up to the next part that is built */
	word next = masterOffset;
	do {
	    next += sizeof(word);
	    end = *MasterWord(o, next);
	} while (!end && next < last);
	if (!end) {
	    if (*MasterWord(o, masterOffset))
		KernelFatal(OBJ_BAD_MASTER_PARTS);
	    end = (word) (last + sizeof(word));
	}
    }
    if (!*MasterWord(o, masterOffset))
	*MasterWord(o, masterOffset) = end;
    start = *MasterWord(o, masterOffset);
    cur = (word) (end - start);
    delta = (sword) (size - cur);
    if (delta == 0)
	return;
    if (delta > 0) {
	at = end;
	if (LMemInsertAt(o, at, (word) delta))
	    KernelFatal(LMEM_CANNOT_ALLOCATE);
    } else {
	at = (word) (end + delta);
	LMemDeleteAt(o, at, (word) -delta);
    }
    /* the parts after this one moved */
    for (cur = (word) (masterOffset + sizeof(word)); cur <= last;
	 cur += sizeof(word))
	if (*MasterWord(o, cur))
	    *MasterWord(o, cur) = (word) (*MasterWord(o, cur) + delta);
}

static int CallClass(ClassStruct *cls, optr oself, int isObj, Message msg,
		     const void *params, void *reply);

void ObjInitializeMaster(optr o, ClassStruct *cls)
{
    ObjResizeMaster(o, cls->Class_masterOffset, cls->Class_instanceSize);
    CallClass(cls, o, 1, KMSG_META_INITIALIZE, NULL, NULL);
}

/* the superclass of a variant master class `cls' for this object */
static ClassStruct *ResolveVariant(optr o, ClassStruct *cls)
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
	    if (cls->Class_superClass == VARIANT_SUPER) {
		if (!isObj)
		    return 0;
		cls = ResolveVariant(oself, cls);
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
    if (cls == VARIANT_SUPER)
	cls = isObj ? ResolveVariant(dest, superOf) : NULL;
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
    for (; cls && cls != VARIANT_SUPER; cls = cls->Class_superClass)
	if (cls == of)
	    return TRUE;
    return FALSE;
}
