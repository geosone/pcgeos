/*
 * test_gapi_print.c -- the host's printers through GEOS's own IPP client
 * (Gapi/Linux/gapi_ipp.c).
 *
 * A small IPP server in a child process stands in for CUPS
 * ($CUPS_SERVER is its socket): two printers, a default, and a job whose
 * attributes and document it checks, returning job id 42.  A cancelled
 * job must reach it without its last chunk.  Nothing is ever printed:
 * against the real CUPS (if there is one) the test only lists.
 */
#define _GNU_SOURCE
#include "gapi.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define DOC "%!PS-Adobe-3.0\n%%Title: test\nshowpage\n%%EOF\n"

/* ---------------------------------------------------------------------
 * The stand-in for CUPS
 * ------------------------------------------------------------------- */

typedef struct { unsigned char b[65536]; size_t n; } Buf;

static void Add(Buf *b, const void *p, size_t n)
{
    if (b->n + n <= sizeof b->b) {
        memcpy(b->b + b->n, p, n);
        b->n += n;
    }
}
static void Add8(Buf *b, int v) { unsigned char c = (unsigned char)v; Add(b, &c, 1); }
static void Add16(Buf *b, unsigned v) { Add8(b, (int)(v >> 8)); Add8(b, (int)v); }
static void AttrS(Buf *b, int tag, const char *name, const char *v)
{
    Add8(b, tag);
    Add16(b, (unsigned)strlen(name));
    Add(b, name, strlen(name));
    Add16(b, (unsigned)strlen(v));
    Add(b, v, strlen(v));
}
static void AttrI(Buf *b, int tag, const char *name, unsigned v)
{
    Add8(b, tag);
    Add16(b, (unsigned)strlen(name));
    Add(b, name, strlen(name));
    Add16(b, 4);
    Add16(b, v >> 16);
    Add16(b, v & 0xffff);
}
static void AttrB(Buf *b, const char *name, int v)
{
    Add8(b, 0x22);
    Add16(b, (unsigned)strlen(name));
    Add(b, name, strlen(name));
    Add16(b, 1);
    Add8(b, v);
}

/* read one request; 1: whole, 0: the connection ended inside the
 * chunks (a cancelled job), -1: nothing */
static int ReadRequest(int fd, Buf *body)
{
    static Buf in;
    char *he;
    size_t pos;
    int chunked;

    in.n = 0;
    body->n = 0;
    for (;;) {
        ssize_t k;
        in.b[in.n] = 0;
        if ((he = strstr((char *)in.b, "\r\n\r\n")))
            break;
        k = read(fd, in.b + in.n, sizeof in.b - 1 - in.n);
        if (k <= 0)
            return -1;
        in.n += (size_t)k;
    }
    chunked = strstr((char *)in.b, "chunked") != NULL;
    pos = (size_t)(he - (char *)in.b) + 4;
    for (;;) {
        if (!chunked) {
            long cl = strtol(strstr((char *)in.b, "Content-Length:") + 15, 0, 10);
            if (in.n >= pos + (size_t)cl) {
                Add(body, in.b + pos, (size_t)cl);
                return 1;
            }
        } else {
            char *le;
            in.b[in.n] = 0;
            le = memmem(in.b + pos, in.n - pos, "\r\n", 2);
            if (le) {
                size_t n = strtoul((char *)in.b + pos, 0, 16);
                size_t d = (size_t)(le - (char *)in.b) + 2;
                if (in.n >= d + n + 2) {
                    if (!n)
                        return 1;
                    Add(body, in.b + d, n);
                    pos = d + n + 2;
                    continue;
                }
            }
        }
        {
            ssize_t k = read(fd, in.b + in.n, sizeof in.b - 1 - in.n);
            if (k <= 0)
                return 0;
            in.n += (size_t)k;
        }
    }
}

/* the first value of an attribute of the request, as a string */
static int Find(const Buf *b, const char *want, char *out, size_t size,
                size_t *docOff)
{
    size_t pos = 8;
    int found = 0;
    char name[128] = "";

    while (pos < b->n) {
        int tag = b->b[pos++];
        size_t nn, vn;
        if (tag == 0x03) {
            *docOff = pos;
            return found;
        }
        if (tag < 0x10)
            continue;
        nn = (size_t)b->b[pos] << 8 | b->b[pos + 1];
        pos += 2;
        if (nn) {
            memcpy(name, b->b + pos, nn);
            name[nn] = 0;
        }
        pos += nn;
        vn = (size_t)b->b[pos] << 8 | b->b[pos + 1];
        pos += 2;
        if (!found && nn && !strcmp(name, want)) {
            if (vn == 4 && (tag == 0x21 || tag == 0x23))
                snprintf(out, size, "%u", (unsigned)b->b[pos] << 24 |
                         (unsigned)b->b[pos + 1] << 16 |
                         (unsigned)b->b[pos + 2] << 8 | b->b[pos + 3]);
            else
                snprintf(out, size, "%.*s", (int)vn, b->b + pos);
            found = 1;
        }
        pos += vn;
    }
    *docOff = b->n;
    return found;
}

static void Respond(int fd, unsigned status, Buf *attrs)
{
    static Buf r;
    char h[160];

    r.n = 0;
    Add16(&r, 0x0200);
    Add16(&r, status);
    Add16(&r, 0);
    Add16(&r, 1);
    Add8(&r, 0x01);
    AttrS(&r, 0x47, "attributes-charset", "utf-8");
    AttrS(&r, 0x48, "attributes-natural-language", "en");
    if (attrs)
        Add(&r, attrs->b, attrs->n);
    Add8(&r, 0x03);
    snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: application/ipp"
             "\r\nContent-Length: %zu\r\n\r\n", r.n);
    (void)!write(fd, h, strlen(h));
    (void)!write(fd, r.b, r.n);
}

static void Serve(int ls, int report)
{
    static Buf body, a;
    struct pollfd pf;

    pf.fd = ls;
    pf.events = POLLIN;
    while (poll(&pf, 1, 10000) > 0) {
        int fd = accept(ls, 0, 0), whole;
        unsigned op;
        char v[256], ok = 'J';
        size_t doc;

        if (fd < 0)
            break;
        whole = ReadRequest(fd, &body);
        if (whole < 0 || body.n < 8) {
            close(fd);
            continue;
        }
        op = (unsigned)body.b[2] << 8 | body.b[3];
        a.n = 0;
        if (whole == 0) {
            (void)!write(report, "C", 1);       /* cancelled */
        } else if (op == 0x4002) {
            Add8(&a, 0x04);
            AttrS(&a, 0x42, "printer-name", "fake1");
            /* printer-info as nameWithLanguage */
            Add8(&a, 0x36);
            Add16(&a, 12);
            Add(&a, "printer-info", 12);
            Add16(&a, 2 + 2 + 2 + 9);
            Add16(&a, 2);
            Add(&a, "de", 2);
            Add16(&a, 9);
            Add(&a, "Drucker 1", 9);
            AttrI(&a, 0x23, "printer-state", 3);
            AttrB(&a, "printer-is-accepting-jobs", 1);
            AttrB(&a, "color-supported", 0);
            AttrS(&a, 0x44, "sides-supported", "one-sided");
            Add8(&a, 0x04);
            AttrS(&a, 0x42, "printer-name", "fake2");
            AttrS(&a, 0x41, "printer-location", "Office");
            AttrI(&a, 0x23, "printer-state", 5);
            AttrB(&a, "printer-is-accepting-jobs", 0);
            AttrB(&a, "color-supported", 1);
            AttrS(&a, 0x44, "sides-supported", "one-sided");
            Add8(&a, 0x44); Add16(&a, 0); Add16(&a, 19);
            Add(&a, "two-sided-long-edge", 19);
            Respond(fd, 0, &a);
            (void)!write(report, "L", 1);
        } else if (op == 0x4001) {
            Add8(&a, 0x04);
            AttrS(&a, 0x42, "printer-name", "fake2");
            Respond(fd, 0, &a);
            (void)!write(report, "D", 1);
        } else if (op == 0x0002) {
            if (!Find(&body, "printer-uri", v, sizeof v, &doc) ||
                strcmp(v, "ipp://localhost/printers/fake1"))
                ok = 'u';
            else if (!Find(&body, "document-format", v, sizeof v, &doc) ||
                     strcmp(v, "application/postscript"))
                ok = 'f';
            else if (!Find(&body, "job-name", v, sizeof v, &doc) ||
                     strcmp(v, "A GEOS document"))
                ok = 'n';
            else if (!Find(&body, "copies", v, sizeof v, &doc) ||
                     strcmp(v, "2"))
                ok = 'c';
            else if (!Find(&body, "sides", v, sizeof v, &doc) ||
                     strcmp(v, "two-sided-long-edge"))
                ok = 's';
            else if (body.n - doc != strlen(DOC) ||
                     memcmp(body.b + doc, DOC, strlen(DOC)))
                ok = 'd';
            if (ok == 'J') {
                Add8(&a, 0x02);
                AttrI(&a, 0x21, "job-id", 42);
                Respond(fd, 0, &a);
            } else {
                Respond(fd, 0x0400, NULL);
            }
            (void)!write(report, &ok, 1);
        } else {
            Respond(fd, 0x0501, NULL);
            (void)!write(report, "?", 1);
        }
        close(fd);
    }
}

/* ---------------------------------------------------------------------
 * The test
 * ------------------------------------------------------------------- */

int main(void)
{
    struct sockaddr_un sa;
    gapi_printer p[4];
    gapi_print_job *job;
    char path[108], def[GAPI_PRINT_NAME_SIZE], got[16];
    uint32_t id;
    int ls, pfd[2], n;
    pid_t child;
    ssize_t k;

    unsetenv("LPDEST");
    unsetenv("PRINTER");

    /* the real CUPS, if there is one: only list */
    unsetenv("CUPS_SERVER");
    if (gapi_print_list(p, 4, &n) == GHAL_OK)
        printf("the host's CUPS has %d printer(s)\n", n);
    else
        printf("no CUPS here (%s)\n", gapi_last_error());

    {
        /* an absolute path: CUPS_SERVER is a socket only if it starts
         * with '/' */
        const char *t = getenv("TMPDIR");
        char cwd[64];
        if (!t || t[0] != '/' || strlen(t) >= 60)
            t = getcwd(cwd, sizeof cwd) ? cwd : "/tmp";
        snprintf(path, sizeof path, "%s/gapi_ipp_%d.sock", t, (int)getpid());
    }
    unlink(path);
    ls = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);
    if (ls < 0 || bind(ls, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(ls, 4) != 0 || pipe(pfd) != 0) {
        printf("FAIL: no test socket\n");
        return 1;
    }
    child = fork();
    if (child == 0) {
        close(pfd[0]);
        Serve(ls, pfd[1]);
        _exit(0);
    }
    close(pfd[1]);
    close(ls);
    setenv("CUPS_SERVER", path, 1);

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
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    k = read(pfd[0], got, sizeof got - 1);
    got[k > 0 ? k : 0] = 0;
    printf("the server saw: %s\n", got);
    CHECK(!strcmp(got, "LDLDDJCd"));   /* each list asks for the default */
    unlink(path);

    /* no CUPS at all */
    setenv("CUPS_SERVER", "/nonexistent/cups.sock", 1);
    CHECK(gapi_print_list(p, 4, &n) == GHAL_ERR_UNSUPPORTED && n == 0);

    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
           fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
