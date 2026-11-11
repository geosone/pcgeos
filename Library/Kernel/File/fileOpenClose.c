/*
 * fileOpenClose.c -- opening, creating and closing files (after
 * fileOpenClose.asm).
 *
 * A file handle is a SIG_FILE handle owned by the process of the thread
 * that opened it; its HE_addr is the KFile with the host file
 * (ghal_fs).  FileDuplicateHandle gives another handle to the same
 * KFile, so both share the position as with DOS.  The files a geode
 * still has open are closed when it is freed.
 *
 * Not yet: the deny modes (sharing), the GEOS file header of DOS16
 * files (FCF_NATIVE is what every file is for now), temporary files,
 * open/close notification.
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

KFile *FileOf(FileHandle fh)
{
    HandleEntry *he = HandleEntryOf(fh);

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

static FileHandle Open(const char *name, byte access, unsigned extra,
		       FileAttrs attrs)
{
    char host[PATH_BUFFER_SIZE];
    unsigned flags;
    ghal_file *f;
    ghal_status s;
    ghal_fs_info info;
    Boolean existed;
    KFile *kf;
    FileHandle fh;
    size_t n;

    if (!FileHostPath(name, host, sizeof(host)))
	return 0;
    switch (access & 3) {
    case FILE_ACCESS_R:	flags = GHAL_FS_READ; break;
    case FILE_ACCESS_W:	flags = GHAL_FS_WRITE; break;
    case FILE_ACCESS_RW:	flags = GHAL_FS_READ | GHAL_FS_WRITE; break;
    default:
	KernelSetError(ERROR_ARGS_INVALID);
	return 0;
    }
    existed = ghal_fs_stat(host, &info) == GHAL_OK;
    s = ghal_fs_open(host, flags | extra, &f);
    if (s != GHAL_OK) {
	KernelSetError(FileErrorOf(s, host));
	return 0;
    }
    n = strlen(host) + 1;
    kf = ghal_mem_alloc(sizeof(KFile) + n, GHAL_MEM_ZERO);
    if (!kf) {
	ghal_fs_close(f);
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    kf->KF_file = f;
    /* a new read-only file can still be written over this handle, as
     * with DOS: its attributes are set when it is closed */
    if (!existed)
	kf->KF_attrs = attrs & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM);
    kf->KF_path = (char *) (kf + 1);
    memcpy(kf->KF_path, host, n);
    fh = NewHandle(kf, access);
    if (!fh) {
	ghal_fs_close(f);
	ghal_mem_free(kf);
	return 0;
    }
    KernelSetError(0);
    return fh;
}

FileHandle FileOpen(const char *name, FileAccessFlags flags)
{
    return Open(name, flags, 0, 0);
}

FileHandle FileCreate(const char *name, FileCreateFlags flags,
		      FileAttrs attributes)
{
    unsigned extra = GHAL_FS_CREATE;

    switch (flags & FCF_MODE) {
    case FILE_CREATE_TRUNCATE:	extra |= GHAL_FS_TRUNCATE; break;
    case FILE_CREATE_NO_TRUNCATE:	break;
    case FILE_CREATE_ONLY:	extra |= GHAL_FS_EXCL; break;
    default:
	KernelSetError(ERROR_ARGS_INVALID);
	return 0;
    }
    return Open(name, (byte) (flags & FCF_ACCESS), extra, attributes);
}

FileHandle FileDuplicateHandle(FileHandle fh)
{
    FileHandle nh = NewHandle(FileOf(fh), HandleEntryOf(fh)->HE_flags);

    if (nh)
	KernelSetError(0);
    return nh;
}

static word Close(FileHandle fh)
{
    KFile *kf = FileOf(fh);
    ghal_status s = GHAL_OK;

    HandleFree(fh);
    if (--kf->KF_refs == 0) {
	s = ghal_fs_close(kf->KF_file);
	if (s == GHAL_OK && kf->KF_attrs)
	    s = ghal_fs_set_attrs(kf->KF_path, kf->KF_attrs);
	ghal_mem_free(kf);
    }
    return FileErrorOf(s, NULL);
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
