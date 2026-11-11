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
 * A GEOS file (created without FCF_NATIVE) starts with the
 * GeosFileHeader of DOS16 on every host (Port32/hostfs); the handle
 * reads and writes the data after it (KF_base).  FileOpen takes a file
 * with that header as a GEOS file.
 *
 * Not yet: the deny modes (sharing), temporary files, open/close
 * notification.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"

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

static ghal_status AccessFlags(byte access, unsigned *flags)
{
    switch (access & 3) {
    case FILE_ACCESS_R:	*flags = GHAL_FS_READ; return GHAL_OK;
    case FILE_ACCESS_W:	*flags = GHAL_FS_WRITE; return GHAL_OK;
    case FILE_ACCESS_RW:	*flags = GHAL_FS_READ | GHAL_FS_WRITE; return GHAL_OK;
    default:			return GHAL_ERR_INVAL;
    }
}

/* the handle of an open host file, positioned at the start of its data */
static FileHandle Finish(ghal_file *f, const char *host, byte access,
			 FileAttrs attrs, Boolean geos)
{
    size_t n = strlen(host) + 1;
    KFile *kf = ghal_mem_alloc(sizeof(KFile) + n, GHAL_MEM_ZERO);
    FileHandle fh;

    if (!kf) {
	ghal_fs_close(f);
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    kf->KF_file = f;
    kf->KF_base = geos ? GFH_SIZE : 0;
    ghal_fs_seek(f, kf->KF_base, GHAL_FS_SEEK_SET, NULL);
    /* a new read-only file can still be written over this handle, as
     * with DOS: its attributes are set when it is closed */
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

static Boolean HasHeader(const char *host)
{
    unsigned char hdr[GFH_SIZE];

    return HostFSReadHeader(host, hdr) == GHAL_OK;
}

FileHandle FileOpen(const char *name, FileAccessFlags flags)
{
    char host[HOST_PATH_SIZE];
    unsigned gf;
    ghal_file *f;
    ghal_status s;

    if (!FileHostPath(name, host, sizeof(host)))
	return 0;
    if (AccessFlags((byte) flags, &gf) != GHAL_OK) {
	KernelSetError(ERROR_ARGS_INVALID);
	return 0;
    }
    s = ghal_fs_open(host, gf, &f);
    if (s != GHAL_OK) {
	KernelSetError(FileErrorOf(s, host));
	return 0;
    }
    return Finish(f, host, (byte) flags, 0, HasHeader(host));
}

/* without FCF_NATIVE a GEOS file: the GeosFileHeader (GFT_DATA) first,
 * on DOS a generated 8.3 name (dosPath.asm DOSAllocOpCreate).  An
 * existing file must be of the kind asked for. */
FileHandle FileCreate(const char *name, FileCreateFlags flags,
		      FileAttrs attributes)
{
    Boolean geos = !(flags & FCF_NATIVE), had;
    byte access = (byte) (flags & FCF_ACCESS);
    FileTarget *t;
    unsigned gf;
    ghal_file *f = NULL;
    ghal_status s;
    FileHandle fh = 0;

    if (AccessFlags(access, &gf) != GHAL_OK ||
	((flags & FCF_MODE) != FILE_CREATE_TRUNCATE &&
	 (flags & FCF_MODE) != FILE_CREATE_NO_TRUNCATE &&
	 (flags & FCF_MODE) != FILE_CREATE_ONLY)) {
	KernelSetError(ERROR_ARGS_INVALID);
	return 0;
    }
    t = ghal_mem_alloc(sizeof(FileTarget), 0);
    if (!t) {
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    if (!FileResolveNew(name, (flags & (FCF_NATIVE | FCF_NATIVE_WITH_EXT_ATTRS))
			      ? HOSTFS_NATIVE : HOSTFS_GEOS, t))
	goto done;
    if (t->FT_exists) {
	if ((flags & FCF_MODE) == FILE_CREATE_ONLY) {
	    KernelSetError(ERROR_FILE_EXISTS);
	    goto done;
	}
	had = HasHeader(t->FT_host);
	if (had != geos) {
	    KernelSetError(ERROR_FILE_FORMAT_MISMATCH);
	    goto done;
	}
	s = ghal_fs_open(t->FT_host, gf, &f);
	/* truncating keeps the header */
	if (s == GHAL_OK && (flags & FCF_MODE) == FILE_CREATE_TRUNCATE)
	    s = ghal_fs_truncate(f, geos ? GFH_SIZE : 0);
	if (s != GHAL_OK) {
	    if (f)
		ghal_fs_close(f);
	    KernelSetError(FileErrorOf(s, t->FT_host));
	    goto done;
	}
	fh = Finish(f, t->FT_host, access, 0, geos);
	goto done;
    }
    s = ghal_fs_open(t->FT_host, GHAL_FS_READ | GHAL_FS_WRITE |
		     GHAL_FS_CREATE | GHAL_FS_EXCL, &f);
    if (s == GHAL_OK && geos) {
	unsigned char hdr[GFH_SIZE];
	size_t done = 0;
	HostFSInitHeader(hdr, t->FT_name, GFT_DATA, FileNow());
	s = ghal_fs_write(f, hdr, sizeof(hdr), &done);
	/* DOS updates the directory entry of an open file only on a
	 * commit: before it, the file looks empty and its header is not
	 * found by its GEOS name (HostFSLookup) */
	if (s == GHAL_OK)
	    s = ghal_fs_sync(f);
	if (s != GHAL_OK) {
	    ghal_fs_close(f);
	    ghal_fs_delete(t->FT_host);
	}
    }
    if (s != GHAL_OK) {
	KernelSetError(FileErrorOf(s, t->FT_host));
	goto done;
    }
    fh = Finish(f, t->FT_host, access, attributes, geos);
done:
    ghal_mem_free(t);
    return fh;
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
