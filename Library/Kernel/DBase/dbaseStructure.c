/*
 * dbaseStructure.c -- the DBItemInfo and DBItemBlockInfo structures of a
 * group block (after dbaseStructure.asm).  Each kind has a free list;
 * when it is empty the group block grows by a number of them.
 */
#include "dbaseInt.h"

/* add count structures of size bytes at the end of the group block;
 * the offset of the first, linked to the next ones */
static word Grow(DBGroup_ *g, word size, word count)
{
    word old = DBGH(g)->DBGH_blockSize, i;

    if (!MemReAlloc(g->mh, (word) (old + size * count), HAF_NO_ERR))
	KernelFatal(DB_LOCK_FAILED);
    g->p = MemDeref(g->mh);
    DBGH(g)->DBGH_blockSize = (word) (old + size * count);
    for (i = 0; i < count; i++)
	*(word *) (g->p + old + size * i) =
	    (word) (i + 1 < count ? old + size * (i + 1) : 0);
    return old;
}

word DBItemAlloc(DBGroup_ *g)
{
    DBGroupHeader *h = DBGH(g);
    word item;
    MemHandle mh;
    DBMapBlock *m;

    if (!h->DBGH_itemFreeList) {
	word first = Grow(g, DB_FREE_ITEM_SIZE, DB_ITEM_STRUC_INC);
	h = DBGH(g);
	h->DBGH_itemFreeList = first;
    }
    item = h->DBGH_itemFreeList;
    h->DBGH_itemFreeList = *(word *) (g->p + item);
    if (h->DBGH_flags & GF_NEW_UNGROUP)
	h->DBGH_numItems++;
    DBGroupDirty(g);
    /* the current ungroup group that is too large (and has no free
     * structures left) is full: the next ungrouped item takes another */
    if (h->DBGH_blockSize >= DB_GROUP_BLOCK_DESIRED_SIZE &&
	(h->DBGH_flags & GF_NEW_UNGROUP) && !h->DBGH_itemFreeList &&
	!(h->DBGH_flags & GF_UNGROUP_FULL)) {
	h->DBGH_flags |= GF_UNGROUP_FULL;
	m = DBLockDBMap(g->file, &mh);
	m->DBMB_newUngrouped = 0;
	VMDirty(mh);
	VMUnlock(mh);
    }
    return item;
}

void DBItemFree(DBGroup_ *g, word item)
{
    DBGroupHeader *h = DBGH(g);

    *(word *) (g->p + item) = h->DBGH_itemFreeList;
    h->DBGH_itemFreeList = item;
    if (h->DBGH_flags & GF_NEW_UNGROUP) {
	h->DBGH_numItems--;
	/* a full ungroup group with few items left has room again */
	if ((h->DBGH_flags & GF_UNGROUP_FULL) &&
	    h->DBGH_numItems <= DB_UNGROUP_LOW_WATER_MARK) {
	    h->DBGH_flags &= (word) ~GF_UNGROUP_FULL;
	    VMMarkUngroupAvail(g->file, h->DBGH_vmemHandle);
	}
    }
    DBGroupDirty(g);
}

word DBBlockAlloc(DBGroup_ *g)
{
    DBGroupHeader *h = DBGH(g);
    word info;

    if (!h->DBGH_blockFreeList) {
	word first = Grow(g, DB_FREE_BLOCK_SIZE, DB_BLOCK_STRUC_INC);
	h = DBGH(g);
	h->DBGH_blockFreeList = first;
    }
    info = h->DBGH_blockFreeList;
    h->DBGH_blockFreeList = *(word *) (g->p + info);
    DBGroupDirty(g);
    return info;
}

void DBBlockFree(DBGroup_ *g, word info)
{
    *(word *) (g->p + info) = DBGH(g)->DBGH_blockFreeList;
    DBGH(g)->DBGH_blockFreeList = info;
    DBGroupDirty(g);
}
