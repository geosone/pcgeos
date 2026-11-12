/*
 * dbaseCode.c -- the routines of the database manager (after
 * dbaseCode.asm and the C stubs of dbaseC.asm): items in groups of a VM
 * file, as CInclude/dbase.h has them.
 *
 * A DBGroupAndItem is the group (a VM block) in the high word and the
 * item (an offset in the group block) in the low one.  DB_UNGROUPED as
 * group lets the database choose one.  The item's data is a chunk: a
 * pointer from DBLock stays good until the item block is unlocked
 * (DBUnlock) or an item of the same block grows or is added (DBAlloc,
 * DBReAlloc, DBInsertAt), after which the caller locks it again, as
 * with LMem.
 *
 * Port32: the asm fixed up ds and es if they pointed at an item block
 * that moved; C callers dereference again.  The VM override (file 0)
 * is not supported.  Register/notify (dbaseRegister.asm) was empty and
 * is not ported.
 */
#include "dbaseInt.h"

#include <string.h>

#define GROUP_OF(id)	((VMBlockHandle) ((id) >> 16))
#define ITEM_OF(id)	((word) (id))

/* DBLockGroupValid: the item block locked, the item's chunk in it */
static optr LockItem(VMFileHandle file, DBGroupAndItem id)
{
    DBGroup_ g;
    word info;
    ChunkHandle ch;
    MemHandle mh;

    DBGroupLock(file, GROUP_OF(id), &g);
    EC_CHECK(ITEM_OF(id) >= DB_OLD_GROUP_HEADER_SIZE &&
	     ITEM_OF(id) < DBGH(&g)->DBGH_blockSize, DB_BAD_ITEM);
    info = DBII(&g, ITEM_OF(id))->DBII_block;
    ch = DB_CHUNK_TO_MEM(DBII(&g, ITEM_OF(id))->DBII_chunk);
    (void) DBItemBlockLock(&g, info, &mh);
    DBGroupUnlock(&g);
    return ConstructOptr(mh, ch);
}

void *DBLockUngrouped(VMFileHandle file, DBGroupAndItem id)
{
    optr o;

    DBEnter();
    o = LockItem(file, id);
    DBLeave();
    return LMemDeref(o);
}

void *DBLockGetRefUngrouped(VMFileHandle file, DBGroupAndItem id,
			    optr *refPtr)
{
    optr o;

    DBEnter();
    o = LockItem(file, id);
    DBLeave();
    if (refPtr)
	*refPtr = o;
    return LMemDeref(o);
}

void DBUnlock(void *ptr)
{
    VMUnlock(MemPtrToHandle(ptr));
}

void DBDirty(const void *ptr)
{
    VMDirty(MemPtrToHandle((void *) ptr));
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

/* DBReAllocInternal: a larger item that would make its block too large
 * goes to another one, unless it is alone in it */
static void ReAlloc(VMFileHandle file, DBGroupAndItem id, word size)
{
    DBGroup_ g;
    word item = ITEM_OF(id), info, newInfo, oldSize;
    MemHandle mh, nmh;
    ChunkHandle ch, nch;

    DBGroupLock(file, GROUP_OF(id), &g);
    info = DBII(&g, item)->DBII_block;
    ch = DB_CHUNK_TO_MEM(DBII(&g, item)->DBII_chunk);
    (void) DBItemBlockLock(&g, info, &mh);
    oldSize = LMemGetChunkSize(ConstructOptr(mh, ch));
    if (size > oldSize && DBIBI(&g, info)->DBIBI_refCount != 1 &&
	(dword) size - oldSize +
	((LMemBlockHeader *) MemDeref(mh))->LMBH_blockSize >
	DB_ITEM_BLOCK_DESIRED_SIZE) {
	newInfo = DBItemBlockFind(&g, size);
	DBII(&g, item)->DBII_block = newInfo;
	(void) DBItemBlockLock(&g, newInfo, &nmh);
	nch = LMemAlloc(nmh, size);
	memcpy(LMemDeref(ConstructOptr(nmh, nch)),
	       LMemDeref(ConstructOptr(mh, ch)), oldSize);
	LMemFree(ConstructOptr(mh, ch));
	DBItemBlockAddRef(&g, nmh);
	VMUnlock(nmh);
	DBII(&g, item)->DBII_chunk = DB_CHUNK_TO_FILE(nch);
	(void) DBItemBlockDelRef(&g, mh);	/* others are left in it */
    } else
	LMemReAlloc(ConstructOptr(mh, ch), size);
    VMDirty(mh);
    VMUnlock(mh);
    DBGroupUnlock(&g);
}

void DBReAllocUngrouped(VMFileHandle file, DBGroupAndItem id, word size)
{
    DBEnter();
    ReAlloc(file, id, size);
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
    o = LockItem(file, id);
    oldSize = LMemGetChunkSize(o);
    VMUnlock(OptrToHandle(o));
    ReAlloc(file, id, (word) (oldSize + insertCount));
    o = LockItem(file, id);
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
    o = LockItem(file, id);
    VMDirty(OptrToHandle(o));
    LMemDeleteAt(o, deleteOffset, deleteCount);
    VMUnlock(OptrToHandle(o));
    DBLeave();
}

DBGroup DBGroupAlloc(VMFileHandle file)
{
    VMBlockHandle group = VMAlloc(file, sizeof(DBGroupHeader), DB_GROUP_ID);
    MemHandle mh;
    DBGroupHeader *h = VMLock(file, group, &mh);

    memset(h, 0, sizeof(*h));
    h->DBGH_blockSize = sizeof(DBGroupHeader);
    h->DBGH_vmemHandle = group;
    VMDirty(mh);
    VMUnlock(mh);
    return group;
}

/* DBGroupFree: with all its items */
void DBGroupFree(VMFileHandle file, DBGroup group)
{
    MemHandle mh;
    DBMapBlock *m;
    DBGroup_ g;
    word info;

    DBEnter();
    m = DBLockDBMap(file, &mh);
    if (m->DBMB_newUngrouped == group)
	m->DBMB_newUngrouped = 0;
    if (m->DBMB_ungrouped == group)
	m->DBMB_ungrouped = 0;
    if (m->DBMB_mapGroup == group)
	m->DBMB_mapGroup = m->DBMB_mapItem = 0;
    VMDirty(mh);
    VMUnlock(mh);
    DBGroupLock(file, group, &g);
    for (info = DBGH(&g)->DBGH_itemBlocks; info;
	 info = DBIBI(&g, info)->DBIBI_next)
	VMFree(file, DBIBI(&g, info)->DBIBI_block);
    DBGroupUnlock(&g);
    VMFree(file, group);
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

DBGroupAndItem DBGetMap(VMFileHandle file)
{
    MemHandle mh;
    DBMapBlock *m;
    DBGroupAndItem id;

    DBEnter();
    m = DBLockDBMap(file, &mh);
    id = DBCombineGroupAndItem(m->DBMB_mapGroup, m->DBMB_mapItem);
    VMUnlock(mh);
    DBLeave();
    return id;
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
    src = LockItem(srcFile, srcID);
    size = LMemGetChunkSize(src);
    id = DBRawAlloc(destFile, destGroup, size);
    dest = LockItem(destFile, id);
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
