/*
 * fileEA.c -- extended attributes of files and directories (the
 * FileGet/Set...ExtAttributes of fileFile.asm and fileOpenClose.asm), over
 * FSHOF_GET/SET_EXT_ATTRIBUTES and FSPOF_GET/SET_EXT_ATTRIBUTES of the
 * disk's driver: what they are and where they are kept is the driver's
 * (HostFS: the GeosFileHeader, Driver/IFS/HostFS/hostfsDriver.c).
 */
#include "kernelC.h"

#include <string.h>

static word Done(word e)
{
    KernelSetError(e);
    return e;
}

/*
 * FEA_DISK, the handle of the disk a file is on, is the kernel's own
 * (the asm's drivers answered it from the disk they were passed): alone
 * or among FEA_MULTIPLE it is filled in here.  TRUE if that was all;
 * else *bufferP and *sizeP are what is left for the driver (a list
 * without it: in `rest', or in a block returned in *mhP to free).
 */
#define EA_REST	16

static Boolean DiskAttr(FSDDisk *d, FileExtendedAttribute attr,
			void **bufferP, word *sizeP, FileExtAttrDesc *rest,
			MemHandle *mhP, word *e)
{
    FileExtAttrDesc *a = *bufferP, *r = rest;
    word i, n = 0, disks = 0;

    *mhP = 0;
    *e = 0;
    if (attr == FEA_DISK) {
	if (*sizeP < sizeof(DiskHandle))
	    *e = ERROR_ATTR_SIZE_MISMATCH;
	else
	    *(DiskHandle *) *bufferP = d->FDK_handle;
	return TRUE;
    }
    if (attr != FEA_MULTIPLE)
	return FALSE;
    for (i = 0; i < *sizeP; i++)
	disks += a[i].FEAD_attr == FEA_DISK;
    if (!disks)
	return FALSE;
    if (*sizeP - disks > EA_REST) {
	*mhP = MemAlloc((*sizeP - disks) * sizeof(FileExtAttrDesc),
			HF_SWAPABLE, HAF_LOCK);
	if (!*mhP) {
	    *e = ERROR_INSUFFICIENT_MEMORY;
	    return TRUE;
	}
	r = MemDeref(*mhP);
    }
    for (i = 0; i < *sizeP; i++) {
	if (a[i].FEAD_attr != FEA_DISK)
	    r[n++] = a[i];
	else if (a[i].FEAD_size < sizeof(DiskHandle))
	    *e = ERROR_ATTR_SIZE_MISMATCH;
	else
	    *(DiskHandle *) a[i].FEAD_value = d->FDK_handle;
    }
    if (*e || !n) {
	if (*mhP)
	    MemFree(*mhP);
	*mhP = 0;
	return TRUE;
    }
    *bufferP = r;
    *sizeP = n;
    return FALSE;
}

static word ByPath(const TCHAR *path, FileExtendedAttribute attr,
		   void *buffer, word bufSize, Boolean set)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocate(0, path, dp, sizeof(dp));
    FileExtAttrDesc rest[EA_REST];
    MemHandle mh = 0;
    word e;

    if (!d)
	return ThreadGetError();
    if (!set && DiskAttr(d, attr, &buffer, &bufSize, rest, &mh, &e))
	return Done(e);
    e = FSD_OF(d)->FSD_pathExtAttrs(d, dp, attr, buffer, bufSize, set);
    if (mh)
	MemFree(mh);
    if (set && !e)
	FileNotify(FCNT_ATTRIBUTES, d, FileIDOf(d, dp));
    return Done(e);
}

static word ByHandle(FileHandle fh, FileExtendedAttribute attr, void *buffer,
		     word bufSize, Boolean set)
{
    KFile *kf = FileOf(fh);
    FSDDisk *d = kf->KF_file.FF_disk;
    FileExtAttrDesc rest[EA_REST];
    MemHandle mh = 0;
    word e;

    if (!set && DiskAttr(d, attr, &buffer, &bufSize, rest, &mh, &e))
	return Done(e);
    e = FSD_OF(d)->FSD_handleExtAttrs(&kf->KF_file, attr, buffer, bufSize,
				      set);
    if (mh)
	MemFree(mh);
    if (set && !e)
	FileNotify(FCNT_ATTRIBUTES, d, FileIDOfOpen(&kf->KF_file));
    return Done(e);
}

word FileGetPathExtAttributes(const TCHAR *path, FileExtendedAttribute attr,
			      void *buffer, word bufSize)
{
    return ByPath(path, attr, buffer, bufSize, FALSE);
}

word FileSetPathExtAttributes(const TCHAR *path, FileExtendedAttribute attr,
			      const void *buffer, word bufSize)
{
    return ByPath(path, attr, (void *) buffer, bufSize, TRUE);
}

word FileGetHandleExtAttributes(FileHandle fh, FileExtendedAttribute attr,
				void *buffer, word bufSize)
{
    return ByHandle(fh, attr, buffer, bufSize, FALSE);
}

word FileSetHandleExtAttributes(FileHandle fh, FileExtendedAttribute attr,
				const void *buffer, word bufSize)
{
    return ByHandle(fh, attr, (void *) buffer, bufSize, TRUE);
}

/* ---------------------------------------------------------------------
 * All the attributes that can be set (FSHOF/FSPOF_GET_ALL_EXT_ATTRIBUTES,
 * as the DOS driver's dosvceaAllAttrs): a locked block of
 * FileExtAttrDesc with the values after them; a file that is no GEOS
 * file has only its modification time and FileAttrs.
 * ------------------------------------------------------------------- */

typedef struct {
    FileExtAttrDesc	AA_attrs[13];
    FileDateAndTime	AA_modified;
    FileAttrs		AA_fileAttr;
    word		AA_type;
    word		AA_flags;
    ReleaseNumber	AA_release;
    ProtocolNumber	AA_protocol;
    GeodeToken		AA_token;
    GeodeToken		AA_creator;
    FileUserNotes	AA_notes;
    FileCopyrightNotice	AA_notice;
    FileDateAndTime	AA_created;
    FilePassword	AA_password;
    FileDesktopInfo	AA_desktop;
} AllAttrs;

#define AA_NON_GEOS	2		/* the first two */

typedef word GetAttrFunc(void *what, FileExtendedAttribute attr, void *buf,
			 word size);

static MemHandle AllAttrs_(GetAttrFunc *get, void *what, word *count)
{
    MemHandle mh = MemAlloc(sizeof(AllAttrs), HF_DYNAMIC, HAF_LOCK);
    AllAttrs *a;
    word i, e = 0, n;

    if (!mh) {
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    a = MemDeref(mh);
    memset(a, 0, sizeof(*a));
#define AA(i, attr, field) \
    (a->AA_attrs[i].FEAD_attr = (attr), a->AA_attrs[i].FEAD_value = \
     (void *) &a->field, a->AA_attrs[i].FEAD_size = sizeof(a->field))
    AA(0, FEA_MODIFICATION, AA_modified);
    AA(1, FEA_FILE_ATTR, AA_fileAttr);
    AA(2, FEA_FILE_TYPE, AA_type);
    AA(3, FEA_FLAGS, AA_flags);
    AA(4, FEA_RELEASE, AA_release);
    AA(5, FEA_PROTOCOL, AA_protocol);
    AA(6, FEA_TOKEN, AA_token);
    AA(7, FEA_CREATOR, AA_creator);
    AA(8, FEA_USER_NOTES, AA_notes);
    AA(9, FEA_NOTICE, AA_notice);
    AA(10, FEA_CREATION, AA_created);
    AA(11, FEA_PASSWORD, AA_password);
    AA(12, FEA_DESKTOP_INFO, AA_desktop);
#undef AA
    n = sizeof(a->AA_attrs) / sizeof(a->AA_attrs[0]);
    for (i = 0; i < n; i++) {
	e = get(what, a->AA_attrs[i].FEAD_attr, a->AA_attrs[i].FEAD_value,
		a->AA_attrs[i].FEAD_size);
	if (e == ERROR_ATTR_NOT_FOUND && i >= AA_NON_GEOS) {
	    n = AA_NON_GEOS;
	    e = 0;
	    break;
	}
	if (e)
	    break;
	if (i == 2 && (a->AA_type == GFT_NOT_GEOS_FILE ||
		       a->AA_type == GFT_DIRECTORY + 1)) {	/* GFT_OLD_VM */
	    n = AA_NON_GEOS;
	    break;
	}
    }
    if (e) {
	MemFree(mh);
	KernelSetError(e);
	return 0;
    }
    if (count)
	*count = n;
    KernelSetError(0);
    return mh;
}

static word GetByHandle(void *what, FileExtendedAttribute attr, void *buf,
			word size)
{
    KFile *kf = what;

    return FSD_OF(kf->KF_file.FF_disk)->FSD_handleExtAttrs(&kf->KF_file,
							   attr, buf, size,
							   FALSE);
}

typedef struct {
    FSDDisk	*d;
    const char	*dp;
} PathAt;

static word GetByPath(void *what, FileExtendedAttribute attr, void *buf,
		      word size)
{
    PathAt *p = what;

    return FSD_OF(p->d)->FSD_pathExtAttrs(p->d, p->dp, attr, buf, size,
					  FALSE);
}

MemHandle FileGetHandleAllExtAttributes(FileHandle fh, word *numExtAttrs)
{
    return AllAttrs_(GetByHandle, FileOf(fh), numExtAttrs);
}

/* the attributes of the block to the file or directory at path (disk:
 * from its root, 0 the current path) */
static word SetAll(MemHandle mh, word n, DiskHandle disk, const char *path)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocate(disk, path, dp, sizeof(dp));
    word e;

    e = d ? FSD_OF(d)->FSD_pathExtAttrs(d, dp, FEA_MULTIPLE, MemDeref(mh),
					n, TRUE)
	  : ThreadGetError();
    MemFree(mh);
    return Done(e);
}

/* FileCopyExtAttributes: those of the open file to the named one */
word FileCopyExtAttributes(FileHandle fh, const char *path)
{
    word n;
    MemHandle mh = FileGetHandleAllExtAttributes(fh, &n);

    return mh ? SetAll(mh, n, 0, path) : ThreadGetError();
}

word FileCopyPathExtAttributes(const char *sourcePath, DiskHandle sourceDisk,
			       const char *destPath, DiskHandle destDisk)
{
    char dp[FILE_DISK_PATH_SIZE];
    PathAt p;
    MemHandle mh;
    word n;

    p.d = FileLocate(sourceDisk, sourcePath, dp, sizeof(dp));
    if (!p.d)
	return ThreadGetError();
    p.dp = dp;
    mh = AllAttrs_(GetByPath, &p, &n);
    return mh ? SetAll(mh, n, destDisk, destPath) : ThreadGetError();
}
