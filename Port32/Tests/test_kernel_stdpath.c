/*
 * test_kernel_stdpath.c -- standard paths spread over several
 * directories by [paths] of the init file (File/filePath.c), on every
 * host.
 *
 * The top directory, and a second HostFS drive D with directories that
 * [paths] adds to DOCUMENT (two of them), to the top (so D:\NET\SYSTEM
 * belongs to SYSTEM) and an entry on an unknown drive.  Checked: files
 * found at the first place that has them (the local one first), a
 * current path that exists only at a later place, FileEnum over all
 * places with names hidden by earlier ones, new files in the first place
 * whose directory exists, deleting and renaming where the file is,
 * FileCopy and FileCopyLocal, VMOpen, and the [paths] keys.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <initfile.h>
#include <fileEnum.h>
#include <vm.h>

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

/* the host path of `rel' ('/' separated) below the top */
static const char *H(const char *rel)
{
    static char buf[2][400];
    static int which;
    char *p = buf[which ^= 1];

    sprintf(p, "%s%c%s", top, HostFSSep(), rel);
    for (; *p; p++)
	if (*p == '/')
	    *p = HostFSSep();
    return buf[which];
}

static void Dir(const char *rel)
{
    CHECK(ghal_fs_mkdir(H(rel)) == GHAL_OK);
}

static void Put(const char *rel, const char *text)
{
    ghal_file *f;
    size_t done;

    CHECK(ghal_fs_open(H(rel), GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, text, strlen(text), &done);
    ghal_fs_close(f);
}

static Boolean Exists(const char *rel)
{
    ghal_fs_info info;

    return ghal_fs_stat(H(rel), &info) == GHAL_OK;
}

/* the text of the GEOS file `name' ("" if it cannot be opened) */
static const char *Get(const char *name)
{
    static char buf[40];
    FileHandle fh = FileOpen(name, FILE_ACCESS_R | FILE_DENY_NONE);
    word n;

    buf[0] = 0;
    if (fh) {
	n = FileRead(fh, buf, sizeof(buf) - 1, FALSE);
	buf[n] = 0;
	FileClose(fh, FALSE);
    }
    return buf;
}

static void Upper(char *s)
{
    for (; *s; s++)
	if (*s >= 'a' && *s <= 'z')
	    *s = (char) (*s - 'a' + 'A');
}

/* the names in the current directory, sorted, upper case */
static word Names(char *out)
{
    FileEnumParams p;
    MemHandle mh = 0;
    word n, i, j, nf;
    FileLongName *e;
    char names[16][FILE_LONGNAME_BUFFER_SIZE];

    memset(&p, 0, sizeof(p));
    p.FEP_searchFlags = FESF_NON_GEOS | FESF_DIRS;
    p.FEP_returnAttrs = (void *) FESRT_NAME;
    p.FEP_returnSize = sizeof(FileLongName);
    p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
    n = FileEnum(&p, &mh, &nf);
    *out = 0;
    if (n == (word) -1)
	return n;
    e = mh ? MemLock(mh) : NULL;
    for (i = 0; i < n && i < 16; i++) {
	strcpy(names[i], e[i]);
	Upper(names[i]);
	for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
	    char x[FILE_LONGNAME_BUFFER_SIZE];
	    strcpy(x, names[j]);
	    strcpy(names[j], names[j - 1]);
	    strcpy(names[j - 1], x);
	}
    }
    for (i = 0; i < n && i < 16; i++) {
	if (i)
	    strcat(out, "|");
	strcat(out, names[i]);
    }
    if (mh)
	MemFree(mh);
    return n;
}

int main(int argc, char **argv)
{
    char names[400], *slash;
    dword base;
    FileHandle fh;
    VMFileHandle vf;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GSTOP");
    else
	strcpy(top, "GSTOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);

    Dir("DOCUMENT");
    Put("DOCUMENT/local.txt", "local");
    Put("DOCUMENT/both.txt", "local both");
    Dir("DDRIVE");
    Dir("DDRIVE/DOCS");
    Put("DDRIVE/DOCS/remote.txt", "remote");
    Put("DDRIVE/DOCS/both.txt", "remote both");
    Dir("DDRIVE/DOCS/SUB");
    Put("DDRIVE/DOCS/SUB/deep.txt", "deep");
    Dir("DDRIVE/MOREDOCS");
    Put("DDRIVE/MOREDOCS/more.txt", "more");
    Dir("DDRIVE/NET");
    Dir("DDRIVE/NET/DOCUMENT");
    Put("DDRIVE/NET/DOCUMENT/net.txt", "net");
    Dir("DDRIVE/NET/SYSTEM");
    Put("DDRIVE/NET/SYSTEM/sys.txt", "sys");
    Put("geos.ini",
	"[paths]\r\n"
	"document = {\r\n"
	"D:\\DOCS\r\n"
	"   D:\\MOREDOCS\r\n"
	"}\r\n"
	"top = D:\\NET\r\n"
	"systemfs = Q:\\NOWHERE\r\n");

    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    CHECK(FSDInitDrive(3, "D", DS_PRESENT | DRIVE_FIXED, MEDIA_FIXED_DISK,
		       &hostFSDriver, HostFSDriveData(H("DDRIVE"))));
    FileInitStdPaths();
    base = HandleCountUsed();

    /* found at the first place that has it */
    FileSetStandardPath(SP_DOCUMENT);
    CHECK(!strcmp(Get("local.txt"), "local"));
    CHECK(!strcmp(Get("both.txt"), "local both"));
    CHECK(!strcmp(Get("remote.txt"), "remote"));
    CHECK(!strcmp(Get("more.txt"), "more"));
    CHECK(!strcmp(Get("net.txt"), "net"));
    CHECK(!strcmp(Get("SUB\\deep.txt"), "deep"));
    CHECK(!FileOpen("nothere.txt", FILE_ACCESS_R) &&
	  ThreadGetError() == ERROR_FILE_NOT_FOUND);
    CHECK((FileGetAttributes("remote.txt") & ~FA_ARCHIVE) == 0 &&
	  ThreadGetError() == 0);
    CHECK(!strcmp(Get("\\SYSTEM\\sys.txt"), "sys"));
    CHECK(!strcmp(Get("D:\\DOCS\\remote.txt"), "remote"));

    /* all places, both.txt once */
    CHECK(Names(names) == 6 &&
	  !strcmp(names, "BOTH.TXT|LOCAL.TXT|MORE.TXT|NET.TXT|REMOTE.TXT|SUB"));

    /* a current path only at a later place */
    CHECK(FileSetCurrentPath(SP_DOCUMENT, "SUB") == SP_DOCUMENT);
    CHECK(!strcmp(Get("deep.txt"), "deep"));
    CHECK(Names(names) == 1 && !strcmp(names, "DEEP.TXT"));
    CHECK(FileSetCurrentPath(SP_DOCUMENT, "NOSUCH") == 0 &&
	  ThreadGetError() == ERROR_PATH_NOT_FOUND);
    CHECK(Names(names) == 1);		/* still in SUB */

    /* new files: the first place whose directory exists */
    fh = FileCreate("new2.txt", FCF_NATIVE | FILE_CREATE_ONLY |
		    FILE_ACCESS_RW, 0);
    CHECK(fh != 0);
    if (fh)
	FileClose(fh, FALSE);
    CHECK(Exists("DDRIVE/DOCS/SUB/new2.txt"));
    FileSetStandardPath(SP_DOCUMENT);
    fh = FileCreate("new.txt", FCF_NATIVE | FILE_CREATE_ONLY |
		    FILE_ACCESS_RW, 0);
    CHECK(fh != 0);
    if (fh)
	FileClose(fh, FALSE);
    CHECK(Exists("DOCUMENT/new.txt"));
    CHECK(FileCreateDir("SUB\\NEWDIR") == 0 &&
	  Exists("DDRIVE/DOCS/SUB/NEWDIR"));

    /* deleting and renaming where the file is */
    CHECK(FileDelete("remote.txt") == 0 && !Exists("DDRIVE/DOCS/remote.txt"));
    CHECK(FileRename("more.txt", "more2.txt") == 0 &&
	  Exists("DDRIVE/MOREDOCS/more2.txt"));
    CHECK(FileDelete("both.txt") == 0 && Exists("DDRIVE/DOCS/both.txt"));
    CHECK(!strcmp(Get("both.txt"), "remote both"));

    /* copies: FileCopy finds the source, FileCopyLocal does not */
    CHECK(FileCopy("net.txt", "copy.txt", 0, 0) == 0 &&
	  Exists("DOCUMENT/copy.txt"));
    CHECK(FileCopyLocal("net.txt", "copy2.txt", 0, 0) ==
	  ERROR_FILE_NOT_FOUND);

    /* VM files too */
    FileSetCurrentPath(SP_DOCUMENT, "SUB");
    vf = VMOpen("vmfile", VMAF_FORCE_READ_WRITE, VMO_CREATE_ONLY, 0);
    CHECK(vf != 0);
    /* a GEOS file: its host name may be a DOS name */
    FileGetAttributes("D:\\DOCS\\SUB\\vmfile");
    CHECK(ThreadGetError() == 0);
    if (vf)
	VMClose(vf, FALSE);
    FileSetStandardPath(SP_DOCUMENT);
    vf = VMOpen("SUB\\vmfile", VMAF_FORCE_READ_WRITE, VMO_OPEN, 0);
    CHECK(vf != 0);
    if (vf)
	VMClose(vf, FALSE);

    /* SYSTEM gets D:\NET\SYSTEM from "top"; FS a place on an unknown
     * drive, and no directory at the others */
    FileSetStandardPath(SP_SYSTEM);
    CHECK(!strcmp(Get("sys.txt"), "sys"));
    CHECK(!FileOpen("nothere.txt", FILE_ACCESS_R) &&
	  ThreadGetError() == ERROR_FILE_NOT_FOUND);
    FileSetStandardPath(SP_FILE_SYSTEM_DRIVERS);
    CHECK(!FileOpen("sys.txt", FILE_ACCESS_R) &&
	  ThreadGetError() == ERROR_PATH_NOT_FOUND);
    CHECK(Names(names) == 0);

    FileSetStandardPath(SP_TOP);
    CHECK(FileDelete("geos.ini") == 0);
    InitFileReset();
    FileInitStdPaths();		/* no [paths]: only the top */
    FileSetStandardPath(SP_DOCUMENT);
    CHECK(!strcmp(Get("net.txt"), ""));
    FileSetStandardPath(SP_TOP);
    CHECK(HandleCountUsed() == base);
    RemoveTree(top);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
