/*
 * dbaseC.c -- after dbaseC.asm: DBLockGetRefUngrouped, DBRawAlloc,
 * DBReAllocUngrouped, DBFreeUngrouped, DBInsertAtUngrouped,
 * DBDeleteAtUngrouped, DBSetMapUngrouped, DBRawCopyDBItem,
 * DBInfoUngrouped.
 */
#include "dbaseInt.h"
#include "dbaseMacro.h"
#include <string.h>

void *DBLockGetRefUngrouped(VMFileHandle file, DBGroupAndItem id,
			    optr *refPtr)
{
    optr o;

    DBEnter();
    o = DBLockItem(file, id);
    DBLeave();
    if (refPtr)
	*refPtr = o;
    return LMemDeref(o);
}

DBGroupAndItem DBRawAlloc(VMFileHandle file, DBGroup group, word size)
{
    DBGroup_ g;
    word item, info;
    MemHandle mh;
    ChunkHandle ch;

    DBEnter();
    if (group == DB_UNGROUPED)
	group = DBGroupGetUngrouped(file);
    DBGroupLock(file, group, &g);
    item = DBItemAlloc(&g);
    info = DBItemBlockFind(&g, size);
    DBII(&g, item)->DBII_block = info;
    (void) DBItemBlockLock(&g, info, &mh);
    ch = LMemAlloc(mh, size);
    DBII(&g, item)->DBII_chunk = DB_CHUNK_TO_FILE(ch);
    DBItemBlockAddRef(&g, mh);
    VMUnlock(mh);
    DBGroupUnlock(&g);
    DBLeave();
    return DBCombineGroupAndItem(group, item);
}

void DBReAllocUngrouped(VMFileHandle file, DBGroupAndItem id, word size)
{
    DBEnter();
    DBReAllocInternal(file, id, size);
    DBLeave();
}

void DBFreeUngrouped(VMFileHandle file, DBGroupAndItem id)
{
    DBGroup_ g;
    word item = ITEM_OF(id);
    MemHandle mh;

    DBEnter();
    DBGroupLock(file, GROUP_OF(id), &g);
    (void) DBItemBlockLock(&g, DBII(&g, item)->DBII_block, &mh);
    LMemFree(ConstructOptr(mh, DB_CHUNK_TO_MEM(DBII(&g, item)->DBII_chunk)));
    DBItemFree(&g, item);
    if (!DBItemBlockDelRef(&g, mh))
	VMUnlock(mh);
    DBGroupUnlock(&g);
    DBLeave();
}

/* DBInsertAt: the item grows (perhaps into another block), the bytes
 * from insertOffset move up and the new ones are 0 */
void DBInsertAtUngrouped(VMFileHandle file, DBGroupAndItem id,
			 word insertOffset, word insertCount)
{
    optr o;
    word oldSize;
    byte *p;

    DBEnter();
    o = DBLockItem(file, id);
    oldSize = LMemGetChunkSize(o);
    VMUnlock(OptrToHandle(o));
    DBReAllocInternal(file, id, (word) (oldSize + insertCount));
    o = DBLockItem(file, id);
    p = LMemDeref(o);
    memmove(p + insertOffset + insertCount, p + insertOffset,
	    oldSize - insertOffset);
    memset(p + insertOffset, 0, insertCount);
    VMUnlock(OptrToHandle(o));
    DBLeave();
}

void DBDeleteAtUngrouped(VMFileHandle file, DBGroupAndItem id,
			 word deleteOffset, word deleteCount)
{
    optr o;

    DBEnter();
    o = DBLockItem(file, id);
    VMDirty(OptrToHandle(o));
    LMemDeleteAt(o, deleteOffset, deleteCount);
    VMUnlock(OptrToHandle(o));
    DBLeave();
}

void DBSetMapUngrouped(VMFileHandle file, DBGroupAndItem id)
{
    MemHandle mh;
    DBMapBlock *m;

    DBEnter();
    m = DBLockDBMap(file, &mh);
    m->DBMB_mapGroup = GROUP_OF(id);
    m->DBMB_mapItem = ITEM_OF(id);
    VMDirty(mh);
    VMUnlock(mh);
    DBLeave();
}

/* DBCopyDBItem: to destGroup (or DB_UNGROUPED) of destFile, which may be
 * the same file */
DBGroupAndItem DBRawCopyDBItem(VMFileHandle srcFile, DBGroupAndItem srcID,
			       VMFileHandle destFile, DBGroup destGroup)
{
    optr src, dest;
    DBGroupAndItem id;
    word size;

    DBEnter();
    src = DBLockItem(srcFile, srcID);
    size = LMemGetChunkSize(src);
    id = DBRawAlloc(destFile, destGroup, size);
    dest = DBLockItem(destFile, id);
    memcpy(LMemDeref(dest), LMemDeref(src), size);
    VMDirty(OptrToHandle(dest));
    VMUnlock(OptrToHandle(dest));
    VMUnlock(OptrToHandle(src));
    DBLeave();
    return id;
}

/* DBInfo: whether the group and item are valid, and the item's size */
Boolean DBInfoUngrouped(VMFileHandle file, DBGroupAndItem id, word *sizePtr)
{
    VMInfoStruct vi;
    DBGroup_ g;
    word item = ITEM_OF(id), info, i, block = 0;
    ChunkHandle ch;
    MemHandle mh;
    const byte *b;
    const LMemBlockHeader *lh;
    Boolean ok = FALSE;

    DBEnter();
    if (!VMInfo(file, GROUP_OF(id), &vi) || vi.userId != DB_GROUP_ID ||
	item < DB_OLD_GROUP_HEADER_SIZE || item >= vi.size)
	goto done;
    DBGroupLock(file, GROUP_OF(id), &g);
    info = DBII(&g, item)->DBII_block;
    ch = DB_CHUNK_TO_MEM(DBII(&g, item)->DBII_chunk);
    for (i = DBGH(&g)->DBGH_itemBlocks; i; i = DBIBI(&g, i)->DBIBI_next)
	if (i == info) {
	    block = DBIBI(&g, i)->DBIBI_block;
	    break;
	}
    DBGroupUnlock(&g);
    if (!block || !VMInfo(file, block, &vi) || vi.userId != DB_ITEM_BLOCK_ID)
	goto done;
    b = VMLock(file, block, &mh);
    if (!b)
	goto done;
    lh = (const LMemBlockHeader *) b;
    if (ch >= lh->LMBH_offset &&
	ch < lh->LMBH_offset + 2u * lh->LMBH_nHandles &&
	*(const word *) (b + ch)) {
	/* a chunk of size 0 has no data (LMEM_EMPTY) */
	if (sizePtr)
	    *sizePtr = *(const word *) (b + ch) == DB_CHUNK_EMPTY ? 0 :
		LMemGetChunkSize(ConstructOptr(mh, ch));
	ok = TRUE;
    }
    VMUnlock(mh);
done:
    DBLeave();
    return ok;
}
