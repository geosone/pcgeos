/*
 * dbaseCode.c -- the routines of the database manager (after
 * dbaseCode.asm and the C stubs of dbaseC.asm; what only dbaseC.asm has
 * is in dbaseC.c): items in groups of a VM
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
#include "dbaseMacro.h"

#include <string.h>

/* DBLockGroupValid: the item block locked, the item's chunk in it */
optr DBLockItem(VMFileHandle file, DBGroupAndItem id)
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
    o = DBLockItem(file, id);
    DBLeave();
    return LMemDeref(o);
}

void DBDirty(const void *ptr)
{
    VMDirty(MemPtrToHandle((void *) ptr));
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

/* DBCheckDBFile, DBCheckData */
static Boolean CheckData(VMFileHandle file, word uid, word minSize)
{
    VMBlockHandle b = 0;
    VMInfoStruct info;

    while ((b = VMFind(file, b, uid)) != 0)
	if (!VMInfo(file, b, &info) || info.size < minSize)
	    return FALSE;
    return TRUE;
}

Boolean DBCheckDBFile(VMFileHandle file)
{
    VMBlockHandle map = VMGetDBMap(file);
    VMInfoStruct info;
    MemHandle mh;
    DBMapBlock *m;
    Boolean ok;

    if (!map)
	return TRUE;
    if (!VMInfo(file, map, &info) || info.size != DB_DOS16_MAP_BLOCK_SIZE)
	return FALSE;
    m = VMLock(file, map, &mh);
    if (!m)
	return FALSE;
    ok = m->DBMB_vmemHandle == map;
    VMUnlock(mh);
    return ok &&
	CheckData(file, DB_GROUP_ID, sizeof(DBGroupHeader)) &&
	CheckData(file, DB_ITEM_BLOCK_ID, DB_DOS16_ITEM_BLOCK_HEADER);
}
