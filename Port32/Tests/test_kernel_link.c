/*
 * test_kernel_link.c -- links (Library/Kernel/File/fileLink.c, the link
 * following of filePath.c and the link records of the HostFS driver),
 * on every host.
 *
 * The GEOS top directory is GLTOP next to the program.  Checked: links
 * to a directory and to a file by a standard path, and to a directory by
 * a disk; opening and entering through them; the attributes a link gets
 * from its target (FEA_TARGET_FILE_ID); reading a link, its extra data,
 * renaming and deleting it; FileEnum and FileConstructActualPath; a loop
 * of links; and the DOS16 record in @DIRNAME.000.
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

static void Put(const char *name, const char *data)
{
    FileHandle f = FileCreate(name, FILE_CREATE_TRUNCATE | FILE_ACCESS_RW, 0);

    CHECK(f && FileWrite(f, data, (word) strlen(data), FALSE) ==
	  (word) strlen(data) && FileClose(f, FALSE) == 0);
}

static Boolean Has(const char *name, const char *data)
{
    char buf[64];
    FileHandle f = FileOpen(name, FILE_ACCESS_R);
    word n;

    if (!f)
	return FALSE;
    n = FileRead(f, buf, sizeof(buf) - 1, FALSE);
    FileClose(f, FALSE);
    buf[n == 0xffff ? 0 : n] = 0;
    return !strcmp(buf, data);
}

/* the names in the current directory, "|" between, and the attributes
 * of `which' */
static word List(char *out, const char *which, FileAttrs *attrs)
{
    FileEnumParams p;
    MemHandle mh = 0;
    FENameAndAttr *e;
    word n, i;

    memset(&p, 0, sizeof(p));
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_DIR_TYPES;
    p.FEP_returnAttrs = (FileExtAttrDesc *) FESRT_NAME_AND_ATTR;
    p.FEP_returnSize = sizeof(FENameAndAttr);
    p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
    n = FileEnum(&p, &mh, NULL);
    out[0] = 0;
    if (!mh)
	return n;
    e = MemLock(mh);
    for (i = 0; i < n; i++) {
	if (i)
	    strcat(out, "|");
	strcat(out, e[i].FENAA_name);
	if (!strcmp(e[i].FENAA_name, which))
	    *attrs = e[i].FENAA_attr;
    }
    MemUnlock(mh);
    MemFree(mh);
    return n;
}

int main(int argc, char **argv)
{
    char buf[PATH_BUFFER_SIZE], names[300], *slash, *p;
    FileHandle f;
    DiskHandle disk;
    FileAttrs a = 0;
    GeodeToken token = {{'L', 'N', 'K', 'T'}, 7}, t2;
    dword id = 0, tid = 0;
    word type = 0;
    dword base;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GLTOP");
    else
	strcpy(top, "GLTOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    base = HandleCountUsed();

    /* targets: DOCUMENT\TARGET\IN.TXT and DOCUMENT\Data File */
    CHECK(FileCreateDir("DOCUMENT") == 0 && FileCreateDir("SYSTEM") == 0);
    FileSetStandardPath(SP_DOCUMENT);
    CHECK(FileCreateDir("TARGET") == 0);
    Put("TARGET\\IN.TXT", "inside");
    f = FileCreate("Data File", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileWrite(f, "data", 4, FALSE) == 4 &&
	  FileSetHandleExtAttributes(f, FEA_TOKEN, &token, 6) == 0 &&
	  FileClose(f, FALSE) == 0);
    CHECK(FileGetPathExtAttributes("Data File", FEA_FILE_ID, &tid, 4) == 0);
    FileSetStandardPath(SP_TOP);

    /* a link to a directory, by a standard path */
    CHECK(FileCreateLink("Doc Link", SP_DOCUMENT, "TARGET", 0) == 0);
    a = FileGetAttributes("Doc Link");
    CHECK((a & FA_LINK) && (a & FA_SUBDIR));
    CHECK(Has("Doc Link\\IN.TXT", "inside"));
    CHECK(FileCreateLink("Doc Link", SP_DOCUMENT, "TARGET", 0) ==
	  ERROR_FILE_EXISTS);
    CHECK(FileReadLink("Doc Link", buf) == SP_DOCUMENT &&
	  !strcmp(buf, "TARGET"));
    CHECK(FileReadLink("DOCUMENT", buf) == 0 &&
	  ThreadGetError() == ERROR_NOT_A_LINK);
    /* entering it makes the target the current path */
    CHECK(FileSetCurrentPath(0, "Doc Link") == SP_DOCUMENT);
    CHECK(FileGetCurrentPath(buf, sizeof(buf)) == SP_DOCUMENT &&
	  !strcmp(buf, "TARGET") && Has("IN.TXT", "inside"));
    FileSetStandardPath(SP_TOP);
    /* a new file through it lands in the target */
    Put("Doc Link\\NEW.TXT", "new");
    FileSetStandardPath(SP_DOCUMENT);
    CHECK(Has("TARGET\\NEW.TXT", "new") && FileDelete("TARGET\\NEW.TXT") == 0);
    FileSetStandardPath(SP_TOP);
    p = buf;
    CHECK(FileConstructActualPath(&p, sizeof(buf), SP_TOP,
				  "Doc Link\\IN.TXT", TRUE) != 0 &&
	  !strcmp(buf, "C:\\DOCUMENT\\TARGET\\IN.TXT"));

    /* a link to a GEOS file: its attributes are the target's */
    CHECK(FileCreateLink("File Link", SP_DOCUMENT, "Data File", 0) == 0);
    CHECK(Has("File Link", "data"));
    CHECK(FileGetPathExtAttributes("File Link", FEA_TOKEN, &t2, 6) == 0 &&
	  !memcmp(&t2, &token, 6));
    CHECK(FileGetPathExtAttributes("File Link", FEA_FILE_TYPE, &type, 2) ==
	  0 && type == GFT_DATA);
    CHECK(FileGetPathExtAttributes("File Link", FEA_TARGET_FILE_ID, &id, 4) ==
	  0 && id == tid);
    CHECK(FileGetPathExtAttributes("File Link", FEA_FILE_ID, &id, 4) == 0 &&
	  id != tid);
    a = FileGetAttributes("File Link");
    CHECK((a & FA_LINK) && !(a & FA_SUBDIR));

    /* extra data; setting it moves the link to the end */
    CHECK(FileSetLinkExtraData("File Link", "extra", 5) == 0);
    memset(names, 0, sizeof(names));
    CHECK(FileGetLinkExtraData("File Link", names, sizeof(names)) == 0 &&
	  !strcmp(names, "extra"));
    CHECK(Has("File Link", "data") && Has("Doc Link\\IN.TXT", "inside"));
    CHECK(FileGetLinkExtraData("DOCUMENT", names, 4) == ERROR_NOT_A_LINK);

    /* the links are entries of the directory (after the files, before
     * the standard paths the top has without directories) */
    CHECK(List(names, "Doc Link", &a) == 8 &&
	  !strncmp(names, "DOCUMENT|SYSTEM|Doc Link|File Link|", 35) &&
	  (a & FA_LINK) && (a & FA_SUBDIR));

    /* renamed, also into a directory */
    CHECK(FileRename("File Link", "Renamed") == 0 && Has("Renamed", "data"));
    CHECK(FileGetAttributes("File Link") == 0 &&
	  ThreadGetError() == ERROR_FILE_NOT_FOUND);
    CHECK(FileRename("Renamed", "Doc Link") == ERROR_FILE_EXISTS);
    CHECK(FileRename("Renamed", "SYSTEM\\Moved") == 0 &&
	  Has("SYSTEM\\Moved", "data") && FileDelete("SYSTEM\\Moved") == 0);
    CHECK(FileDeleteDir("SYSTEM") == 0);
    CHECK(FileCreateDir("SYSTEM") == 0);
    CHECK(FileCreateLink("SYSTEM\\Keep", SP_DOCUMENT, "Data File", 0) == 0);
    CHECK(FileDeleteDir("SYSTEM") == ERROR_DIRECTORY_NOT_EMPTY);
    CHECK(FileDelete("SYSTEM\\Keep") == 0 && FileDeleteDir("SYSTEM") == 0);

    /* a link by a disk; a loop of links */
    disk = DiskRegisterDisk(2);
    CHECK(FileCreateLink("Disk Link", disk, "\\DOCUMENT", 0) == 0 &&
	  Has("Disk Link\\Data File", "data"));
    CHECK(FileReadLink("Disk Link", buf) == disk &&
	  !strcmp(buf, "\\DOCUMENT"));
    CHECK(FileCreateLink("Loop", SP_TOP, "Loop", 1) == 0);
    CHECK(FileOpen("Loop\\X", FILE_ACCESS_R) == 0 &&
	  ThreadGetError() == ERROR_TOO_MANY_LINKS);

    /* the record in the top's @DIRNAME.000, as DOS16 wrote it */
    {
	char host[300];
	unsigned char rec[113];
	ghal_file *hf;
	size_t done = 0;

	sprintf(host, "%s%c%s", top, HostFSSep(), HOSTFS_DIRNAME_FILE);
	CHECK(ghal_fs_open(host, GHAL_FS_READ, &hf) == GHAL_OK);
	ghal_fs_seek(hf, GFH_SIZE, GHAL_FS_SEEK_SET, NULL);
	CHECK(ghal_fs_read(hf, rec, sizeof(rec), &done) == GHAL_OK &&
	      done == sizeof(rec) && !memcmp(rec, "LN00Doc Link", 12) &&
	      rec[84] == (FA_LINK | FA_SUBDIR) &&	/* DLH_fileAttr */
	      rec[107] == 3 && rec[109] == 6);	/* the sizes */
	ghal_fs_close(hf);
    }

    /* deleting a link leaves the target */
    CHECK(FileDelete("Doc Link") == 0 && FileDelete("Disk Link") == 0 &&
	  FileDelete("Loop") == 0);
    FileSetStandardPath(SP_DOCUMENT);
    CHECK(Has("TARGET\\IN.TXT", "inside"));
    FileSetStandardPath(SP_TOP);
    CHECK(List(names, "", &a) == 6 && !strstr(names, "Link") &&
	  !strstr(names, "Loop"));

    CHECK(HandleCountUsed() == base);
    RemoveTree(top);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
