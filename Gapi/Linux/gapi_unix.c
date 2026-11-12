/*
 * gapi_unix.c -- Unix domain sockets for the Linux GAPI backend
 * (gapi_unix.h).
 */
#define _GNU_SOURCE
#include "gapi_unix.h"

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* one "unix:..." entry of n characters, or a plain path */
static int connect_one(const char *a, size_t n)
{
    struct sockaddr_un sa;
    socklen_t len;
    int abstract = 0, fd;
    const char *p, *v = NULL;
    size_t vn = 0;

    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (n >= 5 && memcmp(a, "unix:", 5) == 0) {
        /* key=value pairs separated by ',' */
        for (p = a + 5; p < a + n; ) {
            const char *e = memchr(p, ',', (size_t)(a + n - p));
            size_t kn;
            if (!e)
                e = a + n;
            kn = (size_t)(e - p);
            if (kn > 5 && memcmp(p, "path=", 5) == 0) {
                v = p + 5; vn = kn - 5; abstract = 0;
            } else if (kn > 9 && memcmp(p, "abstract=", 9) == 0) {
                v = p + 9; vn = kn - 9; abstract = 1;
            }
            p = e + 1;
        }
        if (!v)
            return -1;
    } else {
        v = a;
        vn = n;
    }
    if (vn + 1 + abstract > sizeof sa.sun_path)
        return -1;
    memcpy(sa.sun_path + abstract, v, vn);
    len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + abstract +
                      vn + (abstract ? 0 : 1));
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (struct sockaddr *)&sa, len) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int gapi_unix_connect(const char *address)
{
    const char *p = address;

    if (!address)
        return -1;
    while (*p) {
        const char *e = strchr(p, ';');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        int fd = n ? connect_one(p, n) : -1;
        if (fd >= 0)
            return fd;
        if (!e)
            break;
        p = e + 1;
    }
    return -1;
}

ghal_status gapi_unix_send(int fd, const void *buf, size_t n)
{
    const char *p = buf;

    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return GHAL_ERR_HOST;
        }
        p += w;
        n -= (size_t)w;
    }
    return GHAL_OK;
}

long gapi_unix_recv(int fd, void *buf, size_t n, int timeoutMs)
{
    struct pollfd pf;
    ssize_t r;
    int k;

    pf.fd = fd;
    pf.events = POLLIN;
    do
        k = poll(&pf, 1, timeoutMs);
    while (k < 0 && errno == EINTR);
    if (k < 0)
        return -1;
    if (k == 0)
        return 0;
    do
        r = recv(fd, buf, n, 0);
    while (r < 0 && errno == EINTR);
    return r > 0 ? (long)r : -1;
}

ghal_status gapi_unix_send_datagram(const char *path, const void *buf,
                                    size_t n)
{
    struct sockaddr_un sa;
    int fd;
    ssize_t w;

    if (strlen(path) >= sizeof sa.sun_path)
        return GHAL_ERR_INVAL;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);
    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return GHAL_ERR_HOST;
    w = sendto(fd, buf, n, MSG_NOSIGNAL, (struct sockaddr *)&sa, sizeof sa);
    close(fd);
    if (w < 0)
        return errno == ENOENT || errno == ECONNREFUSED ?
            GHAL_ERR_UNSUPPORTED : GHAL_ERR_HOST;
    return GHAL_OK;
}
