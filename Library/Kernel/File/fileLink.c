/*
 * fileLink.c -- links (after fileLink.asm and the link parts of
 * fileUtils.asm and fileC.asm).
 *
 * A link is a file system object that names a target: a saved disk
 * (DiskSave) and a path on it.  Its driver keeps it (HostFS: a record in
 * the directory's @DIRNAME.000, as the DOS driver of DOS16) and gives it
 * the attributes the kernel copies from the target when it is made.  The
 * kernel follows links in paths (filePath.c); these routines make and
 * read them.
 */
#include "kernelC.h"

#include <string.h>

static word Fail(word e)
{
    KernelSetError(e);
    return e;
}

/* ---------------------------------------------------------------------
 * FileCreateLink
 * ------------------------------------------------------------------- */

/* the attributes a link gets from its target (fileLink.asm linkAttrs);
 * FEA_FILE_ID of the target becomes FEA_TARGET_FILE_ID of the link */
#define LINK_NUM_ATTRS	10

typedef struct {
    FileExtAttrDesc	LA_attrs[LINK_NUM_ATTRS];
    byte		LA_fileID[4];
    byte		LA_type[2];
    byte		LA_flags[2];
    byte		LA_release[8];
    byte		LA_protocol[4];
    byte		LA_token[6];
    byte		LA_creator[6];
    byte		LA_desktop[16];
    byte		LA_fileAttr[1];
    byte		LA_geodeAttr[2];
} LinkAttrs;

static void InitLinkAttrs(LinkAttrs *la)
{
    static const FileExtendedAttribute attrs[LINK_NUM_ATTRS] = {
	FEA_FILE_ID, FEA_FILE_TYPE, FEA_FLAGS, FEA_RELEASE, FEA_PROTOCOL,
	FEA_TOKEN, FEA_CREATOR, FEA_DESKTOP_INFO, FEA_FILE_ATTR,
	FEA_GEODE_ATTR
    };
    void *values[LINK_NUM_ATTRS];
    word sizes[LINK_NUM_ATTRS];
    int i;

    memset(la, 0, sizeof(*la));
    values[0] = la->LA_fileID;	sizes[0] = sizeof(la->LA_fileID);
    values[1] = la->LA_type;	sizes[1] = sizeof(la->LA_type);
    values[2] = la->LA_flags;	sizes[2] = sizeof(la->LA_flags);
    values[3] = la->LA_release;	sizes[3] = sizeof(la->LA_release);
    values[4] = la->LA_protocol; sizes[4] = sizeof(la->LA_protocol);
    values[5] = la->LA_token;	sizes[5] = sizeof(la->LA_token);
    values[6] = la->LA_creator;	sizes[6] = sizeof(la->LA_creator);
    values[7] = la->LA_desktop;	sizes[7] = sizeof(la->LA_desktop);
    values[8] = la->LA_fileAttr; sizes[8] = sizeof(la->LA_fileAttr);
    values[9] = la->LA_geodeAttr; sizes[9] = sizeof(la->LA_geodeAttr);
    for (i = 0; i < LINK_NUM_ATTRS; i++) {
	la->LA_attrs[i].FEAD_attr = attrs[i];
	la->LA_attrs[i].FEAD_value = values[i];
	la->LA_attrs[i].FEAD_size = sizes[i];
    }
}

/* FileLinkFetchTargetAttrs: a missing attribute keeps its default */
static word FetchTargetAttrs(DiskHandle disk, const char *path,
			     LinkAttrs *la)
{
    word e;

    InitLinkAttrs(la);
    FilePushDir();
    if (disk && !FileSetCurrentPath(disk, "")) {
	e = ThreadGetError();
	FilePopDir();
	return e;
    }
    e = FileGetPathExtAttributes(path, FEA_MULTIPLE, la->LA_attrs,
				 LINK_NUM_ATTRS);
    FilePopDir();
    la->LA_attrs[0].FEAD_attr = FEA_TARGET_FILE_ID;
    return e == ERROR_ATTR_NOT_FOUND ? 0 : e;
}

typedef struct {
    byte	disk[FSD_SAVED_DISK_MAX];
    word	diskSize;
    const char	*target;
} CreateLinkData;

static word CreateLinkAt(FSDDisk *d, const char *dp, void *data)
{
    CreateLinkData *c = data;

    return FSD_OF(d)->FSD_createLink(d, dp, c->disk, c->diskSize,
				     c->target);
}

/* the link `path' (relative to the current path) to targetPath on
 * targetDisk (0: the path has a drive name).  A standard path is parsed
 * down as far as it goes.  targetAttrsFlag 0: the link gets the
 * attributes of the target, which must exist. */
word FileCreateLink(const char *path, word targetDiskHandle,
		    const char *targetPath, word targetAttrsFlag)
{
    DiskHandle td = targetDiskHandle;
    const char *tp = targetPath;
    CreateLinkData c;
    LinkAttrs la;
    word e;

    if (td & DISK_IS_STD_PATH_MASK) {
	td = FileParseStandardPath(td, &tp);
	if (!*tp)
	    tp = ".";
    }
    c.diskSize = 0;
    if (td) {
	c.diskSize = sizeof(c.disk);
	if (!DiskSave(td, c.disk, &c.diskSize))
	    return Fail(ERROR_PATH_TOO_LONG);
    }
    if (!targetAttrsFlag) {
	e = FetchTargetAttrs(td, tp, &la);
	if (e)
	    return Fail(e);
    }
    c.target = tp;
    e = FileOpOnPath(path, CreateLinkAt, &c);
    if (e)
	return Fail(e);
    if (!targetAttrsFlag) {
	e = FileSetPathExtAttributes(path, FEA_MULTIPLE, la.LA_attrs,
				     LINK_NUM_ATTRS);
	if (e) {
	    FileDelete(path);
	    return Fail(e);
	}
    }
    return Fail(0);
}

/* ---------------------------------------------------------------------
 * Reading links
 * ------------------------------------------------------------------- */

static word ReadLinkAt(FSDDisk *d, const char *dp, void *data)
{
    (void) data;
    return FSD_OF(d)->FSD_readLink(d, dp);
}

/* the target's disk (0 if the path has a drive name, or on an error)
 * and its path in targetPath (PATH_BUFFER_SIZE) */
DiskHandle FileReadLink(const char *path, char *targetPath)
{
    KLink *l;
    DiskHandle disk;
    word e = FileOpOnPath(path, ReadLinkAt, NULL);

    if (e) {
	Fail(e);
	return 0;
    }
    l = ThreadOf(KernelThisThread())->T_link;
    disk = FileLinkTarget(targetPath, PATH_BUFFER_SIZE);
    if (!disk)
	return 0;
    KernelSetError(0);
    return l->KL_diskSize ? disk : 0;
}

typedef struct {
    void	*buf;
    word	size;
    Boolean	set;
} ExtraData;

static word ExtraDataAt(FSDDisk *d, const char *dp, void *data)
{
    ExtraData *x = data;

    return FSD_OF(d)->FSD_linkExtraData(d, dp, x->buf, &x->size, x->set);
}

word FileSetLinkExtraData(const char *path, char *buffer, word bufSize)
{
    ExtraData x;

    x.buf = buffer;
    x.size = bufSize;
    x.set = TRUE;
    return Fail(FileOpOnPath(path, ExtraDataAt, &x));
}

/* as much as the buffer holds */
word FileGetLinkExtraData(const char *path, char *buffer, word bufSize)
{
    ExtraData x;

    x.buf = buffer;
    x.size = bufSize;
    x.set = FALSE;
    return Fail(FileOpOnPath(path, ExtraDataAt, &x));
}

/* ---------------------------------------------------------------------
 * Actual paths
 * ------------------------------------------------------------------- */

/* FileConstructFullPath of where the path really is: at the place of a
 * standard path that has it, with every link followed */
DiskHandle FileConstructActualPath(char **buffer, word bufSize,
				   DiskHandle disk, const char *tail,
				   Boolean addDriveLetter)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocateTarget(disk, tail ? tail : "", dp, sizeof(dp));

    if (!d)
	return 0;
    return FileConstructFullPath(buffer, bufSize, d->FDK_handle, dp,
				 addDriveLetter);
}

/* ---------------------------------------------------------------------
 * Comparing paths
 * ------------------------------------------------------------------- */

/* the full path of (disk, path), parsed down to a standard path */
static DiskHandle ComparePrep(DiskHandle disk, const char *path, char *buf,
			      word size, const char **tail)
{
    char *p = buf;
    StandardPath sp;

    disk = FileConstructFullPath(&p, size, disk, path ? path : "", FALSE);
    if (!disk)
	return 0;
    *tail = buf;
    sp = FileParseStandardPath(disk, tail);
    return sp ? (DiskHandle) sp : disk;
}

/* fileLink.asm FileComparePathsReal: PCT_SUBDIR if path2 is below path1
 * (not the other way round).  Links are not followed; the names are
 * compared as they are (FileConstructActualPath first if links may be
 * involved). */
PathCompareType FileComparePaths(const char *path1, DiskHandle disk1,
				 const char *path2, DiskHandle disk2)
{
    char b1[PATH_BUFFER_SIZE], b2[PATH_BUFFER_SIZE];
    const char *t1, *t2;
    StandardPath sp;
    size_t n;

    disk1 = ComparePrep(disk1, path1, b1, sizeof(b1), &t1);
    if (!disk1)
	return PCT_ERROR;
    disk2 = ComparePrep(disk2, path2, b2, sizeof(b2), &t2);
    if (!disk2)
	return PCT_ERROR;
    if (disk1 == disk2) {
	n = strlen(t1);
	if (strncmp(t1, t2, n))
	    return PCT_UNRELATED;
	if (!t2[n] || !strcmp(t2 + n, "\\"))
	    return PCT_EQUAL;
	return t2[n] == '\\' || !n ? PCT_SUBDIR : PCT_UNRELATED;
    }
    /* two standard paths: path1 must be one itself, above path2's */
    if (!(disk1 & disk2 & DISK_IS_STD_PATH_MASK) ||
	(*t1 && strcmp(t1, "\\")))
	return PCT_UNRELATED;
    for (sp = FileStdPathParent((StandardPath) disk2);
	 sp != SP_NOT_STANDARD_PATH; sp = FileStdPathParent(sp))
	if (sp == (StandardPath) disk1)
	    return PCT_SUBDIR;
    return PCT_UNRELATED;
}
