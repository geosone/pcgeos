/*
 * ghal_fs_dos.c -- ghal_fs for dos32 (ghal.h), DJGPP and Open Watcom.
 *
 * The DOS file calls of the C library (handles, 8.3 names, DOS
 * attributes).  DOS has no named attributes: ghal_fs_xattr_* return
 * GHAL_ERR_UNSUPPORTED.  The GEOS data of DOS16 files (the GEOS file
 * header, long names) belongs to the DOS file system driver, not here.
 */
#include "ghal.h"

#include <dos.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#ifdef __WATCOMC__
#include <direct.h>
#include <sys/utime.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct ghal_file {
    int fd;
};

struct ghal_dir {
    DIR  *dir;
    char *path;
};

static ghal_status Err(int e)
{
    switch (e) {
    case ENOENT:        return GHAL_ERR_NOTFOUND;
    case EEXIST:        return GHAL_ERR_EXISTS;
    case EACCES:        return GHAL_ERR_ACCESS;
#ifdef ENOSPC
    case ENOSPC:        return GHAL_ERR_NOSPACE;
#endif
#ifdef ENOTEMPTY
    case ENOTEMPTY:     return GHAL_ERR_NOTEMPTY;
#endif
#ifdef EISDIR
    case EISDIR:        return GHAL_ERR_ISDIR;
#endif
#ifdef ENOTDIR
    case ENOTDIR:       return GHAL_ERR_NOTDIR;
#endif
    case ENOMEM:        return GHAL_ERR_NOMEM;
    case EINVAL:        return GHAL_ERR_INVAL;
    default:            return GHAL_ERR_HOST;
    }
}

static void ToDatetime(time_t t, ghal_datetime *dt)
{
    struct tm *tm = localtime(&t);

    memset(dt, 0, sizeof(*dt));
    if (!tm)
        return;
    dt->year = (uint16_t)(tm->tm_year + 1900);
    dt->month = (uint8_t)(tm->tm_mon + 1);
    dt->day = (uint8_t)tm->tm_mday;
    dt->hour = (uint8_t)tm->tm_hour;
    dt->minute = (uint8_t)tm->tm_min;
    dt->second = (uint8_t)(tm->tm_sec > 59 ? 59 : tm->tm_sec);
    dt->weekday = (uint8_t)tm->tm_wday;
}

static unsigned DosAttrs(const char *path)
{
    unsigned a = 0;

    if (_dos_getfileattr(path, &a) != 0)
        return 0;
    return a & (GHAL_FS_ATTR_READONLY | GHAL_FS_ATTR_HIDDEN |
                GHAL_FS_ATTR_SYSTEM | GHAL_FS_ATTR_DIR |
                GHAL_FS_ATTR_ARCHIVE);
}

static void ToInfo(const struct stat *st, unsigned attrs, ghal_fs_info *info)
{
    int dir = S_ISDIR(st->st_mode);

    info->size = dir ? 0 : (uint64_t)(unsigned long)st->st_size;
    ToDatetime(st->st_mtime, &info->mtime);
    info->attrs = attrs | (dir ? GHAL_FS_ATTR_DIR : 0);
}

ghal_status ghal_fs_open(const char *path, unsigned flags, ghal_file **out)
{
    int oflags, fd;
    struct stat st;
    ghal_file *f;

    if (!path || !out || !(flags & (GHAL_FS_READ | GHAL_FS_WRITE)))
        return GHAL_ERR_INVAL;
    *out = NULL;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return GHAL_ERR_ISDIR;
        if ((flags & GHAL_FS_CREATE) && (flags & GHAL_FS_EXCL))
            return GHAL_ERR_EXISTS;
    }
    oflags = O_BINARY | ((flags & GHAL_FS_WRITE) ?
             ((flags & GHAL_FS_READ) ? O_RDWR : O_WRONLY) : O_RDONLY);
    if (flags & GHAL_FS_CREATE)
        oflags |= O_CREAT;
    if (flags & GHAL_FS_TRUNCATE)
        oflags |= O_TRUNC;
    if (flags & GHAL_FS_EXCL)
        oflags |= O_EXCL;
    fd = open(path, oflags, S_IREAD | S_IWRITE);
    if (fd < 0)
        return Err(errno);
    f = malloc(sizeof(*f));
    if (!f) {
        close(fd);
        return GHAL_ERR_NOMEM;
    }
    f->fd = fd;
    *out = f;
    return GHAL_OK;
}

ghal_status ghal_fs_close(ghal_file *f)
{
    int r;

    if (!f)
        return GHAL_ERR_INVAL;
    r = close(f->fd);
    free(f);
    return r == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_read(ghal_file *f, void *buf, size_t n, size_t *done)
{
    size_t total = 0;

    while (total < n) {
        int r = read(f->fd, (char *)buf + total, (unsigned)(n - total));
        if (r < 0) {
            if (done)
                *done = total;
            return Err(errno);
        }
        if (r == 0)
            break;
        total += (size_t)r;
    }
    if (done)
        *done = total;
    return GHAL_OK;
}

ghal_status ghal_fs_write(ghal_file *f, const void *buf, size_t n,
                          size_t *done)
{
    size_t total = 0;

    while (total < n) {
        int r = write(f->fd, (const char *)buf + total,
                      (unsigned)(n - total));
        if (r < 0) {
            if (done)
                *done = total;
            return Err(errno);
        }
        if (r == 0) {               /* DOS: the disk is full */
            if (done)
                *done = total;
            return GHAL_ERR_NOSPACE;
        }
        total += (size_t)r;
    }
    if (done)
        *done = total;
    return GHAL_OK;
}

ghal_status ghal_fs_seek(ghal_file *f, int64_t offset, int whence,
                         uint64_t *pos)
{
    int w = whence == GHAL_FS_SEEK_CUR ? SEEK_CUR :
            whence == GHAL_FS_SEEK_END ? SEEK_END : SEEK_SET;
    long r;

    if (offset > 0x7fffffffL || offset < -0x7fffffffL)
        return GHAL_ERR_INVAL;
    r = lseek(f->fd, (long)offset, w);
    if (r < 0)
        return Err(errno);
    if (pos)
        *pos = (uint64_t)(unsigned long)r;
    return GHAL_OK;
}

ghal_status ghal_fs_truncate(ghal_file *f, uint64_t size)
{
    if (size > 0x7fffffffUL)
        return GHAL_ERR_INVAL;
#ifdef __WATCOMC__
    return chsize(f->fd, (long)size) == 0 ? GHAL_OK : Err(errno);
#else
    return ftruncate(f->fd, (off_t)size) == 0 ? GHAL_OK : Err(errno);
#endif
}

ghal_status ghal_fs_sync(ghal_file *f)
{
#ifdef __WATCOMC__
    return _dos_commit(f->fd) == 0 ? GHAL_OK : GHAL_ERR_HOST;
#else
    return fsync(f->fd) == 0 ? GHAL_OK : Err(errno);
#endif
}

ghal_status ghal_fs_fstat(ghal_file *f, ghal_fs_info *info)
{
    struct stat st;

    if (fstat(f->fd, &st) != 0)
        return Err(errno);
    ToInfo(&st, 0, info);
    return GHAL_OK;
}

ghal_status ghal_fs_stat(const char *path, ghal_fs_info *info)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return Err(errno);
    ToInfo(&st, DosAttrs(path), info);
    return GHAL_OK;
}

ghal_status ghal_fs_set_attrs(const char *path, unsigned attrs)
{
    unsigned a = attrs & (GHAL_FS_ATTR_READONLY | GHAL_FS_ATTR_HIDDEN |
                          GHAL_FS_ATTR_SYSTEM | GHAL_FS_ATTR_ARCHIVE);

    return _dos_setfileattr(path, a) == 0 ? GHAL_OK : GHAL_ERR_NOTFOUND;
}

ghal_status ghal_fs_set_mtime(const char *path, const ghal_datetime *dt)
{
    struct tm tm;
    struct utimbuf ut;
    time_t t;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = dt->year - 1900;
    tm.tm_mon = dt->month - 1;
    tm.tm_mday = dt->day;
    tm.tm_hour = dt->hour;
    tm.tm_min = dt->minute;
    tm.tm_sec = dt->second;
    tm.tm_isdst = -1;
    t = mktime(&tm);
    if (t == (time_t)-1)
        return GHAL_ERR_INVAL;
    ut.actime = t;
    ut.modtime = t;
    return utime(path, &ut) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_delete(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return GHAL_ERR_ISDIR;
    return unlink(path) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_rename(const char *from, const char *to)
{
    struct stat st;

    if (stat(to, &st) == 0)
        return GHAL_ERR_EXISTS;
    return rename(from, to) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_mkdir(const char *path)
{
#ifdef __WATCOMC__
    return mkdir(path) == 0 ? GHAL_OK : Err(errno);
#else
    return mkdir(path, 0777) == 0 ? GHAL_OK : Err(errno);
#endif
}

ghal_status ghal_fs_rmdir(const char *path)
{
    ghal_dir *d;
    char name[16];

    /* DOS reports a directory that is not empty as access denied */
    if (ghal_fs_opendir(path, &d) == GHAL_OK) {
        ghal_status s = ghal_fs_readdir(d, name, sizeof(name), NULL);
        ghal_fs_closedir(d);
        if (s == GHAL_OK)
            return GHAL_ERR_NOTEMPTY;
    }
    return rmdir(path) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_opendir(const char *path, ghal_dir **out)
{
    ghal_dir *d;
    struct stat st;

    *out = NULL;
    if (stat(path, &st) != 0)
        return Err(errno);
    if (!S_ISDIR(st.st_mode))
        return GHAL_ERR_NOTDIR;
    d = malloc(sizeof(*d));
    if (!d)
        return GHAL_ERR_NOMEM;
    d->path = malloc(strlen(path) + 1);
    if (!d->path) {
        free(d);
        return GHAL_ERR_NOMEM;
    }
    strcpy(d->path, path);
    d->dir = opendir(path);
    if (!d->dir) {
        free(d->path);
        free(d);
        return Err(errno);
    }
    *out = d;
    return GHAL_OK;
}

ghal_status ghal_fs_readdir(ghal_dir *d, char *name, size_t size,
                            ghal_fs_info *info)
{
    struct dirent *e;

    for (;;) {
        e = readdir(d->dir);
        if (!e)
            return GHAL_ERR_END;
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            break;
    }
    if (strlen(e->d_name) >= size)
        return GHAL_ERR_INVAL;
    strcpy(name, e->d_name);
    if (info) {
        size_t n = strlen(d->path) + strlen(e->d_name) + 2;
        char *full = malloc(n);
        if (!full)
            return GHAL_ERR_NOMEM;
        sprintf(full, "%s\\%s", d->path, e->d_name);
        if (ghal_fs_stat(full, info) != GHAL_OK)
            memset(info, 0, sizeof(*info));
        free(full);
    }
    return GHAL_OK;
}

void ghal_fs_closedir(ghal_dir *d)
{
    if (!d)
        return;
    closedir(d->dir);
    free(d->path);
    free(d);
}

ghal_status ghal_fs_xattr_get(const char *path, const char *name, void *buf,
                              size_t *size)
{
    (void)path; (void)name; (void)buf; (void)size;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status ghal_fs_xattr_set(const char *path, const char *name,
                              const void *buf, size_t size)
{
    (void)path; (void)name; (void)buf; (void)size;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status ghal_fs_xattr_remove(const char *path, const char *name)
{
    (void)path; (void)name;
    return GHAL_ERR_UNSUPPORTED;
}
