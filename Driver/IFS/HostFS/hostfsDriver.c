/*
 * hostfsDriver.c -- the HostFS file system driver (Internal/fsDriver.h): GEOS
 * files in a directory of the host, over ghal_fs and the names and
 * headers of hostfs.h.  It serves Linux, GonWare and, until the DOS
 * file system driver is ported, dos32.
 *
 * It takes the place of the asm HostFS driver beside it (hostfs*.asm),
 * which forwards each request to the emulator through int 0xA0.  This
 * one does the work itself, on ghal_fs; its host names follow the DOS
 * driver (dosVirtual.asm).
 *
 * A drive is a host directory (HostFSDriveData).  The paths the kernel
 * passes are "\\" separated from the root of the drive, with GEOS names:
 * each name is looked up in its host directory (HostFSLookup), a new
 * one gets a host name (HostFSNewName).  On DOS a GEOS file has a
 * generated 8.3 name and its GEOS name in its header, on the other hosts
 * the GEOS name is the host name; either tree can be read on every host.
 *
 * A GEOS file (created without FCF_NATIVE) starts with the GeosFileHeader
 * of DOS16; reading, writing, positions and sizes are those of the data
 * after it.  Its extended attributes (type, flags, release, protocol,
 * token, creator, notes, notice, creation, password, desktop info, the
 * GEOS name) are the fields of the header, a directory's those of its
 * @DIRNAME.000 (as the DOS driver keeps them: dosVirtual.asm).  A file
 * without a header (a native file) has none: getting one is
 * ERROR_ATTR_NOT_FOUND, setting one ERROR_ATTR_NOT_SUPPORTED.
 *
 * A link is a record in the @DIRNAME.000 of its directory, after the
 * header, as the DOS driver keeps it (dosLink.asm): the attributes it was
 * given, the saved disk and the path of its target, and extra data.  A
 * path through a link returns ERROR_LINK_ENCOUNTERED and the target
 * (FSDLinkEncountered); the kernel follows it.
 *
 * Not yet: deny modes, record locks, FEA_GEODE_ATTR, FEA_CUSTOM,
 * FEA_PATH_INFO, FEA_DISK, FEA_DRIVE_STATUS (the kernel's in FileEnum),
 * FEA_OWNER, FEA_RIGHTS, volume names.
 */
#include <Internal/fsDriver.h>
#include "hostfs.h"
#include "ghal.h"

#include <ctype.h>
#include <string.h>

#define HPATH	512		/* a host path */

typedef struct {
    char	root[HPATH];
} HDrive;

typedef struct {
    ghal_file	*f;
    dword	base;		/* GFH_SIZE in a GEOS file */
    FileAttrs	attrs;		/* FileCreate: set when it is closed */
    char	host[1];	/* its host path */
} HFile;

#define SEP (HostFSSep())

static const char *RootOf(FSDDisk *disk)
{
    return ((HDrive *) disk->FDK_drive->FDR_private)->root;
}

static HFile *HFileOf(FSDFile *file)
{
    return file->FF_private;
}

void *HostFSDriveData(const char *hostRoot)
{
    HDrive *d = ghal_mem_alloc(sizeof(HDrive), GHAL_MEM_ZERO);
    size_t n = strlen(hostRoot);

    if (!d)
	return NULL;
    if (n >= sizeof(d->root))
	n = sizeof(d->root) - 1;
    memcpy(d->root, hostRoot, n);
    /* without a separator at the end */
    while (n > 1 && (d->root[n - 1] == '/' || d->root[n - 1] == '\\'))
	n--;
    d->root[n] = 0;
    return d;
}

void HostFSFreeDriveData(void *data)
{
    ghal_mem_free(data);
}

/* the FileError of a ghal_fs status; a missing file is a missing path
 * when its directory is missing too */
static word ErrorOf(ghal_status s, const char *host)
{
    switch (s) {
    case GHAL_OK:		return 0;
    case GHAL_ERR_NOTFOUND:
	if (host) {
	    char dir[HPATH];
	    char *sep;
	    ghal_fs_info info;
	    strncpy(dir, host, sizeof(dir) - 1);
	    dir[sizeof(dir) - 1] = 0;
	    sep = strrchr(dir, SEP);
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
 * Links: records in @DIRNAME.000 (dosConstant.def DOSLink)
 * ------------------------------------------------------------------- */

/* DOSLinkHeader: the fields up to DLH_CREATOR are those of the
 * GeosFileHeader */
#define DLH_LONG_NAME	4	/* 36 */
#define DLH_TYPE	40
#define DLH_FLAGS	42
#define DLH_RELEASE	44
#define DLH_PROTOCOL	52
#define DLH_TOKEN	56
#define DLH_CREATOR	62
#define DLH_DESKTOP	68	/* 16 */
#define DLH_FILE_ATTR	84	/* 1 */
#define DLH_GEODE_ATTR	85	/* 2 */
#define DLH_TARGET_ID	87	/* 4, then 16 unused bytes */
/* DOSLinkData: the sizes of what follows */
#define DLD_DISK_SIZE	107
#define DLD_PATH_SIZE	109
#define DLD_EXTRA_SIZE	111
#define DL_SIZE		113	/* then the saved disk, the path (no
				 * NUL) and the extra data */
#define LINK_PATH_MAX	(PATH_BUFFER_SIZE + 2)

typedef struct {
    char	dir[HPATH];		/* the host directory */
    char	file[HPATH];		/* its @DIRNAME.000 */
    uint64_t	pos;			/* of the link in it */
    unsigned char rec[DL_SIZE];
} HLink;

#define AT_LINK	0xffff		/* Walk: the path ends at a link */

static word Get16(const unsigned char *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static void Set16(unsigned char *p, word v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
}

/* the saved disk, the path and the extra data */
static dword LinkDataSize(const unsigned char *rec)
{
    return (dword) Get16(rec + DLD_DISK_SIZE) + Get16(rec + DLD_PATH_SIZE) +
	   Get16(rec + DLD_EXTRA_SIZE);
}

static Boolean SameName(const char *a, const char *b)
{
    while (*a && tolower((byte) *a) == tolower((byte) *b)) {
	a++;
	b++;
    }
    return !*a && !*b;
}

static const char *LastName(const char *host)
{
    const char *s = strrchr(host, SEP);

    return s ? s + 1 : host;
}

/* the link at pos; FALSE at the end, or at one that is damaged */
static Boolean LinkAt(ghal_file *f, uint64_t pos, unsigned char *rec)
{
    size_t done = 0;

    if (ghal_fs_seek(f, (int64_t) pos, GHAL_FS_SEEK_SET, NULL) != GHAL_OK ||
	ghal_fs_read(f, rec, DL_SIZE, &done) != GHAL_OK ||
	done != DL_SIZE || memcmp(rec, "LN00", 4))
	return FALSE;
    rec[DLH_LONG_NAME + FILE_LONGNAME_BUFFER_SIZE - 1] = 0;
    return TRUE;
}

/* the @DIRNAME.000 of the host directory dir, for its links */
static Boolean LinkOpen(const char *dir, HLink *l, ghal_file **f)
{
    if (strlen(dir) >= sizeof(l->dir) ||
	!HostFSJoin(l->file, sizeof(l->file), dir, HOSTFS_DIRNAME_FILE))
	return FALSE;
    strcpy(l->dir, dir);
    return ghal_fs_open(l->file, GHAL_FS_READ, f) == GHAL_OK;
}

/* the link `name' in the host directory dir */
static Boolean LinkFind(const char *dir, const char *name, HLink *l)
{
    ghal_file *f;
    uint64_t pos = GFH_SIZE;
    Boolean found = FALSE;

    if (!LinkOpen(dir, l, &f))
	return FALSE;
    while (LinkAt(f, pos, l->rec)) {
	if (SameName((char *) l->rec + DLH_LONG_NAME, name)) {
	    l->pos = pos;
	    found = TRUE;
	    break;
	}
	pos += DL_SIZE + LinkDataSize(l->rec);
    }
    ghal_fs_close(f);
    return found;
}

/* n bytes of the link's data from off (the saved disk is at 0) */
static word LinkRead(const HLink *l, dword off, void *buf, size_t n)
{
    ghal_file *f;
    size_t done = 0;
    ghal_status s = ghal_fs_open(l->file, GHAL_FS_READ, &f);

    if (s != GHAL_OK)
	return ErrorOf(s, NULL);
    s = ghal_fs_seek(f, (int64_t) (l->pos + DL_SIZE + off), GHAL_FS_SEEK_SET,
		     NULL);
    if (s == GHAL_OK && n)
	s = ghal_fs_read(f, buf, n, &done);
    ghal_fs_close(f);
    if (s == GHAL_OK && done != n)
	return ERROR_SHORT_READ_WRITE;
    return ErrorOf(s, NULL);
}

/* the kernel gets the target, with what followed the link in the path */
static word LinkFollow(const HLink *l, const char *rest)
{
    unsigned char disk[FSD_SAVED_DISK_MAX];
    char path[LINK_PATH_MAX];
    word ds = Get16(l->rec + DLD_DISK_SIZE);
    word ps = Get16(l->rec + DLD_PATH_SIZE);
    word e;

    if (ds > sizeof(disk) || ps >= sizeof(path))
	return ERROR_PATH_TOO_LONG;
    e = LinkRead(l, 0, disk, ds);
    if (!e)
	e = LinkRead(l, ds, path, ps);
    if (e)
	return e;
    FSDLinkEncountered(disk, ds, path, ps, rest);
    return ERROR_LINK_ENCOUNTERED;
}

/* the whole link, in a block of ghal_mem_alloc */
static word LinkLoad(const HLink *l, unsigned char **buf, dword *n)
{
    word e;

    *n = DL_SIZE + LinkDataSize(l->rec);
    *buf = ghal_mem_alloc(*n, 0);
    if (!*buf)
	return ERROR_INSUFFICIENT_MEMORY;
    memcpy(*buf, l->rec, DL_SIZE);
    e = LinkRead(l, 0, *buf + DL_SIZE, *n - DL_SIZE);
    if (e) {
	ghal_mem_free(*buf);
	*buf = NULL;
    }
    return e;
}

/* a link at the end of the @DIRNAME.000 of dir, which is made if it is
 * missing (DOSLinkCreateDirectoryFile) */
static word LinkAppend(const char *dir, const unsigned char *buf, dword n)
{
    char file[HPATH];
    ghal_file *f;
    size_t done = 0;
    ghal_status s;

    if (!HostFSJoin(file, sizeof(file), dir, HOSTFS_DIRNAME_FILE))
	return ERROR_PATH_TOO_LONG;
    if (ghal_fs_stat(file, NULL) != GHAL_OK) {
	s = HostFSWriteDirName(dir, LastName(dir), FileNow());
	if (s != GHAL_OK)
	    return ErrorOf(s, NULL);
    }
    s = ghal_fs_open(file, GHAL_FS_WRITE, &f);
    if (s != GHAL_OK)
	return ErrorOf(s, file);
    s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_END, NULL);
    if (s == GHAL_OK)
	s = ghal_fs_write(f, buf, n, &done);
    if (s == GHAL_OK && done != n)
	s = GHAL_ERR_NOSPACE;
    ghal_fs_close(f);
    return ErrorOf(s, NULL);
}

/* the link out of its @DIRNAME.000: the links after it move up
 * (DOSLinkDeleteSpace) */
static word LinkRemove(const HLink *l)
{
    unsigned char buf[512];
    uint64_t from = l->pos + DL_SIZE + LinkDataSize(l->rec), to = l->pos;
    ghal_fs_info info;
    ghal_file *f;
    size_t got, put;
    ghal_status s = ghal_fs_open(l->file, GHAL_FS_READ | GHAL_FS_WRITE, &f);

    if (s != GHAL_OK)
	return ErrorOf(s, l->file);
    s = ghal_fs_fstat(f, &info);
    while (s == GHAL_OK && from < info.size) {
	got = 0;
	s = ghal_fs_seek(f, (int64_t) from, GHAL_FS_SEEK_SET, NULL);
	if (s == GHAL_OK)
	    s = ghal_fs_read(f, buf, sizeof(buf), &got);
	if (s != GHAL_OK || !got)
	    break;
	s = ghal_fs_seek(f, (int64_t) to, GHAL_FS_SEEK_SET, NULL);
	if (s == GHAL_OK)
	    s = ghal_fs_write(f, buf, got, &put);
	from += got;
	to += got;
    }
    if (s == GHAL_OK)
	s = ghal_fs_truncate(f, to);
    ghal_fs_close(f);
    return ErrorOf(s, NULL);
}

/* the header of the link back to its place */
static word LinkStore(const HLink *l)
{
    ghal_file *f;
    size_t done;
    ghal_status s = ghal_fs_open(l->file, GHAL_FS_WRITE, &f);

    if (s != GHAL_OK)
	return ErrorOf(s, l->file);
    s = ghal_fs_seek(f, (int64_t) l->pos, GHAL_FS_SEEK_SET, NULL);
    if (s == GHAL_OK)
	s = ghal_fs_write(f, l->rec, DL_SIZE, &done);
    ghal_fs_close(f);
    return ErrorOf(s, NULL);
}

/* a name that is no file in the host directory dir may be a link: at
 * the end of the path (rest is no more than separators) it is returned
 * in lk (AT_LINK) if lk is given; else, and on the way, it is followed */
static word CheckLink(const char *dir, const char *name, const char *rest,
		      HLink *lk)
{
    HLink *l = lk ? lk : ghal_mem_alloc(sizeof(HLink), 0);
    const char *q = rest;
    word e = 0;

    if (!l)
	return ERROR_INSUFFICIENT_MEMORY;
    while (*q == '\\')
	q++;
    if (LinkFind(dir, name, l))
	e = lk && !*q ? AT_LINK : LinkFollow(l, rest);
    if (!lk)
	ghal_mem_free(l);
    return e;
}

/* ---------------------------------------------------------------------
 * Names: disk paths to host paths
 * ------------------------------------------------------------------- */

/* the host path of the first n bytes of the disk path p: each name is
 * looked up in its host directory; one that is not there is taken as it
 * is, unless it is a link (CheckLink: lk for one at the end) */
static word Walk(FSDDisk *disk, const char *p, size_t n, char *out,
		 size_t size, HLink *lk)
{
    char name[HOSTFS_NAME_MAX], comp[HOSTFS_NAME_MAX];
    const char *root = RootOf(disk);
    size_t i = 0, t = strlen(root);
    word e;

    if (t >= size)
	return ERROR_PATH_TOO_LONG;
    memcpy(out, root, t + 1);
    while (i < n) {
	size_t end;
	while (i < n && p[i] == '\\')
	    i++;
	end = i;
	while (end < n && p[end] != '\\')
	    end++;
	if (end == i)
	    break;
	if (end - i >= sizeof(comp))
	    return ERROR_PATH_TOO_LONG;
	memcpy(comp, p + i, end - i);
	comp[end - i] = 0;
	if (HostFSLookup(out, comp, name, sizeof(name)) != GHAL_OK) {
	    e = CheckLink(out, comp, p + end, lk);
	    if (e)
		return e;
	    strcpy(name, comp);
	}
	t = strlen(out);
	if (t + 1 + strlen(name) + 1 > size)
	    return ERROR_PATH_TOO_LONG;
	out[t] = SEP;
	strcpy(out + t + 1, name);
	i = end;
    }
    return 0;
}

static word HostOf(FSDDisk *disk, const char *path, char *out)
{
    return Walk(disk, path, strlen(path), out, HPATH, NULL);
}

/* AT_LINK, with lk, if the path ends at a link */
static word HostOfL(FSDDisk *disk, const char *path, char *out, HLink *lk)
{
    return Walk(disk, path, strlen(path), out, HPATH, lk);
}

/* the host path for a new file or directory */
typedef struct {
    char	host[HPATH];		/* existing, or to create */
    char	dir[HPATH];		/* its directory */
    char	name[FILE_LONGNAME_BUFFER_SIZE];	/* the GEOS name */
    Boolean	exists;
    int		dirName;	/* a new directory needs @DIRNAME.000 */
} Target;

/* kind: HOSTFS_NATIVE, HOSTFS_GEOS, HOSTFS_DIR; an error if the
 * directory is missing or the name cannot be mapped.  A link of that
 * name is followed, or ERROR_FILE_EXISTS. */
static word ResolveNew(FSDDisk *disk, const char *path, int kind, Target *t,
		       Boolean follow)
{
    size_t len = strlen(path), last = len;
    char name[HOSTFS_NAME_MAX];
    ghal_fs_info info;
    ghal_status s;
    word e;

    memset(t, 0, sizeof(*t));
    while (last && path[last - 1] != '\\')
	last--;
    if (last == len)
	return ERROR_PATH_NOT_FOUND;		/* the root */
    if (len - last >= sizeof(t->name) - 3)
	return ERROR_INVALID_LONGNAME;
    strcpy(t->name, path + last);
    e = Walk(disk, path, last, t->dir, sizeof(t->dir), NULL);
    if (e)
	return e;
    if (ghal_fs_stat(t->dir, &info) != GHAL_OK ||
	!(info.attrs & GHAL_FS_ATTR_DIR))
	return ERROR_PATH_NOT_FOUND;
    s = HostFSLookup(t->dir, t->name, name, sizeof(name));
    if (s == GHAL_OK)
	t->exists = TRUE;
    else {
	HLink *l = ghal_mem_alloc(sizeof(HLink), 0);
	if (!l)
	    return ERROR_INSUFFICIENT_MEMORY;
	e = LinkFind(t->dir, t->name, l)
	    ? (follow ? LinkFollow(l, "") : ERROR_FILE_EXISTS) : 0;
	ghal_mem_free(l);
	if (e)
	    return e;
	s = HostFSNewName(t->dir, t->name, kind, name, sizeof(name),
			  &t->dirName);
	if (s != GHAL_OK)
	    return s == GHAL_ERR_EXISTS ? ERROR_CANNOT_MAP_NAME
					: ERROR_INVALID_LONGNAME;
    }
    if (!HostFSJoin(t->host, sizeof(t->host), t->dir, name))
	return ERROR_PATH_TOO_LONG;
    return 0;
}

static Boolean HasHeader(const char *host)
{
    unsigned char hdr[GFH_SIZE];

    return HostFSReadHeader(host, hdr) == GHAL_OK;
}

/* ---------------------------------------------------------------------
 * Disks and the current path
 * ------------------------------------------------------------------- */

/* the ID of the disk: the drive's root (FNV-1a) */
static word DiskID(FSDDrive *drive, dword *id)
{
    const char *p = ((HDrive *) drive->FDR_private)->root;
    dword h = 2166136261u;

    if (ghal_fs_stat(p, NULL) != GHAL_OK)
	return ERROR_DRIVE_NOT_READY;
    while (*p)
	h = (h ^ (byte) *p++) * 16777619u;
    *id = h;
    return 0;
}

/* no volume name: the host directory has none */
static word DiskInit(FSDDisk *disk)
{
    ghal_fs_info info;

    if (ghal_fs_stat(RootOf(disk), &info) != GHAL_OK)
	return ERROR_DRIVE_NOT_READY;
    disk->FDK_volume[0] = 0;
    if (!(info.attrs & GHAL_FS_ATTR_READONLY))
	disk->FDK_flags |= FDK_WRITABLE;
    return 0;
}

static word DiskInfo(FSDDisk *disk, DiskInfoStruct *info)
{
    uint64_t total, avail;
    uint32_t unit;
    ghal_status s = ghal_fs_space(RootOf(disk), &total, &avail, &unit);

    if (s != GHAL_OK)
	return ErrorOf(s, NULL);
    /* as much as the fields hold */
    info->DIS_blockSize = (word) (unit > 0xffff ? 0x8000 : unit);
    info->DIS_freeSpace = (sdword) (avail > 0x7fffffff ? 0x7fffffff : avail);
    info->DIS_totalSpace = (sdword) (total > 0x7fffffff ? 0x7fffffff : total);
    strncpy(info->DIS_name, disk->FDK_volume, sizeof(info->DIS_name) - 1);
    info->DIS_name[sizeof(info->DIS_name) - 1] = 0;
    return 0;
}

static word CurPathSet(FSDDisk *disk, const char *path)
{
    char host[HPATH];
    ghal_fs_info info;
    word e = HostOf(disk, path, host);

    if (e)
	return e;
    if (ghal_fs_stat(host, &info) != GHAL_OK ||
	!(info.attrs & GHAL_FS_ATTR_DIR))
	return ERROR_PATH_NOT_FOUND;
    return 0;
}

/* ---------------------------------------------------------------------
 * Opening, creating, closing
 * ------------------------------------------------------------------- */

static ghal_status AccessFlags(byte access, unsigned *flags)
{
    switch (access & 3) {
    case FILE_ACCESS_R:	*flags = GHAL_FS_READ; return GHAL_OK;
    case FILE_ACCESS_W:	*flags = GHAL_FS_WRITE; return GHAL_OK;
    case FILE_ACCESS_RW:	*flags = GHAL_FS_READ | GHAL_FS_WRITE; return GHAL_OK;
    default:			return GHAL_ERR_INVAL;
    }
}

/* an open host file, positioned at the start of its data */
static word Finish(ghal_file *f, const char *host, FileAttrs attrs,
		   Boolean geos, FSDFile *file)
{
    size_t n = strlen(host) + 1;
    HFile *hf = ghal_mem_alloc(sizeof(HFile) + n, GHAL_MEM_ZERO);

    if (!hf) {
	ghal_fs_close(f);
	return ERROR_INSUFFICIENT_MEMORY;
    }
    hf->f = f;
    hf->base = geos ? GFH_SIZE : 0;
    ghal_fs_seek(f, hf->base, GHAL_FS_SEEK_SET, NULL);
    /* a new read-only file can still be written over this handle, as
     * with DOS: its attributes are set when it is closed */
    hf->attrs = attrs & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM);
    memcpy(hf->host, host, n);
    file->FF_private = hf;
    return 0;
}

static word Open(FSDDisk *disk, const char *path, FileAccessFlags flags,
		 FSDFile *file)
{
    char host[HPATH];
    unsigned gf;
    ghal_file *f;
    ghal_status s;
    word e = HostOf(disk, path, host);

    if (e)
	return e;
    if (AccessFlags((byte) flags, &gf) != GHAL_OK)
	return ERROR_ARGS_INVALID;
    s = ghal_fs_open(host, gf, &f);
    if (s != GHAL_OK)
	return ErrorOf(s, host);
    return Finish(f, host, 0, HasHeader(host), file);
}

/* without FCF_NATIVE a GEOS file: the GeosFileHeader (GFT_DATA) first,
 * on DOS a generated 8.3 name (dosPath.asm DOSAllocOpCreate).  An
 * existing file must be of the kind asked for. */
static word Create(FSDDisk *disk, const char *path, FileCreateFlags flags,
		   FileAttrs attrs, FSDFile *file)
{
    Boolean geos = !(flags & FCF_NATIVE);
    Target *t;
    unsigned gf;
    ghal_file *f = NULL;
    ghal_status s;
    word e;

    if (AccessFlags((byte) (flags & FCF_ACCESS), &gf) != GHAL_OK)
	return ERROR_ARGS_INVALID;
    t = ghal_mem_alloc(sizeof(Target), 0);
    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    e = ResolveNew(disk, path, (flags & (FCF_NATIVE |
					  FCF_NATIVE_WITH_EXT_ATTRS))
			       ? HOSTFS_NATIVE : HOSTFS_GEOS, t, TRUE);
    if (e)
	goto done;
    if (t->exists) {
	if ((flags & FCF_MODE) == FILE_CREATE_ONLY) {
	    e = ERROR_FILE_EXISTS;
	    goto done;
	}
	if (HasHeader(t->host) != geos) {
	    e = ERROR_FILE_FORMAT_MISMATCH;
	    goto done;
	}
	s = ghal_fs_open(t->host, gf, &f);
	/* truncating keeps the header */
	if (s == GHAL_OK && (flags & FCF_MODE) == FILE_CREATE_TRUNCATE)
	    s = ghal_fs_truncate(f, geos ? GFH_SIZE : 0);
	if (s != GHAL_OK) {
	    if (f)
		ghal_fs_close(f);
	    e = ErrorOf(s, t->host);
	    goto done;
	}
	e = Finish(f, t->host, 0, geos, file);
	goto done;
    }
    s = ghal_fs_open(t->host, GHAL_FS_READ | GHAL_FS_WRITE |
		     GHAL_FS_CREATE | GHAL_FS_EXCL, &f);
    if (s == GHAL_OK && geos) {
	unsigned char hdr[GFH_SIZE];
	size_t done = 0;
	HostFSInitHeader(hdr, t->name, GFT_DATA, FileNow());
	s = ghal_fs_write(f, hdr, sizeof(hdr), &done);
	/* DOS updates the directory entry of an open file only on a
	 * commit: before it, the file looks empty and its header is not
	 * found by its GEOS name (HostFSLookup) */
	if (s == GHAL_OK)
	    s = ghal_fs_sync(f);
	if (s != GHAL_OK) {
	    ghal_fs_close(f);
	    ghal_fs_delete(t->host);
	}
    }
    e = s != GHAL_OK ? ErrorOf(s, t->host)
		     : Finish(f, t->host, attrs, geos, file);
done:
    ghal_mem_free(t);
    return e;
}

static word Close(FSDFile *file)
{
    HFile *hf = HFileOf(file);
    ghal_status s = ghal_fs_close(hf->f);

    if (s == GHAL_OK && hf->attrs)
	s = ghal_fs_set_attrs(hf->host, hf->attrs);
    ghal_mem_free(hf);
    file->FF_private = NULL;
    return ErrorOf(s, NULL);
}

/* ---------------------------------------------------------------------
 * Input and output
 * ------------------------------------------------------------------- */

/* reading or writing never reaches into the header: the position is
 * at the data at least */
static ghal_file *AtData(HFile *hf)
{
    uint64_t pos;

    if (hf->base &&
	ghal_fs_seek(hf->f, 0, GHAL_FS_SEEK_CUR, &pos) == GHAL_OK &&
	pos < hf->base)
	ghal_fs_seek(hf->f, hf->base, GHAL_FS_SEEK_SET, NULL);
    return hf->f;
}

static word Read(FSDFile *file, void *buf, word count, word *done)
{
    size_t n = 0;
    ghal_status s = ghal_fs_read(AtData(HFileOf(file)), buf, count, &n);

    *done = (word) n;
    return ErrorOf(s, NULL);
}

static word Write(FSDFile *file, const void *buf, word count, word *done)
{
    size_t n = 0;
    ghal_status s = ghal_fs_write(AtData(HFileOf(file)), buf, count, &n);

    *done = (word) n;
    return ErrorOf(s, NULL);
}

static word Position(FSDFile *file, sdword offset, FilePosMode mode,
		     dword *pos)
{
    HFile *hf = HFileOf(file);
    int whence = mode == FILE_POS_RELATIVE ? GHAL_FS_SEEK_CUR :
		 mode == FILE_POS_END ? GHAL_FS_SEEK_END : GHAL_FS_SEEK_SET;
    int64_t off = mode == FILE_POS_START
		  ? (int64_t) (dword) offset + hf->base : (int64_t) offset;
    uint64_t p = 0;
    ghal_status s = ghal_fs_seek(hf->f, off, whence, &p);

    /* not before the data */
    if (s == GHAL_OK && p < hf->base)
	s = ghal_fs_seek(hf->f, hf->base, GHAL_FS_SEEK_SET, &p);
    *pos = (dword) (p - hf->base);
    return ErrorOf(s, NULL);
}

static word Truncate(FSDFile *file, dword size)
{
    HFile *hf = HFileOf(file);
    uint64_t end = (uint64_t) size + hf->base;
    ghal_status s = ghal_fs_truncate(hf->f, end);

    /* the position is at the new end, as DOS left it */
    if (s == GHAL_OK)
	s = ghal_fs_seek(hf->f, (int64_t) end, GHAL_FS_SEEK_SET, NULL);
    return ErrorOf(s, NULL);
}

static word Commit(FSDFile *file)
{
    return ErrorOf(ghal_fs_sync(HFileOf(file)->f), NULL);
}

static word Size(FSDFile *file, dword *size)
{
    HFile *hf = HFileOf(file);
    ghal_fs_info info;
    ghal_status s = ghal_fs_fstat(hf->f, &info);

    *size = s == GHAL_OK && info.size > hf->base
	    ? (dword) (info.size - hf->base) : 0;
    return ErrorOf(s, NULL);
}

static word GetDateTime(FSDFile *file, FileDateAndTime *fdat)
{
    ghal_fs_info info;
    ghal_status s = ghal_fs_fstat(HFileOf(file)->f, &info);

    *fdat = s == GHAL_OK ? FilePackDate(&info.mtime) : 0;
    return ErrorOf(s, NULL);
}

static word SetDateTime(FSDFile *file, FileDateAndTime fdat)
{
    ghal_datetime dt;

    FileUnpackDate(fdat, &dt);
    return ErrorOf(ghal_fs_set_fmtime(HFileOf(file)->f, &dt), NULL);
}

/* ---------------------------------------------------------------------
 * Extended attributes
 * ------------------------------------------------------------------- */

/* the attributes in the header: offset, size; strings may be shorter */
static const struct {
    FileExtendedAttribute attr;
    word	off;
    word	size;
    Boolean	string;
} hdrAttrs[] = {
    {FEA_FILE_TYPE,	GFH_TYPE,	2,	FALSE},
    {FEA_FLAGS,		GFH_FLAGS,	2,	FALSE},
    {FEA_RELEASE,	GFH_RELEASE,	8,	FALSE},
    {FEA_PROTOCOL,	GFH_PROTOCOL,	4,	FALSE},
    {FEA_TOKEN,		GFH_TOKEN,	6,	FALSE},
    {FEA_CREATOR,	GFH_CREATOR,	6,	FALSE},
    {FEA_USER_NOTES,	GFH_USER_NOTES,	100,	TRUE},
    {FEA_NOTICE,	GFH_NOTICE,	32,	TRUE},
    {FEA_CREATION,	GFH_CREATED,	4,	FALSE},
    {FEA_PASSWORD,	GFH_PASSWORD,	8,	FALSE},
    {FEA_DESKTOP_INFO,	GFH_DESKTOP,	16,	FALSE},
    {FEA_NAME,		GFH_LONG_NAME,	FILE_LONGNAME_BUFFER_SIZE, TRUE},
};
#define HDR_ATTR_COUNT (sizeof(hdrAttrs) / sizeof(hdrAttrs[0]))

/* what the attributes are taken from */
typedef struct {
    HFile	*hf;		/* an open file, or */
    HLink	*link;		/* a link, or */
    char	host[HPATH];	/* a host path */
    ghal_fs_info info;
    Boolean	dir;
    Boolean	geos;		/* hdr holds a header */
    Boolean	hdrDirty;
    char	hdrPath[HPATH];	/* a directory's @DIRNAME.000 */
    unsigned char hdr[GFH_SIZE];
} EATarget;

static int HdrIndex(FileExtendedAttribute attr)
{
    unsigned i;

    for (i = 0; i < HDR_ATTR_COUNT; i++)
	if (hdrAttrs[i].attr == attr)
	    return (int) i;
    return -1;
}

static const char *BaseName(const char *path)
{
    const char *s = strrchr(path, SEP);

    return s ? s + 1 : path;
}

static word ReadFileHdr(ghal_file *f, unsigned char *hdr, Boolean *geos)
{
    uint64_t pos = 0;
    size_t done = 0;
    ghal_status s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_CUR, &pos);

    if (s == GHAL_OK)
	s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_SET, NULL);
    if (s == GHAL_OK)
	s = ghal_fs_read(f, hdr, GFH_SIZE, &done);
    ghal_fs_seek(f, (int64_t) pos, GHAL_FS_SEEK_SET, NULL);
    *geos = s == GHAL_OK && done == GFH_SIZE && HostFSIsHeader(hdr);
    return ErrorOf(s, NULL);
}

static word Load(EATarget *t)
{
    ghal_status s;

    if (t->link)
	return 0;
    if (t->hf) {
	ghal_fs_info byPath;
	s = ghal_fs_fstat(t->hf->f, &t->info);
	if (s != GHAL_OK)
	    return ErrorOf(s, NULL);
	strcpy(t->host, t->hf->host);
	/* DOS has the attributes only by the path */
	if (ghal_fs_stat(t->host, &byPath) == GHAL_OK)
	    t->info.attrs = byPath.attrs;
	if (!t->hf->base) {
	    t->geos = FALSE;
	    return 0;
	}
	return ReadFileHdr(t->hf->f, t->hdr, &t->geos);
    }
    s = ghal_fs_stat(t->host, &t->info);
    if (s != GHAL_OK)
	return ErrorOf(s, t->host);
    t->dir = (t->info.attrs & GHAL_FS_ATTR_DIR) != 0;
    if (t->dir) {
	if (!HostFSJoin(t->hdrPath, sizeof(t->hdrPath), t->host,
			HOSTFS_DIRNAME_FILE))
	    return ERROR_PATH_TOO_LONG;
	t->geos = HostFSReadHeader(t->hdrPath, t->hdr) == GHAL_OK;
    } else {
	strcpy(t->hdrPath, t->host);
	t->geos = t->info.size >= GFH_SIZE &&
		  HostFSReadHeader(t->host, t->hdr) == GHAL_OK;
    }
    return 0;
}

static word Store(EATarget *t)
{
    ghal_file *f;
    ghal_status s;
    size_t done;

    if (!t->hdrDirty)
	return 0;
    if (t->link)
	return LinkStore(t->link);
    if (t->hf) {
	uint64_t pos = 0;
	f = t->hf->f;
	s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_CUR, &pos);
	if (s == GHAL_OK)
	    s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_SET, NULL);
	if (s == GHAL_OK)
	    s = ghal_fs_write(f, t->hdr, GFH_SIZE, &done);
	if (s == GHAL_OK)
	    s = ghal_fs_sync(f);	/* for lookups (FileCreate) */
	ghal_fs_seek(f, (int64_t) pos, GHAL_FS_SEEK_SET, NULL);
	return ErrorOf(s, NULL);
    }
    s = ghal_fs_open(t->hdrPath, GHAL_FS_WRITE |
		     (t->dir ? GHAL_FS_CREATE : 0), &f);
    if (s != GHAL_OK)
	return ErrorOf(s, t->hdrPath);
    s = ghal_fs_write(f, t->hdr, GFH_SIZE, &done);
    ghal_fs_close(f);
    return ErrorOf(s, NULL);
}

/* FEA_FILE_ID: the host path (FNV-1a), the same for every name it is
 * reached by */
static dword HostFileID(const char *host)
{
    dword h = 2166136261u;

    while (*host)
	h = (h ^ (byte) *host++) * 16777619u;
    return h;
}

/* the attributes of a link (dosLink.asm getAttrLinkRoutTable): those it
 * was given in its header, its size and an ID of its own */
static const struct {
    FileExtendedAttribute attr;
    word	off;
    word	size;
} linkAttrs[] = {
    {FEA_FILE_ATTR,	DLH_FILE_ATTR,	1},
    {FEA_FILE_TYPE,	DLH_TYPE,	2},
    {FEA_FLAGS,		DLH_FLAGS,	2},
    {FEA_RELEASE,	DLH_RELEASE,	8},
    {FEA_PROTOCOL,	DLH_PROTOCOL,	4},
    {FEA_TOKEN,		DLH_TOKEN,	6},
    {FEA_CREATOR,	DLH_CREATOR,	6},
    {FEA_NAME,		DLH_LONG_NAME,	FILE_LONGNAME_BUFFER_SIZE},
    {FEA_GEODE_ATTR,	DLH_GEODE_ATTR,	2},
    {FEA_DESKTOP_INFO,	DLH_DESKTOP,	16},
    {FEA_TARGET_FILE_ID, DLH_TARGET_ID,	4},
};
#define LINK_ATTR_COUNT (sizeof(linkAttrs) / sizeof(linkAttrs[0]))

static int LinkAttrIndex(FileExtendedAttribute attr)
{
    unsigned i;

    for (i = 0; i < LINK_ATTR_COUNT; i++)
	if (linkAttrs[i].attr == attr)
	    return (int) i;
    return -1;
}

/* FEA_FILE_ID: the directory's path and the link's name, as DOS16 adds
 * the name to the directory's ID (DOSLinkGetAttrFileID) */
static dword LinkFileID(const HLink *l)
{
    dword h = HostFileID(l->dir);
    const char *n = (const char *) l->rec + DLH_LONG_NAME;

    h = (h ^ (byte) SEP) * 16777619u;
    while (*n)
	h = (h ^ (byte) tolower((byte) *n++)) * 16777619u;
    return h;
}

static word LinkGetOne(HLink *l, FileExtendedAttribute attr, void *buf,
		       word size)
{
    unsigned char v[4];
    const unsigned char *src = v;
    word n;
    dword d;
    int i = LinkAttrIndex(attr);

    if (i >= 0) {
	src = l->rec + linkAttrs[i].off;
	n = linkAttrs[i].size;
	if (attr == FEA_FILE_ATTR) {
	    v[0] = (byte) (*src | FA_LINK);
	    src = v;
	} else if (attr == FEA_NAME) {
	    if (!size)
		return ERROR_ATTR_SIZE_MISMATCH;
	    strncpy(buf, (const char *) src, size - 1);
	    ((char *) buf)[size - 1] = 0;
	    return 0;
	}
    } else {
	switch (attr) {
	case FEA_SIZE:
	    d = DL_SIZE + LinkDataSize(l->rec);
	    break;
	case FEA_FILE_ID:
	    d = LinkFileID(l);
	    break;
	default:
	    return ERROR_ATTR_NOT_SUPPORTED;
	}
	v[0] = (byte) d;
	v[1] = (byte) (d >> 8);
	v[2] = (byte) (d >> 16);
	v[3] = (byte) (d >> 24);
	n = 4;
    }
    if (size < n)
	return ERROR_ATTR_SIZE_MISMATCH;
    memcpy(buf, src, n);
    return 0;
}

/* a link is always FA_LINK; a link to a directory has FA_SUBDIR too */
static word LinkSetOne(HLink *l, FileExtendedAttribute attr, const void *buf,
		       word size)
{
    int i = LinkAttrIndex(attr);

    if (attr == FEA_NAME)
	return ERROR_ATTR_CANNOT_BE_SET;	/* FileRename */
    if (i < 0)
	return attr == FEA_SIZE || attr == FEA_FILE_ID
	       ? ERROR_ATTR_CANNOT_BE_SET : ERROR_ATTR_NOT_SUPPORTED;
    if (size != linkAttrs[i].size)
	return ERROR_ATTR_SIZE_MISMATCH;
    memcpy(l->rec + linkAttrs[i].off, buf, size);
    if (attr == FEA_FILE_ATTR)
	l->rec[DLH_FILE_ATTR] |= FA_LINK;
    return 0;
}


static word GetOne(EATarget *t, FileExtendedAttribute attr, void *buf,
		   word size)
{
    union {
	dword	d;
	word	w;
	byte	b;
	char	s[HPATH];
    } v;
    word n, full;
    const void *src = &v;
    Boolean string = FALSE;
    int i = HdrIndex(attr);

    if (t->link)
	return LinkGetOne(t->link, attr, buf, size);
    if (i >= 0) {
	if (t->geos) {
	    src = t->hdr + hdrAttrs[i].off;
	    n = hdrAttrs[i].size;
	    string = hdrAttrs[i].string;
	} else if (attr == FEA_FILE_TYPE) {
	    v.w = t->dir ? GFT_DIRECTORY : GFT_NOT_GEOS_FILE;
	    n = 2;
	} else if (attr == FEA_NAME) {
	    strncpy(v.s, BaseName(t->host), FILE_LONGNAME_BUFFER_SIZE - 1);
	    v.s[FILE_LONGNAME_BUFFER_SIZE - 1] = 0;
	    n = FILE_LONGNAME_BUFFER_SIZE;
	    string = TRUE;
	} else
	    return ERROR_ATTR_NOT_FOUND;
    } else switch (attr) {
    case FEA_MODIFICATION:
	v.d = FilePackDate(&t->info.mtime);
	n = 4;
	break;
    case FEA_FILE_ATTR:
	v.b = (byte) (t->info.attrs & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM |
				       FA_SUBDIR | FA_ARCHIVE));
	n = 1;
	break;
    case FEA_SIZE:
	v.d = t->dir ? 0 : (dword) (t->info.size -
				    (t->geos && t->info.size >= GFH_SIZE
				     ? GFH_SIZE : 0));
	n = 4;
	break;
    case FEA_FILE_ID:
	v.d = HostFileID(t->host);
	n = 4;
	break;
    case FEA_DOS_NAME:
	strncpy(v.s, BaseName(t->host), DOS_DOT_FILE_NAME_LENGTH_ZT - 1);
	v.s[DOS_DOT_FILE_NAME_LENGTH_ZT - 1] = 0;
	n = DOS_DOT_FILE_NAME_LENGTH_ZT;
	string = TRUE;
	break;
    case FEA_GEODE_ATTR:
	return ERROR_ATTR_NOT_FOUND;
    default:
	return ERROR_ATTR_NOT_SUPPORTED;
    }
    full = n;
    if (size < n) {
	if (!string)
	    return ERROR_ATTR_SIZE_MISMATCH;
	n = size;
    }
    memcpy(buf, src, n);
    if (string && n && n < full)
	((char *) buf)[n - 1] = 0;	/* cut */
    return 0;
}

static word SetOne(EATarget *t, FileExtendedAttribute attr, const void *buf,
		   word size)
{
    int i = HdrIndex(attr);
    ghal_status s;
    word e;

    if (t->link) {
	e = LinkSetOne(t->link, attr, buf, size);
	if (!e)
	    t->hdrDirty = TRUE;
	return e;
    }
    if (attr == FEA_NAME)
	return ERROR_ATTR_CANNOT_BE_SET;	/* FileRename */
    if (i >= 0) {
	if (size > hdrAttrs[i].size ||
	    (size < hdrAttrs[i].size && !hdrAttrs[i].string))
	    return ERROR_ATTR_SIZE_MISMATCH;
	if (!t->geos) {
	    if (!t->dir)
		return ERROR_ATTR_NOT_SUPPORTED;
	    /* a directory gets its @DIRNAME.000 */
	    HostFSInitHeader(t->hdr, BaseName(t->host), GFT_DIRECTORY,
			     FileNow());
	    t->geos = TRUE;
	}
	memset(t->hdr + hdrAttrs[i].off, 0, hdrAttrs[i].size);
	memcpy(t->hdr + hdrAttrs[i].off, buf, size);
	t->hdrDirty = TRUE;
	return 0;
    }
    switch (attr) {
    case FEA_MODIFICATION: {
	ghal_datetime dt;
	FileDateAndTime fdat;
	if (size != 4)
	    return ERROR_ATTR_SIZE_MISMATCH;
	memcpy(&fdat, buf, 4);
	FileUnpackDate(fdat, &dt);
	s = t->hf ? ghal_fs_set_fmtime(t->hf->f, &dt)
		  : ghal_fs_set_mtime(t->host, &dt);
	return ErrorOf(s, NULL);
    }
    case FEA_FILE_ATTR: {
	byte a;
	if (size != 1)
	    return ERROR_ATTR_SIZE_MISMATCH;
	a = *(const byte *) buf;
	if (a & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	    return ERROR_ACCESS_DENIED;
	return ErrorOf(ghal_fs_set_attrs(t->host, a), t->host);
    }
    case FEA_SIZE:
    case FEA_FILE_ID:
    case FEA_DOS_NAME:
    case FEA_GEODE_ATTR:
	return ERROR_ATTR_CANNOT_BE_SET;
    default:
	return ERROR_ATTR_NOT_SUPPORTED;
    }
}

/* one attribute, or FEA_MULTIPLE: buffer is FileExtAttrDesc[bufSize];
 * the first error is returned, the other attributes are still done */
static word DoEA(EATarget *t, FileExtendedAttribute attr, void *buffer,
		 word bufSize, Boolean set)
{
    word e = Load(t), e2;

    if (e)
	return e;
    if (attr == FEA_MULTIPLE) {
	FileExtAttrDesc *d = buffer;
	word i;
	for (i = 0; i < bufSize; i++) {
	    e2 = set ? SetOne(t, d[i].FEAD_attr, d[i].FEAD_value,
			      d[i].FEAD_size)
		     : GetOne(t, d[i].FEAD_attr, d[i].FEAD_value,
			      d[i].FEAD_size);
	    if (!e)
		e = e2;
	}
    } else
	e = set ? SetOne(t, attr, buffer, bufSize)
		: GetOne(t, attr, buffer, bufSize);
    e2 = set ? Store(t) : 0;
    return e ? e : e2;
}

static word HandleExtAttrs(FSDFile *file, FileExtendedAttribute attr,
			   void *buf, word size, Boolean set)
{
    EATarget *t = ghal_mem_alloc(sizeof(EATarget), GHAL_MEM_ZERO);
    word e;

    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    t->hf = HFileOf(file);
    e = DoEA(t, attr, buf, size, set);
    ghal_mem_free(t);
    return e;
}

static word PathExtAttrs(FSDDisk *disk, const char *path,
			 FileExtendedAttribute attr, void *buf, word size,
			 Boolean set)
{
    EATarget *t = ghal_mem_alloc(sizeof(EATarget), GHAL_MEM_ZERO);
    HLink *l = ghal_mem_alloc(sizeof(HLink), 0);
    word e;

    if (!t || !l) {
	ghal_mem_free(t);
	ghal_mem_free(l);
	return ERROR_INSUFFICIENT_MEMORY;
    }
    e = HostOfL(disk, path, t->host, l);
    if (e == AT_LINK) {
	t->link = l;
	e = 0;
    }
    if (!e)
	e = DoEA(t, attr, buf, size, set);
    ghal_mem_free(l);
    ghal_mem_free(t);
    return e;
}

/* ---------------------------------------------------------------------
 * Operations on paths
 * ------------------------------------------------------------------- */

static word DeleteFile(FSDDisk *disk, const char *path)
{
    char host[HPATH];
    HLink *l = ghal_mem_alloc(sizeof(HLink), 0);
    word e;

    if (!l)
	return ERROR_INSUFFICIENT_MEMORY;
    e = HostOfL(disk, path, host, l);
    if (e == AT_LINK)
	e = LinkRemove(l);
    else if (!e)
	e = ErrorOf(ghal_fs_delete(host), host);
    ghal_mem_free(l);
    return e;
}

/* a link gets its new name in its header (DOSLinkRenameLink); to
 * another directory it moves there */
static word RenameLink(FSDDisk *disk, HLink *l, const char *newPath)
{
    Target *t = ghal_mem_alloc(sizeof(Target), 0);
    unsigned char *buf = NULL;
    dword n;
    word e;

    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    e = ResolveNew(disk, newPath, HOSTFS_GEOS, t, FALSE);
    if (!e && t->exists)
	e = ERROR_FILE_EXISTS;
    if (!e) {
	memset(l->rec + DLH_LONG_NAME, 0, FILE_LONGNAME_BUFFER_SIZE);
	strcpy((char *) l->rec + DLH_LONG_NAME, t->name);
	if (!strcmp(t->dir, l->dir))
	    e = LinkStore(l);
	else {
	    e = LinkLoad(l, &buf, &n);
	    if (!e)
		e = LinkAppend(t->dir, buf, n);
	    if (!e)
		e = LinkRemove(l);
	    ghal_mem_free(buf);
	}
    }
    ghal_mem_free(t);
    return e;
}

/* a GEOS file or a directory with @DIRNAME.000 gets the new name in its
 * header; with DOS names its host name stays when it stays in its
 * directory, as on DOS16 */
static word Rename(FSDDisk *disk, const char *path, const char *newPath)
{
    char from[HPATH], dirName[HPATH];
    ghal_fs_info info;
    ghal_status s = GHAL_OK;
    Target *t;
    Boolean dir, geos, hasDirName, sameDir;
    HLink *l = ghal_mem_alloc(sizeof(HLink), 0);
    word e;
    size_t n;

    if (!l)
	return ERROR_INSUFFICIENT_MEMORY;
    e = HostOfL(disk, path, from, l);
    if (e == AT_LINK) {
	e = RenameLink(disk, l, newPath);
	ghal_mem_free(l);
	return e;
    }
    ghal_mem_free(l);
    if (e)
	return e;
    s = ghal_fs_stat(from, &info);
    if (s != GHAL_OK)
	return ErrorOf(s, from);
    dir = (info.attrs & GHAL_FS_ATTR_DIR) != 0;
    hasDirName = dir && HostFSJoin(dirName, sizeof(dirName), from,
				   HOSTFS_DIRNAME_FILE) &&
		 ghal_fs_stat(dirName, NULL) == GHAL_OK;
    geos = !dir && HasHeader(from);
    t = ghal_mem_alloc(sizeof(Target), 0);
    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    e = ResolveNew(disk, newPath, dir ? HOSTFS_DIR : geos ? HOSTFS_GEOS
						    : HOSTFS_NATIVE, t, FALSE);
    if (e)
	goto done;
    if (t->exists && strcmp(t->host, from)) {
	e = ERROR_FILE_EXISTS;
	goto done;
    }
    n = strlen(t->dir);
    sameDir = !strncmp(from, t->dir, n) && from[n] == SEP &&
	      !strchr(from + n + 1, SEP);
    if (!(HostFSDosNames() && sameDir && (geos || hasDirName)) &&
	strcmp(t->host, from)) {
	s = ghal_fs_rename(from, t->host);
	if (s != GHAL_OK) {
	    e = ErrorOf(s, from);
	    goto done;
	}
	strcpy(from, t->host);
    }
    if (geos)
	s = HostFSSetName(from, t->name);
    else if (hasDirName || t->dirName) {
	if (!HostFSJoin(dirName, sizeof(dirName), from, HOSTFS_DIRNAME_FILE))
	    s = GHAL_ERR_INVAL;
	else if (hasDirName)
	    s = HostFSSetName(dirName, t->name);
	else
	    s = HostFSWriteDirName(from, t->name, FileNow());
    }
    e = ErrorOf(s, NULL);
done:
    ghal_mem_free(t);
    return e;
}

static word GetAttrs(FSDDisk *disk, const char *path, FileAttrs *attrs)
{
    char host[HPATH];
    ghal_fs_info info;
    ghal_status s;
    HLink *l = ghal_mem_alloc(sizeof(HLink), 0);
    word e;

    *attrs = 0;
    if (!l)
	return ERROR_INSUFFICIENT_MEMORY;
    e = HostOfL(disk, path, host, l);
    if (e == AT_LINK)
	*attrs = (FileAttrs) (l->rec[DLH_FILE_ATTR] | FA_LINK);
    ghal_mem_free(l);
    if (e)
	return e == AT_LINK ? 0 : e;
    s = ghal_fs_stat(host, &info);
    if (s != GHAL_OK)
	return ErrorOf(s, host);
    /* the GHAL attribute bits are the DOS ones */
    *attrs = (FileAttrs) (info.attrs & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM |
					FA_SUBDIR | FA_ARCHIVE));
    return 0;
}

static word SetAttrs(FSDDisk *disk, const char *path, FileAttrs attrs)
{
    char host[HPATH];
    HLink *l;
    word e;

    if (attrs & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	return ERROR_ACCESS_DENIED;
    l = ghal_mem_alloc(sizeof(HLink), 0);
    if (!l)
	return ERROR_INSUFFICIENT_MEMORY;
    e = HostOfL(disk, path, host, l);
    if (e == AT_LINK) {
	/* a link to a directory stays one */
	l->rec[DLH_FILE_ATTR] = (byte) ((l->rec[DLH_FILE_ATTR] & FA_SUBDIR) |
					attrs | FA_LINK);
	e = LinkStore(l);
    } else if (!e)
	e = ErrorOf(ghal_fs_set_attrs(host, attrs), host);
    ghal_mem_free(l);
    return e;
}

/* with DOS names a directory whose name is no DOS name gets a
 * generated one and a @DIRNAME.000 with its GEOS name (dosPath.asm) */
static word CreateDir(FSDDisk *disk, const char *path)
{
    Target *t = ghal_mem_alloc(sizeof(Target), 0);
    ghal_status s;
    word e;

    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    e = ResolveNew(disk, path, HOSTFS_DIR, t, FALSE);
    if (!e && t->exists)
	e = ERROR_FILE_EXISTS;
    else if (!e) {
	s = ghal_fs_mkdir(t->host);
	if (s == GHAL_OK && t->dirName) {
	    s = HostFSWriteDirName(t->host, t->name, FileNow());
	    if (s != GHAL_OK)
		ghal_fs_rmdir(t->host);
	}
	e = ErrorOf(s, t->host);
    }
    ghal_mem_free(t);
    return e;
}

/* the directory has a link */
static Boolean HasLinks(const char *dir)
{
    HLink *l = ghal_mem_alloc(sizeof(HLink), 0);
    ghal_file *f;
    Boolean any = FALSE;

    if (l && LinkOpen(dir, l, &f)) {
	any = LinkAt(f, GFH_SIZE, l->rec);
	ghal_fs_close(f);
    }
    ghal_mem_free(l);
    return any;
}

/* a directory holding only its @DIRNAME.000, without links, is empty */
static word DeleteDir(FSDDisk *disk, const char *path)
{
    char host[HPATH], dn[HPATH];
    char entry[HOSTFS_NAME_MAX];
    ghal_dir *d;
    ghal_status s;
    Boolean other = FALSE, hasDirName = FALSE;
    word e = HostOf(disk, path, host);

    if (e)
	return e;
    s = ghal_fs_opendir(host, &d);
    if (s != GHAL_OK)
	return ErrorOf(s, host);
    while (ghal_fs_readdir(d, entry, sizeof(entry), NULL) == GHAL_OK) {
	if (HostFSIsDirNameFile(entry))
	    hasDirName = TRUE;
	else
	    other = TRUE;
    }
    ghal_fs_closedir(d);
    if (other || (hasDirName && HasLinks(host)))
	return ERROR_DIRECTORY_NOT_EMPTY;
    if (hasDirName && HostFSJoin(dn, sizeof(dn), host, HOSTFS_DIRNAME_FILE))
	ghal_fs_delete(dn);
    return ErrorOf(ghal_fs_rmdir(host), host);
}

/* ---------------------------------------------------------------------
 * Enumerating a directory
 * ------------------------------------------------------------------- */

/* every entry but a directory's @DIRNAME.000, then the links (as
 * DOSLinkEnum); one that cannot be read is left out */
static word FileEnum(FSDDisk *disk, const char *path,
		     word (*cb)(void *data, void *entry), void *data)
{
    char dir[HPATH], name[HOSTFS_NAME_MAX];
    EATarget *t;
    ghal_dir *d;
    ghal_file *f;
    ghal_status s;
    HLink *l;
    uint64_t pos = GFH_SIZE;
    Boolean stop = FALSE;
    word e = HostOf(disk, path, dir);

    if (e)
	return e;
    s = ghal_fs_opendir(dir, &d);
    if (s != GHAL_OK)
	return s == GHAL_ERR_NOTFOUND ? ERROR_PATH_NOT_FOUND : ErrorOf(s, dir);
    t = ghal_mem_alloc(sizeof(EATarget), 0);
    if (!t) {
	ghal_fs_closedir(d);
	return ERROR_INSUFFICIENT_MEMORY;
    }
    while (ghal_fs_readdir(d, name, sizeof(name), NULL) == GHAL_OK) {
	if (HostFSIsDirNameFile(name))
	    continue;
	memset(t, 0, sizeof(*t));
	if (!HostFSJoin(t->host, sizeof(t->host), dir, name) || Load(t))
	    continue;
	if (cb(data, t)) {
	    stop = TRUE;
	    break;
	}
    }
    ghal_fs_closedir(d);
    l = stop ? NULL : ghal_mem_alloc(sizeof(HLink), 0);
    if (l && LinkOpen(dir, l, &f)) {
	while (LinkAt(f, pos, l->rec)) {
	    l->pos = pos;
	    pos += DL_SIZE + LinkDataSize(l->rec);
	    memset(t, 0, sizeof(*t));
	    t->link = l;
	    if (cb(data, t))
		break;
	}
	ghal_fs_close(f);
    }
    ghal_mem_free(l);
    ghal_mem_free(t);
    return e;
}

static word EnumAttr(void *entry, FileExtendedAttribute attr, void *buf,
		     word size)
{
    return GetOne(entry, attr, buf, size);
}

/* ---------------------------------------------------------------------
 * Links: creating, reading, extra data
 * ------------------------------------------------------------------- */

/* DOSLinkCreateLink: GFT_DATA, protocol 1.0, FA_LINK; the kernel then
 * sets the attributes of the target */
static word CreateLink(FSDDisk *disk, const char *path, const void *savedDisk,
		       word diskSize, const char *target)
{
    Target *t;
    unsigned char *buf;
    size_t pn = strlen(target);
    dword n;
    word e;

    if (diskSize > FSD_SAVED_DISK_MAX || pn >= LINK_PATH_MAX)
	return ERROR_PATH_TOO_LONG;
    t = ghal_mem_alloc(sizeof(Target), 0);
    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    e = ResolveNew(disk, path, HOSTFS_GEOS, t, FALSE);
    if (!e && t->exists)
	e = ERROR_FILE_EXISTS;
    if (!e) {
	n = DL_SIZE + diskSize + (dword) pn;
	buf = ghal_mem_alloc(n, GHAL_MEM_ZERO);
	if (!buf)
	    e = ERROR_INSUFFICIENT_MEMORY;
	else {
	    memcpy(buf, "LN00", 4);
	    strcpy((char *) buf + DLH_LONG_NAME, t->name);
	    Set16(buf + DLH_TYPE, GFT_DATA);
	    Set16(buf + DLH_PROTOCOL, 1);
	    buf[DLH_FILE_ATTR] = FA_LINK;
	    Set16(buf + DLD_DISK_SIZE, diskSize);
	    Set16(buf + DLD_PATH_SIZE, (word) pn);
	    memcpy(buf + DL_SIZE, savedDisk, diskSize);
	    memcpy(buf + DL_SIZE + diskSize, target, pn);
	    e = LinkAppend(t->dir, buf, n);
	    ghal_mem_free(buf);
	}
    }
    ghal_mem_free(t);
    return e;
}

/* the link at the end of path, or an error (ERROR_NOT_A_LINK) */
static word FindLink(FSDDisk *disk, const char *path, HLink **l)
{
    char *host = ghal_mem_alloc(HPATH, 0);
    word e;

    *l = ghal_mem_alloc(sizeof(HLink), 0);
    if (!host || !*l) {
	ghal_mem_free(host);
	ghal_mem_free(*l);
	*l = NULL;
	return ERROR_INSUFFICIENT_MEMORY;
    }
    e = HostOfL(disk, path, host, *l);
    ghal_mem_free(host);
    if (e == AT_LINK)
	return 0;
    ghal_mem_free(*l);
    *l = NULL;
    return e ? e : ERROR_NOT_A_LINK;
}

static word ReadLink(FSDDisk *disk, const char *path)
{
    HLink *l;
    word e = FindLink(disk, path, &l);

    if (!e) {
	e = LinkFollow(l, "");
	if (e == ERROR_LINK_ENCOUNTERED)
	    e = 0;
	ghal_mem_free(l);
    }
    return e;
}

/* setting moves the link to the end (DOSLinkSetExtraData) */
static word LinkExtraData(FSDDisk *disk, const char *path, void *buf,
			  word *size, Boolean set)
{
    HLink *l;
    unsigned char *all = NULL, *nb;
    dword n, keep;
    word xs, e = FindLink(disk, path, &l);

    if (e)
	return e;
    keep = DL_SIZE + Get16(l->rec + DLD_DISK_SIZE) +
	   Get16(l->rec + DLD_PATH_SIZE);
    xs = Get16(l->rec + DLD_EXTRA_SIZE);
    if (!set) {
	if (!*size)
	    *size = xs;
	else {
	    if (*size > xs)
		*size = xs;
	    e = LinkRead(l, keep - DL_SIZE, buf, *size);
	}
    } else {
	e = LinkLoad(l, &all, &n);
	nb = e ? NULL : ghal_mem_alloc(keep + *size + 1, 0);
	if (!e && !nb)
	    e = ERROR_INSUFFICIENT_MEMORY;
	if (!e) {
	    memcpy(nb, all, keep);
	    memcpy(nb + keep, buf, *size);
	    Set16(nb + DLD_EXTRA_SIZE, *size);
	    e = LinkAppend(l->dir, nb, keep + *size);
	    if (!e)
		e = LinkRemove(l);
	    ghal_mem_free(nb);
	}
	ghal_mem_free(all);
    }
    ghal_mem_free(l);
    return e;
}

const FSDriver hostFSDriver = {
    "HostFS",
    DiskID,
    DiskInit,
    DiskInfo,
    CurPathSet,
    Open,
    Create,
    Read,
    Write,
    Position,
    Truncate,
    Commit,
    Size,
    GetDateTime,
    SetDateTime,
    Close,
    HandleExtAttrs,
    CreateDir,
    DeleteDir,
    DeleteFile,
    Rename,
    GetAttrs,
    SetAttrs,
    PathExtAttrs,
    FileEnum,
    EnumAttr,
    CreateLink,
    ReadLink,
    LinkExtraData,
};
