/*
 * fakecups.h -- a stand-in for CUPS in the tests (fakecups.c): an IPP
 * server on a Unix socket in a child process.
 *
 * It knows two printers (fake1: "Drucker 1", idle, one-sided, no color;
 * fake2: "Office", stopped, not accepting, color, two-sided, the
 * default).  A Print-Job is checked against fakeCupsJob: it answers job
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
    /* if set, the document and its length are read from here at each
     * job: shared memory (FakeCupsShared) the test fills after the start */
    const volatile size_t *docLenp;
} FakeCupsJob;

extern FakeCupsJob fakeCupsJob;     /* set before FakeCupsStart */

/* Start it; sets $CUPS_SERVER to its socket.  0 on failure. */
pid_t FakeCupsStart(void);
/* A shared buffer of size bytes for the document, filled later; its
 * length is *lenp.  fakeCupsJob.doc and docLenp point to them.  Call
 * before FakeCupsStart.  NULL on failure. */
void *FakeCupsShared(size_t size, size_t **lenp);
/* Stop it; the letters it reported, in got. */
void  FakeCupsStop(pid_t child, char *got, size_t size);

#endif /* FAKECUPS_H */
