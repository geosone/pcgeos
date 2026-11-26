/*
 * dbaseMapBlk.c -- after dbaseMapBlk.asm: DBLockDBMap.
 */
#include "dbaseInt.h"
#include <string.h>

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
