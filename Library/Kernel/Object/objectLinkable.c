/*
 * objectLinkable.c -- linkable objects (after objectLinkable.asm).
 *
 * The link of an object (LinkPart, at `linkOffset' in the master part at
 * `masterOffset') holds its next sibling; the last child holds its
 * parent with LP_IS_PARENT set (objectComposite.c).  The register
 * routines ObjSwapLockParent, ObjLinkCallParent and
 * ObjLinkCallNextSibling have no C form: C code sends to @parent
 * (ObjLinkFindParent) or to the optr in the link.
 */
#include "kernelC.h"

#include <string.h>

/* the link of `o' (0 if the part is not built) */
static optr Link(optr o, word masterOffset, word linkOffset)
{
    MemHandle mh = OptrToHandle(o);
    byte *p;
    optr v = 0;
    word off = 0;

    ObjLockObjBlock(mh);
    p = LMemDeref(o);
    if (!p)
	KernelFatal(NOT_AN_OBJECT);
    if (masterOffset)
	off = *(word *) (p + masterOffset);
    if (!masterOffset || off)
	memcpy(&v, p + off + linkOffset, sizeof(v));
    MemUnlock(mh);
    return v;
}

/* the parent of a linked object: along the siblings to the parent link
 * (0: the object is in no composite) */
optr ObjLinkFindParent(optr obj, word masterOffset, word linkOffset)
{
    optr v = obj;

    do {
	v = Link(v, masterOffset, linkOffset);
	if (!OptrToHandle(v))
	    return 0;
    } while (!(OptrToChunk(v) & LP_IS_PARENT));
    return v & ~(optr) LP_IS_PARENT;
}
