/*
 * test_gapi_print.c -- the host's printers through GEOS's own IPP client
 * (Gapi/Linux/gapi_ipp.c).
 *
 * A small IPP server in a child process stands in for CUPS
 * (fakecups.c; $CUPS_SERVER is its socket): two printers, a default, and a job whose
 * attributes and document it checks, returning job id 42.  A cancelled
 * job must reach it without its last chunk.  Nothing is ever printed:
 * against the real CUPS (if there is one) the test only lists.
 */
#define _GNU_SOURCE
#include "gapi.h"
#include "fakecups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define DOC "%!PS-Adobe-3.0\n%%Title: test\nshowpage\n%%EOF\n"

/* ---------------------------------------------------------------------
 * The test
 * ------------------------------------------------------------------- */

int main(void)
{
    gapi_printer p[4];
    gapi_print_job *job;
    char def[GAPI_PRINT_NAME_SIZE], got[16];
    uint32_t id;
    int n;
    pid_t child;

    unsetenv("LPDEST");
    unsetenv("PRINTER");

    /* the real CUPS, if there is one: only list */
    unsetenv("CUPS_SERVER");
    if (gapi_print_list(p, 4, &n) == GHAL_OK)
        printf("the host's CUPS has %d printer(s)\n", n);
    else
        printf("no CUPS here (%s)\n", gapi_last_error());

    fakeCupsJob.uri = "ipp://localhost/printers/fake1";
    fakeCupsJob.format = "application/postscript";
    fakeCupsJob.name = "A GEOS document";
    fakeCupsJob.copies = "2";
    fakeCupsJob.sides = "two-sided-long-edge";
    fakeCupsJob.doc = DOC;
    fakeCupsJob.docLen = strlen(DOC);
    child = FakeCupsStart();
    if (!child) {
        printf("FAIL: no stand-in for CUPS\n");
        return 1;
    }

    /* the printers and the default */
    CHECK(gapi_print_list(p, 4, &n) == GHAL_OK && n == 2);
    CHECK(!strcmp(p[0].name, "fake1") && !strcmp(p[0].info, "Drucker 1"));
    CHECK(p[0].state == 3 && p[0].accepting && !p[0].color &&
          !p[0].duplex && !p[0].isDefault);
    CHECK(!strcmp(p[1].name, "fake2") && !strcmp(p[1].location, "Office"));
    CHECK(p[1].state == 5 && !p[1].accepting && p[1].color &&
          p[1].duplex && p[1].isDefault);
    CHECK(gapi_print_list(p, 1, &n) == GHAL_OK && n == 2);  /* only one fits */
    CHECK(gapi_print_default(def, sizeof def) == GHAL_OK &&
          !strcmp(def, "fake2"));
    setenv("LPDEST", "mine", 1);
    CHECK(gapi_print_default(def, sizeof def) == GHAL_OK &&
          !strcmp(def, "mine"));
    unsetenv("LPDEST");

    /* a job, written in pieces */
    CHECK(gapi_print_job_open("fake1", "A GEOS document", GAPI_DOC_POSTSCRIPT,
                              2, 1, &job) == GHAL_OK);
    CHECK(gapi_print_job_write(job, DOC, 15) == GHAL_OK);
    CHECK(gapi_print_job_write(job, DOC + 15, 0) == GHAL_OK);
    CHECK(gapi_print_job_write(job, DOC + 15, strlen(DOC) - 15) == GHAL_OK);
    CHECK(gapi_print_job_close(job, &id) == GHAL_OK && id == 42);
    /* a cancelled one */
    CHECK(gapi_print_job_open("fake1", "x", GAPI_DOC_PDF, 1, 0, &job)
          == GHAL_OK);
    CHECK(gapi_print_job_write(job, "%PDF-1.4\n", 9) == GHAL_OK);
    gapi_print_job_cancel(job);
    /* refused by the server: the document is not what it wants */
    CHECK(gapi_print_job_open("fake1", "A GEOS document", GAPI_DOC_POSTSCRIPT,
                              2, 1, &job) == GHAL_OK);
    CHECK(gapi_print_job_write(job, "%!PS\n", 5) == GHAL_OK);
    CHECK(gapi_print_job_close(job, &id) == GHAL_ERR_HOST && id == 0);
    /* bad arguments, checked before CUPS is asked */
    CHECK(gapi_print_job_open("a/b", "t", GAPI_DOC_PDF, 1, 0, &job)
          == GHAL_ERR_INVAL && !job);
    CHECK(gapi_print_job_open("fake1", "t", GAPI_DOC_PDF, 0, 0, &job)
          == GHAL_ERR_INVAL);

    /* what the server saw */
    FakeCupsStop(child, got, sizeof got);
    printf("the server saw: %s\n", got);
    CHECK(!strcmp(got, "LDLDDJCd"));   /* each list asks for the default */

    /* no CUPS at all */
    setenv("CUPS_SERVER", "/nonexistent/cups.sock", 1);
    CHECK(gapi_print_list(p, 4, &n) == GHAL_ERR_UNSUPPORTED && n == 0);

    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
           fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
