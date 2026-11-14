/*
 * filePath.c -- current paths, standard paths and the resolution of
 * paths (after filePath.asm and the path parts of fileC.asm).
 *
 * The kernel resolves a GEOS path to a disk and the path on it from its
 * root; the disk's file system driver maps it to its medium
 * (CInclude/Internal/fsDriver.h).  The GEOS top directory is a drive of the
 * HostFS driver: a directory on the host (FileSetTopHostPath).  A
 * standard path is a directory below it, named as in 16-bit GEOS (WORLD,
 * DOCUMENT, SYSTEM\\FS, ...), so a DOS16 installation keeps its layout.
 * "\\" at the start of a path is the root of the current disk (the top
 * for a standard path), "name:" at the start the root of that drive's
 * disk.  Every thread has a current path (a standard path or a disk, and
 * the directories below it), taken over from the thread that creates
 * it, and a stack for FilePushDir.
 *
 * A standard path may be spread over several directories (Loader/
 * path.asm, Int_SetDirOnPath): the one below the top, then those that
 * "[paths]" of the init files give it ("document = D:\\DOCS E:\\MORE",
 * keys as "systemfs" for SP_FILE_SYSTEM_DRIVERS), then those of the
 * standard paths above it with the names down to it ("top = D:\\NET"
 * gives D:\\NET\\DOCUMENT).  These are the places of a path relative to
 * a standard path; FileOpOnPath tries an operation at each until one
 * does not say the file or path is not there, FileEnum goes over all.
 * The directories of [paths] are GEOS paths with a drive name.
 *
 * Not yet: links, a current directory per drive.
 */
#include "kernelC.h"

#include <initfile.h>
#include <string.h>

#define TOP_DRIVE	2		/* C: */

static DiskHandle topDisk;

/* names and parents of the standard paths (fileVariable.def,
 * stdPathUpwardTree), by StandardPath / 2 */
static const struct {
    const char *name;
    StandardPath parent;
} stdPaths[] = {
    {"", SP_NOT_STANDARD_PATH},		/* top */
    {"WORLD", SP_TOP},
    {"DOCUMENT", SP_TOP},
    {"SYSTEM", SP_TOP},
    {"PRIVDATA", SP_TOP},
    {"STATE", SP_PRIVATE_DATA},
    {"FONT", SP_USER_DATA},
    {"SPOOL", SP_PRIVATE_DATA},
    {"SYSAPPL", SP_SYSTEM},
    {"USERDATA", SP_TOP},
    {"MOUSE", SP_SYSTEM},
    {"PRINTER", SP_SYSTEM},
    {"FS", SP_SYSTEM},
    {"VIDEO", SP_SYSTEM},
    {"SWAP", SP_SYSTEM},
    {"KBD", SP_SYSTEM},
    {"FONT", SP_SYSTEM},
    {"IMPEX", SP_SYSTEM},
    {"TASK", SP_SYSTEM},
    {"HELP", SP_USER_DATA},
    {"TEMPLATE", SP_USER_DATA},
    {"POWER", SP_SYSTEM},
    {"DOSROOM", SP_TOP},
    {"HWR", SP_SYSTEM},
    {"WASTE", SP_PRIVATE_DATA},
    {"BACKUP", SP_USER_DATA},
    {"PAGER", SP_SYSTEM},
    {"COMP", SP_SYSTEM},
};
#define STD_PATH_COUNT ((word) (sizeof(stdPaths) / sizeof(stdPaths[0])))

static Boolean IsStdPath(word sp)
{
    return (sp & 1) && sp / 2 < STD_PATH_COUNT;
}

const char *FileStdPathName(StandardPath sp)
{
    return IsStdPath(sp) ? stdPaths[sp / 2].name : NULL;
}

StandardPath FileStdPathParent(StandardPath sp)
{
    return IsStdPath(sp) ? stdPaths[sp / 2].parent : SP_NOT_STANDARD_PATH;
}

/* the drive of the top directory, and its disk */
void FileSetTopHostPath(const char *hostPath)
{
    FSDDrive *old = FSDDriveOf(TOP_DRIVE);
    void *priv = HostFSDriveData(hostPath);

    if (!priv)
	KernelFatal(FILE_PATH_ALLOC_FAILED);
    if (old && old->FDR_fsd == &hostFSDriver)
	HostFSFreeDriveData(old->FDR_private);
    if (!FSDInitDrive(TOP_DRIVE, "C", DS_PRESENT | DRIVE_FIXED,
		      MEDIA_FIXED_DISK, &hostFSDriver, priv))
	KernelFatal(FILE_PATH_ALLOC_FAILED);
    topDisk = 0;		/* registered when it is used */
}

DiskHandle FileTopDisk(void)
{
    if (!FSDDriveOf(TOP_DRIVE))
	FileSetTopHostPath(".");
    if (!topDisk)
	topDisk = DiskRegisterDiskSilently(TOP_DRIVE);
    return topDisk;
}

/* ---------------------------------------------------------------------
 * Building paths: a list of directory names below the top
 * ------------------------------------------------------------------- */

typedef struct {
    char	buf[PATH_BUFFER_SIZE];	/* "A\\B\\C", from the top */
    size_t	len;
} Comps;

static Boolean Append(Comps *c, const char *name, size_t n)
{
    if (c->len + n + 2 > sizeof(c->buf))
	return FALSE;
    if (c->len)
	c->buf[c->len++] = '\\';
    memcpy(c->buf + c->len, name, n);
    c->len += n;
    c->buf[c->len] = 0;
    return TRUE;
}

static Boolean Up(Comps *c)
{
    if (!c->len)
	return FALSE;			/* above the top */
    while (c->len && c->buf[c->len - 1] != '\\')
	c->len--;
    if (c->len)
	c->len--;
    c->buf[c->len] = 0;
    return TRUE;
}

/* add a relative path ('\\' or '/' separated, "." and "..") */
static Boolean AddPath(Comps *c, const char *p)
{
    while (*p) {
	const char *e = p;
	while (*e && *e != '\\' && *e != '/')
	    e++;
	if (e - p == 2 && p[0] == '.' && p[1] == '.') {
	    if (!Up(c))
		return FALSE;
	} else if (e > p && !(e - p == 1 && p[0] == '.')) {
	    if (!Append(c, p, (size_t) (e - p)))
		return FALSE;
	}
	p = *e ? e + 1 : e;
    }
    return TRUE;
}

static Boolean AddStdPath(Comps *c, StandardPath sp)
{
    if (sp == SP_TOP || sp == SP_NOT_STANDARD_PATH)
	return TRUE;
    if (!AddStdPath(c, stdPaths[sp / 2].parent))
	return FALSE;
    return Append(c, stdPaths[sp / 2].name, strlen(stdPaths[sp / 2].name));
}

/* the names of the standard paths from below `above' down to sp */
static Boolean AddStdPathBelow(Comps *c, StandardPath sp, StandardPath above)
{
    if (sp == above || sp == SP_NOT_STANDARD_PATH)
	return TRUE;
    if (!AddStdPathBelow(c, stdPaths[sp / 2].parent, above))
	return FALSE;
    return Append(c, stdPaths[sp / 2].name, strlen(stdPaths[sp / 2].name));
}

static Boolean DriveSpec(const char **path, DiskHandle *base, Boolean *spec);

/* ---------------------------------------------------------------------
 * The directories of [paths] (GetPaths of Loader/path.asm)
 * ------------------------------------------------------------------- */

/* per standard path: its directories, each null terminated, an empty
 * one at the end (ghal_mem); NULL if it has none */
static char *stdDirs[STD_PATH_COUNT];

/* the key of sp in [paths]: the names from the top down, lower case
 * ("top" for SP_TOP) */
static void StdPathKey(StandardPath sp, char *key, size_t size)
{
    Comps c;
    size_t i, j = 0;

    c.len = 0;
    c.buf[0] = 0;
    if (sp == SP_TOP || !AddStdPath(&c, sp)) {
	strcpy(key, "top");
	return;
    }
    for (i = 0; i < c.len && j + 1 < size; i++)
	if (c.buf[i] != '\\')
	    key[j++] = (char) (c.buf[i] >= 'A' && c.buf[i] <= 'Z'
			       ? c.buf[i] - 'A' + 'a' : c.buf[i]);
    key[j] = 0;
}

static Boolean IsSeparator(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void FileInitStdPaths(void)
{
    StandardPath sp;
    char key[40];
    MemHandle mh;
    word n, i, j;
    const char *v;
    char *dirs;

    for (i = 0; i < STD_PATH_COUNT; i++) {
	if (stdDirs[i])
	    ghal_mem_free(stdDirs[i]);
	stdDirs[i] = NULL;
    }
    for (i = 0; i < STD_PATH_COUNT; i++) {
	sp = (StandardPath) (2 * i + 1);
	StdPathKey(sp, key, sizeof(key));
	if (InitFileReadStringBlock("paths", key, &mh, IFRF_READ_ALL, &n))
	    continue;
	v = MemLock(mh);
	dirs = ghal_mem_alloc((dword) n + 2, 0);
	if (dirs) {
	    /* NullTerminatePathElements */
	    for (j = 0; *v; ) {
		while (IsSeparator(*v))
		    v++;
		if (!*v)
		    break;
		while (*v && !IsSeparator(*v))
		    dirs[j++] = *v++;
		dirs[j++] = 0;
	    }
	    dirs[j] = 0;
	    if (j)
		stdDirs[i] = dirs;
	    else
		ghal_mem_free(dirs);
	}
	MemUnlock(mh);
	MemFree(mh);
    }
    KernelSetError(0);
}

/*
 * The prefix of place n of the standard path sp (FindNextPathEntry,
 * BuildPathEntry): place 0 is the directory below the top, then come
 * the directories of sp and of the standard paths above it.  Returns
 * the disk; NULL with *end set if there is no place n, NULL alone if
 * place n cannot be used (an unknown drive).
 */
static FSDDisk *Place(StandardPath sp, word n, Comps *c, Boolean *end)
{
    StandardPath p;
    const char *e, *q;
    DiskHandle base = 0;
    Boolean spec;

    *end = FALSE;
    c->len = 0;
    c->buf[0] = 0;
    if (n == 0) {
	AddStdPath(c, sp);
	return FSDDiskOf(SP_TOP);
    }
    for (p = sp; p != SP_NOT_STANDARD_PATH; p = stdPaths[p / 2].parent) {
	for (e = stdDirs[p / 2]; e && *e; e += strlen(e) + 1) {
	    if (--n)
		continue;
	    q = e;
	    if (!DriveSpec(&q, &base, &spec) || !spec ||
		!AddPath(c, q) || !AddStdPathBelow(c, sp, p))
		return NULL;
	    return FSDDiskOf(base);
	}
    }
    *end = TRUE;
    return NULL;
}

static KPath *CurPath(void)
{
    KThread *t = ThreadOf(KernelThisThread());

    if (!t->T_path)
	FileThreadInit(t, NULL);
    return t->T_path;
}

/* a drive name at the start of *path ("C:"): *base becomes its disk and
 * *path what follows.  FALSE with the error set for an unknown drive. */
static Boolean DriveSpec(const char **path, DiskHandle *base, Boolean *spec)
{
    const char *p = *path, *e = p;
    sword drive;

    *spec = FALSE;
    while (*e && *e != ':' && *e != '\\' && *e != '/')
	e++;
    if (*e != ':' || e == p)
	return TRUE;
    drive = FSDDriveByName(p, (word) (e - p));
    if (drive < 0) {
	KernelSetError(ERROR_INVALID_VOLUME);
	return FALSE;
    }
    *base = DiskRegisterDiskSilently((word) drive);
    if (!*base)
	return FALSE;
    *path = e + 1;
    *spec = TRUE;
    return TRUE;
}

/* the disk of place n of `path' relative to (*base, *tail), and its
 * components from the root of the disk (full).  A drive name or a
 * separator at the start changes *base and *tail, and *path is what
 * follows a drive name.  Only a standard path has places after 0; *end
 * is set (NULL returned) when there are no more. */
static FSDDisk *ResolveFrom(DiskHandle *base, const char **tail,
			    const char **path, Comps *full, word n,
			    Boolean *end)
{
    FSDDisk *d;
    Boolean spec;

    *end = FALSE;
    full->len = 0;
    full->buf[0] = 0;
    if (!DriveSpec(path, base, &spec))
	return NULL;
    if (spec || **path == '\\' || **path == '/') {
	*tail = "";
	if (*base & DISK_IS_STD_PATH_MASK)
	    *base = SP_TOP;
    }
    if (*base & DISK_IS_STD_PATH_MASK) {
	if (!IsStdPath(*base)) {
	    KernelSetError(ERROR_INVALID_VOLUME);
	    return NULL;
	}
	d = Place((StandardPath) *base, n, full, end);
	if (!d) {
	    KernelSetError(ERROR_PATH_NOT_FOUND);
	    return NULL;
	}
    } else if (n) {
	*end = TRUE;
	return NULL;
    } else
	d = FSDDiskOf(*base);
    if (!d) {
	KernelSetError(ERROR_INVALID_VOLUME);
	return NULL;
    }
    if (!AddPath(full, *tail) || !AddPath(full, *path)) {
	KernelSetError(ERROR_PATH_NOT_FOUND);
	return NULL;
    }
    return d;
}

/* the path on the disk: "\\" and the components */
static Boolean DiskPath(const Comps *c, char *out, size_t size)
{
    if (c->len + 2 > size) {
	KernelSetError(ERROR_PATH_TOO_LONG);
	return FALSE;
    }
    out[0] = '\\';
    memcpy(out + 1, c->buf, c->len + 1);
    return TRUE;
}

/* place n of path relative to (base, tail) as a disk and the path on
 * it; NULL with *end set when there are no more places */
static FSDDisk *ResolvePlace(DiskHandle base, const char *tail,
			     const char *path, word n, char *out, size_t size,
			     Boolean *end)
{
    Comps c;
    FSDDisk *d = ResolveFrom(&base, &tail, &path, &c, n, end);

    return d && DiskPath(&c, out, size) ? d : NULL;
}

/* relative to the root of `disk' (a standard path: to it) */
FSDDisk *FileResolveOn(DiskHandle disk, const char *path, char *out,
		       size_t size)
{
    Boolean end;

    if (!disk)
	return FileResolve(path, out, size);
    return ResolvePlace(disk, "", path, 0, out, size, &end);
}

FSDDisk *FileResolve(const char *path, char *out, size_t size)
{
    KPath *cur = CurPath();
    Boolean end;

    return ResolvePlace(cur->KP_disk, cur->KP_tail, path, 0, out, size,
			&end);
}

FSDDisk *FileResolvePlace(const char *path, word n, char *out, size_t size,
			  Boolean *end)
{
    KPath *cur = CurPath();

    return ResolvePlace(cur->KP_disk, cur->KP_tail, path, n, out, size,
			end);
}

/* FileOpOnPath: op at each place of the path (from base, tail) until
 * one returns other than "not found"; that, else ERROR_FILE_NOT_FOUND
 * if a place had the directory, else ERROR_PATH_NOT_FOUND */
static word OpOnPlaces(DiskHandle base, const char *tail, const char *path,
		       FileOpFunc *op, void *data)
{
    char dp[FILE_DISK_PATH_SIZE];
    word n, e, notFound = ERROR_PATH_NOT_FOUND;
    Boolean end;
    FSDDisk *d;

    for (n = 0; ; n++) {
	d = ResolvePlace(base, tail, path, n, dp, sizeof(dp), &end);
	if (end)
	    break;
	if (!d) {
	    if (n == 0)
		return ThreadGetError();
	    continue;
	}
	e = op(d, dp, data);
	if (e == ERROR_FILE_NOT_FOUND)
	    notFound = e;
	else if (e != ERROR_PATH_NOT_FOUND)
	    return e;
    }
    return notFound;
}

word FileOpOnPath(const char *path, FileOpFunc *op, void *data)
{
    KPath *cur = CurPath();

    return OpOnPlaces(cur->KP_disk, cur->KP_tail, path, op, data);
}

/* the first place of path (relative to the root of disk; 0: to the
 * current path) where it exists, else place 0 (an operation there then
 * finds it missing) */
FSDDisk *FileLocate(DiskHandle disk, const char *path, char *out,
		    size_t size)
{
    KPath *cur = CurPath();
    DiskHandle base = disk ? disk : cur->KP_disk;
    const char *tail = disk ? "" : cur->KP_tail;
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d0, *d;
    FileAttrs a;
    Boolean end, probed = FALSE;
    word n;

    d0 = ResolvePlace(base, tail, path, 0, out, size, &end);
    if (!d0)
	return NULL;
    for (n = 1; ; n++) {
	d = ResolvePlace(base, tail, path, n, dp, sizeof(dp), &end);
	if (end)
	    break;
	if (!probed) {
	    probed = TRUE;
	    if (!FSD_OF(d0)->FSD_getAttrs(d0, out, &a))
		break;
	}
	if (d && !FSD_OF(d)->FSD_getAttrs(d, dp, &a) &&
	    strlen(dp) < size) {
	    strcpy(out, dp);
	    KernelSetError(0);
	    return d;
	}
    }
    KernelSetError(0);
    return d0;
}

/* ---------------------------------------------------------------------
 * Current paths of threads
 * ------------------------------------------------------------------- */

static KPath *NewPath(const KPath *from)
{
    KPath *p = ghal_mem_alloc(sizeof(KPath), GHAL_MEM_ZERO);

    if (!p)
	KernelFatal(FILE_PATH_ALLOC_FAILED);
    if (from) {
	p->KP_disk = from->KP_disk;
	strcpy(p->KP_tail, from->KP_tail);
    } else
	p->KP_disk = SP_TOP;
    return p;
}

void FileThreadInit(KThread *t, KThread *creator)
{
    t->T_path = NewPath(creator ? creator->T_path : NULL);
}

void FileThreadExit(KThread *t)
{
    while (t->T_path) {
	KPath *p = t->T_path;
	t->T_path = p->KP_next;
	ghal_mem_free(p);
    }
}

/* DR_FS_CUR_PATH_SET at a place (full: the components, if dp is NULL) */
static word CurPathSetAt(FSDDisk *d, const char *dp, void *full)
{
    char buf[FILE_DISK_PATH_SIZE];

    if (!dp) {
	if (!DiskPath(full, buf, sizeof(buf)))
	    return ThreadGetError();
	dp = buf;
    }
    return FSD_OF(d)->FSD_curPathSet(d, dp);
}

/* a path relative to a standard path stays relative to it unless it
 * leaves it: then it is relative to the top */
DiskHandle FileSetCurrentPath(DiskHandle disk, const char *path)
{
    KPath *cur = CurPath();
    Comps c, full;
    DiskHandle base = disk ? disk : cur->KP_disk;
    const char *tail = disk ? "" : cur->KP_tail;
    FSDDisk *d;
    word e;
    Boolean end;

    if (!path)
	path = "";
    d = ResolveFrom(&base, &tail, &path, &full, 0, &end);
    if (!d)
	return 0;
    c = full;
    if (base & DISK_IS_STD_PATH_MASK) {
	c.len = 0;
	c.buf[0] = 0;
	if (!AddPath(&c, tail) || !AddPath(&c, path)) {
	    c = full;
	    base = SP_TOP;
	}
    }
    /* a standard path itself need not exist; below it, the directory
     * must be at one of its places */
    if (c.len) {
	e = (base & DISK_IS_STD_PATH_MASK)
	    ? OpOnPlaces(base, "", c.buf, CurPathSetAt, NULL)
	    : CurPathSetAt(d, NULL, &full);
	if (e) {
	    KernelSetError(e);
	    return 0;
	}
    }
    cur->KP_disk = base;
    strcpy(cur->KP_tail, c.buf);
    KernelSetError(0);
    return base;
}

DiskHandle FileSetCurrentPathRaw(DiskHandle disk, const char *path)
{
    return FileSetCurrentPath(disk, path);
}

void FileSetStandardPath(StandardPath sp)
{
    FileSetCurrentPath((DiskHandle) sp, "");
}

DiskHandle FileGetCurrentPath(char *buffer, word bufferSize)
{
    KPath *cur = CurPath();

    if (buffer && bufferSize) {
	strncpy(buffer, cur->KP_tail, bufferSize - 1);
	buffer[bufferSize - 1] = 0;
    }
    return cur->KP_disk;
}

void FilePushDir(void)
{
    KThread *t = ThreadOf(KernelThisThread());
    KPath *p;

    CurPath();
    p = NewPath(t->T_path);
    p->KP_next = t->T_path;
    t->T_path = p;
}

void FilePopDir(void)
{
    KThread *t = ThreadOf(KernelThisThread());
    KPath *p = t->T_path;

    if (!p || !p->KP_next)
	KernelFatal(FILE_POP_DIR_WITHOUT_PUSH);
    t->T_path = p->KP_next;
    ghal_mem_free(p);
}

/* ---------------------------------------------------------------------
 * Full paths (FileConstructFullPath, FileParseStandardPath,
 * FileResolveStandardPath)
 *
 * The full path of a standard path is its logical path from the root of
 * the top's disk, as in 16-bit GEOS with KLV_topLevelPath: the top is
 * the root here, so SP_DOCUMENT is "\\DOCUMENT" ("C:\\DOCUMENT" with the
 * drive name).
 * ------------------------------------------------------------------- */

typedef struct {
    char	*b;
    word	size;		/* of b, the null included */
    word	len;
    word	root;		/* the root's backslash (bufStart) */
    word	stdEnd;		/* the backslash after the standard path */
    Boolean	std;
    Boolean	full;		/* it did not fit */
} FPBuf;

static void FPPut(FPBuf *f, const char *s, size_t n)
{
    if (f->full || f->len + n + 1 > f->size) {
	f->full = TRUE;
	return;
    }
    memcpy(f->b + f->len, s, n);
    f->len = (word) (f->len + n);
    f->b[f->len] = 0;
}

static void FPSep(FPBuf *f)
{
    if (!f->len || f->b[f->len - 1] != '\\')
	FPPut(f, "\\", 1);
}

/* the standard path's components below the top (CopyStandardPath-
 * Components) */
static void FPStdComps(FPBuf *f, StandardPath sp)
{
    if (sp == SP_TOP || sp == SP_NOT_STANDARD_PATH)
	return;
    FPStdComps(f, stdPaths[sp / 2].parent);
    FPSep(f);
    FPPut(f, stdPaths[sp / 2].name, strlen(stdPaths[sp / 2].name));
}

/* the leading part for a disk or standard path: the drive name if asked
 * for, the root, a standard path's components; the real disk */
static DiskHandle FPLeading(FPBuf *f, DiskHandle disk, Boolean addDrive)
{
    FSDDisk *d;

    f->std = (disk & DISK_IS_STD_PATH_MASK) != 0;
    if (f->std)
	disk = FileTopDisk();
    d = FSDDiskOf(disk);
    if (!d) {
	KernelSetError(ERROR_INVALID_VOLUME);
	return 0;
    }
    if (addDrive) {
	FPPut(f, d->FDK_drive->FDR_name, strlen(d->FDK_drive->FDR_name));
	FPPut(f, ":", 1);
    }
    f->root = f->len;
    FPPut(f, "\\", 1);
    return disk;
}

/* the path tail into f without its "." and ".." components, no further
 * up than the root; an absolute tail starts at the root, or after the
 * standard path if f has one (CopyNTStringCXMaxCompressPointAtNull) */
static void FPCompress(FPBuf *f, const char *src)
{
    const char *e;
    size_t n;

    if (*src == '\\') {
	f->len = (word) ((f->std ? f->stdEnd : f->root) + 1);
	f->b[f->len] = 0;
	src++;
    }
    FPSep(f);
    while (*src && !f->full) {
	for (e = src; *e && *e != '\\'; e++)
	    ;
	n = (size_t) (e - src);
	if (n == 2 && src[0] == '.' && src[1] == '.') {
	    /* back to the backslash before the last component */
	    if (f->len - 1 > f->root) {
		f->len--;
		while (f->len - 1 > f->root && f->b[f->len - 1] != '\\')
		    f->len--;
		f->b[f->len] = 0;
	    }
	} else if (n && !(n == 1 && src[0] == '.')) {
	    FPPut(f, src, n);
	    FPPut(f, "\\", 1);
	}
	src = *e ? e + 1 : e;
    }
    /* no backslash at the end but the root's */
    if (!f->full && f->len - 1 > f->root && f->b[f->len - 1] == '\\')
	f->b[--f->len] = 0;
}

/* the length of a drive name at the start of path ("name:"), 0 if none */
static word FPDriveSpecLen(const char *path)
{
    const char *e = path;

    while (*e && *e != ':' && *e != '\\' && *e != '/')
	e++;
    return (word) (*e == ':' && e != path ? e - path + 1 : 0);
}

DiskHandle FileConstructFullPath(char **buffer, word bufSize,
				 DiskHandle disk, const char *tail,
				 Boolean addDriveLetter)
{
    FPBuf f;
    KPath *cur;
    DiskHandle real;
    const char *p;
    Boolean spec;

    memset(&f, 0, sizeof(f));
    f.b = *buffer;
    f.size = bufSize;
    if (!tail)
	tail = "";
    if (!bufSize) {
	KernelSetError(ERROR_PATH_TOO_LONG);
	return 0;
    }
    f.b[0] = 0;
    if (FPDriveSpecLen(tail) && !(disk & DISK_IS_STD_PATH_MASK)) {
	/* the drive of the path, not the disk passed */
	if (addDriveLetter) {
	    FPPut(&f, tail, strlen(tail));
	    real = disk;
	} else {
	    p = tail;
	    real = 0;
	    if (!DriveSpec(&p, &real, &spec))
		return 0;
	    FPPut(&f, p, strlen(p));
	}
    } else {
	if (!disk) {
	    /* the current path first */
	    cur = CurPath();
	    real = FPLeading(&f, cur->KP_disk, addDriveLetter);
	    if (!real)
		return 0;
	    if (f.std)
		FPStdComps(&f, (StandardPath) cur->KP_disk);
	    f.stdEnd = f.len > f.root + 1 ? f.len : f.root;
	    FPCompress(&f, cur->KP_tail);
	} else {
	    real = FPLeading(&f, disk, addDriveLetter);
	    if (!real)
		return 0;
	    if (f.std)
		FPStdComps(&f, (StandardPath) disk);
	    f.stdEnd = f.len > f.root + 1 ? f.len : f.root;
	    if (!f.std)
		real = disk;
	}
	FPSep(&f);
	FPCompress(&f, tail);
    }
    if (f.full) {
	KernelSetError(ERROR_PATH_TOO_LONG);
	return 0;
    }
    *buffer = f.b + f.len;
    KernelSetError(0);
    return real;
}

/* the deepest standard path that the absolute path starts with (disk 0:
 * the path has the top's drive name; the top's disk; a standard path: the
 * path is below it); *path is advanced past it.  SP_NOT_STANDARD_PATH,
 * *path unchanged, if it is not in one. */
StandardPath FileParseStandardPath(DiskHandle disk, const char **path)
{
    const char *p = *path;
    StandardPath verdict, c;
    size_t n;
    word i;
    sword drive;

    if (disk & DISK_IS_STD_PATH_MASK) {
	verdict = (StandardPath) disk;
	if (*p == '\\')
	    p++;
    } else {
	if (!disk) {
	    n = FPDriveSpecLen(p);
	    if (!n)
		return SP_NOT_STANDARD_PATH;
	    drive = FSDDriveByName(p, (word) (n - 1));
	    if (drive != TOP_DRIVE)
		return SP_NOT_STANDARD_PATH;
	    p += n;
	} else if (disk != FileTopDisk())
	    return SP_NOT_STANDARD_PATH;
	if (*p != '\\')
	    return SP_NOT_STANDARD_PATH;
	p++;
	verdict = SP_TOP;
    }
    while (*p) {
	for (i = 1; i < STD_PATH_COUNT; i++) {
	    c = (StandardPath) (2 * i + 1);
	    if (stdPaths[i].parent != verdict)
		continue;
	    n = strlen(stdPaths[i].name);
	    if (!strncmp(p, stdPaths[i].name, n) &&
		(p[n] == 0 || p[n] == '\\'))
		break;
	}
	if (i == STD_PATH_COUNT)
	    break;
	verdict = c;
	p += n;
	if (*p == '\\')
	    p++;
    }
    *path = p;
    return verdict;
}

typedef struct {
    char	**buffer;
    word	size;
    FileResolveStandardPathFlags flags;
    const char	*tail;
    DiskHandle	disk;
    FileAttrs	attrs;
} FRSPData;

/* FRSP_callback: at a place of the path, the place's full path if the
 * file is there (or at once for FRSPF_RETURN_FIRST_DIR) */
static word FRSPAt(FSDDisk *d, const char *dp, void *data)
{
    FRSPData *r = data;
    FPBuf f;
    FileAttrs a = 0;
    word e;

    if (!(r->flags & FRSPF_RETURN_FIRST_DIR)) {
	a = FA_SUBDIR;
	if (*r->tail && strcmp(r->tail, ".")) {
	    e = FSD_OF(d)->FSD_getAttrs(d, dp, &a);
	    if (e)
		return e;
	}
    }
    memset(&f, 0, sizeof(f));
    f.b = *r->buffer;
    f.size = r->size;
    if (r->flags & FRSPF_ADD_DRIVE_NAME) {
	FPPut(&f, d->FDK_drive->FDR_name, strlen(d->FDK_drive->FDR_name));
	FPPut(&f, ":", 1);
    }
    f.root = f.len;
    FPPut(&f, "\\", 1);
    FPCompress(&f, dp);
    if (f.full)
	return ERROR_PATH_TOO_LONG;
    *r->buffer = f.b + f.len;
    r->disk = d->FDK_handle;
    r->attrs = a;
    return 0;
}

/* the actual path of a file or directory relative to the current path:
 * the first place of it that has it (FileOpOnPath); 0 if none has */
DiskHandle FileResolveStandardPath(char **buffer, word bufSize,
				   const char *tail,
				   FileResolveStandardPathFlags flags,
				   FileAttrs *attrsPtr)
{
    FRSPData r;
    word e;

    r.buffer = buffer;
    r.size = bufSize;
    r.flags = flags;
    r.tail = tail ? tail : "";
    r.disk = 0;
    r.attrs = 0;
    e = FileOpOnPath(r.tail, FRSPAt, &r);
    KernelSetError(e);
    if (e)
	return 0;
    if (attrsPtr)
	*attrsPtr = r.attrs;
    return r.disk;
}
