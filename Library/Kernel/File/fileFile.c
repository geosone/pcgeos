/*
 * fileFile.c -- operations on files and directories by name (after
 * fileFile.asm), over DR_FS_PATH_OP of the disk's driver.  Every routine
 * sets the error for ThreadGetError.
 *
 * FileCopy copies the data and the attributes of a file from one disk to
 * another (or the same) over both drivers; FileMove asks the driver to
 * move a file within a disk (FSPOF_MOVE_FILE), and copies and deletes
 * it between disks.  A source or destination disk of 0 is the current
 * path's; with a disk, the path is relative to its root.
 *
 * In a standard path spread over several directories (filePath.c) a
 * file is found at the first of them that has it; a new file or
 * directory goes to the first whose directory exists.  FileCopyLocal
 * and FileMoveLocal take the source from the first only.
 *
 * Links on the way are followed (FileLocate); a link at the end is
 * deleted, renamed or given attributes itself, as on DOS16.  Not yet:
 * links in the destination of FileCopy or of a rename with a path, the
 * notification of file changes.
 */
#include "kernelC.h"

#include <string.h>

static word Fail(word e)
{
    KernelSetError(e);
    return e;
}

word FileDelete(const char *name)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocate(0, name, dp, sizeof(dp));

    if (!d)
	return ThreadGetError();
    return Fail(FSD_OF(d)->FSD_deleteFile(d, dp));
}

/* the new name is in the directory of the old one unless it has a
 * path; both must be on the same disk */
word FileRename(const char *oldName, const char *newName)
{
    char from[FILE_DISK_PATH_SIZE], to[FILE_DISK_PATH_SIZE];
    char rel[PATH_BUFFER_SIZE];
    const char *sep;
    FSDDisk *d, *d2;
    size_t n;

    d = FileLocate(0, oldName, from, sizeof(from));
    if (!d)
	return ThreadGetError();
    if (strchr(newName, '\\') || strchr(newName, '/') ||
	strchr(newName, ':')) {
	if (strlen(newName) >= sizeof(rel))
	    return Fail(ERROR_PATH_TOO_LONG);
	strcpy(rel, newName);
	d2 = FileResolve(rel, to, sizeof(to));
	if (!d2)
	    return ThreadGetError();
	if (d2 != d)
	    return Fail(ERROR_DIFFERENT_DEVICE);
    } else {
	/* in the directory where the old one was found */
	sep = strrchr(from, '\\');
	n = sep ? (size_t) (sep - from) + 1 : 0;
	if (n + strlen(newName) >= sizeof(to))
	    return Fail(ERROR_PATH_TOO_LONG);
	memcpy(to, from, n);
	strcpy(to + n, newName);
    }
    return Fail(FSD_OF(d)->FSD_rename(d, from, to));
}

FileAttrs FileGetAttributes(const char *path)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocate(0, path, dp, sizeof(dp));
    FileAttrs a = 0;

    if (!d)
	return 0;
    Fail(FSD_OF(d)->FSD_getAttrs(d, dp, &a));
    return a;
}

word FileSetAttributes(const char *path, FileAttrs attr)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d;

    if (attr & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	return Fail(ERROR_ACCESS_DENIED);
    d = FileLocate(0, path, dp, sizeof(dp));
    if (!d)
	return ThreadGetError();
    return Fail(FSD_OF(d)->FSD_setAttrs(d, dp, attr));
}

static word CreateDirAt(FSDDisk *d, const char *dp, void *data)
{
    (void) data;
    return FSD_OF(d)->FSD_createDir(d, dp);
}

word FileCreateDir(const char *name)
{
    return Fail(FileOpOnPath(name, CreateDirAt, NULL));
}

/* not the current directory (the same file as "") */
word FileDeleteDir(const char *name)
{
    char dp[FILE_DISK_PATH_SIZE], cp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocate(0, name, dp, sizeof(dp)), *cd;
    dword id, cid;

    if (!d)
	return ThreadGetError();
    cd = FileResolve("", cp, sizeof(cp));
    if (cd == d &&
	!FSD_OF(d)->FSD_pathExtAttrs(d, dp, FEA_FILE_ID, &id, sizeof(id),
				     FALSE) &&
	!FSD_OF(d)->FSD_pathExtAttrs(d, cp, FEA_FILE_ID, &cid, sizeof(cid),
				     FALSE) &&
	id == cid)
	return Fail(ERROR_IS_CURRENT_DIRECTORY);
    return Fail(FSD_OF(d)->FSD_deleteDir(d, dp));
}

/* ---------------------------------------------------------------------
 * Copying and moving (FileCopyCommon, FileMoveCommon)
 * ------------------------------------------------------------------- */

#define COPY_BUF	32768

/* the attributes of the GeosFileHeader that go with a copy (not the
 * name: the copy has its own) */
static const struct {
    FileExtendedAttribute attr;
    word size;
} copyAttrs[] = {
    {FEA_FILE_TYPE, 2}, {FEA_FLAGS, 2}, {FEA_RELEASE, 8},
    {FEA_PROTOCOL, 4}, {FEA_TOKEN, 6}, {FEA_CREATOR, 6},
    {FEA_USER_NOTES, 100}, {FEA_NOTICE, 32}, {FEA_CREATION, 4},
    {FEA_PASSWORD, 8}, {FEA_DESKTOP_INFO, 16},
};

static word SameFile(FSDDisk *d1, const char *p1, FSDDisk *d2, const char *p2)
{
    dword id1, id2;

    return d1 == d2 &&
	   !FSD_OF(d1)->FSD_pathExtAttrs(d1, p1, FEA_FILE_ID, &id1,
					 sizeof(id1), FALSE) &&
	   !FSD_OF(d2)->FSD_pathExtAttrs(d2, p2, FEA_FILE_ID, &id2,
					 sizeof(id2), FALSE) &&
	   id1 == id2;
}

/* src (open) to a new file dst on dd: the data, then the header
 * attributes, the date and the file attributes (attrs: those of the
 * source by its path; DOS has them only there) */
static word CopyOpen(FSDFile *src, FileAttrs attrs, FSDDisk *dd,
		     const char *dst)
{
    const FSDriver *sf = FSD_OF(src->FF_disk), *df = FSD_OF(dd);
    FSDFile out;
    byte *buf, v[100];
    word type = GFT_NOT_GEOS_FILE, n, done, e, e2;
    FileDateAndTime fdat = 0;
    unsigned i;

    sf->FSD_handleExtAttrs(src, FEA_FILE_TYPE, &type, sizeof(type), FALSE);
    sf->FSD_getDateTime(src, &fdat);
    buf = ghal_mem_alloc(COPY_BUF, 0);
    if (!buf)
	return ERROR_INSUFFICIENT_MEMORY;
    out.FF_disk = dd;
    out.FF_private = NULL;
    e = df->FSD_create(dd, dst, (FileCreateFlags) (FILE_CREATE_ONLY |
		       FILE_ACCESS_RW |
		       (type == GFT_NOT_GEOS_FILE ? FCF_NATIVE : 0)), 0, &out);
    if (e) {
	ghal_mem_free(buf);
	return e;
    }
    do {
	e = sf->FSD_read(src, buf, COPY_BUF, &n);
	if (!e && n) {
	    e = df->FSD_write(&out, buf, n, &done);
	    if (!e && done < n)
		e = ERROR_SHORT_READ_WRITE;	/* the disk is full */
	}
    } while (!e && n == COPY_BUF);
    if (!e && type != GFT_NOT_GEOS_FILE)
	for (i = 0; !e && i < sizeof(copyAttrs) / sizeof(copyAttrs[0]); i++)
	    if (!sf->FSD_handleExtAttrs(src, copyAttrs[i].attr, v,
					copyAttrs[i].size, FALSE))
		e = df->FSD_handleExtAttrs(&out, copyAttrs[i].attr, v,
					   copyAttrs[i].size, TRUE);
    /* the date last: writing changes it */
    if (!e && fdat)
	e = df->FSD_setDateTime(&out, fdat);
    e2 = df->FSD_close(&out);
    if (!e)
	e = e2;
    attrs &= FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCHIVE;
    if (!e && attrs)
	e = df->FSD_setAttrs(dd, dst, attrs);
    if (e)
	df->FSD_deleteFile(dd, dst);
    ghal_mem_free(buf);
    return e;
}

/* an existing destination is replaced (FileCopy) */
static word Copy(FSDDisk *sd, const char *src, FSDDisk *dd, const char *dst)
{
    FSDFile in;
    FileAttrs a, sa = 0;
    word e;

    if (SameFile(sd, src, dd, dst))
	return 0;
    FSD_OF(sd)->FSD_getAttrs(sd, src, &sa);
    in.FF_disk = sd;
    in.FF_private = NULL;
    e = FSD_OF(sd)->FSD_open(sd, src, FILE_ACCESS_R | FILE_DENY_W, &in);
    if (e)
	return e;
    if (!FSD_OF(dd)->FSD_getAttrs(dd, dst, &a))
	e = (a & FA_SUBDIR) ? ERROR_ACCESS_DENIED
			    : FSD_OF(dd)->FSD_deleteFile(dd, dst);
    if (!e)
	e = CopyOpen(&in, sa, dd, dst);
    FSD_OF(sd)->FSD_close(&in);
    return e;
}

static word CopyMove(const TCHAR *source, const TCHAR *dest,
		     DiskHandle sourceDisk, DiskHandle destDisk, Boolean move,
		     Boolean local)
{
    char src[FILE_DISK_PATH_SIZE], dst[FILE_DISK_PATH_SIZE];
    FSDDisk *sd, *dd;
    FileAttrs a;
    word e;

    sd = local ? FileResolveOn(sourceDisk, source, src, sizeof(src))
	       : FileLocate(sourceDisk, source, src, sizeof(src));
    if (!sd)
	return ThreadGetError();
    dd = FileResolveOn(destDisk, dest, dst, sizeof(dst));
    if (!dd)
	return ThreadGetError();
    if (!move)
	return Fail(Copy(sd, src, dd, dst));
    e = FSD_OF(sd)->FSD_getAttrs(sd, src, &a);
    if (e)
	return Fail(e);
    if (!FSD_OF(dd)->FSD_getAttrs(dd, dst, &a) &&
	!SameFile(sd, src, dd, dst))
	return Fail(ERROR_FILE_EXISTS);
    if (sd == dd)
	return Fail(FSD_OF(sd)->FSD_rename(sd, src, dst));
    FSD_OF(sd)->FSD_getAttrs(sd, src, &a);
    if (a & FA_SUBDIR)
	return Fail(ERROR_DIFFERENT_DEVICE);
    e = Copy(sd, src, dd, dst);
    if (!e) {
	if (a & FA_RDONLY)
	    FSD_OF(sd)->FSD_setAttrs(sd, src, 0);
	e = FSD_OF(sd)->FSD_deleteFile(sd, src);
	if (e)
	    FSD_OF(dd)->FSD_deleteFile(dd, dst);
    }
    return Fail(e);
}

word FileCopy(const TCHAR *source, const TCHAR *dest, DiskHandle sourceDisk,
	      DiskHandle destDisk)
{
    return CopyMove(source, dest, sourceDisk, destDisk, FALSE, FALSE);
}

word FileCopyLocal(const TCHAR *source, const TCHAR *dest,
		   DiskHandle sourceDisk, DiskHandle destDisk)
{
    return CopyMove(source, dest, sourceDisk, destDisk, FALSE, TRUE);
}

word FileMove(const TCHAR *source, const TCHAR *dest, DiskHandle sourceDisk,
	      DiskHandle destDisk)
{
    return CopyMove(source, dest, sourceDisk, destDisk, TRUE, FALSE);
}

word FileMoveLocal(const TCHAR *source, const TCHAR *dest,
		   DiskHandle sourceDisk, DiskHandle destDisk)
{
    return CopyMove(source, dest, sourceDisk, destDisk, TRUE, TRUE);
}
