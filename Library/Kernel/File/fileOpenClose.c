/*
 * fileOpenClose.c -- opening, creating and closing files (after
 * fileOpenClose.asm).
 *
 * A file handle is a SIG_FILE handle owned by the process of the thread
 * that opened it (a VM file's SIG_VM handle stands for its file, FileOf); its HE_addr is the KFile with what the file system
 * driver of its disk keeps (DR_FS_ALLOC_OP, DR_FS_HANDLE_OP).
 * FileDuplicateHandle gives another handle to the same KFile, so both
 * share the position as with DOS.  The files a geode still has open are
 * closed when it is freed.
 *
 * Not yet: the deny modes (sharing), temporary files, open/close
 * notification.
 */
#include "kernelC.h"

#include <string.h>

KFile *FileOf(FileHandle fh)
{
    HandleEntry *he = HandleEntryOf(fh);

    /* a VM file's handle stands for its file, as in DOS16, where it was
     * the file handle (the TrueType driver asks its cache file's
     * protocol so) */
    if (he->HE_type == SIG_VM && ((KVMFile *) he->HE_addr)->VF_file)
	he = HandleEntryOf(((KVMFile *) he->HE_addr)->VF_file);
    if (he->HE_type != SIG_FILE)
	KernelFatal(ILLEGAL_HANDLE);
    return he->HE_addr;
}

static FileHandle NewHandle(KFile *kf, byte access)
{
    FileHandle fh = HandleAlloc(SIG_FILE, GeodeGetProcessHandle());

    if (!fh) {
	KernelSetError(ERROR_TOO_MANY_OPEN_FILES);
	return 0;
    }
    HandleEntryOf(fh)->HE_addr = kf;
    HandleEntryOf(fh)->HE_flags = access;
    kf->KF_refs++;
    return fh;
}

typedef struct {
    word	O_flags;
    FileAttrs	O_attrs;
    Boolean	O_create;
    KFile	*O_kf;
} OpenOp;

static word OpenAt(FSDDisk *d, const char *dp, void *data)
{
    OpenOp *o = data;

    o->O_kf->KF_file.FF_disk = d;
    return o->O_create
	   ? FSD_OF(d)->FSD_create(d, dp, o->O_flags, o->O_attrs,
				   &o->O_kf->KF_file)
	   : FSD_OF(d)->FSD_open(d, dp, o->O_flags, &o->O_kf->KF_file);
}

/* open or create (create: FileCreate's flags) the file at `name', at
 * the first place of the path that has it (or its directory) */
static FileHandle Open(const char *name, word flags, FileAttrs attrs,
		       Boolean create)
{
    KFile *kf;
    FSDDisk *d;
    FileHandle fh;
    OpenOp o;
    word e;

    kf = ghal_mem_alloc(sizeof(KFile), GHAL_MEM_ZERO);
    if (!kf) {
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    o.O_flags = flags;
    o.O_attrs = attrs;
    o.O_create = create;
    o.O_kf = kf;
    e = FileOpOnPath(name, OpenAt, &o);
    if (e) {
	ghal_mem_free(kf);
	KernelSetError(e);
	return 0;
    }
    d = kf->KF_file.FF_disk;
    fh = NewHandle(kf, (byte) (flags & FCF_ACCESS));
    if (!fh) {
	FSD_OF(d)->FSD_close(&kf->KF_file);
	ghal_mem_free(kf);
	return 0;
    }
    d->FDK_inUse++;
    KernelSetError(0);
    return fh;
}

FileHandle FileOpen(const char *name, FileAccessFlags flags)
{
    return Open(name, flags, 0, FALSE);
}

/* without FCF_NATIVE a GEOS file (the driver: with the GeosFileHeader) */
FileHandle FileCreate(const char *name, FileCreateFlags flags,
		      FileAttrs attributes)
{
    if ((flags & FCF_MODE) != FILE_CREATE_TRUNCATE &&
	(flags & FCF_MODE) != FILE_CREATE_NO_TRUNCATE &&
	(flags & FCF_MODE) != FILE_CREATE_ONLY) {
	KernelSetError(ERROR_ARGS_INVALID);
	return 0;
    }
    return Open(name, flags, attributes, TRUE);
}

FileHandle FileDuplicateHandle(FileHandle fh)
{
    FileHandle nh = NewHandle(FileOf(fh), HandleEntryOf(fh)->HE_flags);

    if (nh)
	KernelSetError(0);
    return nh;
}

DiskHandle FileGetDiskHandle(FileHandle fh)
{
    KernelSetError(0);
    return FileOf(fh)->KF_file.FF_disk->FDK_handle;
}

static word Close(FileHandle fh)
{
    KFile *kf = FileOf(fh);
    word e = 0;

    HandleFree(fh);
    if (--kf->KF_refs == 0) {
	FSDDisk *d = kf->KF_file.FF_disk;
	e = FSD_OF(d)->FSD_close(&kf->KF_file);
	d->FDK_inUse--;
	ghal_mem_free(kf);
    }
    return e;
}

word FileClose(FileHandle fh, Boolean noErrorFlag)
{
    word e = Close(fh);

    if (e && noErrorFlag)
	KernelFatal(FILE_ERROR_NOT_EXPECTED);
    KernelSetError(e);
    return e;
}

void FileFreeHandle(FileHandle fh)
{
    Close(fh);
}
