/*
 * dbaseItemBlock.c -- the item blocks of a group (after
 * dbaseItemBlock.asm): LMem VM blocks that hold the items, a chunk each.
 * An item block counts its items in its DBItemBlockInfo and goes when the
 * last one does.
 */
#include "dbaseInt.h"

/* DBItemBlockAlloc: a new, empty item block */
static word ItemBlockAlloc(DBGroup_ *g)
{
    word info = DBBlockAlloc(g);
    MemHandle mh = MemAlloc(DB_ITEM_BLOCK_INIT_SIZE, HF_SWAPABLE,
			    HAF_NO_ERR | HAF_ZERO_INIT);
    VMBlockHandle block;
    DBItemBlockHeader *ib;

    LMemInitHeap(mh, LMEM_TYPE_GENERAL, 0, sizeof(DBItemBlockHeader), 32,
		 256);
    block = VMAttach(g->file, 0, mh);
    VMModifyUserID(g->file, block, DB_ITEM_BLOCK_ID);
    DBIBI(g, info)->DBIBI_next = DBGH(g)->DBGH_itemBlocks;
    DBGH(g)->DBGH_itemBlocks = info;
    DBIBI(g, info)->DBIBI_block = block;
    DBIBI(g, info)->DBIBI_refCount = 0;
    ib = MemLock(mh);
    ib->DBIBH_vmHandle = block;
    ib->DBIBH_infoStruct = info;
    MemUnlock(mh);
    VMDirty(mh);
    return info;
}

/* DBItemBlockFind: one with room for size more bytes, else a new one */
word DBItemBlockFind(DBGroup_ *g, word size)
{
    word info;
    VMInfoStruct vi;

    for (info = DBGH(g)->DBGH_itemBlocks; info;
	 info = DBIBI(g, info)->DBIBI_next)
	if (VMInfo(g->file, DBIBI(g, info)->DBIBI_block, &vi) &&
	    (dword) vi.size + size < DB_ITEM_BLOCK_DESIRED_SIZE)
	    return info;
    return ItemBlockAlloc(g);
}

/* DBItemBlockFree: it has no items left */
void DBItemBlockFree(DBGroup_ *g, word info)
{
    word *link = &DBGH(g)->DBGH_itemBlocks;

    while (*link != info)
	link = &DBIBI(g, *link)->DBIBI_next;
    *link = DBIBI(g, info)->DBIBI_next;
    VMFree(g->file, DBIBI(g, info)->DBIBI_block);
    DBBlockFree(g, info);
}

byte *DBItemBlockLock(DBGroup_ *g, word info, MemHandle *mh)
{
    byte *p = VMLock(g->file, DBIBI(g, info)->DBIBI_block, mh);

    if (!p)
	KernelFatal(DB_LOCK_FAILED);
    return p;
}

void DBItemBlockAddRef(DBGroup_ *g, MemHandle mh)
{
    word info = ((DBItemBlockHeader *) MemDeref(mh))->DBIBH_infoStruct;

    DBIBI(g, info)->DBIBI_refCount++;
    DBGroupDirty(g);
    VMDirty(mh);
}

/* DBItemBlockDelRef: the last item gone, the block is unlocked and
 * freed */
Boolean DBItemBlockDelRef(DBGroup_ *g, MemHandle mh)
{
    word info = ((DBItemBlockHeader *) MemDeref(mh))->DBIBH_infoStruct;

    if (--DBIBI(g, info)->DBIBI_refCount) {
	VMDirty(mh);
	return FALSE;
    }
    DBGroupDirty(g);
    VMUnlock(mh);
    DBItemBlockFree(g, info);
    return TRUE;
}
