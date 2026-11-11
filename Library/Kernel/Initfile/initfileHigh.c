/*
 * initfileHigh.c -- the init file API (after initfileHigh.asm, the C
 * stubs of initfileC.asm and, for finding the files, Loader/ini.asm).
 *
 * The init files are read on first use: geos.ini in the top directory,
 * then the files that "[paths] ini" of an init file names, up to
 * MAX_INI_FILES (the loader's chain).  Reads search them in that order
 * (IFRF_FIRST_ONLY: the first only, IFRF_READ_ALL: all of them, the
 * values joined by line ends); writes and deletions change the first.
 * The text lives in memory until InitFileCommit writes the first file
 * back; InitFileSave and InitFileRevert copy it to and from ini.bak in
 * PRIVDATA.  Port32/ABI.md section 13h.
 *
 * Differences from the asm kernel:
 *  - the files of "[paths] ini" are GEOS paths (with a drive name, or
 *    relative to the top directory), not paths of the host;
 *  - without a geos.ini the first file starts empty; InitFileCommit
 *    creates it;
 *  - the init file is not kept open between commits;
 *  - InitFileGrab copies the passed block: what is written while grabbed
 *    goes back into it at InitFileRelease;
 *  - IFRF_READ_ALL also reads into a buffer; a section must fit in the
 *    buffer with its null.
 *
 * Not yet: the /P switch of the loader, geosec.ini, the hash table,
 * InitFileBackupLanguage, InitFilePush/PopPrimaryFile.
 */
#include "initfileInt.h"
#include "ghal.h"

#include <string.h>

#define MAX_INI_FILES	4

typedef struct {
    char	*IT_text;	/* IT_len bytes, then INI_EOF */
    word	IT_len;
} IniText;

static IniText ini[MAX_INI_FILES];
static word iniCount;
static Boolean iniLoaded;
static char iniName[16];		/* of the first, in SP_TOP */
static dword iniModified;

static IniText grabSaved;
static MemHandle grabMem;
static FileHandle grabFile;
static Boolean grabbed;

/* exclusive access, nesting for the thread that has it (callbacks may
 * call the init file routines) */
static KSem iniSem = { 1, 0 };
static ThreadHandle iniOwner;
static word iniNesting;

static void Load(void);

static void PInitFile(void)
{
    ThreadHandle me = KernelThisThread();

    if (iniNesting && iniOwner == me) {
	iniNesting++;
	return;
    }
    KSemP(&iniSem, KSEM_FOREVER);
    iniOwner = me;
    iniNesting = 1;
    if (!iniLoaded)
	Load();
}

static void VInitFile(void)
{
    if (--iniNesting == 0) {
	iniOwner = 0;
	KSemV(&iniSem);
    }
}

/* ---------------------------------------------------------------------
 * Texts
 */

/* `len' bytes at `data' (up to a C_CTRL_Z) as the text of it */
static Boolean SetText(IniText *it, const char *data, dword len)
{
    const char *eof = memchr(data, INI_EOF, len);
    char *t;

    if (eof)
	len = (dword) (eof - data);
    if (len > INI_MAX_SIZE)
	return FALSE;
    t = ghal_mem_alloc(len + 1, 0);
    if (!t)
	return FALSE;
    memcpy(t, data, len);
    t[len] = INI_EOF;
    it->IT_text = t;
    it->IT_len = (word) len;
    return TRUE;
}

static void FreeText(IniText *it)
{
    if (it->IT_text)
	ghal_mem_free(it->IT_text);
    it->IT_text = NULL;
    it->IT_len = 0;
}

/* replace `del' bytes at `at' by `n' bytes of `ins' */
static Boolean Splice(IniText *it, word at, word del, const char *ins,
		      word n)
{
    dword len = (dword) it->IT_len - del + n;
    char *t = it->IT_text;

    if (len > INI_MAX_SIZE)
	return FALSE;
    if (n > del) {
	t = ghal_mem_resize(t, len + 1);
	if (!t)
	    return FALSE;
	it->IT_text = t;
    }
    memmove(t + at + n, t + at + del, (size_t) (it->IT_len - at - del + 1));
    memcpy(t + at, ins, n);
    it->IT_len = (word) len;
    return TRUE;
}

/* ---------------------------------------------------------------------
 * Loading (OpenIniFiles, SearchForIniPath of Loader/ini.asm)
 */

/* read the file `name' (current directory) into `it' */
static Boolean ReadIniFile(const char *name, FileAccessFlags access,
			   IniText *it)
{
    FileHandle fh = FileOpen(name, access);
    dword size;
    char *data;
    Boolean ok = FALSE;

    if (!fh)
	return FALSE;
    size = FileSize(fh);
    if (size <= INI_MAX_SIZE) {
	data = ghal_mem_alloc(size + 1, 0);
	if (data) {
	    if (FileRead(fh, data, (word) size, FALSE) == size)
		ok = SetText(it, data, size);
	    ghal_mem_free(data);
	}
    }
    FileClose(fh, FALSE);
    return ok;
}

/* the value of [paths] ini of file i: the further files */
static void Chain(word i)
{
    const char *t = ini[i].IT_text;
    word pos, ks, n, start = 0, end;
    char *value;

    if (!IniFindCategory(t, "paths", &pos) ||
	!IniFindKey(t, &pos, "ini", &ks))
	return;
    n = IniValueSize(t, pos);
    value = ghal_mem_alloc((dword) n + 1, 0);
    if (!value)
	return;
    n = IniGetString(t, pos, value, (word) (n + 1), IFCC_INTACT);
    while (start < n && iniCount < MAX_INI_FILES) {
	for (end = start; end < n && value[end] != ' '; end++)
	    ;
	value[end] = 0;
	if (end > start &&
	    ReadIniFile(value + start, FILE_ACCESS_R | FILE_DENY_W,
			&ini[iniCount]))
	    Chain(iniCount++);
	start = (word) (end + 1);
    }
    ghal_mem_free(value);
}

static void Load(void)
{
    static const char *const names[] = { "geos.ini", "GEOS.INI" };
    word i;

    iniLoaded = TRUE;
    iniCount = 1;
    FilePushDir();
    FileSetStandardPath(SP_TOP);
    strcpy(iniName, names[0]);
    for (i = 0; i < 2; i++) {
	if (ReadIniFile(names[i], FILE_ACCESS_R | FILE_DENY_W, &ini[0])) {
	    strcpy(iniName, names[i]);
	    break;
	}
    }
    if (!ini[0].IT_text && !SetText(&ini[0], "", 0))
	KernelFatal(INIT_FILE_ERROR);
    Chain(0);
    FilePopDir();
    KernelSetError(0);
}

void InitFileReset(void)
{
    word i;

    KSemP(&iniSem, KSEM_FOREVER);
    for (i = 0; i < MAX_INI_FILES; i++)
	FreeText(&ini[i]);
    iniCount = 0;
    iniLoaded = FALSE;
    KSemV(&iniSem);
}

/* ---------------------------------------------------------------------
 * Finding a key
 */

/* the first file from *file on (only the first with IFRF_FIRST_ONLY)
 * with cat and key: *file and the position of the value */
static Boolean Locate(const char *cat, const char *key,
		      InitFileReadFlags flags, word *file, word *body)
{
    word last = (flags & IFRF_FIRST_ONLY) ? 1 : iniCount, f, pos, ks;

    for (f = *file; f < last; f++) {
	if (IniFindCategory(ini[f].IT_text, cat, &pos) &&
	    IniFindKey(ini[f].IT_text, &pos, key, &ks)) {
	    *file = f;
	    *body = pos;
	    return TRUE;
	}
    }
    return FALSE;
}

/*
 * InitFileRead/InitFileReadAll: the string of cat/key as a null
 * terminated ghal_mem block, *len its length; NULL if there is none.
 */
static char *ReadString(const char *cat, const char *key,
			InitFileReadFlags flags, word *len)
{
    byte conv = (byte) ((flags & IFRF_CHAR_CONVERT) >>
			IFRF_CHAR_CONVERT_OFFSET);
    word f = 0, body, n = 0, size;
    char *s = NULL, *ns;

    if (flags & IFRF_READ_ALL)
	flags &= ~IFRF_FIRST_ONLY;
    PInitFile();
    while (Locate(cat, key, flags, &f, &body)) {
	const char *t = ini[f].IT_text;
	size = IniValueSize(t, body);
	ns = s ? ghal_mem_resize(s, (dword) n + 2 + size + 1)
	       : ghal_mem_alloc((dword) size + 1, 0);
	if (!ns)
	    break;
	s = ns;
	if (n) {
	    s[n++] = '\r';
	    s[n++] = '\n';
	}
	n = (word) (n + IniGetString(t, body, s + n, (word) (size + 1), conv));
	if (!(flags & IFRF_READ_ALL))
	    break;
	f++;
    }
    VInitFile();
    *len = n;
    return s;
}

static MemHandle NewBlock(const void *data, word size)
{
    MemHandle mh = MemAlloc(size ? size : 1, HF_SWAPABLE, HAF_STANDARD);

    if (mh) {
	memcpy(MemLock(mh), data, size);
	MemUnlock(mh);
    }
    return mh;
}

/* ---------------------------------------------------------------------
 * Reading
 */

Boolean InitFileReadStringBuffer(const char *category, const char *key,
				 char *buffer, InitFileReadFlags flags,
				 word *dataSize)
{
    word len, size = flags & IFRF_SIZE;
    char *s = ReadString(category, key, flags, &len);

    *dataSize = 0;
    if (!s)
	return TRUE;
    if (size) {
	if (len >= size)
	    len = (word) (size - 1);
	memcpy(buffer, s, len);
	buffer[len] = 0;
	*dataSize = len;
    }
    ghal_mem_free(s);
    return FALSE;
}

Boolean InitFileReadStringBlock(const char *category, const char *key,
				MemHandle *block, InitFileReadFlags flags,
				word *dataSize)
{
    word len;
    char *s = ReadString(category, key, flags & ~IFRF_SIZE, &len);

    *block = 0;
    *dataSize = 0;
    if (!s)
	return TRUE;
    *block = NewBlock(s, (word) (len + 1));
    ghal_mem_free(s);
    if (!*block)
	return TRUE;
    *dataSize = len;
    return FALSE;
}

/* the section of the string of cat/key: s (to free) and where in it */
static char *ReadSection(const char *cat, const char *key, word section,
			 InitFileReadFlags flags, word *start, word *len)
{
    word n;
    char *s = ReadString(cat, key, flags & ~IFRF_SIZE, &n);

    if (s && !IniSection(s, n, section, start, len)) {
	ghal_mem_free(s);
	s = NULL;
    }
    return s;
}

Boolean InitFileReadStringSectionBuffer(const char *category,
					const char *key, word section,
					char *buffer, InitFileReadFlags flags,
					word *dataSize)
{
    word start, len;
    char *s = ReadSection(category, key, section, flags, &start, &len);

    *dataSize = 0;
    if (!s)
	return TRUE;
    if ((flags & IFRF_SIZE) <= len) {
	ghal_mem_free(s);
	return TRUE;
    }
    memcpy(buffer, s + start, len);
    buffer[len] = 0;
    *dataSize = len;
    ghal_mem_free(s);
    return FALSE;
}

Boolean InitFileReadStringSectionBlock(const char *category, const char *key,
				       word section, MemHandle *block,
				       InitFileReadFlags flags, word *dataSize)
{
    word start, len;
    char *s = ReadSection(category, key, section, flags, &start, &len);

    *block = 0;
    *dataSize = 0;
    if (!s)
	return TRUE;
    s[start + len] = 0;
    *block = NewBlock(s + start, (word) (len + 1));
    ghal_mem_free(s);
    if (!*block)
	return TRUE;
    *dataSize = len;
    return FALSE;
}

/* TRUE only if the callback stopped the enumeration */
Boolean InitFileEnumStringSection(const char *category, const char *key,
				  InitFileReadFlags flags,
				  PCB(Boolean, callback,
				      (const char *stringSection,
				       word sectionNum, void *enumData)),
				  void *enumData)
{
    word n, start, len, i;
    char *s = ReadString(category, key, flags & ~IFRF_SIZE, &n);
    Boolean stop = FALSE;

    if (!s)
	return FALSE;
    for (i = 0; !stop && IniSection(s, n, i, &start, &len); i++) {
	char save = s[start + len];
	s[start + len] = 0;
	stop = callback(s + start, i, enumData) != FALSE;
	s[start + len] = save;
    }
    ghal_mem_free(s);
    return stop;
}

static Boolean ReadData(const char *cat, const char *key, byte *buffer,
			word bufSize, MemHandle *block, word *dataSize)
{
    word f = 0, body, n;
    Boolean found;
    byte *data;

    *dataSize = 0;
    PInitFile();
    found = Locate(cat, key, 0, &f, &body);
    if (found && block) {
	const char *t = ini[f].IT_text;
	n = IniGetData(t, body, NULL, 0, TRUE);
	*block = MemAlloc(n ? n : 1, HF_SWAPABLE, HAF_STANDARD);
	if (!*block)
	    found = FALSE;
	else {
	    data = MemLock(*block);
	    *dataSize = IniGetData(t, body, data, n, FALSE);
	    MemUnlock(*block);
	}
    } else if (found)
	*dataSize = IniGetData(ini[f].IT_text, body, buffer, bufSize, FALSE);
    VInitFile();
    return !found;
}

Boolean InitFileReadDataBuffer(const char *category, const char *key,
			       void *buffer, word bufSize, word *dataSize)
{
    return ReadData(category, key, buffer, bufSize, NULL, dataSize);
}

Boolean InitFileReadDataBlock(const char *category, const char *key,
			      MemHandle *block, word *dataSize)
{
    *block = 0;
    return ReadData(category, key, NULL, 0, block, dataSize);
}

Boolean InitFileReadInteger(const char *category, const char *key, word *i)
{
    word f = 0, body;
    Boolean ok;

    PInitFile();
    ok = Locate(category, key, 0, &f, &body) &&
	 IniGetDecimal(ini[f].IT_text, body, i);
    VInitFile();
    return !ok;
}

/* the callback for the value of cat/key in each init file that has it;
 * TRUE if there is none or the callback stopped */
Boolean InitFileReadAllInteger(const char *category, const char *key,
			       PCB(Boolean, callback,
				   (word integerValue, void *enumData)),
			       void *enumData)
{
    word f = 0, body, v;
    Boolean any = FALSE, stop = FALSE;

    PInitFile();
    while (!stop && Locate(category, key, 0, &f, &body)) {
	any = TRUE;
	if (IniGetDecimal(ini[f].IT_text, body, &v))
	    stop = callback(v, enumData) != FALSE;
	f++;
    }
    VInitFile();
    return stop || !any;
}

Boolean InitFileReadBoolean(const char *category, const char *key,
			    Boolean *bool)
{
    word f = 0, body;
    Boolean ok;

    PInitFile();
    ok = Locate(category, key, 0, &f, &body) &&
	 IniGetBoolean(ini[f].IT_text, body, bool);
    VInitFile();
    return !ok;
}

/* ---------------------------------------------------------------------
 * Writing: always into the first file
 */

/* InitFileWrite: the entry for key (from IniBuild*) into cat */
static void Write(const char *cat, const char *key, char *entry, word size)
{
    IniText *it = &ini[0];
    word pos, ks;
    Boolean ok;

    if (!entry)
	return;
    PInitFile();
    if (!IniFindCategory(it->IT_text, cat, &pos)) {
	/* a new category at the end */
	dword clen = strlen(cat);
	char *c = ghal_mem_alloc(clen + 6, 0);
	word n = 0;
	ok = FALSE;
	if (c && clen < INI_MAX_SIZE) {
	    if (it->IT_len && it->IT_text[it->IT_len - 1] != '\n') {
		c[n++] = '\r';
		c[n++] = '\n';
	    }
	    c[n++] = '[';
	    memcpy(c + n, cat, clen);
	    n = (word) (n + clen);
	    c[n++] = ']';
	    c[n++] = '\r';
	    c[n++] = '\n';
	    pos = it->IT_len;
	    ok = Splice(it, pos, 0, c, n);
	    pos = (word) (pos + n);
	}
	if (c)
	    ghal_mem_free(c);
	if (ok)
	    ok = Splice(it, pos, 0, entry, size);
    } else if (IniFindKey(it->IT_text, &pos, key, &ks)) {
	/* replace the entry */
	ok = Splice(it, ks, (word) (IniEntryEnd(it->IT_text, pos) - ks),
		    entry, size);
    } else {
	/* a new key: before the next category or at the end */
	ok = TRUE;
	if (pos == it->IT_len && pos && it->IT_text[pos - 1] != '\n') {
	    ok = Splice(it, pos, 0, "\r\n", 2);
	    pos = (word) (pos + 2);
	}
	if (ok)
	    ok = Splice(it, pos, 0, entry, size);
    }
    if (ok)
	iniModified = TimerGetCount();
    VInitFile();
    ghal_mem_free(entry);
}

void InitFileWriteString(const char *category, const char *key,
			 const char *str)
{
    word size = 0;
    char *entry = IniBuildString(key, str, &size);

    Write(category, key, entry, size);
}

void InitFileWriteData(const char *category, const char *key,
		       const void *buffer, word bufSize)
{
    word size = 0;
    char *entry = IniBuildData(key, buffer, bufSize, &size);

    Write(category, key, entry, size);
}

/* Hex16ToAscii: decimal */
void InitFileWriteInteger(const char *category, const char *key, word value)
{
    char digits[6], *p = digits + 5;

    *p = 0;
    do {
	*--p = (char) ('0' + value % 10);
	value /= 10;
    } while (value);
    InitFileWriteString(category, key, p);
}

void InitFileWriteBoolean(const char *category, const char *key,
			  Boolean bool)
{
    InitFileWriteString(category, key, bool ? "true" : "false");
}

/* a section after the string of the first file (CR/LF between) */
void InitFileWriteStringSection(const char *category, const char *key,
				const char *string)
{
    word len;
    dword slen = strlen(string);
    char *s, *ns;

    PInitFile();
    s = ReadString(category, key, IFRF_FIRST_ONLY, &len);
    if (s && len) {
	ns = ghal_mem_resize(s, (dword) len + 2 + slen + 1);
	if (ns) {
	    s = ns;
	    s[len] = '\r';
	    s[len + 1] = '\n';
	    memcpy(s + len + 2, string, slen + 1);
	    InitFileWriteString(category, key, s);
	}
    } else
	InitFileWriteString(category, key, string);
    if (s)
	ghal_mem_free(s);
    VInitFile();
}

void InitFileDeleteEntry(const char *category, const char *key)
{
    IniText *it;
    word pos, ks;

    PInitFile();
    it = &ini[0];
    if (IniFindCategory(it->IT_text, category, &pos) &&
	IniFindKey(it->IT_text, &pos, key, &ks))
	Splice(it, ks, (word) (IniEntryEnd(it->IT_text, pos) - ks), "", 0);
    VInitFile();
}

/* the section and the line ends after it (at the end: before it); the
 * entry if nothing is left */
void InitFileDeleteStringSection(const char *category, const char *key,
				 word stringNum)
{
    word len, start, slen, end, dummy;
    char *s;

    PInitFile();
    s = ReadString(category, key, IFRF_FIRST_ONLY, &len);
    if (s && IniSection(s, len, stringNum, &start, &slen)) {
	end = (word) (start + slen);
	while (end < len && (byte) s[end] < ' ')
	    end++;
	if (end == len)
	    while (start > 0 && (byte) s[start - 1] < ' ')
		start--;
	memmove(s + start, s + end, (size_t) (len - end + 1));
	len = (word) (len - (end - start));
	if (IniSection(s, len, 0, &dummy, &slen))
	    InitFileWriteString(category, key, s);
	else
	    InitFileDeleteEntry(category, key);
    }
    if (s)
	ghal_mem_free(s);
    VInitFile();
}

/* from its "[" to the "[" of the next category or the end */
void InitFileDeleteCategory(const char *category)
{
    IniText *it;
    word pos, start, end;
    Boolean blob = FALSE;

    PInitFile();
    it = &ini[0];
    if (IniFindCategory(it->IT_text, category, &pos)) {
	end = pos;
	if (IniFindChar(it->IT_text, &end, '[', &blob))
	    end--;
	for (start = pos; start > 0 && it->IT_text[start - 1] != '['; start--)
	    ;
	if (start)
	    start--;
	Splice(it, start, (word) (end - start), "", 0);
    }
    VInitFile();
}

dword InitFileGetTimeLastModified(void)
{
    return iniModified;
}

/* ---------------------------------------------------------------------
 * The files
 */

static Boolean WriteAll(FileHandle fh, const IniText *it)
{
    return FileWrite(fh, it->IT_text, it->IT_len, FALSE) == it->IT_len &&
	   FileTruncate(fh, it->IT_len, FALSE) == 0 &&
	   FileCommit(fh, FALSE) == 0;
}

/* INITFILECOMMIT: the first file back to its file (one that cannot be
 * written is left alone, as a read-only geos.ini) */
void InitFileCommit(void)
{
    FileHandle fh;

    PInitFile();
    if (grabbed && grabFile) {
	FilePos(grabFile, 0, FILE_POS_START);
	WriteAll(grabFile, &ini[0]);
    } else {
	FilePushDir();
	FileSetStandardPath(SP_TOP);
	fh = FileOpen(iniName, FILE_ACCESS_RW | FILE_DENY_W);
	if (!fh && ThreadGetError() == ERROR_FILE_NOT_FOUND)
	    fh = FileCreate(iniName, FCF_NATIVE | FILE_CREATE_NO_TRUNCATE |
				     FILE_ACCESS_RW | FILE_DENY_W,
			    FILE_ATTR_NORMAL);
	if (fh) {
	    WriteAll(fh, &ini[0]);
	    FileClose(fh, FALSE);
	}
	FilePopDir();
    }
    KernelSetError(0);
    VInitFile();
}

static const char backupName[] = "ini.bak";

Boolean InitFileSave(void)
{
    FileHandle fh;
    Boolean ok = FALSE;

    PInitFile();
    FilePushDir();
    FileSetStandardPath(SP_PRIVATE_DATA);
    fh = FileCreate(backupName, FCF_NATIVE | FILE_CREATE_TRUNCATE |
				FILE_ACCESS_RW | FILE_DENY_RW,
		    FILE_ATTR_NORMAL);
    if (fh) {
	ok = FileWrite(fh, ini[0].IT_text, ini[0].IT_len, FALSE) ==
	     ini[0].IT_len;
	FileClose(fh, FALSE);
    }
    FilePopDir();
    VInitFile();
    return !ok;
}

Boolean InitFileRevert(void)
{
    IniText it;
    Boolean ok;

    PInitFile();
    FilePushDir();
    FileSetStandardPath(SP_PRIVATE_DATA);
    ok = ReadIniFile(backupName, FILE_ACCESS_R | FILE_DENY_RW, &it);
    if (ok) {
	FreeText(&ini[0]);
	ini[0] = it;
    }
    FilePopDir();
    VInitFile();
    return !ok;
}

/*
 * InitFileGrab: the init file routines to this thread alone, on the
 * init file of `size' bytes in `mem' (and the file fh for
 * InitFileCommit), until InitFileRelease.  Non-zero if it is no init
 * file (control characters other than tab and line ends).
 */
word InitFileGrab(MemHandle mem, FileHandle fh, word size)
{
    const byte *p;
    IniText it;
    word i;
    Boolean ok = TRUE;

    PInitFile();
    p = MemLock(mem);
    for (i = 0; i < size && p[i] != INI_EOF; i++)
	if (p[i] < ' ' && p[i] != '\t' && p[i] != '\r' && p[i] != '\n')
	    ok = FALSE;
    ok = ok && !grabbed && SetText(&it, (const char *) p, size);
    MemUnlock(mem);
    if (!ok) {
	VInitFile();
	return 1;
    }
    grabSaved = ini[0];
    ini[0] = it;
    grabMem = mem;
    grabFile = fh;
    grabbed = TRUE;
    return 0;		/* the lock stays with this thread */
}

/* the text back into the grabbed block (with a C_CTRL_Z), the init file
 * as before */
void InitFileRelease(void)
{
    char *p;

    if (!grabbed || iniOwner != KernelThisThread())
	return;
    if (MemReAlloc(grabMem, (word) (ini[0].IT_len + 1), HAF_STANDARD)) {
	p = MemLock(grabMem);
	memcpy(p, ini[0].IT_text, (size_t) ini[0].IT_len + 1);
	MemUnlock(grabMem);
    }
    FreeText(&ini[0]);
    ini[0] = grabSaved;
    grabbed = FALSE;
    grabMem = 0;
    grabFile = 0;
    VInitFile();
}

/*
 * InitFileMakeCanonicKeyCategory: src as a category or key name: what
 * could be read as syntax (control characters, ";<=>", "[\]", "{" and
 * up) as two hex digits.
 */
void InitFileMakeCanonicKeyCategory(char *keyCat, const TCHAR *src)
{
    static const char nibbles[] = "0123456789ABCDEF";
    byte c;

    while ((c = (byte) *src++) != 0) {
	if (c < ' ' || (c >= ';' && c <= '>') || (c >= '[' && c <= ']') ||
	    c >= '{') {
	    *keyCat++ = nibbles[c >> 4];
	    *keyCat++ = nibbles[c & 15];
	} else
	    *keyCat++ = (char) c;
    }
    *keyCat = 0;
}
