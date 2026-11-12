/*
 * gapi_unix.h -- Unix domain sockets for the Linux GAPI backend: the
 * connection under the D-Bus client (gapi_dbus.c), the journal
 * (gapi_journal.c) and later CUPS.
 */
#ifndef GAPI_UNIX_H
#define GAPI_UNIX_H

#include "ghal.h"

#include <stddef.h>

/* Connect a stream socket to an address "unix:path=/x", "unix:abstract=x"
 * (D-Bus address syntax; the first of several separated by ';' that
 * works) or a plain path.  Returns the descriptor or -1. */
int gapi_unix_connect(const char *address);

/* Write all of buf; GHAL_ERR_HOST if the peer went away. */
ghal_status gapi_unix_send(int fd, const void *buf, size_t n);

/* Read what is there, at most n bytes, waiting up to timeoutMs (-1:
 * forever).  Returns the count, 0 on timeout, -1 on error or end. */
long gapi_unix_recv(int fd, void *buf, size_t n, int timeoutMs);

/* Send one datagram to a socket path (the journal). */
ghal_status gapi_unix_send_datagram(const char *path, const void *buf,
                                    size_t n);

#endif /* GAPI_UNIX_H */
