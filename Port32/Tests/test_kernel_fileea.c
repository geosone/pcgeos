/*
 * test_kernel_fileea.c -- GEOS files of the C kernel: the GeosFileHeader
 * at the start of a GEOS file, extended attributes (File/fileEA.c) and
 * the host names of GEOS files and directories (Driver/IFS/HostFS), on
 * every host.
 *
 * Checked: the header of a new GEOS file and the data after it (size,
 * positions, truncation), native files, the attributes by handle, by
 * path and FEA_MULTIPLE with their errors, a VM file of type GFT_VM,
 * DOS names (generated 8.3 names, @DIRNAME.000, renaming, deleting a
 * directory), and, not on dos32 (whose mode depends on DOS), a tree of
 * one kind read with the names of the other and DOS names with LFN.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <vm.h>

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

    sprintf(b, "%s%c%s", top, HostFSSep(), rel);
    for (p = b; *p; p++)
	if (*p == '/')
	    *p = HostFSSep();
    return b;
}

static void RemoveTree(const char *path)
{
    ghal_dir *d;
    ghal_fs_info info;
    char name[HOSTFS_NAME_MAX], sub[400];

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

/* the first bytes of a host file */
static size_t Raw(const char *path, unsigned char *buf, size_t size)
{
    ghal_file *f;
    size_t done = 0;

    if (ghal_fs_open(path, GHAL_FS_READ, &f) != GHAL_OK)
	return 0;
    ghal_fs_read(f, buf, size, &done);
    ghal_fs_close(f);
    return done;
}

/* the host path of a GEOS file (on DOS a generated name) */
static const char *HostOf(const char *geosName)
{
    char dos[16];

    if (FileGetPathExtAttributes(geosName, FEA_DOS_NAME, dos, sizeof(dos)))
	return "";
    return H(dos);
}

static int Exists(const char *path)
{
    return ghal_fs_stat(path, NULL) == GHAL_OK;
}

static void HeaderAndAttributes(void)
{
    FileHandle f;
    unsigned char raw[GFH_SIZE + 16];
    char buf[64];
    word w, type;
    dword d;
    GeodeToken token = {{'T', 'E', 'S', 'T'}, 0x1234};
    GeodeToken t2;
    ProtocolNumber proto = {2, 5}, p2;
    FileExtAttrDesc desc[3];
    FileDateAndTime created;

    /* a new GEOS file: header first, the data after it */
    f = FileCreate("DATA.TXT", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f != 0);
    CHECK(FileWrite(f, "abcdef", 6, FALSE) == 6);
    CHECK(FileSize(f) == 6 && FilePos(f, 0, FILE_POS_RELATIVE) == 6);
    CHECK(FilePos(f, 1, FILE_POS_START) == 1);
    CHECK(FileRead(f, buf, 2, FALSE) == 2 && !memcmp(buf, "bc", 2));
    /* never before the data */
    CHECK(FilePos(f, (dword) -100, FILE_POS_RELATIVE) == 0);
    CHECK(FileRead(f, buf, 1, FALSE) == 1 && buf[0] == 'a');
    CHECK(FileGetHandleExtAttributes(f, FEA_FILE_TYPE, &type, 2) == 0 &&
	  type == GFT_DATA);
    CHECK(FileGetHandleExtAttributes(f, FEA_NAME, buf, sizeof(buf)) == 0 &&
	  !strcmp(buf, "DATA.TXT"));
    CHECK(FileGetHandleExtAttributes(f, FEA_SIZE, &d, 4) == 0 && d == 6);
    CHECK(FileGetHandleExtAttributes(f, FEA_CREATION, &created, 4) == 0 &&
	  created != 0);
    CHECK(FileSetHandleExtAttributes(f, FEA_TOKEN, &token, 6) == 0);
    /* the position is kept */
    CHECK(FilePos(f, 0, FILE_POS_RELATIVE) == 1);
    CHECK(FileTruncate(f, 3, FALSE) == 0 && FileSize(f) == 3);
    CHECK(FileClose(f, FALSE) == 0);
    CHECK(Raw(HostOf("DATA.TXT"), raw, sizeof(raw)) == GFH_SIZE + 3);
    CHECK(HostFSIsHeader(raw) && !strcmp((char *) raw + GFH_LONG_NAME,
					 "DATA.TXT"));
    CHECK(!memcmp(raw + GFH_TOKEN, "TEST\x34\x12", 6));
    CHECK(!memcmp(raw + GFH_SIZE, "abc", 3));

    /* by path, several at once */
    CHECK(FileGetPathExtAttributes("DATA.TXT", FEA_TOKEN, &t2, 6) == 0 &&
	  !memcmp(&t2, &token, 6));
    desc[0].FEAD_attr = FEA_PROTOCOL;
    desc[0].FEAD_value = &proto;
    desc[0].FEAD_size = sizeof(proto);
    desc[0].FEAD_name = NULL;
    desc[1].FEAD_attr = FEA_USER_NOTES;
    desc[1].FEAD_value = (void *) "some notes";
    desc[1].FEAD_size = 11;
    desc[1].FEAD_name = NULL;
    CHECK(FileSetPathExtAttributes("DATA.TXT", FEA_MULTIPLE, desc, 2) == 0);
    desc[0].FEAD_value = &p2;
    desc[1].FEAD_value = buf;
    desc[1].FEAD_size = sizeof(buf);
    desc[2].FEAD_attr = FEA_FLAGS;
    desc[2].FEAD_value = &w;
    desc[2].FEAD_size = 1;		/* too small */
    desc[2].FEAD_name = NULL;
    CHECK(FileGetPathExtAttributes("DATA.TXT", FEA_MULTIPLE, desc, 3) ==
	  ERROR_ATTR_SIZE_MISMATCH);
    CHECK(p2.PN_major == 2 && p2.PN_minor == 5 && !strcmp(buf, "some notes"));
    CHECK(ThreadGetError() == ERROR_ATTR_SIZE_MISMATCH);
    /* strings may be cut */
    CHECK(FileGetPathExtAttributes("DATA.TXT", FEA_USER_NOTES, buf, 5) == 0 &&
	  !strcmp(buf, "some"));
    CHECK(FileSetPathExtAttributes("DATA.TXT", FEA_NAME, "X", 2) ==
	  ERROR_ATTR_CANNOT_BE_SET);
    CHECK(FileSetPathExtAttributes("DATA.TXT", FEA_SIZE, &d, 4) ==
	  ERROR_ATTR_CANNOT_BE_SET);
    CHECK(FileGetPathExtAttributes("DATA.TXT", FEA_OWNER, buf, 32) ==
	  ERROR_ATTR_NOT_SUPPORTED);
    CHECK(FileGetPathExtAttributes("NONE.TXT", FEA_TOKEN, &t2, 6) ==
	  ERROR_FILE_NOT_FOUND);

    /* opened again: a GEOS file, data from 0; truncating keeps the header */
    f = FileOpen("DATA.TXT", FILE_ACCESS_RW);
    CHECK(f && FileSize(f) == 3 && FileRead(f, buf, 3, FALSE) == 3 &&
	  !memcmp(buf, "abc", 3));
    CHECK(FileClose(f, FALSE) == 0);
    f = FileCreate("DATA.TXT", FILE_CREATE_TRUNCATE | FILE_ACCESS_RW, 0);
    CHECK(f && FileSize(f) == 0);
    CHECK(FileGetHandleExtAttributes(f, FEA_TOKEN, &t2, 6) == 0 &&
	  !memcmp(&t2, &token, 6));
    CHECK(FileClose(f, FALSE) == 0);
    /* but not as a native file */
    CHECK(FileCreate("DATA.TXT", FCF_NATIVE | FILE_CREATE_NO_TRUNCATE |
		     FILE_ACCESS_RW, 0) == 0 &&
	  ThreadGetError() == ERROR_FILE_FORMAT_MISMATCH);

    /* a native file has no GEOS attributes */
    f = FileCreate("NATIVE.TXT", FCF_NATIVE | FILE_CREATE_ONLY |
		   FILE_ACCESS_RW, 0);
    CHECK(f && FileWrite(f, "xy", 2, FALSE) == 2);
    CHECK(FileGetHandleExtAttributes(f, FEA_FILE_TYPE, &type, 2) == 0 &&
	  type == GFT_NOT_GEOS_FILE);
    CHECK(FileGetHandleExtAttributes(f, FEA_TOKEN, &t2, 6) ==
	  ERROR_ATTR_NOT_FOUND);
    CHECK(FileSetHandleExtAttributes(f, FEA_TOKEN, &token, 6) ==
	  ERROR_ATTR_NOT_SUPPORTED);
    CHECK(FileClose(f, FALSE) == 0);
    CHECK(Raw(H("NATIVE.TXT"), raw, sizeof(raw)) == 2);
    CHECK(FileGetPathExtAttributes("NATIVE.TXT", FEA_NAME, buf, sizeof(buf))
	  == 0 && !strcmp(buf, "NATIVE.TXT"));
    CHECK(FileCreate("NATIVE.TXT", FILE_CREATE_NO_TRUNCATE | FILE_ACCESS_RW,
		     0) == 0 &&
	  ThreadGetError() == ERROR_FILE_FORMAT_MISMATCH);
    d = 0;
    CHECK(FileGetPathExtAttributes("NATIVE.TXT", FEA_SIZE, &d, 4) == 0 &&
	  d == 2);

    /* the attributes of the host */
    {
	byte a = FA_RDONLY;
	FileDateAndTime m = (FileDateAndTime) ((((dword) 10 << 11) |
						 (30 << 5)) << 16) |
			    ((2010 - 1980) << 9) | (6 << 5) | 15;
	CHECK(FileSetPathExtAttributes("NATIVE.TXT", FEA_MODIFICATION, &m, 4)
	      == 0);
	CHECK(FileGetPathExtAttributes("NATIVE.TXT", FEA_MODIFICATION, &d, 4)
	      == 0 && d == m);
	CHECK(FileSetPathExtAttributes("NATIVE.TXT", FEA_FILE_ATTR, &a, 1)
	      == 0);
	a = 0;
	CHECK(FileGetPathExtAttributes("NATIVE.TXT", FEA_FILE_ATTR, &a, 1)
	      == 0 && (a & FA_RDONLY));
	a = 0;
	CHECK(FileSetPathExtAttributes("NATIVE.TXT", FEA_FILE_ATTR, &a, 1)
	      == 0);
    }
    CHECK(FileDelete("NATIVE.TXT") == 0 && FileDelete("DATA.TXT") == 0);
}

static void VMFileType(void)
{
    VMFileHandle vm = VMOpen("FILE.VM", 0, VMO_CREATE_ONLY, 0);
    word type = 0;
    unsigned char raw[GFH_SIZE + 4];

    CHECK(vm != 0);
    CHECK(VMClose(vm, FALSE) == 0);
    CHECK(FileGetPathExtAttributes("FILE.VM", FEA_FILE_TYPE, &type, 2) == 0 &&
	  type == GFT_VM);
    /* the VM file header after the GEOS one */
    CHECK(Raw(HostOf("FILE.VM"), raw, sizeof(raw)) == sizeof(raw) &&
	  HostFSIsHeader(raw) && raw[GFH_SIZE] == 0xeb && raw[GFH_SIZE + 1] ==
	  0xad);
    vm = VMOpen("FILE.VM", 0, VMO_OPEN, 0);
    CHECK(vm != 0);
    /* the VM file's handle stands for its file (DOS16: it was the file
     * handle), as the TrueType driver uses its cache file's */
    {
	ProtocolNumber pn = { 3, 4 }, got = { 0, 0 };
	type = 0;
	CHECK(FileGetHandleExtAttributes(vm, FEA_FILE_TYPE, &type, 2) == 0 &&
	      type == GFT_VM);
	CHECK(FileSetHandleExtAttributes(vm, FEA_PROTOCOL, &pn,
					 sizeof(pn)) == 0);
	CHECK(FileGetHandleExtAttributes(vm, FEA_PROTOCOL, &got,
					 sizeof(got)) == 0 &&
	      got.PN_major == 3 && got.PN_minor == 4);
    }
    CHECK(VMClose(vm, FALSE) == 0);
    CHECK(FileDelete("FILE.VM") == 0);
}

/* DOS names (dosVirtual.asm), on every host */
static void DosNames(void)
{
    FileHandle f;
    unsigned char raw[GFH_SIZE];
    char buf[64];
    int was = HostFSDosNames();

    HostFSSetDosNames(HOSTFS_NAMES_DOS);
    f = FileCreate("Long Document Name", FILE_CREATE_ONLY | FILE_ACCESS_RW,
		   0);
    CHECK(f && FileWrite(f, "1", 1, FALSE) == 1 && FileClose(f, FALSE) == 0);
    CHECK(Exists(H("LONG_DOC.000")));
    f = FileCreate("Long Document 2", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileClose(f, FALSE) == 0);
    CHECK(Exists(H("LONG_DOC.001")));
    /* found by the GEOS name in the header */
    f = FileOpen("Long Document Name", FILE_ACCESS_R);
    CHECK(f && FileRead(f, buf, 1, FALSE) == 1 && buf[0] == '1');
    CHECK(f && FileClose(f, FALSE) == 0);
    CHECK(FileGetPathExtAttributes("Long Document 2", FEA_DOS_NAME, buf,
				   sizeof(buf)) == 0 &&
	  !strcmp(buf, "LONG_DOC.001"));
    /* a native file needs a DOS name */
    CHECK(FileCreate("Long Native Name", FCF_NATIVE | FILE_CREATE_ONLY |
		     FILE_ACCESS_RW, 0) == 0 &&
	  ThreadGetError() == ERROR_INVALID_LONGNAME);
    f = FileCreate("dos.txt", FCF_NATIVE | FILE_CREATE_ONLY | FILE_ACCESS_RW,
		   0);
    CHECK(f && FileClose(f, FALSE) == 0 && Exists(H("DOS.TXT")));
    CHECK(FileCreate("This name is far too long for GEOS",
		     FILE_CREATE_ONLY | FILE_ACCESS_RW, 0) == 0 &&
	  ThreadGetError() == ERROR_INVALID_LONGNAME);

    /* renaming in the directory: the host name stays */
    CHECK(FileRename("Long Document 2", "Renamed Document") == 0);
    CHECK(Exists(H("LONG_DOC.001")) &&
	  Raw(H("LONG_DOC.001"), raw, sizeof(raw)) == GFH_SIZE &&
	  !strcmp((char *) raw + GFH_LONG_NAME, "Renamed Document"));
    CHECK(FileRename("Renamed Document", "Long Document Name") ==
	  ERROR_FILE_EXISTS);

    /* directories: a DOS name as it is, others with @DIRNAME.000 */
    CHECK(FileCreateDir("PLAIN") == 0 && Exists(H("PLAIN")) &&
	  !Exists(H("PLAIN/@DIRNAME.000")));
    CHECK(FileCreateDir("My Folder") == 0 && Exists(H("MY_FOLDE.000")) &&
	  Exists(H("MY_FOLDE.000/@DIRNAME.000")));
    CHECK(FileGetPathExtAttributes("My Folder", FEA_FILE_TYPE, buf, 2) == 0 &&
	  *(word *) buf == GFT_DIRECTORY);
    CHECK(FileGetPathExtAttributes("My Folder", FEA_NAME, buf, sizeof(buf)) ==
	  0 && !strcmp(buf, "My Folder"));
    CHECK(FileSetCurrentPath(0, "My Folder") != 0);
    f = FileCreate("Inner File", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileClose(f, FALSE) == 0 && Exists(H("MY_FOLDE.000/INNER_FI.000")));
    CHECK(FileSetCurrentPath(0, "..") != 0);
    CHECK(FileOpen("My Folder\\Inner File", FILE_ACCESS_R) != 0);
    CHECK(FileDeleteDir("My Folder") == ERROR_DIRECTORY_NOT_EMPTY);
    CHECK(FileDelete("My Folder\\Inner File") == 0);
    CHECK(FileRename("My Folder", "Other Folder") == 0 &&
	  Exists(H("MY_FOLDE.000")));
    CHECK(FileRename("PLAIN", "Plain Folder") == 0 &&
	  Exists(H("PLAIN_FO.000/@DIRNAME.000")) && !Exists(H("PLAIN")));
    CHECK(FileDeleteDir("Other Folder") == 0 && !Exists(H("MY_FOLDE.000")));
    CHECK(FileDeleteDir("Plain Folder") == 0);
#ifndef GEOS_HOST_DOS32
    /* the same tree with long names: found by the headers */
    HostFSSetDosNames(HOSTFS_NAMES_LONG);
    f = FileOpen("Long Document Name", FILE_ACCESS_R);
    CHECK(f && FileRead(f, buf, 1, FALSE) == 1 && buf[0] == '1');
    CHECK(f && FileClose(f, FALSE) == 0);
    f = FileCreate("A Long Name", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileClose(f, FALSE) == 0 && Exists(H("A Long Name")));
    CHECK(FileCreateDir("Long Folder") == 0 && Exists(H("Long Folder")) &&
	  !Exists(H("Long Folder/@DIRNAME.000")));
    CHECK(FileRename("A Long Name", "Another Name") == 0 &&
	  Exists(H("Another Name")) && !Exists(H("A Long Name")));
    /* and those with DOS names again */
    HostFSSetDosNames(HOSTFS_NAMES_DOS);
    f = FileOpen("Another Name", FILE_ACCESS_R);
    CHECK(f && FileClose(f, FALSE) == 0);
    CHECK(FileSetCurrentPath(0, "Long Folder") != 0 &&
	  FileSetCurrentPath(0, "..") != 0);
    CHECK(FileDelete("Another Name") == 0 && FileDeleteDir("Long Folder") == 0);

    /* DOS with long file names (the MSLF driver): GEOS files still get
     * 8.3 names, native files and directories keep long DOS names */
    HostFSSetDosNames(HOSTFS_NAMES_DOS_LFN);
    f = FileCreate("Lfn Document", FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
    CHECK(f && FileClose(f, FALSE) == 0 && Exists(H("LFN_DOCU.000")));
    f = FileCreate("Long Native.txt", FCF_NATIVE | FILE_CREATE_ONLY |
		   FILE_ACCESS_RW, 0);
    CHECK(f && FileClose(f, FALSE) == 0 && Exists(H("Long Native.txt")));
    CHECK(FileCreate("Bad|Native", FCF_NATIVE | FILE_CREATE_ONLY |
		     FILE_ACCESS_RW, 0) == 0 &&
	  ThreadGetError() == ERROR_INVALID_LONGNAME);
    CHECK(FileCreateDir("Lfn Folder") == 0 && Exists(H("Lfn Folder")) &&
	  !Exists(H("Lfn Folder/@DIRNAME.000")));
    CHECK(FileCreateDir("Odd|Folder") == 0 && Exists(H("ODD_FOLD.000")) &&
	  Exists(H("ODD_FOLD.000/@DIRNAME.000")));
    CHECK(FileOpen("Lfn Document", FILE_ACCESS_R) != 0);
    CHECK(FileDelete("Lfn Document") == 0 &&
	  FileDelete("Long Native.txt") == 0 &&
	  FileDeleteDir("Lfn Folder") == 0 && FileDeleteDir("Odd|Folder") == 0);
#endif
    HostFSSetDosNames(was);
    CHECK(FileDelete("Long Document Name") == 0 &&
	  FileDelete("Renamed Document") == 0 && FileDelete("DOS.TXT") == 0);
}

int main(int argc, char **argv)
{
    char *slash;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);	/* all of it, should DOS crash */
    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GEATOP");
    else
	strcpy(top, "GEATOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    if (setjmp(catcher)) {
	printf("FAIL: fatal error %d\n", (int) caught);
	return 1;
    }

    HeaderAndAttributes();
    VMFileType();
    DosNames();

    RemoveTree(top);
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
