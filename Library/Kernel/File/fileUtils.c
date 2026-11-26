/*
 * fileUtils.c -- after fileUtils.asm: FileOpOnPath.
 */
#include "kernelC.h"
#include <initfile.h>
#include <string.h>

static word OpOnPlaces(DiskHandle base, const char *tail, const char *path,
		       FileOpFunc *op, void *data)
{
    return FileOpOnPlacesN(base, tail, path, op, data, 0);
}

word FileOpOnPath(const char *path, FileOpFunc *op, void *data)
{
    KPath *cur = FileCurPath();

    return OpOnPlaces(cur->KP_disk, cur->KP_tail, path, op, data);
}

void FileBatchChangeNotifications(void)
{
    KThread *t = ThreadOf(KernelThisThread());

    if (t && !t->T_fsNotifyBatch)
	t->T_fsNotifyBatch = FSDNewBatch();
}

void FileFlushChangeNotifications(void)
{
    KThread *t = ThreadOf(KernelThisThread());
    MemHandle mh;

    if (!t || !t->T_fsNotifyBatch)
	return;
    mh = t->T_fsNotifyBatch;
    t->T_fsNotifyBatch = 0;
    FSDSendNotify(FCNT_BATCH, mh);
}
