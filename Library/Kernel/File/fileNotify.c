/*
 * fileNotify.c -- Port32: file change notification for every driver
 * (the routines of the asm are beside it: FSDGenerateNotify and
 * FSDAddNotifyToBatch in FSD/fsdFile.c, FileBatchChangeNotifications and
 * FileFlushChangeNotifications in fileUtils.c, File{En,Dis}able-
 * OpenCloseNotification in fileOpenClose.c); here what they share and
 * what the file routines call.
 *
 * What changes on a disk goes to the objects of the GCN list
 * GCNSLT_FILE_SYSTEM as MSG_NOTIFY_FILE_CHANGE (FileChangeNotificationType,
 * a sharable block of FileChangeNotificationData), forced onto their
 * queues.  The DOS16 file system drivers sent them; here the kernel does,
 * after the driver has done the operation, for every driver alike:
 *
 *   FCNT_CREATE	a file, directory or link made: the ID of the directory
 *			it is in, its name
 *   FCNT_RENAME	the ID of the file (before), its new name
 *   FCNT_DELETE	the ID of the file or directory (before)
 *   FCNT_ATTRIBUTES	FileAttrs or extended attributes set: the ID of the file
 *   FCNT_CONTENTS	a file written to is closed: its ID
 *   FCNT_OPEN, FCNT_CLOSE  while FileEnableOpenCloseNotification is on
 *
 * A move to another directory is a delete and a create, a copy a
 * create.  The ID is what FEA_FILE_ID gives (disk and ID are what
 * FileGetCurrentPathIDs gives for a directory).
 *
 * Between FileBatchChangeNotifications and FileFlushChangeNotifications
 * a thread's notifications go into one FCNT_BATCH block
 * (FileChangeBatchNotificationData); the asm chained more blocks when
 * one grew past FSD_MAX_BATCH_SIZE, here the full one is sent at once
 * and a new one begun (the order stays the same).
 */
#include "kernelC.h"

#include <string.h>

#define GCNSLT_FILE_SYSTEM_LIST	0	/* gcnlist.goh */
#define KMSG_NOTIFY_FILE_CHANGE	693	/* gcnlist.goh */
#define FSD_ALLOC_GRANULARITY	1024

word fsdOpenCloseCount;

/* the message to the list; the block goes with it (or is freed) */
void FSDSendNotify(FileChangeNotificationType type, MemHandle block)
{
    struct {
	FileChangeNotificationType type;
	MemHandle data;
    } p;
    EventHandle ev;

    MemInitRefCount(block, 1);
    p.type = type;
    p.data = block;
    CObjMessageP(MF_RECORD, 0, KMSG_NOTIFY_FILE_CHANGE, &p, sizeof(p), &ev,
		 sizeof(ev));
    (void) GCNListSend(MANUFACTURER_ID_GEOWORKS, GCNSLT_FILE_SYSTEM_LIST, ev,
		       block, GCNLSF_FORCE_QUEUE);
}

MemHandle FSDNewBatch(void)
{
    MemHandle mh = MemAlloc(FSD_ALLOC_GRANULARITY, HF_DYNAMIC | HF_SHARABLE,
			    HAF_ZERO_INIT);
    FileChangeBatchNotificationData *b;

    if (!mh)
	return 0;
    b = MemLock(mh);
    b->FCBND_end = (word) offsetof(FileChangeBatchNotificationData,
				   FCBND_notifications);
    MemUnlock(mh);
    return mh;
}

/* ---------------------------------------------------------------------
 * For the kernel's file operations
 * ------------------------------------------------------------------- */

FileID FileIDOf(FSDDisk *d, const char *dp)
{
    FileID id = 0;

    (void) FSD_OF(d)->FSD_pathExtAttrs(d, dp, FEA_FILE_ID, &id, sizeof(id),
				       FALSE);
    return id;
}

FileID FileIDOfOpen(FSDFile *f)
{
    FileID id = 0;

    (void) FSD_OF(f->FF_disk)->FSD_handleExtAttrs(f, FEA_FILE_ID, &id,
						  sizeof(id), FALSE);
    return id;
}

/* the last component of a path on the disk */
const char *FileNameOf(const char *dp)
{
    const char *s = strrchr(dp, '\\');

    return s ? s + 1 : dp;
}

/* FCNT_CREATE for dp: the ID of its directory, its name */
void FileNotifyCreate(FSDDisk *d, const char *dp)
{
    char dir[FILE_DISK_PATH_SIZE];
    const char *name = FileNameOf(dp);
    size_t n = (size_t) (name - dp);

    if (n >= sizeof(dir))
	return;
    memcpy(dir, dp, n);
    /* "\\A\\B" -> "\\A"; "\\B" -> "\\" */
    if (n > 1)
	n--;
    dir[n] = 0;
    (void) FSDGenerateNotify(FCNT_CREATE, d->FDK_handle, FileIDOf(d, dir),
			     name);
}

/* a notification about the file itself */
void FileNotify(FileChangeNotificationType type, FSDDisk *d, FileID id)
{
    (void) FSDGenerateNotify(type, d->FDK_handle, id, NULL);
}
