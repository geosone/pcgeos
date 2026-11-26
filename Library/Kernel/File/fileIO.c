/*
 * fileIO.c -- input and output on an open file (after fileIO.asm), over
 * DR_FS_HANDLE_OP of the file's driver.
 *
 * Every routine sets the error for ThreadGetError.  A read or write
 * that moves fewer bytes than asked is ERROR_SHORT_READ_WRITE (the end
 * of the file, a full disk).  With noErrorFlag set, an error is fatal.
 * Positions and sizes are those of the data (in a GEOS file: after the
 * GeosFileHeader), as on DOS16.
 *
 * Not yet: record locks.
 */
#include "kernelC.h"
#include "fileMacro.h"
#include <string.h>
#include "ghal.h"

static word Done(word e, Boolean noErrorFlag)
{
    if (e && e != ERROR_SHORT_READ_WRITE && noErrorFlag)
	KernelFatal(FILE_ERROR_NOT_EXPECTED);
    KernelSetError(e);
    return e;
}

#define DRV(kf)	FSD_OF((kf)->KF_file.FF_disk)

word FileRead(FileHandle fh, void *buf, word count, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);
    word done = 0, e = DRV(kf)->FSD_read(&kf->KF_file, buf, count, &done);

    Done(e ? e : done < count ? ERROR_SHORT_READ_WRITE : 0, noErrorFlag);
    return done;
}

word FileWrite(FileHandle fh, const void *buf, word count,
	       Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);
    word done = 0, e = DRV(kf)->FSD_write(&kf->KF_file, buf, count, &done);

    if (done)
	kf->KF_written = TRUE;

    Done(e ? e : done < count ? ERROR_SHORT_READ_WRITE : 0, noErrorFlag);
    return done;
}

dword FilePos(FileHandle fh, dword posOrOffset, FilePosMode mode)
{
    KFile *kf = FileOf(fh);
    dword pos = 0;
    word e = DRV(kf)->FSD_position(&kf->KF_file, (sdword) posOrOffset, mode,
				   &pos);

    KernelSetError(e);
    return e ? (dword) -1 : pos;
}

word FileTruncate(FileHandle fh, dword offset, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);

    kf->KF_written = TRUE;
    return Done(DRV(kf)->FSD_truncate(&kf->KF_file, offset), noErrorFlag);
}

dword FileSize(FileHandle fh)
{
    KFile *kf = FileOf(fh);
    dword size = 0;

    KernelSetError(DRV(kf)->FSD_size(&kf->KF_file, &size));
    return size;
}

word FileCommit(FileHandle fh, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);

    return Done(DRV(kf)->FSD_commit(&kf->KF_file), noErrorFlag);
}

/* ---------------------------------------------------------------------
 * Date and time: FileDateAndTime is the DOS format
 * ------------------------------------------------------------------- */

FileDateAndTime FilePackDate(const ghal_datetime *dt)
{
    if (dt->year < FDAT_BASE_YEAR)
	return 0;
    return ((dword) dt->hour << FDAT_HOUR_OFFSET) |
	   ((dword) dt->minute << FDAT_MINUTE_OFFSET) |
	   ((dword) (dt->second / 2) << FDAT_2SECOND_OFFSET) |
	   ((dword) (dt->year - FDAT_BASE_YEAR) << FDAT_YEAR_OFFSET) |
	   ((dword) dt->month << FDAT_MONTH_OFFSET) |
	   ((dword) dt->day << FDAT_DAY_OFFSET);
}

FileDateAndTime FileGetDateAndTime(FileHandle fh)
{
    KFile *kf = FileOf(fh);
    FileDateAndTime fdat = 0;

    KernelSetError(DRV(kf)->FSD_getDateTime(&kf->KF_file, &fdat));
    return fdat;
}

void FileUnpackDate(FileDateAndTime dateAndTime, ghal_datetime *dt)
{
    dt->year = (uint16_t) FDATExtractYearAD(dateAndTime);
    dt->month = FDATExtractMonth(dateAndTime);
    dt->day = FDATExtractDay(dateAndTime);
    dt->hour = FDATExtractHour(dateAndTime);
    dt->minute = FDATExtractMinute(dateAndTime);
    dt->second = (uint8_t) FDATExtractSecond(dateAndTime);
    dt->weekday = 0;
}

FileDateAndTime FileNow(void)
{
    ghal_datetime dt;

    KernelGetDateTime(&dt);		/* GEOS's clock (timerMisc.c) */
    return FilePackDate(&dt);
}

word FileSetDateAndTime(FileHandle fh, FileDateAndTime dateAndTime)
{
    KFile *kf = FileOf(fh);

    return Done(DRV(kf)->FSD_setDateTime(&kf->KF_file, dateAndTime), FALSE);
}

/* ---------------------------------------------------------------------
 * Record locks (FileLockRecord, FileUnlockRecord; FSHOF_LOCK/UNLOCK)
 *
 * DOS locked a region of a file for its handle: another handle of the
 * same file, of this or any process, got ERROR_ALREADY_LOCKED reading,
 * writing or locking it.  The host's locks (POSIX) are per process and
 * GEOS is one, so the kernel keeps the locks: per open file (KFile, its
 * duplicated handles share them), for the file by its disk and
 * FEA_FILE_ID.  A region is unlocked as it was locked; closing the file
 * unlocks its regions.  Not yet: reading and writing a region another
 * open file locked.
 * ------------------------------------------------------------------- */

typedef struct FileLock {
    struct FileLock *FL_next;
    KFile	*FL_file;
    FSDDisk	*FL_disk;
    dword	FL_id;
    dword	FL_start, FL_length;
} FileLock;

static FileLock *fileLocks;

static word LockId(KFile *kf, dword *id)
{
    return FSD_OF(kf->KF_file.FF_disk)->FSD_handleExtAttrs(
	&kf->KF_file, FEA_FILE_ID, id, sizeof(*id), FALSE);
}

static Boolean Overlap(const FileLock *l, dword start, dword length)
{
    return start < l->FL_start + l->FL_length &&
	   l->FL_start < start + length;
}

word FileLockRecord(FileHandle fh, dword filePos, dword regLength)
{
    KFile *kf = FileOf(fh);
    FileLock *l;
    dword id;
    word e = LockId(kf, &id);

    if (!e) {
	for (l = fileLocks; l; l = l->FL_next)
	    if (l->FL_disk == kf->KF_file.FF_disk && l->FL_id == id &&
		Overlap(l, filePos, regLength)) {
		e = ERROR_ALREADY_LOCKED;
		break;
	    }
    }
    if (!e) {
	l = ghal_mem_alloc(sizeof(*l), GHAL_MEM_ZERO);
	if (!l)
	    e = ERROR_INSUFFICIENT_MEMORY;
	else {
	    l->FL_file = kf;
	    l->FL_disk = kf->KF_file.FF_disk;
	    l->FL_id = id;
	    l->FL_start = filePos;
	    l->FL_length = regLength;
	    l->FL_next = fileLocks;
	    fileLocks = l;
	}
    }
    KernelSetError(e);
    return e;
}

word FileUnlockRecord(FileHandle fh, dword filePos, dword regLength)
{
    KFile *kf = FileOf(fh);
    FileLock **pl, *l;

    for (pl = &fileLocks; (l = *pl) != NULL; pl = &l->FL_next)
	if (l->FL_file == kf && l->FL_start == filePos &&
	    l->FL_length == regLength) {
	    *pl = l->FL_next;
	    ghal_mem_free(l);
	    KernelSetError(0);
	    return 0;
	}
    KernelSetError(ERROR_ALREADY_LOCKED);
    return ERROR_ALREADY_LOCKED;
}

/* the open file is closed: its regions are free */
void FileUnlockAll(KFile *kf)
{
    FileLock **pl = &fileLocks, *l;

    while ((l = *pl) != NULL)
	if (l->FL_file == kf) {
	    *pl = l->FL_next;
	    ghal_mem_free(l);
	} else
	    pl = &l->FL_next;
}

/* FileOpenAndRead: the file (flags: its FileAccessFlags and
 * FileOpenAndReadFlags) in a new block, with CR LF at its end unless
 * there is one, an EOF (^Z) and a null as the flags ask; the block
 * unlocked, *fh the open file */
MemHandle FileOpenAndRead(FileOpenAndReadFlags flags, const char *filename,
			  FileHandle *fh)
{
    FileHandle f;
    MemHandle mh;
    dword size;
    word n, extra = 0, e;
    byte *b;

    f = FileOpen(filename, (FileAccessFlags) (flags & 0xff));
    if (!f)
	return 0;
    size = FileSize(f);
    if (size > 0xffff - 4) {
	e = ERROR_INSUFFICIENT_MEMORY;
	goto error;
    }
    if (flags & FOARF_ADD_CRLF)
	extra += 2;
    if (flags & FOARF_ADD_EOF)
	extra++;
    if (flags & FOARF_NULL_TERMINATE)
	extra++;
    mh = MemAlloc((word) (size + extra), HF_DYNAMIC, HAF_LOCK);
    if (!mh) {
	e = ERROR_INSUFFICIENT_MEMORY;
	goto error;
    }
    b = MemDeref(mh);
    n = FileRead(f, b, (word) size, FALSE);
    if (n != (word) size && ThreadGetError() != ERROR_SHORT_READ_WRITE) {
	e = ThreadGetError();
	MemFree(mh);
	goto error;
    }
    if ((flags & FOARF_ADD_CRLF) &&
	(n < 2 || b[n - 2] != '\r' || b[n - 1] != '\n')) {
	b[n++] = '\r';
	b[n++] = '\n';
    }
    if (flags & FOARF_ADD_EOF)
	b[n++] = 0x1a;			/* MSDOS_TEXT_FILE_EOF */
    if (flags & FOARF_NULL_TERMINATE)
	b[n++] = 0;
    MemUnlock(mh);
    if (fh)
	*fh = f;
    KernelSetError(0);
    return mh;

error:
    FileClose(f, FALSE);
    KernelSetError(e);
    return 0;
}

static word ByHandle(FileHandle fh, FileExtendedAttribute attr, void *buffer,
		     word bufSize, Boolean set)
{
    KFile *kf = FileOf(fh);
    FSDDisk *d = kf->KF_file.FF_disk;
    FileExtAttrDesc rest[EA_REST];
    MemHandle mh = 0;
    word e;

    if (!set && FileDiskAttr(d, attr, &buffer, &bufSize, rest, &mh, &e))
	return FileEADone(e);
    e = FSD_OF(d)->FSD_handleExtAttrs(&kf->KF_file, attr, buffer, bufSize,
				      set);
    if (mh)
	MemFree(mh);
    if (set && !e)
	FileNotify(FCNT_ATTRIBUTES, d, FileIDOfOpen(&kf->KF_file));
    return FileEADone(e);
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
    return FileEADone(e);
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

DiskHandle FileGetDiskHandle(FileHandle fh)
{
    KernelSetError(0);
    return FileOf(fh)->KF_file.FF_disk->FDK_handle;
}
