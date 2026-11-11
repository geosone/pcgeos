/*
 * filePath.c -- current paths, standard paths and the resolution of
 * paths (after filePath.asm and the path parts of fileC.asm).
 *
 * The kernel resolves a GEOS path to a disk and the path on it from its
 * root; the disk's file system driver maps it to its medium
 * (Port32/include/fsdriver.h).  The GEOS top directory is a drive of the
 * HostFS driver: a directory on the host (FileSetTopHostPath).  A
 * standard path is a directory below it, named as in 16-bit GEOS (WORLD,
 * DOCUMENT, SYSTEM\\FS, ...), so a DOS16 installation keeps its layout.
 * "\\" at the start of a path is the root of the current disk (the top
 * for a standard path), "name:" at the start the root of that drive's
 * disk.  Every thread has a current path (a standard path or a disk, and
 * the directories below it), taken over from the thread that creates
 * it, and a stack for FilePushDir.
 *
 * Not yet: paths of standard paths spread over several directories
 * (geos.ini "paths"), links, a current directory per drive.
 */
#include "kernelC.h"

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

/* the disk of `path' relative to (*base, *tail), and its components
 * from the root of the disk (full).  A drive name or a separator at the
 * start changes *base and *tail, and *path is what follows a drive
 * name. */
static FSDDisk *ResolveFrom(DiskHandle *base, const char **tail,
			    const char **path, Comps *full)
{
    FSDDisk *d;
    Boolean spec;

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
	AddStdPath(full, (StandardPath) *base);
    }
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

/* relative to the root of `disk' (a standard path: to it) */
FSDDisk *FileResolveOn(DiskHandle disk, const char *path, char *out,
		       size_t size)
{
    DiskHandle base = disk;
    const char *tail = "";
    Comps c;
    FSDDisk *d;

    if (!disk)
	return FileResolve(path, out, size);
    d = ResolveFrom(&base, &tail, &path, &c);
    return d && DiskPath(&c, out, size) ? d : NULL;
}

FSDDisk *FileResolve(const char *path, char *out, size_t size)
{
    KPath *cur = CurPath();
    DiskHandle base = cur->KP_disk;
    const char *tail = cur->KP_tail;
    Comps c;
    FSDDisk *d = ResolveFrom(&base, &tail, &path, &c);

    return d && DiskPath(&c, out, size) ? d : NULL;
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

/* a path relative to a standard path stays relative to it unless it
 * leaves it: then it is relative to the top */
DiskHandle FileSetCurrentPath(DiskHandle disk, const char *path)
{
    KPath *cur = CurPath();
    Comps c, full;
    DiskHandle base = disk ? disk : cur->KP_disk;
    const char *tail = disk ? "" : cur->KP_tail;
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d;
    word e;

    if (!path)
	path = "";
    d = ResolveFrom(&base, &tail, &path, &full);
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
    /* a standard path itself need not exist */
    if (c.len) {
	if (!DiskPath(&full, dp, sizeof(dp)))
	    return 0;
	e = FSD_OF(d)->FSD_curPathSet(d, dp);
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
