/*
 * fakecups.h -- a stand-in for CUPS in the tests (fakecups.c): an IPP
 * server on a Unix socket in a child process.
 *
 * It knows two printers (fake1: "Drucker 1", idle, one-sided, no color,
 * US letter; fake2: in the "Office", stopped, not accepting, color,
 * two-sided, A4, the default).  A Print-Job is checked against fakeCupsJob: it answers job
 * id 42 when everything matches, else IPP status 0x0400.
 *
 * It reports one letter per request on a pipe: L (printers), D
 * (default), J (a matching job), C (a job whose connection ended inside
 * the chunks: cancelled), u/f/n/c/s/d (a job whose printer-uri, format,
 * name, copies, sides or document differ), ? (another operation).
 */
#ifndef FAKECUPS_H
#define FAKECUPS_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    const char *uri, *format, *name;
    const char *copies;         /* NULL: must not be there */
    const char *sides;
    const void *doc;
    size_t      docLen;
} FakeCupsJob;

extern FakeCupsJob fakeCupsJob;     /* set before FakeCupsStart */

/* Start it; sets $CUPS_SERVER to its socket.  0 on failure. */
pid_t FakeCupsStart(void);
/* The job it expects in shared memory, with a buffer of size bytes for
 * the document (doc): the test may change both after the start.  A copy
 * of fakeCupsJob; call before FakeCupsStart.  NULL on failure. */
FakeCupsJob *FakeCupsShared(size_t size, char **doc);
/* Stop it; the letters it reported, in got. */
void  FakeCupsStop(pid_t child, char *got, size_t size);

#endif /* FAKECUPS_H */
