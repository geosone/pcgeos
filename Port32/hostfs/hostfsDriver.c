/*
 * hostfsDriver.c -- the HostFS file system driver (fsdriver.h): GEOS
 * files in a directory of the host, over ghal_fs and the names and
 * headers of hostfs.h.  It serves Linux, GonWare and, until the DOS
 * file system driver is ported, dos32.
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
 * Not yet: deny modes, record locks, links, FEA_GEODE_ATTR, FEA_CUSTOM,
 * FEA_PATH_INFO, FEA_DISK, FEA_DRIVE_STATUS, FEA_OWNER, FEA_RIGHTS, file
 * enumeration, volume names.
 */
#include "fsdriver.h"
#include "hostfs.h"
#include "ghal.h"

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
 * Names: disk paths to host paths
 * ------------------------------------------------------------------- */

/* the host path of the first n bytes of the disk path p: each name is
 * looked up in its host directory, a missing one is taken as it is */
static Boolean ToHostN(const char *root, const char *p, size_t n, char *out,
		       size_t size)
{
    char name[HOSTFS_NAME_MAX], comp[HOSTFS_NAME_MAX];
    size_t i = 0, t = strlen(root);

    if (t >= size)
	return FALSE;
    memcpy(out, root, t + 1);
    while (i < n) {
	size_t e;
	while (i < n && p[i] == '\\')
	    i++;
	e = i;
	while (e < n && p[e] != '\\')
	    e++;
	if (e == i)
	    break;
	if (e - i >= sizeof(comp))
	    return FALSE;
	memcpy(comp, p + i, e - i);
	comp[e - i] = 0;
	if (HostFSLookup(out, comp, name, sizeof(name)) != GHAL_OK)
	    strcpy(name, comp);
	t = strlen(out);
	if (t + 1 + strlen(name) + 1 > size)
	    return FALSE;
	out[t] = SEP;
	strcpy(out + t + 1, name);
	i = e;
    }
    return TRUE;
}

static word HostOf(FSDDisk *disk, const char *path, char *out)
{
    return ToHostN(RootOf(disk), path, strlen(path), out, HPATH)
	   ? 0 : ERROR_PATH_TOO_LONG;
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
 * directory is missing or the name cannot be mapped */
static word ResolveNew(FSDDisk *disk, const char *path, int kind, Target *t)
{
    size_t len = strlen(path), last = len;
    char name[HOSTFS_NAME_MAX];
    ghal_fs_info info;
    ghal_status s;

    memset(t, 0, sizeof(*t));
    while (last && path[last - 1] != '\\')
	last--;
    if (last == len)
	return ERROR_PATH_NOT_FOUND;		/* the root */
    if (len - last >= sizeof(t->name) - 3)
	return ERROR_INVALID_LONGNAME;
    strcpy(t->name, path + last);
    if (!ToHostN(RootOf(disk), path, last, t->dir, sizeof(t->dir)))
	return ERROR_PATH_TOO_LONG;
    if (ghal_fs_stat(t->dir, &info) != GHAL_OK ||
	!(info.attrs & GHAL_FS_ATTR_DIR))
	return ERROR_PATH_NOT_FOUND;
    s = HostFSLookup(t->dir, t->name, name, sizeof(name));
    if (s == GHAL_OK)
	t->exists = TRUE;
    else {
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

    if (HostOf(disk, path, host))
	return ERROR_PATH_TOO_LONG;
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

    if (HostOf(disk, path, host))
	return ERROR_PATH_TOO_LONG;
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
			       ? HOSTFS_NATIVE : HOSTFS_GEOS, t);
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

    if (t->hf) {
	s = ghal_fs_fstat(t->hf->f, &t->info);
	if (s != GHAL_OK)
	    return ErrorOf(s, NULL);
	strcpy(t->host, t->hf->host);
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
    word e;

    if (!t)
	return ERROR_INSUFFICIENT_MEMORY;
    e = HostOf(disk, path, t->host);
    if (!e)
	e = DoEA(t, attr, buf, size, set);
    ghal_mem_free(t);
    return e;
}

/* ---------------------------------------------------------------------
 * Operations on paths
 * ------------------------------------------------------------------- */

static word DeleteFile(FSDDisk *disk, const char *path)
{
    char host[HPATH];

    if (HostOf(disk, path, host))
	return ERROR_PATH_TOO_LONG;
    return ErrorOf(ghal_fs_delete(host), host);
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
    word e;
    size_t n;

    if (HostOf(disk, path, from))
	return ERROR_PATH_TOO_LONG;
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
						    : HOSTFS_NATIVE, t);
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

    *attrs = 0;
    if (HostOf(disk, path, host))
	return ERROR_PATH_TOO_LONG;
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

    if (attrs & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	return ERROR_ACCESS_DENIED;
    if (HostOf(disk, path, host))
	return ERROR_PATH_TOO_LONG;
    return ErrorOf(ghal_fs_set_attrs(host, attrs), host);
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
    e = ResolveNew(disk, path, HOSTFS_DIR, t);
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

/* a directory holding only its @DIRNAME.000 is empty */
static word DeleteDir(FSDDisk *disk, const char *path)
{
    char host[HPATH], dn[HPATH];
    char entry[HOSTFS_NAME_MAX];
    ghal_dir *d;
    ghal_status s;
    Boolean other = FALSE, hasDirName = FALSE;

    if (HostOf(disk, path, host))
	return ERROR_PATH_TOO_LONG;
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
    if (other)
	return ERROR_DIRECTORY_NOT_EMPTY;
    if (hasDirName && HostFSJoin(dn, sizeof(dn), host, HOSTFS_DIRNAME_FILE))
	ghal_fs_delete(dn);
    return ErrorOf(ghal_fs_rmdir(host), host);
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
};
