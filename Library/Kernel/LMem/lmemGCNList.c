/*
 * lmemGCNList.c -- general change notification lists (after
 * lmemGCNList.asm and the GCN stubs of lmemC.asm).
 *
 * A GCN list is a chunk array of optrs (GCNListHeader, GCNListElement)
 * with an optional status: an event (and data block) that the list
 * keeps and sends to each object that is added later.  A list of lists
 * (GCNListOfListsElement) finds a list by its GCNListType.  The system
 * lists live in a kernel block, used under its semaphore (MemPLock);
 * objects keep their own lists in their blocks (MetaClass,
 * TEMP_META_GCN).
 *
 * The Port32 header keeps a 32-bit status event and aligns the elements
 * to 8 (Port32/ABI.md section 13).
 *
 * Sending: each object on the list gets the event (MessageDispatch, the
 * event is not freed; GCNLSF_FORCE_QUEUE queues it even for the running
 * thread), and the data block, if any, gets one more reference per
 * object (MetaClass's MSG_META_NOTIFY_WITH_DATA_BLOCK takes it back).
 * The sender's reference to the block is given up at the end.  The
 * objects are taken from the list before the first one gets the event,
 * so a handler may add to or remove from the list; the system list is
 * unlocked by then, as GCNListAdd does for its status message in the
 * asm kernel.
 *
 * Not yet: the system lists' events and data blocks are not given to
 * the kernel (HandleModifyOwner) -- that comes with geode exit.
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

/* ---------------------------------------------------------------------
 * One list
 * ------------------------------------------------------------------- */

static optr ItemAt(optr list, word i)
{
    GCNListElement *e = ChunkArrayElementToPtr(list, i, NULL);
    optr item;

    memcpy(&item, &e->GCNLE_item, sizeof(item));
    return item;
}

/* the index of `od' in the list, or 0xffff (chunk 0: any chunk of the
 * handle) */
static word FindItem(optr list, optr od)
{
    word i, n = ChunkArrayGetCount(list);

    for (i = 0; i < n; i++) {
	optr item = ItemAt(list, i);
	if (OptrToHandle(item) == OptrToHandle(od) &&
	    (OptrToChunk(od) == 0 || OptrToChunk(item) == OptrToChunk(od)))
	    return i;
    }
    return 0xffff;
}

static GCNListHeader *Header(optr list)
{
    return LMemDeref(list);
}

ChunkHandle GCNListCreateList(MemHandle mh)
{
    ChunkHandle ch = ChunkArrayCreateAt(ConstructOptr(mh, 0),
					sizeof(GCNListElement),
					sizeof(GCNListHeader), OCF_DIRTY);
    GCNListHeader *h;

    if (!ch)
	KernelFatal(LMEM_CANNOT_ALLOCATE);
    h = Header(ConstructOptr(mh, ch));
    h->GCNLH_statusEvent = 0;
    h->GCNLH_statusData = 0;
    h->GCNLH_statusCount = 0;
    memset(h->GCNLH_reserved, 0, sizeof(h->GCNLH_reserved));
    return ch;
}

Boolean GCNListFindItemInList(optr OD, optr list)
{
    return FindItem(list, OD) != 0xffff;
}

/* the status event to one object (DispatchStatusMessage) */
static void DispatchStatus(EventHandle ev, MemHandle data, optr od)
{
    if (data)
	MemIncRefCount(data);
    MessageSetDestination(ev, od);
    CMessageDispatchP(ev, MF_DISPATCH_DONT_FREE, NULL, 0);
}

/* add without sending the status; TRUE if added */
static Boolean AddItem(optr od, optr list)
{
    GCNListElement *e;

    if (FindItem(list, od) != 0xffff)
	return FALSE;
    e = ChunkArrayAppend(list, 0);
    memcpy(&e->GCNLE_item, &od, sizeof(od));
    ObjMarkDirty(list);
    return TRUE;
}

/* TRUE if added (FALSE: it was there); the list's status goes to the
 * object either way */
Boolean GCNListAddToList(optr OD, optr list)
{
    Boolean added = AddItem(OD, list);
    GCNListHeader *h = Header(list);

    if (h->GCNLH_statusEvent)
	DispatchStatus(h->GCNLH_statusEvent, h->GCNLH_statusData, OD);
    return added;
}

/* TRUE if found and removed (chunk 0: every chunk of the handle) */
Boolean GCNListRemoveFromList(optr OD, optr list)
{
    Boolean found = FALSE;
    word i;

    while ((i = FindItem(list, OD)) != 0xffff) {
	ChunkArrayDelete(list, ChunkArrayElementToPtr(list, i, NULL));
	found = TRUE;
	if (OptrToChunk(OD))
	    break;
    }
    return found;
}

static void FreeEventAndData(optr list)
{
    GCNListHeader *h = Header(list);
    EventHandle ev = h->GCNLH_statusEvent;
    MemHandle data = h->GCNLH_statusData;

    h->GCNLH_statusEvent = 0;
    h->GCNLH_statusData = 0;
    if (ev)
	ObjFreeMessage(ev);
    if (data)
	MemDecRefCount(data);
}

void GCNListDestroyList(optr list)
{
    if (LMemDeref(list))
	FreeEventAndData(list);
    LMemFree(list);
}

/*
 * The send of GCNListSendToList, in two halves: Collect under the
 * list's lock (status, the objects), Deliver after.  `items' is NULL if
 * nothing is to be sent; the result of Collect is the number of objects.
 */
typedef struct {
    optr	*items;
    word	count;
    EventHandle	event;
    MemHandle	data;
    GCNListSendFlags flags;
} Send;

static void Collect(optr list, Send *s)
{
    GCNListHeader *h = Header(list);
    word i;

    s->items = NULL;
    s->count = 0;
    if (s->flags & GCNLSF_SET_STATUS) {
	EventHandle old;
	MemHandle oldData;

	h->GCNLH_statusCount++;
	if (EventSameStatus(s->event, s->data, h->GCNLH_statusEvent,
			    h->GCNLH_statusData)) {
	    /* the same status again: nobody needs it */
	    ObjFreeMessage(s->event);
	    s->event = 0;
	    return;
	}
	old = h->GCNLH_statusEvent;
	oldData = h->GCNLH_statusData;
	h->GCNLH_statusEvent = s->event;
	if (s->data)
	    MemIncRefCount(s->data);	/* the list's reference */
	h->GCNLH_statusData = s->data;
	if (old)
	    ObjFreeMessage(old);
	if (oldData)
	    MemDecRefCount(oldData);
    }
    if (!s->event)
	return;				/* clears the status: legal */
    s->count = ChunkArrayGetCount(list);
    if (!s->count)
	return;
    s->items = ghal_mem_alloc(s->count * sizeof(optr), 0);
    if (!s->items)
	KernelFatal(GCN_LIST_ERROR);
    for (i = 0; i < s->count; i++)
	s->items[i] = ItemAt(list, i);
}

static word Deliver(Send *s)
{
    word i;
    word flags = MF_DISPATCH_DONT_FREE;

    if (s->flags & GCNLSF_FORCE_QUEUE)
	flags |= MF_FORCE_QUEUE;
    for (i = 0; s->items && i < s->count; i++) {
	if (s->data)
	    MemIncRefCount(s->data);
	MessageSetDestination(s->event, s->items[i]);
	CMessageDispatchP(s->event, flags, NULL, 0);
    }
    if (s->items)
	ghal_mem_free(s->items);
    /* a status event stays with the list */
    if (s->event && !(s->flags & GCNLSF_SET_STATUS))
	ObjFreeMessage(s->event);
    if (s->data)
	MemDecRefCount(s->data);	/* the sender's reference */
    return s->count;
}

/* nothing to send to: free the event, give up the data block */
static word Drop(EventHandle event, MemHandle data)
{
    if (event)
	ObjFreeMessage(event);
    if (data)
	MemDecRefCount(data);
    return 0;
}

static word SendToList(optr list, EventHandle event, MemHandle dataBlock,
		       GCNListSendFlags flags)
{
    Send s;

    s.event = event;
    s.data = dataBlock;
    s.flags = flags;
    Collect(list, &s);
    return Deliver(&s);
}

void GCNListSendToList(optr list, EventHandle event, MemHandle dataBlock,
		       GCNListSendFlags flags)
{
    SendToList(list, event, dataBlock, flags);
}

/* ---------------------------------------------------------------------
 * Lists in a block
 * ------------------------------------------------------------------- */

ChunkHandle GCNListCreateBlock(MemHandle mh)
{
    ChunkHandle ch = ChunkArrayCreateAt(ConstructOptr(mh, 0),
					sizeof(GCNListOfListsElement),
					sizeof(GCNListOfListsHeader),
					OCF_DIRTY);

    if (!ch)
	KernelFatal(LMEM_CANNOT_ALLOCATE);
    return ch;
}

ChunkHandle GCNListFindListInBlock(ManufacturerID manufID, word listType,
				   MemHandle mh, ChunkHandle listOfLists,
				   Boolean createIfNotFound)
{
    optr lol = ConstructOptr(mh, listOfLists);
    word i, n = ChunkArrayGetCount(lol);
    GCNListOfListsElement *e;
    ChunkHandle list;

    for (i = 0; i < n; i++) {
	e = ChunkArrayElementToPtr(lol, i, NULL);
	if (e->GCNLOLE_ID.GCNLT_manuf == manufID &&
	    !((e->GCNLOLE_ID.GCNLT_type ^ listType) & ~GCNLTF_SAVE_TO_STATE))
	    return e->GCNLOLE_list;
    }
    if (!createIfNotFound)
	return 0;
    list = GCNListCreateList(mh);
    e = ChunkArrayAppend(lol, 0);
    e->GCNLOLE_ID.GCNLT_manuf = manufID;
    e->GCNLOLE_ID.GCNLT_type = listType;
    e->GCNLOLE_list = list;
    ObjMarkDirty(lol);
    return list;
}

Boolean GCNListAddToBlock(optr OD, ManufacturerID manufID, word listType,
			  MemHandle mh, ChunkHandle listOfLists)
{
    ChunkHandle list = GCNListFindListInBlock(manufID, listType, mh,
					      listOfLists, TRUE);

    if (!GCNListAddToList(OD, ConstructOptr(mh, list)))
	return FALSE;
    ObjMarkDirty(ConstructOptr(mh, listOfLists));
    return TRUE;
}

Boolean GCNListRemoveFromBlock(optr OD, ManufacturerID manufID,
			       word listType, MemHandle mh,
			       ChunkHandle listOfLists)
{
    ChunkHandle list = GCNListFindListInBlock(manufID, listType, mh,
					      listOfLists, FALSE);

    if (!list || !GCNListRemoveFromList(OD, ConstructOptr(mh, list)))
	return FALSE;
    ObjMarkDirty(ConstructOptr(mh, listOfLists));
    return TRUE;
}

/* the number of objects sent to; a list is made if the status is set */
word GCNListSendToBlock(ManufacturerID manufID, word listType,
			EventHandle event, MemHandle dataBlock, MemHandle mh,
			ChunkHandle listOfLists, GCNListSendFlags flags)
{
    ChunkHandle list = GCNListFindListInBlock(manufID, listType, mh,
					      listOfLists,
					      (flags & GCNLSF_SET_STATUS) != 0);

    if (!list)
	return Drop(event, dataBlock);
    return SendToList(ConstructOptr(mh, list), event, dataBlock, flags);
}

void GCNListDestroyBlock(MemHandle mh, ChunkHandle listOfLists)
{
    optr lol = ConstructOptr(mh, listOfLists);
    word i, n = ChunkArrayGetCount(lol);

    for (i = 0; i < n; i++) {
	GCNListOfListsElement *e = ChunkArrayElementToPtr(lol, i, NULL);
	GCNListDestroyList(ConstructOptr(mh, e->GCNLOLE_list));
    }
    LMemFree(lol);
}

/* ---------------------------------------------------------------------
 * Relocation: the handles of the optrs, as resources of the owner of
 * `relocBlock' (objectReloc.c).  Unrelocating drops the status and the
 * lists that are not saved to state (GCNLTF_SAVE_TO_STATE).
 * ------------------------------------------------------------------- */

static void RelocList(optr list, MemHandle relocBlock, int unreloc)
{
    word i, n;

    if (!LMemDeref(list))
	return;				/* not dirty: emptied */
    n = ChunkArrayGetCount(list);
    for (i = 0; i < n; i++) {
	GCNListElement *e = ChunkArrayElementToPtr(list, i, NULL);
	optr item;
	Handle h;

	memcpy(&item, &e->GCNLE_item, sizeof(item));
	h = OptrToHandle(item);
	if (unreloc)
	    ObjDoUnRelocation(RELOC_RELOC_HANDLE, relocBlock, &h, &h);
	else
	    ObjDoRelocation(RELOC_RELOC_HANDLE, relocBlock, &h, &h);
	item = ConstructOptr(h, OptrToChunk(item));
	memcpy(&e->GCNLE_item, &item, sizeof(item));
    }
}

void GCNListRelocateList(optr list, MemHandle relocBlock)
{
    RelocList(list, relocBlock, 0);
}

void GCNListUnRelocateList(optr list, MemHandle relocBlock)
{
    RelocList(list, relocBlock, 1);
    if (LMemDeref(list))
	FreeEventAndData(list);
}

void GCNListRelocateBlock(MemHandle mh, ChunkHandle listOfLists,
			  MemHandle relocBlock)
{
    optr lol = ConstructOptr(mh, listOfLists);
    word i, n;

    if (!LMemDeref(lol))
	return;
    n = ChunkArrayGetCount(lol);
    for (i = 0; i < n; i++) {
	GCNListOfListsElement *e = ChunkArrayElementToPtr(lol, i, NULL);
	GCNListRelocateList(ConstructOptr(mh, e->GCNLOLE_list), relocBlock);
    }
}

/* TRUE if no list is left: the list of lists is freed then */
Boolean GCNListUnRelocateBlock(MemHandle mh, ChunkHandle listOfLists,
			       MemHandle relocBlock)
{
    optr lol = ConstructOptr(mh, listOfLists);
    word i = 0;

    if (!LMemDeref(lol))
	return FALSE;			/* empty: in a resource, keep it */
    while (i < ChunkArrayGetCount(lol)) {
	GCNListOfListsElement *e = ChunkArrayElementToPtr(lol, i, NULL);
	optr list = ConstructOptr(mh, e->GCNLOLE_list);
	if (!(e->GCNLOLE_ID.GCNLT_type & GCNLTF_SAVE_TO_STATE)) {
	    GCNListDestroyList(list);
	    ChunkArrayDelete(lol, ChunkArrayElementToPtr(lol, i, NULL));
	    ObjMarkDirty(lol);
	    continue;
	}
	GCNListUnRelocateList(list, relocBlock);
	i++;
    }
    if (ChunkArrayGetCount(lol))
	return FALSE;
    LMemFree(lol);
    return TRUE;
}

/* ---------------------------------------------------------------------
 * The system lists
 * ------------------------------------------------------------------- */

static MemHandle systemBlock;
static ChunkHandle systemLists;

/* LockGCNBlock: made on first use */
static MemHandle LockSystem(void)
{
    MemHandle mh = systemBlock;

    if (!mh) {
	ChunkHandle lol;

	mh = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
	lol = GCNListCreateBlock(mh);
	ghal_crit_enter();
	if (!systemBlock) {
	    systemLists = lol;
	    systemBlock = mh;
	    mh = 0;
	}
	ghal_crit_leave();
	if (mh)
	    MemFree(mh);		/* another thread was first */
	mh = systemBlock;
    }
    MemPLock(mh);
    return mh;
}

static optr SystemList(MemHandle mh, ManufacturerID manufID, word listType,
		       Boolean create)
{
    ChunkHandle list = GCNListFindListInBlock(manufID, listType, mh,
					      systemLists, create);

    return list ? ConstructOptr(mh, list) : 0;
}

/* TRUE if added; the list's status goes to the object after the lists
 * are unlocked */
Boolean GCNListAdd(optr OD, ManufacturerID manufID, word listType)
{
    MemHandle mh = LockSystem();
    optr list = SystemList(mh, manufID, listType, TRUE);
    GCNListHeader *h = Header(list);
    EventHandle status = h->GCNLH_statusEvent;
    MemHandle data = h->GCNLH_statusData;
    Boolean added = AddItem(OD, list);

    MemUnlockV(mh);
    if (status)
	DispatchStatus(status, data, OD);
    return added;
}

Boolean GCNListRemove(optr OD, ManufacturerID manufID, word listType)
{
    MemHandle mh = LockSystem();
    optr list = SystemList(mh, manufID, listType, FALSE);
    Boolean found = list && GCNListRemoveFromList(OD, list);

    MemUnlockV(mh);
    return found;
}

/* the number of objects sent to */
word GCNListSend(ManufacturerID manufID, word listType, EventHandle event,
		 MemHandle dataBlock, GCNListSendFlags flags)
{
    MemHandle mh = LockSystem();
    optr list = SystemList(mh, manufID, listType,
			   (flags & GCNLSF_SET_STATUS) != 0);
    Send s;

    if (!list) {
	MemUnlockV(mh);
	return Drop(event, dataBlock);
    }
    s.event = event;
    s.data = dataBlock;
    s.flags = flags;
    Collect(list, &s);
    MemUnlockV(mh);
    return Deliver(&s);
}

/* empty a system list (EndGeos) */
void GCNListClearList(ManufacturerID manufID, word listType)
{
    MemHandle mh = LockSystem();
    optr list = SystemList(mh, manufID, listType, FALSE);

    if (list)
	ChunkArrayDeleteRange(list, 0, 0xffff);
    MemUnlockV(mh);
}
