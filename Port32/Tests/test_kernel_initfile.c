/*
 * test_kernel_initfile.c -- the init file of the C kernel
 * (Library/Kernel/Initfile) on every host.
 *
 * A geos.ini with comments, blobs, escapes, odd spacing and case, a line
 * without "=", and "[paths] ini" naming a second file with LF line ends.
 * Checked: integers, booleans, strings (trimmed, unescaped, converted,
 * cut to the buffer), blocks, the chain of files (first only, read all,
 * all integers), string sections and their enumeration, writing strings
 * that need blobs and escapes and reading them back, data in one line
 * and in a blob, string sections written and deleted, deleting entries
 * and categories, InitFileCommit to the file and reading it again,
 * InitFileSave/Revert, InitFileGrab/Release and
 * InitFileMakeCanonicKeyCategory.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <initfile.h>

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

static void HostWrite(const char *name, const char *text)
{
    char path[400];
    ghal_file *f;
    size_t done;

    sprintf(path, "%s%c%s", top, HostFSSep(), name);
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE |
			     GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, text, strlen(text), &done);
    ghal_fs_close(f);
}

static size_t HostRead(const char *name, char *buf, size_t size)
{
    char path[400];
    ghal_file *f;
    size_t done = 0;

    sprintf(path, "%s%c%s", top, HostFSSep(), name);
    if (ghal_fs_open(path, GHAL_FS_READ, &f) != GHAL_OK)
	return 0;
    ghal_fs_read(f, buf, size - 1, &done);
    ghal_fs_close(f);
    buf[done] = 0;
    return done;
}

static const char geosIni[] =
    "; the test's init file\r\n"
    "[system]\r\n"
    "  Handles = 2500   ; comment\r\n"
    "fs = hostfs.geo\r\n"
    "this line has no equals sign\r\n"
    "Some Key=Value with  spaces   \r\n"
    "lower = MiXeD\r\n"
    "esc = a\\[b\\{c\\d\r\n"
    "empty =\r\n"
    "after = 1\r\n"
    "yes = TRUE\r\n"
    "no = False ; no\r\n"
    "bad = truely\r\n"
    "\r\n"
    "[ My Cat ]\r\n"
    "list = {\r\n"
    "first one\r\n"
    "second\r\n"
    "\r\n"
    "  third\r\n"
    "}\r\n"
    "esc = {\r\n"
    "x\\}y\\\\z;w\r\n"
    "}\r\n"
    "num = 7\r\n"
    "[paths]\r\n"
    "ini = SECOND.INI MISSING.INI\r\n";

static const char secondIni[] =
    "[system]\n"
    "fs = other.geo\n"
    "only2 = here\n"
    "[ my cat ]\n"
    "num = 8\n"
    "list = {\n"
    "fourth\n"
    "}\n";

static word ints[4], nints;

static Boolean IntCB(word v, void *data)
{
    (void) data;
    ints[nints++] = v;
    return FALSE;
}

static char sections[200];

static Boolean SectionCB(const char *s, word n, void *data)
{
    sprintf(sections + strlen(sections), "%u:%s|", n, s);
    return n == *(word *) data;
}

/* the string of cat/key ("" if there is none) */
static const char *Str(const char *cat, const char *key, word flags)
{
    static char buf[300];
    word n;

    if (InitFileReadStringBuffer(cat, key, buf, flags | sizeof(buf), &n))
	return "<none>";
    if (n != strlen(buf))
	return "<bad size>";
    return buf;
}

static void Reads(void)
{
    word v, n;
    Boolean b;
    char buf[8];
    MemHandle mh;

    CHECK(!InitFileReadInteger("system", "handles", &v) && v == 2500);
    CHECK(!InitFileReadInteger("SYSTEM", "HANDLES", &v) && v == 2500);
    CHECK(InitFileReadInteger("system", "fs", &v));
    CHECK(InitFileReadInteger("system", "nothere", &v));
    CHECK(InitFileReadInteger("nocat", "handles", &v));
    CHECK(!strcmp(Str("system", "fs", 0), "hostfs.geo"));
    CHECK(!strcmp(Str("system", "somekey", 0), "Value with  spaces"));
    CHECK(!strcmp(Str("system", "lower", IFCC_UPCASE << IFRF_CHAR_CONVERT_OFFSET),
		  "MIXED"));
    CHECK(!strcmp(Str("system", "lower", IFCC_DOWNCASE << IFRF_CHAR_CONVERT_OFFSET),
		  "mixed"));
    CHECK(!strcmp(Str("system", "esc", 0), "a[b{c\\d"));
    CHECK(!strcmp(Str("system", "empty", 0), ""));
    CHECK(!InitFileReadInteger("system", "after", &v) && v == 1);
    CHECK(!InitFileReadBoolean("system", "yes", &b) && b);
    CHECK(!InitFileReadBoolean("system", "no", &b) && !b);
    CHECK(InitFileReadBoolean("system", "bad", &b));
    CHECK(!strcmp(Str("mycat", "esc", 0), "x}y\\z;w"));
    CHECK(!InitFileReadInteger("my cat", "num", &v) && v == 7);

    /* cut to the buffer */
    CHECK(!InitFileReadStringBuffer("system", "fs", buf, sizeof(buf), &n) &&
	  n == 7 && !strcmp(buf, "hostfs."));
    CHECK(!InitFileReadStringBlock("system", "fs", &mh, 0, &n) && n == 10);
    if (mh) {
	CHECK(!strcmp(MemLock(mh), "hostfs.geo"));
	MemFree(mh);
    }
    CHECK(InitFileReadStringBlock("system", "none", &mh, 0, &n) && !mh);

    /* the chain */
    CHECK(!strcmp(Str("system", "only2", 0), "here"));
    CHECK(!strcmp(Str("system", "only2", IFRF_FIRST_ONLY), "<none>"));
    CHECK(!strcmp(Str("system", "fs", IFRF_READ_ALL),
		  "hostfs.geo\r\nother.geo"));
    nints = 0;
    CHECK(!InitFileReadAllInteger("mycat", "num", IntCB, NULL) &&
	  nints == 2 && ints[0] == 7 && ints[1] == 8);
    CHECK(InitFileReadAllInteger("mycat", "none", IntCB, NULL));

    /* sections */
    CHECK(!InitFileReadStringSectionBuffer("mycat", "list", 2, buf,
					   sizeof(buf), &n) &&
	  n == 7 && !strcmp(buf, "  third"));
    CHECK(InitFileReadStringSectionBuffer("mycat", "list", 0, buf,
					  sizeof(buf), &n));	/* too long */
    CHECK(InitFileReadStringSectionBuffer("mycat", "list", 3, buf,
					  sizeof(buf), &n));
    CHECK(!InitFileReadStringSectionBlock("mycat", "list", 3, &mh,
					  IFRF_READ_ALL, &n) && n == 6);
    if (mh) {
	CHECK(!strcmp(MemLock(mh), "fourth"));
	MemFree(mh);
    }
    sections[0] = 0;
    v = 99;
    CHECK(!InitFileEnumStringSection("mycat", "list", 0, SectionCB, &v));
    CHECK(!strcmp(sections, "0:first one|1:second|2:  third|"));
    sections[0] = 0;
    v = 1;
    CHECK(InitFileEnumStringSection("mycat", "list", 0, SectionCB, &v));
    CHECK(!strcmp(sections, "0:first one|1:second|"));
    CHECK(!InitFileEnumStringSection("nocat", "list", 0, SectionCB, &v));
}

static const char *const tricky[] = {
    "plain", " leading", "trailing\t", "semi;colon", "x[y{z}\\w",
    "line1\r\nline2", "lf\nonly", "cr\ronly", "", "{\\}", 0
};

static void Writes(void)
{
    word v, n, i;
    byte data[100], back[120];
    char key[8];
    MemHandle mh;
    dword t0 = InitFileGetTimeLastModified();
    Boolean b;

    /* new key in a category, replaced value, new category */
    InitFileWriteInteger("system", "newint", 65535);
    CHECK(!InitFileReadInteger("system", "newint", &v) && v == 65535);
    InitFileWriteString("system", "Some Key", "short");
    CHECK(!strcmp(Str("system", "somekey", 0), "short"));
    CHECK(!InitFileReadInteger("system", "after", &v) && v == 1);
    InitFileWriteString("mycat", "list", "one");	/* a blob replaced */
    CHECK(!strcmp(Str("mycat", "list", IFRF_FIRST_ONLY), "one"));
    CHECK(!InitFileReadInteger("mycat", "num", &v) && v == 7);
    InitFileWriteBoolean("new cat", "flag", TRUE);
    CHECK(!InitFileReadBoolean("newcat", "flag", &b) && b);
    InitFileWriteBoolean("new cat", "flag", FALSE);
    CHECK(!InitFileReadBoolean("newcat", "flag", &b) && !b);
    CHECK(InitFileGetTimeLastModified() >= t0);

    for (i = 0; tricky[i]; i++) {
	sprintf(key, "t%u", i);
	InitFileWriteString("tricky", key, tricky[i]);
    }
    for (i = 0; tricky[i]; i++) {
	sprintf(key, "t%u", i);
	CHECK(!strcmp(Str("tricky", key, 0), tricky[i]));
	if (strcmp(Str("tricky", key, 0), tricky[i]))
	    printf("  t%u: \"%s\"\n", i, Str("tricky", key, 0));
    }

    /* data: one line, then a blob */
    for (i = 0; i < sizeof(data); i++)
	data[i] = (byte) (i * 37 + 1);
    InitFileWriteData("data", "small", data, 5);
    InitFileWriteData("data", "big", data, sizeof(data));
    InitFileWriteData("data", "none", data, 0);
    CHECK(!InitFileReadDataBuffer("data", "small", back, sizeof(back), &n) &&
	  n == 5 && !memcmp(back, data, 5));
    CHECK(!InitFileReadDataBuffer("data", "big", back, sizeof(back), &n) &&
	  n == sizeof(data) && !memcmp(back, data, sizeof(data)));
    CHECK(!InitFileReadDataBuffer("data", "big", back, 10, &n) && n == 10);
    CHECK(!InitFileReadDataBuffer("data", "none", back, 10, &n) && n == 0);
    CHECK(!InitFileReadDataBlock("data", "big", &mh, &n) &&
	  n == sizeof(data));
    if (mh) {
	CHECK(!memcmp(MemLock(mh), data, sizeof(data)));
	MemFree(mh);
    }
    CHECK(InitFileReadDataBlock("data", "nope", &mh, &n) && !mh);

    /* string sections */
    InitFileWriteStringSection("sect", "s", "alpha");
    InitFileWriteStringSection("sect", "s", "beta gamma");
    InitFileWriteStringSection("sect", "s", "delta");
    CHECK(!strcmp(Str("sect", "s", 0), "alpha\r\nbeta gamma\r\ndelta"));
    InitFileDeleteStringSection("sect", "s", 1);
    CHECK(!strcmp(Str("sect", "s", 0), "alpha\r\ndelta"));
    InitFileDeleteStringSection("sect", "s", 1);
    CHECK(!strcmp(Str("sect", "s", 0), "alpha"));
    InitFileDeleteStringSection("sect", "s", 5);
    CHECK(!strcmp(Str("sect", "s", 0), "alpha"));
    InitFileDeleteStringSection("sect", "s", 0);
    CHECK(!strcmp(Str("sect", "s", 0), "<none>"));

    /* deleting */
    InitFileDeleteEntry("system", "fs");
    CHECK(!strcmp(Str("system", "fs", 0), "other.geo"));
    CHECK(!InitFileReadInteger("system", "handles", &v) && v == 2500);
    InitFileDeleteCategory("tricky");
    CHECK(!strcmp(Str("tricky", "t0", 0), "<none>"));
    CHECK(!InitFileReadInteger("mycat", "num", &v) && v == 7);
    CHECK(!InitFileReadDataBuffer("data", "small", back, sizeof(back), &n) &&
	  n == 5);
}

/* what is in memory goes to geos.ini and comes back */
static void Commit(void)
{
    static char text[4000];
    word v;

    InitFileCommit();
    CHECK(HostRead("geos.ini", text, sizeof(text)) > 0);
    CHECK(strstr(text, "newint = 65535\r\n") != NULL);
    CHECK(strstr(text, "[new cat]\r\nflag = false\r\n") != NULL);
    CHECK(strstr(text, "fs = hostfs") == NULL);
    InitFileReset();
    CHECK(!InitFileReadInteger("system", "newint", &v) && v == 65535);
    CHECK(!strcmp(Str("system", "only2", 0), "here"));

    /* Save, change, Revert */
    CHECK(!InitFileSave());
    InitFileWriteInteger("system", "newint", 1);
    CHECK(!InitFileRevert());
    CHECK(!InitFileReadInteger("system", "newint", &v) && v == 65535);

    /* a file without a line end at its end, and without geos.ini */
    HostWrite("geos.ini", "[a]\r\nx = 1");
    InitFileReset();
    InitFileWriteInteger("a", "y", 2);
    InitFileWriteInteger("b", "z", 3);
    InitFileCommit();
    HostRead("geos.ini", text, sizeof(text));
    CHECK(!strcmp(text, "[a]\r\nx = 1\r\ny = 2\r\n[b]\r\nz = 3\r\n"));
    {
	char path[400];
	sprintf(path, "%s%cgeos.ini", top, HostFSSep());
	ghal_fs_delete(path);
    }
    InitFileReset();
    CHECK(InitFileReadInteger("a", "x", &v));
    InitFileWriteString("c", "k", "v");
    InitFileCommit();
    HostRead("geos.ini", text, sizeof(text));
    CHECK(!strcmp(text, "[c]\r\nk = v\r\n"));
}

static void Grab(void)
{
    static const char g[] = "[g]\r\nk = 1\r\n";
    MemHandle mh = MemAlloc(sizeof(g), HF_SWAPABLE, HAF_STANDARD);
    word v;
    char *p;

    strcpy(MemLock(mh), g);
    MemUnlock(mh);
    CHECK(InitFileGrab(mh, 0, sizeof(g) - 1) == 0);
    CHECK(!InitFileReadInteger("g", "k", &v) && v == 1);
    CHECK(InitFileReadInteger("c", "k", &v));		/* not the file */
    InitFileWriteInteger("g", "k", 2);
    InitFileRelease();
    p = MemLock(mh);
    CHECK(!strncmp(p, "[g]\r\nk = 2\r\n\x1a", 13));
    MemUnlock(mh);
    CHECK(InitFileReadInteger("g", "k", &v));
    CHECK(!strcmp(Str("c", "k", 0), "v"));

    /* no init file */
    strcpy(MemLock(mh), "[g]\001");
    MemUnlock(mh);
    CHECK(InitFileGrab(mh, 0, 4) != 0);
    MemFree(mh);
}

int main(int argc, char **argv)
{
    char canon[40], *slash;
    dword base;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GITOP");
    else
	strcpy(top, "GITOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    HostWrite("geos.ini", geosIni);
    HostWrite("SECOND.INI", secondIni);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    CHECK(FileCreateDir("\\PRIVDATA") == 0);
    base = HandleCountUsed();

    Reads();
    Writes();
    Commit();
    Grab();

    InitFileMakeCanonicKeyCategory(canon, "a;b[c]{ ~\001Z");
    CHECK(!strcmp(canon, "a3Bb5Bc5D7B 7E01Z"));

    InitFileReset();
    CHECK(HandleCountUsed() == base);
    RemoveTree(top);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
