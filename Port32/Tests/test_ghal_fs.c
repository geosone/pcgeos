/*
 * test_ghal_fs.c -- ghal_fs on every host (linux, dos32).
 *
 * Works in a directory GFSTEST next to the program (on DOS the program
 * runs from a writable disk).  Names are 8.3 so that DOS takes them.
 */
#include "ghal.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char base[260];

static const char *P(const char *name)
{
    static char buf[4][300];
    static int n;
    char *b = buf[n++ & 3];

    sprintf(b, "%s%s", base, name);
    return b;
}

static void Cleanup(void)
{
    ghal_fs_set_attrs(P("GFSTEST/B.DAT"), 0);
    ghal_fs_delete(P("GFSTEST/A.DAT"));
    ghal_fs_delete(P("GFSTEST/B.DAT"));
    ghal_fs_delete(P("GFSTEST/SUB/C.DAT"));
    ghal_fs_rmdir(P("GFSTEST/SUB"));
    ghal_fs_rmdir(P("GFSTEST"));
}

int main(int argc, char **argv)
{
    ghal_file *f;
    ghal_dir *d;
    ghal_fs_info info;
    char buf[64], name[64];
    size_t done;
    uint64_t pos;
    int seenA = 0, seenB = 0, seenSub = 0, other = 0;
    ghal_status s;
    char *slash;

    (void)argc;
    /* the directory of the program */
    strncpy(base, argv[0], sizeof(base) - 1);
    slash = strrchr(base, '/');
    if (!slash || (strrchr(base, '\\') && strrchr(base, '\\') > slash))
        slash = strrchr(base, '\\');
    if (slash)
        slash[1] = 0;
    else
        base[0] = 0;

    Cleanup();
    CHECK(ghal_fs_mkdir(P("GFSTEST")) == GHAL_OK);
    CHECK(ghal_fs_mkdir(P("GFSTEST")) == GHAL_ERR_EXISTS ||
          ghal_fs_mkdir(P("GFSTEST")) == GHAL_ERR_ACCESS);

    /* create, write, read back, seek, truncate */
    CHECK(ghal_fs_open(P("GFSTEST/A.DAT"), GHAL_FS_READ, &f) ==
          GHAL_ERR_NOTFOUND);
    CHECK(ghal_fs_open(P("GFSTEST/A.DAT"), GHAL_FS_READ | GHAL_FS_WRITE |
                       GHAL_FS_CREATE | GHAL_FS_EXCL, &f) == GHAL_OK);
    CHECK(ghal_fs_write(f, "hello, geos", 11, &done) == GHAL_OK && done == 11);
    CHECK(ghal_fs_seek(f, 7, GHAL_FS_SEEK_SET, &pos) == GHAL_OK && pos == 7);
    CHECK(ghal_fs_read(f, buf, sizeof(buf), &done) == GHAL_OK && done == 4 &&
          memcmp(buf, "geos", 4) == 0);
    CHECK(ghal_fs_seek(f, 0, GHAL_FS_SEEK_END, &pos) == GHAL_OK && pos == 11);
    CHECK(ghal_fs_truncate(f, 5) == GHAL_OK);
    CHECK(ghal_fs_sync(f) == GHAL_OK);
    CHECK(ghal_fs_fstat(f, &info) == GHAL_OK && info.size == 5 &&
          !(info.attrs & GHAL_FS_ATTR_DIR));
    CHECK(ghal_fs_close(f) == GHAL_OK);
    CHECK(ghal_fs_open(P("GFSTEST/A.DAT"), GHAL_FS_WRITE | GHAL_FS_CREATE |
                       GHAL_FS_EXCL, &f) == GHAL_ERR_EXISTS);

    /* stat, attributes, time */
    CHECK(ghal_fs_stat(P("GFSTEST/A.DAT"), &info) == GHAL_OK &&
          info.size == 5 && info.mtime.year >= 2000);
    CHECK(ghal_fs_stat(P("GFSTEST"), &info) == GHAL_OK &&
          (info.attrs & GHAL_FS_ATTR_DIR));
    CHECK(ghal_fs_stat(P("GFSTEST/NONE.DAT"), &info) == GHAL_ERR_NOTFOUND);
    {
        ghal_datetime dt;
        memset(&dt, 0, sizeof(dt));
        dt.year = 2001; dt.month = 2; dt.day = 3;
        dt.hour = 4; dt.minute = 5; dt.second = 6;
        CHECK(ghal_fs_set_mtime(P("GFSTEST/A.DAT"), &dt) == GHAL_OK);
        CHECK(ghal_fs_stat(P("GFSTEST/A.DAT"), &info) == GHAL_OK &&
              info.mtime.year == 2001 && info.mtime.month == 2 &&
              info.mtime.day == 3 && info.mtime.hour == 4 &&
              info.mtime.minute == 5 && info.mtime.second == 6);
    }

    /* rename: never over an existing file */
    CHECK(ghal_fs_open(P("GFSTEST/B.DAT"), GHAL_FS_WRITE | GHAL_FS_CREATE,
                       &f) == GHAL_OK && ghal_fs_close(f) == GHAL_OK);
    CHECK(ghal_fs_rename(P("GFSTEST/A.DAT"), P("GFSTEST/B.DAT")) ==
          GHAL_ERR_EXISTS);
    CHECK(ghal_fs_delete(P("GFSTEST/B.DAT")) == GHAL_OK);
    CHECK(ghal_fs_rename(P("GFSTEST/A.DAT"), P("GFSTEST/B.DAT")) == GHAL_OK);
    CHECK(ghal_fs_stat(P("GFSTEST/A.DAT"), &info) == GHAL_ERR_NOTFOUND);
    CHECK(ghal_fs_set_attrs(P("GFSTEST/B.DAT"), GHAL_FS_ATTR_READONLY) ==
          GHAL_OK);
    CHECK(ghal_fs_stat(P("GFSTEST/B.DAT"), &info) == GHAL_OK &&
          (info.attrs & GHAL_FS_ATTR_READONLY));
    CHECK(ghal_fs_set_attrs(P("GFSTEST/B.DAT"), 0) == GHAL_OK);
    CHECK(ghal_fs_stat(P("GFSTEST/B.DAT"), &info) == GHAL_OK &&
          !(info.attrs & GHAL_FS_ATTR_READONLY));
    CHECK(ghal_fs_open(P("GFSTEST"), GHAL_FS_READ, &f) == GHAL_ERR_ISDIR);
    CHECK(ghal_fs_delete(P("GFSTEST")) == GHAL_ERR_ISDIR);

    /* directories */
    CHECK(ghal_fs_mkdir(P("GFSTEST/SUB")) == GHAL_OK);
    CHECK(ghal_fs_open(P("GFSTEST/SUB/C.DAT"), GHAL_FS_WRITE |
                       GHAL_FS_CREATE, &f) == GHAL_OK &&
          ghal_fs_close(f) == GHAL_OK);
    CHECK(ghal_fs_rmdir(P("GFSTEST/SUB")) == GHAL_ERR_NOTEMPTY);
    CHECK(ghal_fs_opendir(P("GFSTEST"), &d) == GHAL_OK);
    while ((s = ghal_fs_readdir(d, name, sizeof(name), &info)) == GHAL_OK) {
        if (!strcmp(name, "B.DAT") || !strcmp(name, "b.dat"))
            seenB++;
        else if (!strcmp(name, "SUB") || !strcmp(name, "sub"))
            seenSub += (info.attrs & GHAL_FS_ATTR_DIR) ? 1 : 100;
        else if (!strcmp(name, "A.DAT"))
            seenA++;
        else
            other++;
    }
    CHECK(s == GHAL_ERR_END);
    ghal_fs_closedir(d);
    CHECK(seenB == 1 && seenSub == 1 && seenA == 0 && other == 0);
    CHECK(ghal_fs_opendir(P("GFSTEST/NONE"), &d) == GHAL_ERR_NOTFOUND);

    /* named attributes: where the host has them */
    {
        size_t n = sizeof(buf);
        s = ghal_fs_xattr_set(P("GFSTEST/B.DAT"), "token", "TEST", 4);
        if (s == GHAL_OK) {
            CHECK(ghal_fs_xattr_get(P("GFSTEST/B.DAT"), "token", buf, &n) ==
                  GHAL_OK && n == 4 && memcmp(buf, "TEST", 4) == 0);
            CHECK(ghal_fs_xattr_remove(P("GFSTEST/B.DAT"), "token") ==
                  GHAL_OK);
            n = sizeof(buf);
            CHECK(ghal_fs_xattr_get(P("GFSTEST/B.DAT"), "token", buf, &n) ==
                  GHAL_ERR_NOTFOUND);
        } else {
            printf("named attributes: not on this host or file system\n");
            CHECK(s == GHAL_ERR_UNSUPPORTED);
        }
    }

    Cleanup();
    CHECK(ghal_fs_stat(P("GFSTEST"), &info) == GHAL_ERR_NOTFOUND);
    printf(failures ? "FAILED\n" : "ok\n");
    return failures != 0;
}
