/*
 * objectC.c -- after objectC.asm: ObjCompFindChildByOptr,
 * ObjCompFindChildByNumber, CObjSendToChildren.
 */
#include "kernelC.h"
#include <string.h>

word ObjCompFindChildByOptr(optr obj, optr childToFind, word masterOffset,
			    word compOffset, word linkOffset)
{
    ObjCompPlace pl;
    word pos;

    return ObjCompFindChildLow(obj, childToFind, masterOffset, compOffset, linkOffset,
		     &pl, &pos) ? pos : (word) -1;
}

optr ObjCompFindChildByNumber(optr obj, word childToFind, word masterOffset,
			      word compOffset, word linkOffset)
{
    ObjCompPlace pl = ObjFindLocation(obj, childToFind, masterOffset, compOffset,
			    linkOffset);
    optr v = ObjPlaceGet(&pl, masterOffset);

    return OptrToHandle(v) && !ObjIsParentLink(v) ? v : 0;
}

/* the event to each child, then it is freed */
static Boolean SendCallback(optr parent, optr child, void *cbData)
{
    EventHandle ev = *(EventHandle *) cbData;

    (void) parent;
    MessageSetDestination(ev, child);
    CMessageDispatchP(ev, MF_DISPATCH_DONT_FREE, NULL, 0);
    return FALSE;
}

void CObjSendToChildren(optr o, EventHandle message, word masterOffset,
			word compOffset, word linkOffset)
{
    ObjCompProcessChildren(o, 0, OCCT_SAVE_PARAMS_DONT_TEST_ABORT, &message,
			   masterOffset, compOffset, linkOffset,
			   SendCallback);
    ObjFreeMessage(message);
}
