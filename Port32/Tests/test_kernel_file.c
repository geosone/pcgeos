/*
 * test_kernel_file.c -- the file routines of the C kernel
 * (Library/Kernel/File: filePath.c, fileOpenClose.c, fileIO.c,
 * fileFile.c) on ghal_fs, on every host.
 *
 * The GEOS top directory is GFTOP next to the program (on DOS the
 * program runs from a writable disk).  Checked: standard paths and
 * their directories, current paths (relative, absolute, "..", push and
 * pop, taken over by a new thread), create/open/read/write/seek/
 * truncate/size/close, duplicated handles sharing the position, date
 * and time, attributes, rename, directories, and the error codes.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static jmp_buf catcher;
static KernelFatalError caught;

static void Catch(KernelFatalError code)
{
    caught = code;
    longjmp(catcher, 1);
}

static char top[260];

static const char *H(const char *rel)
{
    static char buf[4][300];
    static int n;
    char *b = buf[n++ & 3], *p;

    sprintf(b, "%s/%s", top, rel);
#ifdef GEOS_HOST_DOS32
    for (p = b; *p; p++)
	if (*p == '/')
	    *p = '\\';
#else
    (void) p;
#endif
    return b;
}

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

/* everything below the top (GEOS files have generated names on DOS) */
static void Cleanup(void)
{
    RemoveTree(top);
}

static SemaphoreHandle done;
static DiskHandle childDisk;
static char childPath[64];

static word Child(word v)
{
    (void) v;
    childDisk = FileGetCurrentPath(childPath, sizeof(childPath));
    ThreadVSem(done);
    return 0;
}

int main(int argc, char **argv)
{
    FileHandle f, g;
    char buf[64], *slash;
    dword base;
    FileDateAndTime fdat;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);	/* all of it, should DOS crash */
    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GFTOP");
    else
	strcpy(top, "GFTOP");
    Cleanup();
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    base = HandleCountUsed();

    /* the first thread starts at the top */
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) == (DiskHandle) SP_TOP &&
	  buf[0] == 0);

    /* standard paths: the directories of DOS16 GEOS */
    CHECK(FileCreateDir("DOCUMENT") == 0);
    CHECK(FileCreateDir("\\SYSTEM") == 0);
    CHECK(FileCreateDir("SYSTEM\\FS") == 0);
    FileSetStandardPath(SP_FILE_SYSTEM_DRIVERS);
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) ==
	  (DiskHandle) SP_FILE_SYSTEM_DRIVERS && buf[0] == 0);
    f = FileCreate("D.TXT", FCF_NATIVE | FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f != 0 && ThreadGetError() == 0);
    CHECK(FileClose(f, FALSE) == 0);
    CHECK(ghal_fs_stat(H("SYSTEM/FS/D.TXT"), NULL) != GHAL_ERR_NOTFOUND);
    /* a standard path need not exist */
    FileSetStandardPath(SP_FONT);
    CHECK(FileGetCurrentPath(NULL, 0) == (DiskHandle) SP_FONT);
    CHECK(FileOpen("X.FNT", FILE_ACCESS_R) == 0 &&
	  ThreadGetError() == ERROR_PATH_NOT_FOUND);

    /* current paths */
    CHECK(FileSetCurrentPath((DiskHandle) SP_DOCUMENT, "") ==
	  (DiskHandle) SP_DOCUMENT);
    CHECK(FileCreateDir("SUB") == 0);
    CHECK(FileCreateDir("SUB") == ERROR_FILE_EXISTS ||
	  FileCreateDir("SUB") == ERROR_ACCESS_DENIED);
    CHECK(FileSetCurrentPath(0, "SUB") == (DiskHandle) SP_DOCUMENT);
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) == (DiskHandle) SP_DOCUMENT &&
	  !strcmp(buf, "SUB"));
    CHECK(FileSetCurrentPath(0, "NONE") == 0 &&
	  ThreadGetError() == ERROR_PATH_NOT_FOUND);
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) == (DiskHandle) SP_DOCUMENT &&
	  !strcmp(buf, "SUB"));
    FilePushDir();
    /* above the standard path: a path below the top */
    CHECK(FileSetCurrentPath(0, "..\\..\\SYSTEM") == (DiskHandle) SP_TOP);
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) == (DiskHandle) SP_TOP &&
	  !strcmp(buf, "SYSTEM"));
    CHECK(FileSetCurrentPath(0, "..\\..") == 0 &&
	  ThreadGetError() == ERROR_PATH_NOT_FOUND);
    CHECK(FileSetCurrentPath((DiskHandle) SP_DOCUMENT, "\\") ==
	  (DiskHandle) SP_TOP);
    CHECK(FileSetCurrentPath((DiskHandle) 0x1230, "") == 0 &&
	  ThreadGetError() == ERROR_INVALID_VOLUME);
    FilePopDir();
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) == (DiskHandle) SP_DOCUMENT &&
	  !strcmp(buf, "SUB"));
    caught = KFE_NONE;
    if (setjmp(catcher) == 0)
	FilePopDir();
    CHECK(caught == FILE_POP_DIR_WITHOUT_PUSH);

    /* a new thread starts with its creator's path */
    done = ThreadAllocSem(0);
    CHECK(ThreadCreate(PRIORITY_STANDARD, 0, Child, 512, 0x2340) != 0);
    ThreadPSem(done);
    CHECK(childDisk == (DiskHandle) SP_DOCUMENT && !strcmp(childPath, "SUB"));

    /* files: create, write, read, positions */
    f = FileCreate("C.TXT", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f != 0);
    CHECK(FileWrite(f, "0123456789", 10, FALSE) == 10 &&
	  ThreadGetError() == 0);
    CHECK(FileSize(f) == 10);
    CHECK(FilePos(f, 2, FILE_POS_START) == 2);
    CHECK(FileRead(f, buf, 3, FALSE) == 3 && !memcmp(buf, "234", 3));
    CHECK(FilePos(f, (dword) -2, FILE_POS_RELATIVE) == 3);
    CHECK(FilePos(f, (dword) -1, FILE_POS_END) == 9);
    CHECK(FileRead(f, buf, 5, FALSE) == 1 &&
	  ThreadGetError() == ERROR_SHORT_READ_WRITE && buf[0] == '9');
    /* a duplicate shares the position */
    g = FileDuplicateHandle(f);
    CHECK(g != 0 && g != f);
    CHECK(FilePos(g, 4, FILE_POS_START) == 4);
    CHECK(FileRead(f, buf, 1, FALSE) == 1 && buf[0] == '4');
    CHECK(FileClose(g, FALSE) == 0);
    CHECK(FileTruncate(f, 6, FALSE) == 0 && FileSize(f) == 6 &&
	  FilePos(f, 0, FILE_POS_RELATIVE) == 6);
    CHECK(FileCommit(f, FALSE) == 0);
    fdat = (dword) 12 << FDAT_HOUR_OFFSET | (dword) 34 << FDAT_MINUTE_OFFSET |
	   (dword) 28 << FDAT_2SECOND_OFFSET |
	   (dword) (2005 - FDAT_BASE_YEAR) << FDAT_YEAR_OFFSET |
	   (dword) 6 << FDAT_MONTH_OFFSET | (dword) 7 << FDAT_DAY_OFFSET;
    CHECK(FileSetDateAndTime(f, fdat) == 0);
    CHECK(FileGetDateAndTime(f) == fdat);
    CHECK(FileClose(f, FALSE) == 0);
    CHECK(FileCreate("C.TXT", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0) == 0 &&
	  ThreadGetError() == ERROR_FILE_EXISTS);
    f = FileCreate("C.TXT", FILE_CREATE_NO_TRUNCATE | FILE_ACCESS_W, 0);
    CHECK(f && FileSize(f) == 6 && FileClose(f, FALSE) == 0);
    f = FileOpen("\\DOCUMENT\\SUB\\C.TXT", FILE_ACCESS_R | FILE_DENY_W);
    CHECK(f && FileRead(f, buf, 6, FALSE) == 6 && !memcmp(buf, "012345", 6));
    CHECK(FileWrite(f, "x", 1, FALSE) == 0 && ThreadGetError() != 0);
    CHECK(FileClose(f, FALSE) == 0);
    f = FileCreate("C.TXT", FILE_CREATE_TRUNCATE | FILE_ACCESS_RW, 0);
    CHECK(f && FileSize(f) == 0 && FileClose(f, FALSE) == 0);
    CHECK(FileOpen("NONE.TXT", FILE_ACCESS_R) == 0 &&
	  ThreadGetError() == ERROR_FILE_NOT_FOUND);
    CHECK(FileOpen("NONE\\X.TXT", FILE_ACCESS_R) == 0 &&
	  ThreadGetError() == ERROR_PATH_NOT_FOUND);

    /* by name: attributes, rename, delete, directories */
    CHECK(FileSetCurrentPath((DiskHandle) SP_DOCUMENT, "") != 0);
    f = FileCreate("RO.TXT", FILE_CREATE_ONLY | FILE_ACCESS_RW, FA_RDONLY);
    CHECK(f && FileWrite(f, "ro", 2, FALSE) == 2 && FileClose(f, FALSE) == 0);
    CHECK((FileGetAttributes("RO.TXT") & FA_RDONLY) && ThreadGetError() == 0);
    CHECK(FileOpen("RO.TXT", FILE_ACCESS_W) == 0 &&
	  ThreadGetError() == ERROR_ACCESS_DENIED);
    CHECK(FileSetAttributes("RO.TXT", 0) == 0 &&
	  !(FileGetAttributes("RO.TXT") & FA_RDONLY));
    CHECK(FileSetAttributes("RO.TXT", FA_SUBDIR) == ERROR_ACCESS_DENIED);
    CHECK(FileGetAttributes("SUB") & FA_SUBDIR);
    CHECK(FileGetAttributes("NONE") == 0 &&
	  ThreadGetError() == ERROR_FILE_NOT_FOUND);
    CHECK(FileRename("SUB\\C.TXT", "A.TXT") == 0);	/* stays in SUB */
    f = FileOpen("SUB\\A.TXT", FILE_ACCESS_R);
    CHECK(f && FileClose(f, FALSE) == 0);
    CHECK(FileRename("RO.TXT", "SUB\\A.TXT") == ERROR_FILE_EXISTS);
    CHECK(FileRename("SUB\\A.TXT", "\\DOCUMENT\\B.TXT") == 0);
    CHECK(FileDelete("B.TXT") == 0);
    CHECK(FileDelete("B.TXT") == ERROR_FILE_NOT_FOUND);
    CHECK(FileDelete("SUB") == ERROR_ACCESS_DENIED);
    CHECK(FileCreateDir("SUB\\DEEP") == 0);
    CHECK(FileDeleteDir("SUB") == ERROR_DIRECTORY_NOT_EMPTY);
    CHECK(FileSetCurrentPath(0, "SUB\\DEEP") != 0);
    CHECK(FileDeleteDir(".") == ERROR_IS_CURRENT_DIRECTORY);
    CHECK(FileSetCurrentPath(0, "..\\..") == (DiskHandle) SP_DOCUMENT);
    CHECK(FileDeleteDir("SUB\\DEEP") == 0 && FileDeleteDir("SUB") == 0);
    CHECK(FileDelete("RO.TXT") == 0);

    /* every handle is gone again (the child thread's too) */
    f = FileOpen("\\SYSTEM\\FS\\D.TXT", FILE_ACCESS_R);
    CHECK(f != 0);
    FileFreeHandle(f);
    ThreadFreeSem(done);
    CHECK(HandleCountUsed() == base);

    Cleanup();
    CHECK(ghal_fs_stat(top, NULL) == GHAL_ERR_NOTFOUND);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
