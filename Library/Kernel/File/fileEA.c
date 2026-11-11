/*
 * fileEA.c -- extended attributes of files and directories (the
 * FileGet/Set...ExtAttributes of fileFile.asm and fileOpenClose.asm,
 * as the DOS file system driver keeps them: dosVirtual.asm).
 *
 * The GEOS attributes (type, flags, release, protocol, token, creator,
 * notes, notice, creation, password, desktop info, the GEOS name) are
 * the fields of the GeosFileHeader at the start of a GEOS file, or of
 * the @DIRNAME.000 file of a directory (Port32/hostfs).  The others come
 * from the host file.  Values are the little-endian file data, as on
 * every host GEOS runs on.
 *
 * A file without a header (a native file) has no GEOS attributes:
 * getting one is ERROR_ATTR_NOT_FOUND, setting one
 * ERROR_ATTR_NOT_SUPPORTED.  Setting one on a directory without
 * @DIRNAME.000 creates it.
 *
 * Not yet: FEA_GEODE_ATTR, FEA_CUSTOM, FEA_PATH_INFO, FEA_DISK,
 * FEA_DRIVE_STATUS, FEA_OWNER, FEA_RIGHTS, FileGetHandleAllExtAttributes,
 * FileCopyPathExtAttributes.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"

#include <string.h>

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
    KFile	*kf;		/* an open file, or */
    char	host[HOST_PATH_SIZE];	/* a host path */
    ghal_fs_info info;
    Boolean	dir;
    Boolean	geos;		/* hdr holds a header */
    Boolean	hdrDirty;
    char	hdrPath[HOST_PATH_SIZE];	/* a directory's @DIRNAME.000 */
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
    const char *s = strrchr(path, HostFSSep());

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
    return FileErrorOf(s, NULL);
}

static word Load(EATarget *t)
{
    ghal_status s;

    if (t->kf) {
	s = ghal_fs_fstat(t->kf->KF_file, &t->info);
	if (s != GHAL_OK)
	    return FileErrorOf(s, NULL);
	strcpy(t->host, t->kf->KF_path);
	if (!t->kf->KF_base) {
	    t->geos = FALSE;
	    return 0;
	}
	return ReadFileHdr(t->kf->KF_file, t->hdr, &t->geos);
    }
    s = ghal_fs_stat(t->host, &t->info);
    if (s != GHAL_OK)
	return FileErrorOf(s, t->host);
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
    if (t->kf) {
	uint64_t pos = 0;
	f = t->kf->KF_file;
	s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_CUR, &pos);
	if (s == GHAL_OK)
	    s = ghal_fs_seek(f, 0, GHAL_FS_SEEK_SET, NULL);
	if (s == GHAL_OK)
	    s = ghal_fs_write(f, t->hdr, GFH_SIZE, &done);
	if (s == GHAL_OK)
	    s = ghal_fs_sync(f);	/* for lookups (FileCreate) */
	ghal_fs_seek(f, (int64_t) pos, GHAL_FS_SEEK_SET, NULL);
	return FileErrorOf(s, NULL);
    }
    s = ghal_fs_open(t->hdrPath, GHAL_FS_WRITE |
		     (t->dir ? GHAL_FS_CREATE : 0), &f);
    if (s != GHAL_OK)
	return FileErrorOf(s, t->hdrPath);
    s = ghal_fs_write(f, t->hdr, GFH_SIZE, &done);
    ghal_fs_close(f);
    return FileErrorOf(s, NULL);
}

static dword HostFileID(const char *host)
{
    dword h = 2166136261u;	/* FNV-1a */

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
	char	s[HOST_PATH_SIZE];
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
	s = t->kf ? ghal_fs_set_fmtime(t->kf->KF_file, &dt)
		  : ghal_fs_set_mtime(t->host, &dt);
	return FileErrorOf(s, NULL);
    }
    case FEA_FILE_ATTR: {
	byte a;
	if (size != 1)
	    return ERROR_ATTR_SIZE_MISMATCH;
	a = *(const byte *) buf;
	if (a & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	    return ERROR_ACCESS_DENIED;
	return FileErrorOf(ghal_fs_set_attrs(t->host, a), t->host);
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
static word Do(EATarget *t, FileExtendedAttribute attr, void *buffer,
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

static word Done(word e)
{
    KernelSetError(e);
    return e;
}

static word ByPath(const TCHAR *path, FileExtendedAttribute attr,
		   void *buffer, word bufSize, Boolean set)
{
    EATarget *t = ghal_mem_alloc(sizeof(EATarget), GHAL_MEM_ZERO);
    word e;

    if (!t)
	return Done(ERROR_INSUFFICIENT_MEMORY);
    e = FileHostPath(path, t->host, sizeof(t->host))
	? Do(t, attr, buffer, bufSize, set) : ThreadGetError();
    ghal_mem_free(t);
    return Done(e);
}

static word ByHandle(FileHandle fh, FileExtendedAttribute attr, void *buffer,
		     word bufSize, Boolean set)
{
    EATarget *t = ghal_mem_alloc(sizeof(EATarget), GHAL_MEM_ZERO);
    word e;

    if (!t)
	return Done(ERROR_INSUFFICIENT_MEMORY);
    t->kf = FileOf(fh);
    e = Do(t, attr, buffer, bufSize, set);
    ghal_mem_free(t);
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
