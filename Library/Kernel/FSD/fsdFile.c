/*
 * fsdFile.c -- after fsdFile.asm: FSDCheckOpenCloseNotifyEnabled,
 * FSDGenerateNotify.
 */
#include "kernelC.h"
#include "fsdConstant.h"
#include <string.h>

Boolean FSDCheckOpenCloseNotifyEnabled(void)
{
    return openCloseNotificationCount != 0;
}

static Boolean HasName(FileChangeNotificationType type)
{
    return type == FCNT_CREATE || type == FCNT_RENAME;
}

static void CopyName(char *to, const char *name)
{
    size_t n = name ? strlen(name) : 0;

    if (n >= sizeof(FileLongName))
	n = sizeof(FileLongName) - 1;
    memset(to, 0, sizeof(FileLongName));
    if (n)
	memcpy(to, name, n);
}

static Boolean AddToBatch(KThread *t, FileChangeNotificationType type,
			  DiskHandle disk, FileID id, const char *name)
{
    FileChangeBatchNotificationData *b;
    FileChangeBatchNotificationItem *it;
    word size = (word) (HasName(type)
		? sizeof(FileChangeBatchNotificationItem)
		: offsetof(FileChangeBatchNotificationItem, FCBNI_name));
    word end, need;

    b = MemLock(t->T_fsNotifyBatch);
    end = b->FCBND_end;
    if (end > FSD_MAX_BATCH_SIZE) {
	MemUnlock(t->T_fsNotifyBatch);
	FSDSendNotify(FCNT_BATCH, t->T_fsNotifyBatch);
	t->T_fsNotifyBatch = FSDNewBatch();
	if (!t->T_fsNotifyBatch)
	    return TRUE;
	b = MemLock(t->T_fsNotifyBatch);
	end = b->FCBND_end;
    }
    need = (word) ((end + size + FSD_ALLOC_GRANULARITY - 1) &
		   ~(FSD_ALLOC_GRANULARITY - 1));
    if (need > MemGetInfo(t->T_fsNotifyBatch, MGIT_SIZE)) {
	MemUnlock(t->T_fsNotifyBatch);
	if (!MemReAlloc(t->T_fsNotifyBatch, need, HAF_ZERO_INIT))
	    return TRUE;
	b = MemLock(t->T_fsNotifyBatch);
    }
    it = (FileChangeBatchNotificationItem *) ((byte *) b + end);
    it->FCBNI_type = type;
    it->FCBNI_disk = disk;
    it->FCBNI_id = id;
    if (HasName(type))
	CopyName(it->FCBNI_name, name);
    b->FCBND_end = (word) (end + size);
    MemUnlock(t->T_fsNotifyBatch);
    return FALSE;
}

/* TRUE if the notification was dropped for want of memory */
Boolean FSDGenerateNotify(FileChangeNotificationType type, DiskHandle disk,
			  FileID id, const char *name)
{
    KThread *t = ThreadOf(KernelThisThread());
    FileChangeNotificationData *n;
    MemHandle mh;

    if (t && t->T_fsNotifyBatch)
	return AddToBatch(t, type, disk, id, name);
    mh = MemAlloc(sizeof(FileChangeNotificationData),
		  HF_DYNAMIC | HF_SHARABLE, HAF_ZERO_INIT);
    if (!mh)
	return TRUE;
    n = MemLock(mh);
    n->FCND_disk = disk;
    n->FCND_id = id;
    if (HasName(type))
	CopyName(n->FCND_name, name);
    MemUnlock(mh);
    FSDSendNotify(type, mh);
    return FALSE;
}
