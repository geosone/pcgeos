/*
 * fileEnum.c -- enumerating the current directory (after fileEnum.asm
 * and the FileEnum parts of fileC.asm), over DR_FS_FILE_ENUM of the
 * disk's driver.
 *
 * For every entry the driver reports, the kernel fetches the attributes
 * named in FEP_returnAttrs, FEP_matchAttrs and FEP_callbackAttrs (and
 * FEA_FILE_ATTR, FEA_FILE_TYPE) into an array of FileExtAttrDesc, the
 * FileEnumCallbackData; an attribute the entry does not have has a null
 * FEAD_value.  A directory is taken if FESF_DIRS is set; any other entry
 * must match FEP_matchAttrs (FEA_FILE_ATTR, FEA_FLAGS, FEA_GEODE_ATTR,
 * FEA_PATH_INFO, FEA_DRIVE_STATUS: the FEAD_value holds the bits that
 * must be set in its low 16 bits and those that must be clear in the
 * next 16; strings: equal; others: the same bytes) and the file types of
 * FEP_searchFlags.  Then the callback (FESF_CALLBACK) decides: non-zero
 * accepts.  In the return buffer the FEAD_value of FEP_returnAttrs is
 * the offset in an entry.
 *
 * Port32: FEP_callback is called with frame 0; FileEnumWildcard finds
 * the FileEnum it was called from by its thread.  With FESF_REAL_SKIP,
 * FEP_skipCount is set to the real skip count for the next call (the
 * DI of the asm routine).
 *
 * In a standard path, its standard subdirectories are entries even
 * where no directory exists (FEEnumSpecials).
 *
 * Not yet: standard paths spread over several directories, FEA_CUSTOM,
 * FESF_LOCK_CB_DATA (nothing to lock).
 */
#include "kernelC.h"
#include <fileEnum.h>

#include <stddef.h>
#include <string.h>

#define FE_INCR_SIZE	40	/* entries the buffer grows by */
#define FE_MAX_ATTRS	32

/* the standard return types (fileEnumStandardReturnArrays) */
static const FileExtAttrDesc fesrtaCountOnly[] = {
    {FEA_END_OF_LIST, 0, 0, 0}
};
static const FileExtAttrDesc fesrtaDosInfo[] = {
    {FEA_FILE_ATTR, (void *) offsetof(FEDosInfo, DFIS_attributes),
     sizeof(FileAttrs), 0},
    {FEA_MODIFICATION, (void *) offsetof(FEDosInfo, DFIS_modTimeDate),
     sizeof(FileDateAndTime), 0},
    {FEA_SIZE, (void *) offsetof(FEDosInfo, DFIS_fileSize), sizeof(dword), 0},
    {FEA_NAME, (void *) offsetof(FEDosInfo, DFIS_name), sizeof(FileLongName),
     0},
    {FEA_PATH_INFO, (void *) offsetof(FEDosInfo, DFIS_pathInfo),
     sizeof(DirPathInfo), 0},
    {FEA_END_OF_LIST, 0, 0, 0}
};
static const FileExtAttrDesc fesrtaName[] = {
    {FEA_NAME, 0, sizeof(FileLongName), 0},
    {FEA_END_OF_LIST, 0, 0, 0}
};
static const FileExtAttrDesc fesrtaNameAndAttr[] = {
    {FEA_FILE_ATTR, (void *) offsetof(FENameAndAttr, FENAA_attr),
     sizeof(FileAttrs), 0},
    {FEA_NAME, (void *) offsetof(FENameAndAttr, FENAA_name),
     sizeof(FileLongName), 0},
    {FEA_END_OF_LIST, 0, 0, 0}
};
static const FileExtAttrDesc *const standardReturns[] = {
    fesrtaCountOnly, fesrtaDosInfo, fesrtaName, fesrtaNameAndAttr
};
static const FileExtAttrDesc fescaName[] = {	/* FESC_WILDCARD */
    {FEA_NAME, 0, FILE_LONGNAME_BUFFER_SIZE, 0},
    {FEA_END_OF_LIST, 0, 0, 0}
};

typedef struct {
    FileEnumParams	*params;	/* the caller's */
    const FileExtAttrDesc *ret;		/* FEP_returnAttrs */
    word	(*callback)(FileEnumParams *, FileEnumCallbackData *, word);
    const FileExtAttrDesc *cbAttrs;
    word	bufSize;
    word	returnSize;
    sword	realSkip;	/* FEL_realSkipCount */
    word	skip;		/* FEP_skipCount, without FESF_REAL_SKIP */
    MemHandle	buf;
    word	bufAlloc;	/* bytes */
    word	cur;		/* the offset of the next entry */
    word	found;
    word	missed;
    word	error;
    FSDDisk	*disk;
    StandardPath sp;	/* the current path, if a standard path */
    Boolean	spTop;		/* ... and not below it */
    word	nAttrs;
    FileExtAttrDesc fecd[FE_MAX_ATTRS + 1];	/* FileEnumCallbackData */
    word	off[FE_MAX_ATTRS];	/* of each value in vals */
    byte	*vals;
    word	valSize;
    char	seen[64][FILE_LONGNAME_BUFFER_SIZE];	/* directories */
    word	nSeen;
} FEState;

static Boolean IsBits(FileExtendedAttribute a)
{
    return a == FEA_FILE_ATTR || a == FEA_FLAGS || a == FEA_GEODE_ATTR ||
	   a == FEA_PATH_INFO || a == FEA_DRIVE_STATUS;
}

static Boolean IsString(FileExtendedAttribute a)
{
    return a == FEA_USER_NOTES || a == FEA_NOTICE || a == FEA_PASSWORD ||
	   a == FEA_NAME || a == FEA_DOS_NAME || a == FEA_OWNER ||
	   a == FEA_RIGHTS;
}

/* an attribute into the FECD, with the larger of its sizes */
static Boolean AddAttr(FEState *st, FileExtendedAttribute a, word size)
{
    word i;

    if (a == FEA_CUSTOM)
	return TRUE;
    for (i = 0; i < st->nAttrs; i++)
	if (st->fecd[i].FEAD_attr == a) {
	    if (size > st->fecd[i].FEAD_size)
		st->fecd[i].FEAD_size = size;
	    return TRUE;
	}
    if (st->nAttrs == FE_MAX_ATTRS)
	return FALSE;
    st->fecd[st->nAttrs].FEAD_attr = a;
    st->fecd[st->nAttrs].FEAD_size = size;
    st->nAttrs++;
    return TRUE;
}

static Boolean AddAttrs(FEState *st, const FileExtAttrDesc *d)
{
    if (!d)
	return TRUE;
    for (; d->FEAD_attr != FEA_END_OF_LIST; d++)
	if (!AddAttr(st, d->FEAD_attr,
		     d->FEAD_size ? d->FEAD_size :
		     IsString(d->FEAD_attr) ? FILE_LONGNAME_BUFFER_SIZE : 4))
	    return FALSE;
    return TRUE;
}

static void *Locate(FileExtAttrDesc *fecd, FileExtendedAttribute attr)
{
    for (; fecd->FEAD_attr != FEA_END_OF_LIST; fecd++)
	if (fecd->FEAD_attr == attr)
	    return fecd->FEAD_value;
    return NULL;
}

void *FileEnumLocateAttr(FileEnumCallbackData *fecd,
			 FileExtendedAttribute attr, const char *name)
{
    (void) name;			/* FEA_CUSTOM: not yet */
    return Locate(fecd->FECD_attrs, attr);
}

/* FEStringMatch: `*', `?', `[..]', `[^..]', `\\' escapes */
static Boolean Upper(Boolean nocase, char c, char *out)
{
    *out = nocase && c >= 'a' && c <= 'z' ? (char) (c - 'a' + 'A') : c;
    return TRUE;
}

static Boolean Match(const char *p, const char *s, Boolean nocase)
{
    char pc, sc, lo, hi;

    for (;; p++, s++) {
	Upper(nocase, *p, &pc);
	Upper(nocase, *s, &sc);
	switch (pc) {
	case 0:
	    return sc == 0;
	case '*':
	    while (p[1] == '*')
		p++;
	    if (!p[1])
		return TRUE;
	    for (; *s; s++)
		if (Match(p + 1, s, nocase))
		    return TRUE;
	    return FALSE;
	case '?':
	    if (!sc)
		return FALSE;
	    break;
	case '[': {
	    Boolean neg = p[1] == '^', in = FALSE;
	    if (!sc)
		return FALSE;
	    p += neg ? 2 : 1;
	    while (*p && *p != ']') {
		if (*p == '\\' && p[1])
		    p++;
		Upper(nocase, *p, &lo);
		hi = lo;
		if (p[1] == '-' && p[2] && p[2] != ']') {
		    p += 2;
		    if (*p == '\\' && p[1])
			p++;
		    Upper(nocase, *p, &hi);
		}
		if (sc >= lo && sc <= hi)
		    in = TRUE;
		p++;
	    }
	    if (!in == !neg || !*p)
		return FALSE;
	    break;
	}
	case '\\':
	    if (p[1]) {
		p++;
		Upper(nocase, *p, &pc);
	    }
	    /* fall through */
	default:
	    if (pc != sc)
		return FALSE;
	}
    }
}

Boolean FileEnumWildcard(FileEnumCallbackData *fecd, word frame)
{
    FEState *st = ThreadOf(KernelThisThread())->T_fileEnum;
    const char *name = FileEnumLocateAttr(fecd, FEA_NAME, NULL);

    (void) frame;
    if (!st || !name || !st->params->FEP_cbData1)
	return FALSE;
    return Match((const char *) st->params->FEP_cbData1, name,
		 (st->params->FEP_cbData2 & 0xff) != 0);
}

static word WildcardCallback(FileEnumParams *params, FileEnumCallbackData *fecd,
			     word frame)
{
    (void) params;
    return FileEnumWildcard(fecd, frame);
}

/* FECompareAttrs */
static Boolean MatchAttrs(FEState *st, const FileExtAttrDesc *m)
{
    for (; m->FEAD_attr != FEA_END_OF_LIST; m++) {
	const byte *v = Locate(st->fecd, m->FEAD_attr);
	if (!v)
	    return FALSE;
	if (IsBits(m->FEAD_attr)) {
	    uintptr_t bits = (uintptr_t) m->FEAD_value;
	    word set = (word) bits, clear = (word) (bits >> 16), have;
	    if (m->FEAD_size == 1) {
		have = v[0];
		set &= 0xff;
		clear &= 0xff;
	    } else
		have = (word) (v[0] | v[1] << 8);
	    if ((have & clear) || (~have & set))
		return FALSE;
	} else if (IsString(m->FEAD_attr)) {
	    if (strncmp((const char *) v, (const char *) m->FEAD_value,
			m->FEAD_size))
		return FALSE;
	} else if (memcmp(v, m->FEAD_value, m->FEAD_size))
	    return FALSE;
    }
    return TRUE;
}

/* the entry into the return buffer (FECopyAttrs) */
static void Store(FEState *st)
{
    FileEnumParams *p = st->params;
    const FileExtAttrDesc *r;
    byte *base;
    word need;

    if (st->found >= st->bufSize) {
	/* the first one that does not fit: the real skip count for the
	 * next call is the number of entries passed before it */
	if (!st->missed && (p->FEP_searchFlags & FESF_REAL_SKIP))
	    p->FEP_skipCount = (word) (p->FEP_skipCount - st->realSkip - 1);
	st->missed++;
	return;
    }
    need = (word) (st->cur + st->returnSize);
    if (need > st->bufAlloc) {
	dword grow = st->bufAlloc + (dword) st->returnSize * FE_INCR_SIZE;
	if (grow > 0xffff || !MemReAlloc(st->buf, (word) grow, HAF_ZERO_INIT)) {
	    st->bufSize = st->found;	/* full from now on */
	    Store(st);
	    return;
	}
	st->bufAlloc = (word) grow;
    }
    base = (byte *) MemLock(st->buf) + st->cur;
    memset(base, 0, st->returnSize);
    for (r = st->ret; r->FEAD_attr != FEA_END_OF_LIST; r++) {
	const void *v = Locate(st->fecd, r->FEAD_attr);
	if (v)
	    memcpy(base + (uintptr_t) r->FEAD_value, v, r->FEAD_size);
    }
    MemUnlock(st->buf);
    st->cur = need;
    st->found++;
}

/* FEEnumCallback: the attributes of the entry are in st->fecd */
static word Consider(FEState *st)
{
    FileEnumParams *p = st->params;
    const byte *fa = Locate(st->fecd, FEA_FILE_ATTR);
    byte flags = p->FEP_searchFlags;

    if (--st->realSkip >= 0)
	return 0;
    if (fa && (*fa & FA_SUBDIR)) {
	if (!(flags & FESF_DIRS))
	    return 0;
    } else {
	if (p->FEP_matchAttrs && !MatchAttrs(st, p->FEP_matchAttrs))
	    return 0;
	if ((flags & FILE_ENUM_ALL_FILE_TYPES) != FILE_ENUM_ALL_FILE_TYPES) {
	    const byte *ft = Locate(st->fecd, FEA_FILE_TYPE);
	    word type = ft ? (word) (ft[0] | ft[1] << 8) : GFT_NOT_GEOS_FILE;
	    byte need = type == GFT_NOT_GEOS_FILE ? FESF_NON_GEOS :
			type == GFT_EXECUTABLE ? FESF_GEOS_EXECS
					       : FESF_GEOS_NON_EXECS;
	    if (!(flags & need))
		return 0;
	}
    }
    if ((flags & FESF_CALLBACK) &&
	!st->callback(p, (FileEnumCallbackData *) st->fecd, 0))
	return 0;
    if (!(flags & FESF_REAL_SKIP) && st->skip) {
	st->skip--;
	return 0;
    }
    Store(st);
    return 0;
}

/* the values the kernel knows itself */
static Boolean KernelAttr(FEState *st, FileExtendedAttribute a, void *v,
			  word size)
{
    union { word w; DiskHandle d; } u;
    word n;

    switch (a) {
    case FEA_PATH_INFO:
	u.w = (word) (DPI_EXISTS_LOCALLY | (st->spTop ? (st->sp & DPI_STD_PATH)
						      : 0));
	n = sizeof(word);
	break;
    case FEA_DISK:
	u.d = st->disk->FDK_handle;
	n = sizeof(DiskHandle);
	break;
    case FEA_DRIVE_STATUS:
	u.w = DriveGetExtStatus(st->disk->FDK_drive->FDR_number);
	n = sizeof(word);
	break;
    default:
	return FALSE;
    }
    memcpy(v, &u, n < size ? n : size);
    return TRUE;
}

/* names compared without case, as DOS does */
static Boolean SameName(const char *a, const char *b)
{
    char x, y;

    do {
	Upper(TRUE, *a++, &x);
	Upper(TRUE, *b++, &y);
	if (x != y)
	    return FALSE;
    } while (x);
    return TRUE;
}

static void Seen(FEState *st, const char *name)
{
    if (st->nSeen < sizeof(st->seen) / sizeof(st->seen[0])) {
	strncpy(st->seen[st->nSeen], name, FILE_LONGNAME_BUFFER_SIZE - 1);
	st->seen[st->nSeen][FILE_LONGNAME_BUFFER_SIZE - 1] = 0;
	st->nSeen++;
    }
}

static word Entry(void *data, void *entry)
{
    FEState *st = data;
    word i;

    memset(st->vals, 0, st->valSize);
    for (i = 0; i < st->nAttrs; i++) {
	FileExtAttrDesc *d = &st->fecd[i];
	void *v = st->vals + st->off[i];
	d->FEAD_value = KernelAttr(st, d->FEAD_attr, v, d->FEAD_size) ||
			!FSD_OF(st->disk)->FSD_enumAttr(entry, d->FEAD_attr, v,
							d->FEAD_size)
			? v : NULL;
    }
    if (st->spTop) {
	const byte *fa = Locate(st->fecd, FEA_FILE_ATTR);
	const char *name = Locate(st->fecd, FEA_NAME);
	if (fa && (*fa & FA_SUBDIR) && name)
	    Seen(st, name);
    }
    return Consider(st);
}

/* FEEnumSpecials: the standard subdirectories that do not exist */
static void Specials(FEState *st)
{
    StandardPath sp;
    const char *name;
    word i, j;

    for (sp = SP_TOP + 2; (name = FileStdPathName(sp)) != NULL; sp += 2) {
	if (FileStdPathParent(sp) != st->sp)
	    continue;
	for (j = 0; j < st->nSeen; j++)
	    if (SameName(st->seen[j], name))
		break;
	if (j < st->nSeen)
	    continue;
	memset(st->vals, 0, st->valSize);
	for (i = 0; i < st->nAttrs; i++) {
	    FileExtAttrDesc *d = &st->fecd[i];
	    void *v = st->vals + st->off[i];
	    d->FEAD_value = v;
	    switch (d->FEAD_attr) {
	    case FEA_FILE_ATTR:
		*(byte *) v = FA_SUBDIR;
		break;
	    case FEA_FILE_TYPE: {
		word t = GFT_DIRECTORY;
		memcpy(v, &t, 2);
		break;
	    }
	    case FEA_NAME:
		strncpy(v, name, d->FEAD_size);
		if (d->FEAD_size)
		    ((char *) v)[d->FEAD_size - 1] = 0;
		break;
	    case FEA_PATH_INFO: {
		word w = (word) (sp & DPI_STD_PATH);	/* not on the disk */
		memcpy(v, &w, 2);
		break;
	    }
	    default:
		if (!KernelAttr(st, d->FEAD_attr, v, d->FEAD_size))
		    d->FEAD_value = NULL;
	    }
	}
	Consider(st);
    }
}

word FileEnum(FileEnumParams *params, MemHandle *bufCreated, word *numNoFit)
{
    KThread *t = ThreadOf(KernelThisThread());
    FEState *st;
    char dp[FILE_DISK_PATH_SIZE];
    DiskHandle cur;
    char tail[2];
    void *outer;
    word i, e, count;

    *bufCreated = 0;
    if (numNoFit)
	*numNoFit = 0;
    st = ghal_mem_alloc(sizeof(FEState), GHAL_MEM_ZERO);
    if (!st) {
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return (word) -1;
    }
    st->params = params;
    /* FEFinishParams: the standard return types and callbacks */
    st->ret = (uintptr_t) params->FEP_returnAttrs <
	      sizeof(standardReturns) / sizeof(standardReturns[0])
	      ? standardReturns[(uintptr_t) params->FEP_returnAttrs]
	      : params->FEP_returnAttrs;
    st->returnSize = st->ret == fesrtaCountOnly ? 0 : params->FEP_returnSize;
    st->bufSize = st->returnSize ? params->FEP_bufSize : 0;
    st->cbAttrs = params->FEP_callbackAttrs;
    if (params->FEP_searchFlags & FESF_CALLBACK) {
	if ((uintptr_t) params->FEP_callback == FESC_WILDCARD) {
	    st->callback = WildcardCallback;
	    st->cbAttrs = fescaName;
	} else
	    st->callback = params->FEP_callback;
    }
    st->realSkip = (params->FEP_searchFlags & FESF_REAL_SKIP)
		   ? (sword) params->FEP_skipCount : 0;
    st->skip = (params->FEP_searchFlags & FESF_REAL_SKIP)
	       ? 0 : params->FEP_skipCount;

    /* FEFormFECD */
    e = 0;
    if (!AddAttr(st, FEA_FILE_ATTR, sizeof(FileAttrs)) ||
	!AddAttr(st, FEA_FILE_TYPE, sizeof(word))	/* GeosFileType */ ||
	!AddAttr(st, FEA_NAME, FILE_LONGNAME_BUFFER_SIZE) ||
	!AddAttrs(st, st->ret) || !AddAttrs(st, params->FEP_matchAttrs) ||
	!AddAttrs(st, st->cbAttrs))
	e = ERROR_ARGS_INVALID;
    st->fecd[st->nAttrs].FEAD_attr = FEA_END_OF_LIST;
    for (i = 0; i < st->nAttrs; i++) {
	st->off[i] = st->valSize;
	st->valSize = (word) (st->valSize + ((st->fecd[i].FEAD_size + 7) & ~7));
    }
    if (!e) {
	st->vals = ghal_mem_alloc(st->valSize ? st->valSize : 1, 0);
	if (!st->vals)
	    e = ERROR_INSUFFICIENT_MEMORY;
    }

    /* FEAllocReturnBuffer */
    if (!e) {
	if (params->FEP_searchFlags & FESF_LEAVE_HEADER)
	    st->cur = params->FEP_headerSize;
	if (st->returnSize || st->cur) {
	    dword n = st->cur + (dword) st->returnSize *
		      (st->bufSize < FE_INCR_SIZE ? st->bufSize : FE_INCR_SIZE);
	    st->bufAlloc = (word) (n > 0xffff ? 0xffff : n ? n : 1);
	    st->buf = MemAlloc(st->bufAlloc, HF_SWAPABLE | HF_SHARABLE,
			       HAF_ZERO_INIT);
	    if (!st->buf)
		e = ERROR_INSUFFICIENT_MEMORY;
	}
    }

    if (!e) {
	st->disk = FileResolve("", dp, sizeof(dp));
	if (!st->disk)
	    e = ThreadGetError();
    }
    if (!e) {
	cur = FileGetCurrentPath(tail, sizeof(tail));
	if (cur & DISK_IS_STD_PATH_MASK) {
	    st->sp = (StandardPath) cur;
	    st->spTop = !tail[0];
	}
	outer = t->T_fileEnum;
	t->T_fileEnum = st;
	e = FSD_OF(st->disk)->FSD_fileEnum(st->disk, dp, Entry, st);
	/* a standard path need not exist */
	if (e == ERROR_PATH_NOT_FOUND && st->spTop)
	    e = 0;
	if (!e && st->spTop)
	    Specials(st);
	t->T_fileEnum = outer;
    }

    /* FECleanup */
    if (e) {
	if (st->buf)
	    MemFree(st->buf);
	if (numNoFit)
	    *numNoFit = (word) (st->missed + st->found);
	count = (word) -1;
    } else {
	if (st->buf && !st->found && !(params->FEP_searchFlags &
					FESF_LEAVE_HEADER)) {
	    MemFree(st->buf);
	    st->buf = 0;
	} else if (st->buf && st->cur < st->bufAlloc)
	    MemReAlloc(st->buf, st->cur ? st->cur : 1, 0);
	*bufCreated = st->buf;
	if (numNoFit)
	    *numNoFit = st->missed;
	count = st->found;
    }
    ghal_mem_free(st->vals);
    ghal_mem_free(st);
    KernelSetError(e);
    return count;
}
