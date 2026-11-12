/*
 * dbaseGroup.c -- group blocks and the DB map block (after dbaseGroup.asm
 * and dbaseMapBlk.asm), and the lock of the database manager.
 *
 * Items allocated DB_UNGROUPED go to the current "new ungroup" group;
 * when it is full (DB_GROUP_BLOCK_DESIRED_SIZE) another one is taken:
 * one of the set that has room again (VMBF_UNGROUPED_AVAIL), else a new
 * group.
 */
#include "dbaseInt.h"

#include <string.h>

/* the VM file was the asm's mutex point; one lock for all files here */
static KSem dbSem = { 1, 0 };
static ThreadHandle dbOwner;
static word dbNesting;

void DBEnter(void)
{
    ThreadHandle me = KernelThisThread();

    if (dbNesting && dbOwner == me) {
	dbNesting++;
	return;
    }
    KSemP(&dbSem, KSEM_FOREVER);
    dbOwner = me;
    dbNesting = 1;
}

void DBLeave(void)
{
    EC_CHECK(dbNesting && dbOwner == KernelThisThread(), BAD_UNLOCK);
    if (!--dbNesting) {
	dbOwner = 0;
	KSemV(&dbSem);
    }
}

void DBGroupLock(VMFileHandle file, VMBlockHandle group, DBGroup_ *g)
{
    g->file = file;
    g->group = group;
    g->p = VMLock(file, group, &g->mh);
    if (!g->p)
	KernelFatal(DB_LOCK_FAILED);
}

void DBGroupUnlock(DBGroup_ *g)
{
    VMUnlock(g->mh);
    g->p = NULL;
}

void DBGroupDirty(DBGroup_ *g)
{
    VMDirty(g->mh);
}

/* DBInitDBMap */
static VMBlockHandle InitDBMap(VMFileHandle file)
{
    VMBlockHandle map = VMAlloc(file, sizeof(DBMapBlock), DB_MAP_ID);
    MemHandle mh;
    DBMapBlock *m;
    DBGroup_ g;
    VMBlockHandle ungroup;

    VMSetDBMap(file, map);
    ungroup = DBGroupAlloc(file);
    DBGroupLock(file, ungroup, &g);
    DBGH(&g)->DBGH_flags |= GF_IS_UNGROUP;
    DBGroupDirty(&g);
    DBGroupUnlock(&g);
    m = VMLock(file, map, &mh);
    memset(m, 0, sizeof(*m));
    m->DBMB_ungrouped = ungroup;
    m->DBMB_vmemHandle = map;
    VMDirty(mh);
    VMUnlock(mh);
    return map;
}

/* DBLockDBMap: made the first time */
DBMapBlock *DBLockDBMap(VMFileHandle file, MemHandle *mh)
{
    VMBlockHandle map = VMGetDBMap(file);
    DBMapBlock *m;

    if (!map)
	map = InitDBMap(file);
    m = VMLock(file, map, mh);
    if (!m)
	KernelFatal(DB_LOCK_FAILED);
    return m;
}

/* DBGroupNewUngrouped */
static VMBlockHandle NewUngrouped(VMFileHandle file)
{
    VMBlockHandle group = VMFindAvailUngroup(file);
    MemHandle mh;
    DBMapBlock *m;
    DBGroup_ g;

    if (!group) {
	group = DBGroupAlloc(file);
	DBGroupLock(file, group, &g);
	DBGH(&g)->DBGH_flags |= GF_NEW_UNGROUP;
	DBGroupDirty(&g);
	DBGroupUnlock(&g);
    }
    m = DBLockDBMap(file, &mh);
    m->DBMB_newUngrouped = group;
    VMDirty(mh);
    VMUnlock(mh);
    return group;
}

VMBlockHandle DBGroupGetUngrouped(VMFileHandle file)
{
    MemHandle mh;
    DBMapBlock *m = DBLockDBMap(file, &mh);
    VMBlockHandle group = m->DBMB_newUngrouped;

    VMUnlock(mh);
    return group ? group : NewUngrouped(file);
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
