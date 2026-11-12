/*
 * gapi_journal.c -- the log to systemd's journal (gapi_log_to_system),
 * by its native protocol: one datagram of FIELD=value lines to
 * /run/systemd/journal/socket.
 */
#include "gapi.h"
#include "gapi_unix.h"

#include <stdio.h>
#include <string.h>

#define JOURNAL_SOCKET "/run/systemd/journal/socket"

static void JournalSink(int level, const char *msg)
{
    static const int prio[] = { 7, 6, 4, 3 };   /* debug info warning err */
    char buf[1200];
    int n;
    char *p;

    n = snprintf(buf, sizeof buf, "PRIORITY=%d\nSYSLOG_IDENTIFIER=geos\n"
                 "MESSAGE=%s\n", prio[level & 3], msg);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof buf) {
        n = (int)sizeof buf - 1;
        buf[n - 1] = '\n';
    }
    /* a newline inside the message would end the field */
    p = strstr(buf, "MESSAGE=");
    for (p += 8; p < buf + n - 1; p++)
        if (*p == '\n')
            *p = ' ';
    if (gapi_unix_send_datagram(JOURNAL_SOCKET, buf, (size_t)n) != GHAL_OK)
        fprintf(stderr, "geos: %s\n", msg);
}

ghal_status gapi_log_to_system(void)
{
    static const char probe[] = "PRIORITY=7\nSYSLOG_IDENTIFIER=geos\n"
                                "MESSAGE=GEOS logs to the journal\n";
    ghal_status st = gapi_unix_send_datagram(JOURNAL_SOCKET, probe,
                                             sizeof probe - 1);
    if (st != GHAL_OK)
        return st;
    ghal_log_set_sink(JournalSink);
    return GHAL_OK;
}
