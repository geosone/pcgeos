/*
 * fileUtils.c -- after fileUtils.asm: FileOpOnPath.
 */
#include "kernelC.h"
#include <initfile.h>
#include <string.h>
#include "fileVariable.h"
#include "fileConstant.h"

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

/* FileGetLinkDataCommon: the target of the link a driver met: its disk (a
 * standard path, a disk, or SP_TOP for a path with a drive name) and
 * the path; 0 with the error set */
DiskHandle FileGetLinkDataCommon(char *path, size_t size)
{
    KLink *l = ThreadOf(KernelThisThread())->T_link;
    DiskHandle disk = SP_TOP;
    word e;

    if (!l || l->KL_tooLong || strlen(l->KL_path) >= size) {
	KernelSetError(l ? ERROR_PATH_TOO_LONG : ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    strcpy(path, l->KL_path);
    if (l->KL_diskSize) {
	disk = DiskRestore(l->KL_disk, NULL);
	if (!disk) {
	    /* the DiskRestoreError as a FileError */
	    e = ThreadGetError();
	    KernelSetError(e == DRE_NOT_ATTACHED_TO_SERVER
			   ? ERROR_NETWORK_NOT_LOGGED_IN
			   : e == DRE_PERMISSION_DENIED ? ERROR_ACCESS_DENIED
			   : ERROR_DISK_UNAVAILABLE);
	    return 0;
	}
    }
    return disk;
}
