/*
 * objectComposite.c -- composite objects (after objectComposite.asm;
 * the routines of objectC.asm are in objectC.c).
 *
 * A composite holds its first child in a CompPart; each child holds the
 * next sibling in its LinkPart, and the last child holds the composite
 * with LP_IS_PARENT set in the chunk.  An empty composite has 0, as has
 * the link of an object that is in no composite.  Both parts lie in the
 * master part at `masterOffset' (0: in the chunk), at `compOffset' and
 * `linkOffset'.  Children may lie in other blocks; each object is
 * reached through its block, which is locked while it is used, and the
 * pointers are not kept across calls that may move blocks.
 */
#include "kernelC.h"

#include <string.h>

/* the start of the master part (0: not built) */
static byte *Part(optr o, word masterOffset)
{
    byte *p = LMemDeref(o);
    word off;

    if (!p)
	KernelFatal(NOT_AN_OBJECT);
    if (!masterOffset)
	return p;
    off = *(word *) (p + masterOffset);
    return off ? p + off : NULL;
}

/* the optr at `offset' in the master part (0 if it is not built) */
static optr GetField(optr o, word masterOffset, word offset)
{
    byte *p;
    optr v = 0;

    ObjLockObjBlock(OptrToHandle(o));
    p = Part(o, masterOffset);
    if (p)
	memcpy(&v, p + offset, sizeof(v));
    MemUnlock(OptrToHandle(o));
    return v;
}

static void SetField(optr o, word masterOffset, word offset, optr v)
{
    byte *p;

    ObjLockObjBlock(OptrToHandle(o));
    p = Part(o, masterOffset);
    if (!p)
	KernelFatal(OBJ_BAD_MASTER_PARTS);
    memcpy(p + offset, &v, sizeof(v));
    MemUnlock(OptrToHandle(o));
}

int ObjIsParentLink(optr link)
{
    return (OptrToChunk(link) & LP_IS_PARENT) != 0;
}

static optr ParentLink(optr parent)
{
    return parent | LP_IS_PARENT;
}

optr ObjPlaceGet(const ObjCompPlace *pl, word masterOffset)
{
    return GetField(pl->owner, masterOffset, pl->offset);
}

static void PlaceSet(const ObjCompPlace *pl, word masterOffset, optr v)
{
    SetField(pl->owner, masterOffset, pl->offset, v);
}

/*
 * FindObjLocation: the place of child number `n' (CCO_LAST: after the
 * last child).
 */
ObjCompPlace ObjFindLocation(optr comp, word n, word masterOffset,
			  word compOffset, word linkOffset)
{
    ObjCompPlace pl;
    optr v;

    pl.owner = comp;
    pl.offset = compOffset;
    while (n) {
	v = ObjPlaceGet(&pl, masterOffset);
	if (!OptrToHandle(v) || ObjIsParentLink(v))
	    break;
	pl.owner = v;
	pl.offset = linkOffset;
	n--;
    }
    return pl;
}

/*
 * FindChildLow: the place that holds `child' and its position.  FALSE if
 * it is not a child; *pos is then the number of children.
 */
Boolean ObjCompFindChildLow(optr comp, optr child, word masterOffset,
			 word compOffset, word linkOffset, ObjCompPlace *pl,
			 word *pos)
{
    optr v;

    pl->owner = comp;
    pl->offset = compOffset;
    *pos = 0;
    for (;;) {
	v = ObjPlaceGet(pl, masterOffset);
	if (!OptrToHandle(v) || ObjIsParentLink(v))
	    return FALSE;
	if (v == child)
	    return TRUE;
	pl->owner = v;
	pl->offset = linkOffset;
	(*pos)++;
    }
}

void ObjCompAddChild(optr obj, optr objToAdd, word flags, word masterOffset,
		     word compOffset, word linkOffset)
{
    ObjCompPlace pl;
    optr at, link;
    byte *p;

    if (masterOffset) {			/* the composite's part */
	p = LMemDeref(obj);
	if (!*(word *) (p + masterOffset))
	    ObjInitializePart(obj, masterOffset);
	p = LMemDeref(objToAdd);
	if (!p)
	    KernelFatal(NOT_AN_OBJECT);
	if (!*(word *) (p + masterOffset))
	    ObjInitializePart(objToAdd, masterOffset);
    }
    if (OptrToHandle(GetField(objToAdd, masterOffset, linkOffset)))
	KernelFatal(ADD_CHILD_OF_OBJ_ALREADY_IN_COMPOSITE);

    pl = ObjFindLocation(obj, (word) (flags & CCF_REFERENCE), masterOffset,
		      compOffset, linkOffset);
    at = ObjPlaceGet(&pl, masterOffset);
    link = OptrToHandle(at) ? at : ParentLink(obj);
    PlaceSet(&pl, masterOffset, objToAdd);
    SetField(objToAdd, masterOffset, linkOffset, link);
    if (flags & CCF_MARK_DIRTY) {
	ObjMarkDirty(pl.owner);
	ObjMarkDirty(objToAdd);
    }
}

void ObjCompRemoveChild(optr obj, optr objToRemove, word flags,
			word masterOffset, word compOffset, word linkOffset)
{
    ObjCompPlace pl;
    word pos;
    optr next;
    Boolean found;

    found = ObjCompFindChildLow(obj, objToRemove, masterOffset, compOffset,
		      linkOffset, &pl, &pos);
    next = GetField(objToRemove, masterOffset, linkOffset);
    SetField(objToRemove, masterOffset, linkOffset, 0);
    if (!found)
	return;				/* a one-way link: only cleared */
    if (flags & CCF_MARK_DIRTY) {
	ObjMarkDirty(pl.owner);
	ObjMarkDirty(objToRemove);
    }
    if (pos == 0 && ObjIsParentLink(next))
	next = 0;			/* it was the only child */
    PlaceSet(&pl, masterOffset, next);
}

void ObjCompMoveChild(optr obj, optr objToMove, word flags,
		      word masterOffset, word compOffset, word linkOffset)
{
    ObjCompRemoveChild(obj, objToMove, flags, masterOffset, compOffset,
		       linkOffset);
    ObjCompAddChild(obj, objToMove, flags, masterOffset, compOffset,
		    linkOffset);
}

/*
 * Call `callback' for each child from `firstChild' on (0: from child
 * number OptrToChunk(firstChild)), until it returns TRUE (then TRUE).
 * The next sibling is taken before the call, so a callback may remove
 * the child it gets.  Without a callback only OCCT_COUNT_CHILDREN means
 * something in C: the number of children is added to *(word *) cbData.
 * The other types send the message in registers in the asm kernel; in
 * C, CObjSendToChildren and @send @children do that.
 */
Boolean ObjCompProcessChildren(optr obj, optr firstChild,
			       ObjCompCallType stdCallback, void *cbData,
			       word masterOffset, word compOffset,
			       word linkOffset, ObjCompCallback *callback)
{
    optr child, next;
    word countdown = 0;

    if (!callback && stdCallback != OCCT_COUNT_CHILDREN)
	KernelFatal(BAD_OBJ_COMP_CALL_TYPE);
    child = firstChild;
    if (!OptrToHandle(child)) {
	countdown = OptrToChunk(firstChild);
	child = GetField(obj, masterOffset, compOffset);
    }
    while (OptrToHandle(child) && !ObjIsParentLink(child)) {
	next = GetField(child, masterOffset, linkOffset);
	if (countdown)
	    countdown--;
	else if (!callback)
	    (*(word *) cbData)++;
	else if (callback(obj, child, cbData))
	    return TRUE;
	child = next;
    }
    return FALSE;
}
