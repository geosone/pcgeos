/*
 * test_kernel_fileenum.c -- FileEnum of the C kernel (File/fileEnum.c)
 * on the HostFS driver, on every host, with long names and (not on DOS,
 * where they are the only ones) DOS names.
 *
 * Checked: the standard return types, return attributes of the caller
 * with a header, the search flags, match attributes (bits, strings), the
 * wildcard callback with and without case, a callback of the caller with
 * FileEnumLocateAttr, the buffer limit and the files that did not fit,
 * the skip count and the real skip count, counting only, the standard
 * subdirectories of a standard path, and that @DIRNAME.000 is no entry.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <fileEnum.h>

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260];

static void RemoveTree(const char *path)
{
    ghal_dir *d;
    ghal_fs_info info;
    char name[260], sub[400];

    if (ghal_fs_opendir(path, &d) == GHAL_OK) {
	while (ghal_fs_readdir(d, name, sizeof(name), &info) == GHAL_OK) {
	    sprintf(sub, "%s%c%s", path, HostFSSep(), name);
	    if (info.attrs & GHAL_FS_ATTR_DIR)
		RemoveTree(sub);
	    else {
		ghal_fs_set_attrs(sub, 0);
		ghal_fs_delete(sub);
	    }
	}
	ghal_fs_closedir(d);
    }
    ghal_fs_rmdir(path);
}

static void Params(FileEnumParams *p, FileEnumSearchFlags flags,
		   const void *ret, word size)
{
    memset(p, 0, sizeof(*p));
    p->FEP_searchFlags = flags;
    p->FEP_returnAttrs = (FileExtAttrDesc *) ret;
    p->FEP_returnSize = size;
    p->FEP_bufSize = FE_BUFSIZE_UNLIMITED;
}

/* the names in a FENameAndAttr buffer, sorted into one string */
static void Names(MemHandle mh, word n, char *out)
{
    FENameAndAttr *e = mh ? MemLock(mh) : NULL;
    const char *names[16];
    word i, j;

    for (i = 0; i < n && i < 16; i++) {
	names[i] = e[i].FENAA_name;
	for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
	    const char *x = names[j];
	    names[j] = names[j - 1];
	    names[j - 1] = x;
	}
    }
    *out = 0;
    for (i = 0; i < n && i < 16; i++) {
	if (i)
	    strcat(out, "|");
	strcat(out, names[i]);
    }
    if (mh)
	MemUnlock(mh);
}

static word Run(FileEnumParams *p, char *names, word *noFit)
{
    MemHandle mh = 0;
    word nf = 0xffff, n = FileEnum(p, &mh, &nf);

    if (names)
	Names(mh, n == 0xffff ? 0 : n, names);
    if (noFit)
	*noFit = nf;
    if (mh)
	MemFree(mh);
    return n;
}

/* no directories, and nothing larger than 4 bytes */
static word NoDirsSmall(FileEnumParams *params, FileEnumCallbackData *fecd,
			word frame)
{
    const byte *a = FileEnumLocateAttr(fecd, FEA_FILE_ATTR, NULL);
    const dword *size = FileEnumLocateAttr(fecd, FEA_SIZE, NULL);

    (void) frame;
    CHECK(params->FEP_cbData1 == 0x1234);
    return a && !(*a & FA_SUBDIR) && size && *size <= 4;
}

static void Checks(const char *dir)
{
    FileEnumParams p;
    FileExtAttrDesc match[3], ret[3];
    char names[400];
    word n, noFit, type = GFT_EXECUTABLE;
    FileHandle f;
    MemHandle mh;

    CHECK(FileSetCurrentPath((DiskHandle) SP_DOCUMENT, "") != 0);
    CHECK(FileCreateDir(dir) == 0);
    CHECK(FileSetCurrentPath(0, dir) != 0);
    f = FileCreate("A.TXT", FILE_CREATE_ONLY | FCF_NATIVE | FILE_ACCESS_RW, 0);
    CHECK(f && FileWrite(f, "abc", 3, FALSE) == 3 && FileClose(f, FALSE) == 0);
    f = FileCreate("Long Name Doc", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileWrite(f, "12345", 5, FALSE) == 5 &&
	  FileClose(f, FALSE) == 0);
    f = FileCreate("App Geode", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileClose(f, FALSE) == 0);
    CHECK(FileSetPathExtAttributes("App Geode", FEA_FILE_TYPE, &type, 2) == 0);
    CHECK(FileCreateDir("Sub Dir") == 0);
    CHECK(FileSetAttributes("A.TXT", FA_RDONLY) == 0);

    /* everything; Sub Dir's @DIRNAME.000 (DOS names) is no entry */
    Params(&p, FILE_ENUM_ALL_FILE_DIR_TYPES, (void *) FESRT_NAME_AND_ATTR, 0);
    p.FEP_returnSize = sizeof(FENameAndAttr);
    CHECK(Run(&p, names, &noFit) == 4 && noFit == 0 &&
	  !strcmp(names, "A.TXT|App Geode|Long Name Doc|Sub Dir"));
    CHECK(ThreadGetError() == 0);
    {
	FENameAndAttr *e;
	word i, dirs = 0;
	n = FileEnum(&p, &mh, NULL);
	e = MemLock(mh);
	for (i = 0; i < n; i++)
	    if (e[i].FENAA_attr & FA_SUBDIR)
		dirs += !strcmp(e[i].FENAA_name, "Sub Dir");
	    else if (!strcmp(e[i].FENAA_name, "A.TXT"))
		CHECK(e[i].FENAA_attr & FA_RDONLY);
	MemUnlock(mh);
	MemFree(mh);
	CHECK(dirs == 1);
    }

    /* the search flags */
    p.FEP_searchFlags = FESF_NON_GEOS;
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "A.TXT"));
    p.FEP_searchFlags = FESF_GEOS_EXECS;
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "App Geode"));
    p.FEP_searchFlags = FESF_GEOS_NON_EXECS;
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "Long Name Doc"));
    p.FEP_searchFlags = FESF_DIRS;
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "Sub Dir"));

    /* match attributes: bits to be set and to be clear, a string */
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_TYPES;
    match[0].FEAD_attr = FEA_FILE_ATTR;
    match[0].FEAD_value = (void *) (uintptr_t) FA_RDONLY;
    match[0].FEAD_size = 1;
    match[1].FEAD_attr = FEA_END_OF_LIST;
    p.FEP_matchAttrs = match;
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "A.TXT"));
    match[0].FEAD_value = (void *) ((uintptr_t) FA_RDONLY << 16);
    CHECK(Run(&p, names, NULL) == 2 &&
	  !strcmp(names, "App Geode|Long Name Doc"));
    match[0].FEAD_attr = FEA_NAME;
    match[0].FEAD_value = "App Geode";
    match[0].FEAD_size = sizeof(FileLongName);
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "App Geode"));
    p.FEP_matchAttrs = NULL;

    /* the wildcard callback, with and without case */
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_DIR_TYPES | FESF_CALLBACK;
    p.FEP_callback = (void *) FESC_WILDCARD;
    p.FEP_cbData1 = (uintptr_t) "*N?me*";
    p.FEP_cbData2 = 0;
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "Long Name Doc"));
    p.FEP_cbData1 = (uintptr_t) "[a-c]*";
    CHECK(Run(&p, names, NULL) == 0);
    p.FEP_cbData2 = 1;
    CHECK(Run(&p, names, NULL) == 2 && !strcmp(names, "A.TXT|App Geode"));
    p.FEP_cbData1 = (uintptr_t) "[^a-c]*";
    CHECK(Run(&p, names, NULL) == 2 &&
	  !strcmp(names, "Long Name Doc|Sub Dir"));
    p.FEP_cbData1 = (uintptr_t) "*.*";
    CHECK(Run(&p, names, NULL) == 1 && !strcmp(names, "A.TXT"));

    /* a callback of the caller, with an attribute of its own */
    ret[0].FEAD_attr = FEA_SIZE;
    ret[0].FEAD_value = 0;
    ret[0].FEAD_size = 4;
    ret[1].FEAD_attr = FEA_END_OF_LIST;
    p.FEP_callback = NoDirsSmall;
    p.FEP_callbackAttrs = ret;
    p.FEP_cbData1 = 0x1234;
    CHECK(Run(&p, names, NULL) == 2 && !strcmp(names, "A.TXT|App Geode"));
    p.FEP_callbackAttrs = NULL;
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_DIR_TYPES;

    /* the buffer limit, the skip count, the real skip count */
    p.FEP_bufSize = 3;
    CHECK(Run(&p, NULL, &noFit) == 3 && noFit == 1);
    p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
    p.FEP_skipCount = 3;
    CHECK(Run(&p, NULL, &noFit) == 1 && noFit == 0);
    {
	char all[400] = "", one[64];
	word i;
	p.FEP_searchFlags = FILE_ENUM_ALL_FILE_DIR_TYPES | FESF_REAL_SKIP;
	p.FEP_bufSize = 1;
	p.FEP_skipCount = 0;
	for (i = 0; i < 4; i++) {
	    word before = p.FEP_skipCount;
	    CHECK(Run(&p, one, &noFit) == 1 && noFit == (word) (3 - i));
	    CHECK(i == 3 || p.FEP_skipCount == before + 1);
	    if (i == 3)
		p.FEP_skipCount = 4;
	    strcat(all, one);
	    strcat(all, "/");
	}
	CHECK(strstr(all, "A.TXT/") && strstr(all, "App Geode/") &&
	      strstr(all, "Long Name Doc/") && strstr(all, "Sub Dir/"));
	CHECK(Run(&p, NULL, &noFit) == 0 && noFit == 0);
	p.FEP_searchFlags = FILE_ENUM_ALL_FILE_DIR_TYPES;
	p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
	p.FEP_skipCount = 0;
    }

    /* counting only */
    p.FEP_returnAttrs = (void *) FESRT_COUNT_ONLY;
    mh = 1;
    CHECK(FileEnum(&p, &mh, &noFit) == 0 && noFit == 4 && mh == 0);

    /* FEDosInfo: the size of the data, the path */
    p.FEP_returnAttrs = (void *) FESRT_DOS_INFO;
    p.FEP_returnSize = sizeof(FEDosInfo);
    p.FEP_searchFlags = FESF_GEOS_NON_EXECS;
    n = FileEnum(&p, &mh, NULL);
    CHECK(n == 1 && mh);
    if (n == 1 && mh) {
	FEDosInfo *e = MemLock(mh);
	CHECK(e->DFIS_fileSize == 5 && !strcmp(e->DFIS_name, "Long Name Doc") &&
	      e->DFIS_pathInfo == DPI_EXISTS_LOCALLY && e->DFIS_modTimeDate);
	MemUnlock(mh);
	MemFree(mh);
    }

    /* attributes of the caller after a header; missing ones are 0 */
    ret[0].FEAD_attr = FEA_FILE_TYPE;
    ret[0].FEAD_value = (void *) 0;
    ret[0].FEAD_size = 2;
    ret[1].FEAD_attr = FEA_TOKEN;
    ret[1].FEAD_value = (void *) 2;
    ret[1].FEAD_size = 6;
    ret[2].FEAD_attr = FEA_END_OF_LIST;
    p.FEP_returnAttrs = ret;
    p.FEP_returnSize = 8;
    p.FEP_searchFlags = FESF_NON_GEOS | FESF_GEOS_EXECS | FESF_LEAVE_HEADER;
    p.FEP_headerSize = 10;
    n = FileEnum(&p, &mh, NULL);
    CHECK(n == 2 && mh && MemGetInfo(mh, MGIT_SIZE) >= 10 + 2 * 8);
    if (n == 2 && mh) {
	byte *b = MemLock(mh), zero[8] = {0};
	word t0 = (word) (b[10] | b[11] << 8), t1 = (word) (b[18] | b[19] << 8);
	CHECK((t0 == GFT_EXECUTABLE && t1 == GFT_NOT_GEOS_FILE) ||
	      (t1 == GFT_EXECUTABLE && t0 == GFT_NOT_GEOS_FILE));
	CHECK(!memcmp(t0 ? b + 20 : b + 12, zero, 6));
	MemUnlock(mh);
	MemFree(mh);
    }

    /* an empty directory: no buffer */
    CHECK(FileSetCurrentPath(0, "Sub Dir") != 0);
    Params(&p, FILE_ENUM_ALL_FILE_DIR_TYPES, (void *) FESRT_NAME, 0);
    p.FEP_returnSize = sizeof(FileLongName);
    mh = 1;
    CHECK(FileEnum(&p, &mh, &noFit) == 0 && mh == 0 && noFit == 0 &&
	  ThreadGetError() == 0);
}

int main(int argc, char **argv)
{
    FileEnumParams p;
    char names[400], *slash;
    dword base;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GETOP");
    else
	strcpy(top, "GETOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    base = HandleCountUsed();

    /* the standard subdirectories of the top, whether they exist or not
     * (DOCUMENT does), each once */
    CHECK(FileCreateDir("\\DOCUMENT") == 0);
    CHECK(FileSetCurrentPath((DiskHandle) SP_TOP, "") != 0);
    Params(&p, FESF_DIRS, (void *) FESRT_NAME_AND_ATTR, 0);
    p.FEP_returnSize = sizeof(FENameAndAttr);
    CHECK(Run(&p, names, NULL) == 6 &&
	  !strcmp(names, "DOCUMENT|DOSROOM|PRIVDATA|SYSTEM|USERDATA|WORLD"));
    CHECK(FileSetCurrentPath((DiskHandle) SP_SYSTEM, "") != 0);
    CHECK(Run(&p, NULL, NULL) == 14);	/* FS, VIDEO, ... */
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_TYPES;
    CHECK(Run(&p, NULL, NULL) == 0 && ThreadGetError() == 0);

    Checks("LONG");
#ifndef GEOS_HOST_DOS32
    {
	int was = HostFSDosNames();
	HostFSSetDosNames(HOSTFS_NAMES_DOS);
	Checks("DOSNAMES");
	HostFSSetDosNames(was);
    }
#endif

    CHECK(FileSetCurrentPath((DiskHandle) SP_TOP, "") != 0);
    CHECK(HandleCountUsed() == base);
    RemoveTree(top);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
