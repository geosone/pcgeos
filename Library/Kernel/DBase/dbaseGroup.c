/*
 * dbaseGroup.c -- group blocks (after dbaseGroup.asm) and the lock of
 * the database manager; the DB map block is in dbaseMapBlk.c.
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
