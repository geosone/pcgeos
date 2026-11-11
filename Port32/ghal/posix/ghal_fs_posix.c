/*
 * ghal_fs_posix.c -- ghal_fs for POSIX hosts (ghal.h).
 *
 * Plain POSIX file calls; named attributes are the extended attributes
 * "user.geos.<name>" (Linux xattrs).  The DOS attribute bits are mapped
 * as far as POSIX has them: READONLY is "no write permission for
 * anybody", DIR is a directory; HIDDEN, SYSTEM and ARCHIVE are kept in
 * the attribute "user.geos.dosattr" when the file system can.
 */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include "ghal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

struct ghal_file {
    int fd;
};

struct ghal_dir {
    DIR  *dir;
    char *path;
};

#define XATTR_PREFIX "user.geos."
#define DOSATTR_XATTR "dosattr"
#define DOSATTR_KEPT (GHAL_FS_ATTR_HIDDEN | GHAL_FS_ATTR_SYSTEM | \
                      GHAL_FS_ATTR_ARCHIVE)

static ghal_status Err(int e)
{
    switch (e) {
    case ENOENT:        return GHAL_ERR_NOTFOUND;
    case EEXIST:        return GHAL_ERR_EXISTS;
    case EACCES:
    case EPERM:
    case EROFS:
    case EBUSY:
    case ETXTBSY:       return GHAL_ERR_ACCESS;
    case ENOSPC:
    case EDQUOT:        return GHAL_ERR_NOSPACE;
    case ENOTEMPTY:     return GHAL_ERR_NOTEMPTY;
    case EISDIR:        return GHAL_ERR_ISDIR;
    case ENOTDIR:       return GHAL_ERR_NOTDIR;
    case ENOMEM:        return GHAL_ERR_NOMEM;
    case EINVAL:        return GHAL_ERR_INVAL;
    case ENOTSUP:       return GHAL_ERR_UNSUPPORTED;
    default:            return GHAL_ERR_HOST;
    }
}

static void ToDatetime(time_t t, ghal_datetime *dt)
{
    struct tm tm;

    memset(dt, 0, sizeof(*dt));
    if (!localtime_r(&t, &tm))
        return;
    dt->year = (uint16_t)(tm.tm_year + 1900);
    dt->month = (uint8_t)(tm.tm_mon + 1);
    dt->day = (uint8_t)tm.tm_mday;
    dt->hour = (uint8_t)tm.tm_hour;
    dt->minute = (uint8_t)tm.tm_min;
    dt->second = (uint8_t)(tm.tm_sec > 59 ? 59 : tm.tm_sec);
    dt->weekday = (uint8_t)tm.tm_wday;
}

static unsigned DosAttrOfPath(const char *path)
{
    unsigned char a;
    ssize_t n = getxattr(path, XATTR_PREFIX DOSATTR_XATTR, &a, 1);

    return n == 1 ? (a & DOSATTR_KEPT) : 0;
}

static void ToInfo(const struct stat *st, unsigned dosattr, ghal_fs_info *info)
{
    info->size = S_ISDIR(st->st_mode) ? 0 : (uint64_t)st->st_size;
    ToDatetime(st->st_mtime, &info->mtime);
    info->attrs = dosattr;
    if (S_ISDIR(st->st_mode))
        info->attrs |= GHAL_FS_ATTR_DIR;
    if (!(st->st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
        info->attrs |= GHAL_FS_ATTR_READONLY;
}

ghal_status ghal_fs_open(const char *path, unsigned flags, ghal_file **out)
{
    int oflags, fd;
    struct stat st;
    ghal_file *f;

    if (!path || !out || !(flags & (GHAL_FS_READ | GHAL_FS_WRITE)))
        return GHAL_ERR_INVAL;
    *out = NULL;
    oflags = (flags & GHAL_FS_WRITE) ?
             ((flags & GHAL_FS_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (flags & GHAL_FS_CREATE)
        oflags |= O_CREAT;
    if (flags & GHAL_FS_TRUNCATE)
        oflags |= O_TRUNC;
    if (flags & GHAL_FS_EXCL)
        oflags |= O_EXCL;
    fd = open(path, oflags | O_CLOEXEC, 0666);
    if (fd < 0)
        return Err(errno);
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        return GHAL_ERR_ISDIR;
    }
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
        ssize_t r = read(f->fd, (char *)buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR)
                continue;
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
        ssize_t r = write(f->fd, (const char *)buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            if (done)
                *done = total;
            return Err(errno);
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
    off_t r = lseek(f->fd, (off_t)offset, w);

    if (r < 0)
        return Err(errno);
    if (pos)
        *pos = (uint64_t)r;
    return GHAL_OK;
}

ghal_status ghal_fs_truncate(ghal_file *f, uint64_t size)
{
    return ftruncate(f->fd, (off_t)size) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_sync(ghal_file *f)
{
    return fsync(f->fd) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_fstat(ghal_file *f, ghal_fs_info *info)
{
    struct stat st;
    unsigned char a;
    ssize_t n;

    if (fstat(f->fd, &st) != 0)
        return Err(errno);
    n = fgetxattr(f->fd, XATTR_PREFIX DOSATTR_XATTR, &a, 1);
    ToInfo(&st, n == 1 ? (a & DOSATTR_KEPT) : 0, info);
    return GHAL_OK;
}

ghal_status ghal_fs_stat(const char *path, ghal_fs_info *info)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return Err(errno);
    if (info)
        ToInfo(&st, DosAttrOfPath(path), info);
    return GHAL_OK;
}

ghal_status ghal_fs_set_attrs(const char *path, unsigned attrs)
{
    struct stat st;
    mode_t mode;
    unsigned char a = (unsigned char)(attrs & DOSATTR_KEPT);

    if (stat(path, &st) != 0)
        return Err(errno);
    mode = st.st_mode & 07777;
    if (attrs & GHAL_FS_ATTR_READONLY)
        mode &= (mode_t)~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        mode |= S_IWUSR;
    if (chmod(path, mode) != 0)
        return Err(errno);
    if (a)
        (void)setxattr(path, XATTR_PREFIX DOSATTR_XATTR, &a, 1, 0);
    else
        (void)removexattr(path, XATTR_PREFIX DOSATTR_XATTR);
    return GHAL_OK;
}

static int ToTimes(const ghal_datetime *dt, struct timespec ts[2])
{
    struct tm tm;
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
        return 0;
    ts[0].tv_sec = 0;
    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = t;
    ts[1].tv_nsec = 0;
    return 1;
}

ghal_status ghal_fs_set_mtime(const char *path, const ghal_datetime *dt)
{
    struct timespec ts[2];

    if (!ToTimes(dt, ts))
        return GHAL_ERR_INVAL;
    return utimensat(AT_FDCWD, path, ts, 0) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_set_fmtime(ghal_file *f, const ghal_datetime *dt)
{
    struct timespec ts[2];

    if (!ToTimes(dt, ts))
        return GHAL_ERR_INVAL;
    return futimens(f->fd, ts) == 0 ? GHAL_OK : Err(errno);
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

    /* DOS semantics: never replace an existing file */
    if (lstat(to, &st) == 0)
        return GHAL_ERR_EXISTS;
    return rename(from, to) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_mkdir(const char *path)
{
    return mkdir(path, 0777) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_rmdir(const char *path)
{
    return rmdir(path) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_opendir(const char *path, ghal_dir **out)
{
    ghal_dir *d;

    *out = NULL;
    d = malloc(sizeof(*d));
    if (!d)
        return GHAL_ERR_NOMEM;
    d->path = strdup(path);
    d->dir = d->path ? opendir(path) : NULL;
    if (!d->dir) {
        ghal_status e = d->path ? Err(errno) : GHAL_ERR_NOMEM;
        free(d->path);
        free(d);
        return e;
    }
    *out = d;
    return GHAL_OK;
}

ghal_status ghal_fs_readdir(ghal_dir *d, char *name, size_t size,
                            ghal_fs_info *info)
{
    struct dirent *e;

    for (;;) {
        errno = 0;
        e = readdir(d->dir);
        if (!e)
            return errno ? Err(errno) : GHAL_ERR_END;
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            break;
    }
    if (strlen(e->d_name) >= size)
        return GHAL_ERR_INVAL;
    strcpy(name, e->d_name);
    if (info) {
        size_t n = strlen(d->path) + strlen(e->d_name) + 2;
        char *full = malloc(n);
        struct stat st;
        if (!full)
            return GHAL_ERR_NOMEM;
        snprintf(full, n, "%s/%s", d->path, e->d_name);
        if (stat(full, &st) == 0)
            ToInfo(&st, DosAttrOfPath(full), info);
        else
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

static int XattrName(char *buf, size_t size, const char *name)
{
    return snprintf(buf, size, XATTR_PREFIX "%s", name) < (int)size;
}

ghal_status ghal_fs_xattr_get(const char *path, const char *name, void *buf,
                              size_t *size)
{
    char xn[256];
    ssize_t n;

    if (!XattrName(xn, sizeof(xn), name))
        return GHAL_ERR_INVAL;
    n = getxattr(path, xn, buf, *size);
    if (n < 0) {
        if (errno == ENODATA)
            return GHAL_ERR_NOTFOUND;
        if (errno == ERANGE) {
            n = getxattr(path, xn, NULL, 0);
            if (n >= 0)
                *size = (size_t)n;
            return GHAL_ERR_INVAL;
        }
        return Err(errno);
    }
    *size = (size_t)n;
    return GHAL_OK;
}

ghal_status ghal_fs_xattr_set(const char *path, const char *name,
                              const void *buf, size_t size)
{
    char xn[256];

    if (!XattrName(xn, sizeof(xn), name))
        return GHAL_ERR_INVAL;
    return setxattr(path, xn, buf, size, 0) == 0 ? GHAL_OK : Err(errno);
}

ghal_status ghal_fs_xattr_remove(const char *path, const char *name)
{
    char xn[256];

    if (!XattrName(xn, sizeof(xn), name))
        return GHAL_ERR_INVAL;
    if (removexattr(path, xn) == 0)
        return GHAL_OK;
    return errno == ENODATA ? GHAL_ERR_NOTFOUND : Err(errno);
}
