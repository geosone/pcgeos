/*
 * filePath.c -- current paths, standard paths and host paths (after
 * filePath.asm and the path parts of fileC.asm).
 *
 * Until the file system driver layer exists, the kernel has one disk:
 * the GEOS top directory on the host (FileSetTopHostPath), reached over
 * ghal_fs.  A standard path is a directory below it, named as in
 * 16-bit GEOS (WORLD, DOCUMENT, SYSTEM\\FS, ...), so a DOS16
 * installation keeps its layout.  "\\" at the start of a path is the
 * top.  Every thread has a current path (a standard path and the
 * directories below it), taken over from the thread that creates it,
 * and a stack for FilePushDir.
 *
 * Not yet: disk handles and drives, paths of standard paths spread over
 * several directories (geos.ini "paths"), links, long names on DOS.
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

#ifdef GEOS_HOST_DOS32
#define HOST_SEP '\\'
#else
#define HOST_SEP '/'
#endif

static char topHost[PATH_BUFFER_SIZE] = ".";

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

void FileSetTopHostPath(const char *hostPath)
{
    size_t n = strlen(hostPath);

    if (n >= sizeof(topHost))
	n = sizeof(topHost) - 1;
    memcpy(topHost, hostPath, n);
    /* without a separator at the end */
    while (n > 1 && (topHost[n - 1] == '/' || topHost[n - 1] == '\\'))
	n--;
    topHost[n] = 0;
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

/* the components of `path' from the top: relative to the current path
 * unless it starts with a separator */
static Boolean Resolve(const char *path, Comps *c)
{
    KPath *cur = CurPath();

    c->len = 0;
    c->buf[0] = 0;
    if (*path == '\\' || *path == '/')
	return AddPath(c, path);
    return AddStdPath(c, cur->KP_disk) && AddPath(c, cur->KP_tail) &&
	   AddPath(c, path);
}

static Boolean ToHost(const Comps *c, char *out, size_t size)
{
    size_t t = strlen(topHost), i;

    if (t + 1 + c->len + 1 > size)
	return FALSE;
    memcpy(out, topHost, t);
    if (c->len) {
	out[t++] = HOST_SEP;
	for (i = 0; i < c->len; i++)
	    out[t + i] = c->buf[i] == '\\' ? HOST_SEP : c->buf[i];
	t += c->len;
    }
    out[t] = 0;
    return TRUE;
}

Boolean FileHostPath(const char *path, char *out, size_t size)
{
    Comps c;

    if (!Resolve(path, &c)) {
	KernelSetError(ERROR_PATH_NOT_FOUND);
	return FALSE;
    }
    if (!ToHost(&c, out, size)) {
	KernelSetError(ERROR_PATH_TOO_LONG);
	return FALSE;
    }
    return TRUE;
}

/* the FileError of a ghal_fs status; a missing file is a missing path
 * when its directory is missing too */
word FileErrorOf(int status, const char *hostPath)
{
    switch ((ghal_status) status) {
    case GHAL_OK:		return 0;
    case GHAL_ERR_NOTFOUND:
	if (hostPath) {
	    char dir[PATH_BUFFER_SIZE];
	    char *sep;
	    ghal_fs_info info;
	    strncpy(dir, hostPath, sizeof(dir) - 1);
	    dir[sizeof(dir) - 1] = 0;
	    sep = strrchr(dir, HOST_SEP);
	    if (sep && sep != dir) {
		*sep = 0;
		if (ghal_fs_stat(dir, &info) != GHAL_OK ||
		    !(info.attrs & GHAL_FS_ATTR_DIR))
		    return ERROR_PATH_NOT_FOUND;
	    }
	}
	return ERROR_FILE_NOT_FOUND;
    case GHAL_ERR_NOTDIR:	return ERROR_PATH_NOT_FOUND;
    case GHAL_ERR_EXISTS:	return ERROR_FILE_EXISTS;
    case GHAL_ERR_ACCESS:
    case GHAL_ERR_ISDIR:	return ERROR_ACCESS_DENIED;
    case GHAL_ERR_NOSPACE:	return ERROR_SHORT_READ_WRITE;
    case GHAL_ERR_NOTEMPTY:	return ERROR_DIRECTORY_NOT_EMPTY;
    case GHAL_ERR_NOMEM:	return ERROR_INSUFFICIENT_MEMORY;
    case GHAL_ERR_INVAL:	return ERROR_ARGS_INVALID;
    case GHAL_ERR_UNSUPPORTED:	return ERROR_UNSUPPORTED_FUNCTION;
    default:			return ERROR_GENERAL_FAILURE;
    }
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

DiskHandle FileSetCurrentPath(DiskHandle disk, const char *path)
{
    KPath *cur = CurPath();
    Comps c, full;
    StandardPath sp = SP_TOP;
    const char *tail = "";
    char host[PATH_BUFFER_SIZE];
    ghal_fs_info info;

    if (!path)
	path = "";
    if (disk && !IsStdPath((word) disk)) {
	KernelSetError(ERROR_INVALID_VOLUME);	/* no disks yet */
	return 0;
    }
    if (*path != '\\' && *path != '/') {
	sp = disk ? (StandardPath) disk : cur->KP_disk;
	if (!disk)
	    tail = cur->KP_tail;
    }
    /* the directories below the standard path, or below the top if the
     * path leaves the standard path */
    full.len = 0;
    full.buf[0] = 0;
    if (!AddStdPath(&full, sp) || !AddPath(&full, tail) ||
	!AddPath(&full, path)) {
	KernelSetError(ERROR_PATH_NOT_FOUND);
	return 0;
    }
    c.len = 0;
    c.buf[0] = 0;
    if (!AddPath(&c, tail) || !AddPath(&c, path)) {
	c = full;
	sp = SP_TOP;
    }
    /* a standard path itself need not exist on the host */
    if (c.len) {
	if (!ToHost(&full, host, sizeof(host))) {
	    KernelSetError(ERROR_PATH_TOO_LONG);
	    return 0;
	}
	if (ghal_fs_stat(host, &info) != GHAL_OK ||
	    !(info.attrs & GHAL_FS_ATTR_DIR)) {
	    KernelSetError(ERROR_PATH_NOT_FOUND);
	    return 0;
	}
    }
    cur->KP_disk = sp;
    strcpy(cur->KP_tail, c.buf);
    KernelSetError(0);
    return (DiskHandle) sp;
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
    return (DiskHandle) cur->KP_disk;
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
