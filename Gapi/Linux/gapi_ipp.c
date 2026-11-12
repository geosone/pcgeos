/*
 * gapi_ipp.c -- the host's printers on Linux (gapi_print, CInclude/gapi.h):
 * a small IPP client of GEOS's own, over HTTP on the CUPS socket.
 *
 * IPP (RFC 8010/8011) is a binary request in an HTTP POST: version 2.0,
 * operation, request id, attribute groups (tag, then attributes as value
 * tag, name, value; a further value of the same attribute has an empty
 * name), end tag, then the document.  CUPS adds its operations
 * CUPS-Get-Printers and CUPS-Get-Default.
 *
 * A job sends its document as it comes, in HTTP chunks; closing it sends
 * the last chunk and reads the job id.  Cancelling closes the connection
 * without the last chunk, and CUPS drops the job.
 *
 * The socket: $CUPS_SERVER if it is a path (as the CUPS library), else
 * /run/cups/cups.sock.
 */
#define _GNU_SOURCE
#include "gapi.h"
#include "gapi_linux.h"
#include "gapi_unix.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* operations */
#define IPP_PRINT_JOB           0x0002
#define CUPS_GET_DEFAULT        0x4001
#define CUPS_GET_PRINTERS       0x4002

/* tags */
#define TAG_OPERATION   0x01
#define TAG_JOB         0x02
#define TAG_END         0x03
#define TAG_PRINTER     0x04
#define TAG_INTEGER     0x21
#define TAG_BOOLEAN     0x22
#define TAG_ENUM        0x23
#define TAG_TEXT_LANG   0x35
#define TAG_NAME_LANG   0x36
#define TAG_TEXT        0x41
#define TAG_NAME        0x42
#define TAG_KEYWORD     0x44
#define TAG_URI         0x45
#define TAG_CHARSET     0x47
#define TAG_LANGUAGE    0x48
#define TAG_MIMETYPE    0x49

#define TIMEOUT_MS      30000

/* ---------------------------------------------------------------------
 * Building a request
 * ------------------------------------------------------------------- */

typedef struct {
    uint8_t *b;
    size_t   len, cap;
    int      error;
} Buf;

static void Put(Buf *b, const void *p, size_t n)
{
    if (b->error)
        return;
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        uint8_t *nb;
        while (cap < b->len + n)
            cap *= 2;
        nb = realloc(b->b, cap);
        if (!nb) {
            b->error = 1;
            return;
        }
        b->b = nb;
        b->cap = cap;
    }
    memcpy(b->b + b->len, p, n);
    b->len += n;
}

static void Put8(Buf *b, int v)
{
    uint8_t c = (uint8_t)v;
    Put(b, &c, 1);
}

static void Put16(Buf *b, unsigned v)
{
    Put8(b, (int)(v >> 8));
    Put8(b, (int)v);
}

static void Put32(Buf *b, uint32_t v)
{
    Put16(b, v >> 16);
    Put16(b, v & 0xffff);
}

/* an attribute (name NULL: a further value of the last one) */
static void Attr(Buf *b, int tag, const char *name, const void *v, size_t n)
{
    size_t nn = name ? strlen(name) : 0;
    Put8(b, tag);
    Put16(b, (unsigned)nn);
    Put(b, name, nn);
    Put16(b, (unsigned)n);
    Put(b, v, n);
}

static void AttrStr(Buf *b, int tag, const char *name, const char *v)
{
    Attr(b, tag, name, v, strlen(v));
}

static void AttrInt(Buf *b, int tag, const char *name, int32_t v)
{
    uint8_t d[4];
    d[0] = (uint8_t)(v >> 24);
    d[1] = (uint8_t)(v >> 16);
    d[2] = (uint8_t)(v >> 8);
    d[3] = (uint8_t)v;
    Attr(b, tag, name, d, 4);
}

static uint32_t requestId;

static const char *UserName(void)
{
    struct passwd *pw = getpwuid(getuid());
    return pw && pw->pw_name ? pw->pw_name : "geos";
}

/* the header and the operation attributes every request has */
static void Begin(Buf *b, unsigned op)
{
    Put16(b, 0x0200);
    Put16(b, op);
    Put32(b, ++requestId);
    Put8(b, TAG_OPERATION);
    AttrStr(b, TAG_CHARSET, "attributes-charset", "utf-8");
    AttrStr(b, TAG_LANGUAGE, "attributes-natural-language", "en");
}

/* ---------------------------------------------------------------------
 * HTTP
 * ------------------------------------------------------------------- */

static int Connect(void)
{
    const char *s = getenv("CUPS_SERVER");
    int fd;

    if (s && s[0] == '/')
        return gapi_unix_connect(s);
    fd = gapi_unix_connect("/run/cups/cups.sock");
    if (fd < 0)
        fd = gapi_unix_connect("/var/run/cups/cups.sock");
    return fd;
}

static ghal_status SendHead(int fd, long contentLength)
{
    char h[256];

    if (contentLength >= 0)
        snprintf(h, sizeof h, "POST / HTTP/1.1\r\nHost: localhost\r\n"
                 "Content-Type: application/ipp\r\n"
                 "Content-Length: %ld\r\n\r\n", contentLength);
    else
        snprintf(h, sizeof h, "POST / HTTP/1.1\r\nHost: localhost\r\n"
                 "Content-Type: application/ipp\r\n"
                 "Transfer-Encoding: chunked\r\n\r\n");
    return gapi_unix_send(fd, h, strlen(h));
}

static ghal_status SendChunk(int fd, const void *p, size_t n)
{
    char h[24];
    ghal_status st;

    if (!n)
        return GHAL_OK;
    snprintf(h, sizeof h, "%zx\r\n", n);
    if ((st = gapi_unix_send(fd, h, strlen(h))) != GHAL_OK ||
        (st = gapi_unix_send(fd, p, n)) != GHAL_OK)
        return st;
    return gapi_unix_send(fd, "\r\n", 2);
}

/* more bytes into b; 0 at the end of the connection */
static ghal_status Fill(int fd, Buf *b, int *eof)
{
    uint8_t tmp[4096];
    long k = gapi_unix_recv(fd, tmp, sizeof tmp, TIMEOUT_MS);

    if (k == 0) {
        gapi_linux_set_error("CUPS: no answer");
        return GHAL_ERR_HOST;
    }
    if (k < 0) {
        *eof = 1;
        return GHAL_OK;
    }
    Put(b, tmp, (size_t)k);
    return b->error ? GHAL_ERR_NOMEM : GHAL_OK;
}

/* the response: the HTTP status and the body (owned by *body) */
static ghal_status ReadResponse(int fd, int *http, Buf *body)
{
    Buf in = { 0 };
    char *hend, *p;
    size_t head, pos;
    long clen = -1;
    int chunked = 0, eof = 0;
    ghal_status st = GHAL_OK;

    memset(body, 0, sizeof *body);
    *http = 0;
    for (;;) {
        if (in.len && (hend = memmem(in.b, in.len, "\r\n\r\n", 4)))
            break;
        if (eof || (st = Fill(fd, &in, &eof)) != GHAL_OK)
            goto fail;
    }
    head = (size_t)(hend - (char *)in.b) + 4;
    *hend = 0;
    if (sscanf((char *)in.b, "HTTP/%*d.%*d %d", http) != 1)
        goto fail;
    for (p = (char *)in.b; p && *p; p = strstr(p, "\r\n")) {
        if (*p == '\r')
            p += 2;
        if (!strncasecmp(p, "Content-Length:", 15))
            clen = strtol(p + 15, NULL, 10);
        else if (!strncasecmp(p, "Transfer-Encoding:", 18) &&
                 strstr(p, "chunked"))
            chunked = 1;
    }
    pos = head;
    if (chunked) {
        for (;;) {
            char *le;
            size_t n;
            while (!(le = memmem(in.b + pos, in.len - pos, "\r\n", 2))) {
                if (eof || (st = Fill(fd, &in, &eof)) != GHAL_OK)
                    goto fail;
            }
            n = strtoul((char *)in.b + pos, NULL, 16);
            pos = (size_t)(le - (char *)in.b) + 2;
            while (in.len < pos + n + 2) {
                if (eof || (st = Fill(fd, &in, &eof)) != GHAL_OK)
                    goto fail;
            }
            if (!n)
                break;
            Put(body, in.b + pos, n);
            pos += n + 2;
        }
    } else if (clen >= 0) {
        while (in.len < head + (size_t)clen) {
            if (eof || (st = Fill(fd, &in, &eof)) != GHAL_OK)
                goto fail;
        }
        Put(body, in.b + head, (size_t)clen);
    } else {
        while (!eof)
            if ((st = Fill(fd, &in, &eof)) != GHAL_OK)
                goto fail;
        Put(body, in.b + head, in.len - head);
    }
    free(in.b);
    return body->error ? GHAL_ERR_NOMEM : GHAL_OK;
fail:
    free(in.b);
    free(body->b);
    memset(body, 0, sizeof *body);
    if (st == GHAL_OK) {
        gapi_linux_set_error("CUPS: a broken answer");
        st = GHAL_ERR_HOST;
    }
    return st;
}

/* ---------------------------------------------------------------------
 * Reading a response
 * ------------------------------------------------------------------- */

typedef void AttrFn(void *ctx, int group, int tag, const char *name,
                    const uint8_t *v, size_t n);

static unsigned Get16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static uint32_t Get32(const uint8_t *p)
{
    return (uint32_t)Get16(p) << 16 | Get16(p + 2);
}

/* the status code; calls fn for every value (name: the attribute's, also
 * for its further values) */
static int Parse(const Buf *b, AttrFn *fn, void *ctx)
{
    size_t pos = 8;
    int group = 0;
    char name[128] = "";

    if (b->len < 8)
        return -1;
    while (pos < b->len) {
        int tag = b->b[pos++];
        size_t nn, vn;
        if (tag == TAG_END)
            break;
        if (tag < 0x10) {               /* a new group */
            group = tag;
            if (fn)
                fn(ctx, group, 0, "", NULL, 0);
            continue;
        }
        if (pos + 2 > b->len)
            return -1;
        nn = Get16(b->b + pos);
        pos += 2;
        if (pos + nn + 2 > b->len)
            return -1;
        if (nn) {
            size_t k = nn < sizeof name - 1 ? nn : sizeof name - 1;
            memcpy(name, b->b + pos, k);
            name[k] = 0;
        }
        pos += nn;
        vn = Get16(b->b + pos);
        pos += 2;
        if (pos + vn > b->len)
            return -1;
        if (fn)
            fn(ctx, group, tag, name, b->b + pos, vn);
        pos += vn;
    }
    return (int)Get16(b->b + 2);
}

static ghal_status StatusOf(int http, int ipp)
{
    if (http == 401 || http == 403) {
        gapi_linux_set_error("CUPS: HTTP %d", http);
        return GHAL_ERR_ACCESS;
    }
    if (http != 200) {
        gapi_linux_set_error("CUPS: HTTP %d", http);
        return GHAL_ERR_HOST;
    }
    if (ipp < 0) {
        gapi_linux_set_error("CUPS: a broken IPP answer");
        return GHAL_ERR_HOST;
    }
    if (ipp < 0x100)
        return GHAL_OK;
    gapi_linux_set_error("CUPS: IPP status 0x%04x", (unsigned)ipp);
    switch (ipp) {
    case 0x0401: case 0x0402: case 0x0403:
        return GHAL_ERR_ACCESS;
    case 0x0406:
        return GHAL_ERR_NOTFOUND;
    case 0x0501:
        return GHAL_ERR_UNSUPPORTED;
    default:
        return GHAL_ERR_HOST;
    }
}

/* one request with its body, the response in *body */
static ghal_status Request(const Buf *req, Buf *body, int *ipp)
{
    int fd = Connect(), http;
    ghal_status st;

    memset(body, 0, sizeof *body);
    if (fd < 0) {
        gapi_linux_set_error("no CUPS");
        return GHAL_ERR_UNSUPPORTED;
    }
    st = SendHead(fd, (long)req->len);
    if (st == GHAL_OK)
        st = gapi_unix_send(fd, req->b, req->len);
    if (st == GHAL_OK)
        st = ReadResponse(fd, &http, body);
    close(fd);
    if (st != GHAL_OK)
        return st;
    *ipp = Parse(body, NULL, NULL);
    return StatusOf(http, *ipp);
}

/* the text of a name or text value, with or without a language */
static void Text(char *dst, size_t size, int tag, const uint8_t *v,
                 size_t n)
{
    if ((tag == TAG_TEXT_LANG || tag == TAG_NAME_LANG) && n >= 4) {
        size_t ln = Get16(v);
        if (2 + ln + 2 <= n) {
            size_t tn = Get16(v + 2 + ln);
            v += 4 + ln;
            n = tn <= n - 4 - ln ? tn : n - 4 - ln;
        }
    }
    if (n >= size)
        n = size - 1;
    memcpy(dst, v, n);
    dst[n] = 0;
}

/* ---------------------------------------------------------------------
 * The printers
 * ------------------------------------------------------------------- */

typedef struct {
    gapi_printer *list;
    int max, count;
    gapi_printer *cur;
    gapi_printer scratch;
} ListCtx;

static void ListAttr(void *ctx, int group, int tag, const char *name,
                     const uint8_t *v, size_t n)
{
    ListCtx *c = ctx;
    gapi_printer *p;

    if (!tag) {                         /* a new group */
        c->cur = NULL;
        if (group == TAG_PRINTER) {
            c->cur = c->count < c->max ? &c->list[c->count] : &c->scratch;
            memset(c->cur, 0, sizeof *c->cur);
            c->count++;
        }
        return;
    }
    if (!(p = c->cur))
        return;
    if (!strcmp(name, "printer-name"))
        Text(p->name, sizeof p->name, tag, v, n);
    else if (!strcmp(name, "printer-info"))
        Text(p->info, sizeof p->info, tag, v, n);
    else if (!strcmp(name, "printer-location"))
        Text(p->location, sizeof p->location, tag, v, n);
    else if (!strcmp(name, "printer-make-and-model"))
        Text(p->makeModel, sizeof p->makeModel, tag, v, n);
    else if (!strcmp(name, "printer-state") && tag == TAG_ENUM && n == 4)
        p->state = (int)Get32(v);
    else if (!strcmp(name, "printer-is-accepting-jobs") && n == 1)
        p->accepting = v[0] != 0;
    else if (!strcmp(name, "color-supported") && n == 1)
        p->color = v[0] != 0;
    else if (!strcmp(name, "sides-supported") && n > 9 &&
             !memcmp(v, "two-sided", 9))
        p->duplex = 1;
}

ghal_status gapi_print_default(char *name, size_t size)
{
    const char *e = getenv("LPDEST");
    Buf req = { 0 }, body;
    ghal_status st;
    int ipp;

    if (!size)
        return GHAL_ERR_INVAL;
    name[0] = 0;
    if (!e || !*e)
        e = getenv("PRINTER");
    if (e && *e) {
        snprintf(name, size, "%s", e);
        return GHAL_OK;
    }
    Begin(&req, CUPS_GET_DEFAULT);
    Attr(&req, TAG_KEYWORD, "requested-attributes", "printer-name", 12);
    Put8(&req, TAG_END);
    if (req.error) {
        free(req.b);
        return GHAL_ERR_NOMEM;
    }
    st = Request(&req, &body, &ipp);
    free(req.b);
    if (st == GHAL_OK) {
        /* the printer-name in the printer group */
        ListCtx c;
        gapi_printer one;
        memset(&c, 0, sizeof c);
        c.list = &one;
        c.max = 1;
        (void)Parse(&body, ListAttr, &c);
        if (c.count && one.name[0])
            snprintf(name, size, "%s", one.name);
        else
            st = GHAL_ERR_NOTFOUND;
    }
    free(body.b);
    return st;
}

ghal_status gapi_print_list(gapi_printer *list, int max, int *count)
{
    static const char *const want[] = {
        "printer-name", "printer-info", "printer-location",
        "printer-make-and-model", "printer-state",
        "printer-is-accepting-jobs", "color-supported", "sides-supported"
    };
    Buf req = { 0 }, body;
    ListCtx c;
    char def[GAPI_PRINT_NAME_SIZE];
    ghal_status st;
    int ipp, i;

    *count = 0;
    Begin(&req, CUPS_GET_PRINTERS);
    for (i = 0; i < (int)(sizeof want / sizeof want[0]); i++)
        AttrStr(&req, TAG_KEYWORD, i ? NULL : "requested-attributes",
                want[i]);
    Put8(&req, TAG_END);
    if (req.error) {
        free(req.b);
        return GHAL_ERR_NOMEM;
    }
    st = Request(&req, &body, &ipp);
    free(req.b);
    if (st == GHAL_ERR_NOTFOUND) {      /* CUPS: no printers at all */
        free(body.b);
        return GHAL_OK;
    }
    if (st != GHAL_OK) {
        free(body.b);
        return st;
    }
    memset(&c, 0, sizeof c);
    c.list = list;
    c.max = max;
    (void)Parse(&body, ListAttr, &c);
    free(body.b);
    *count = c.count;
    if (gapi_print_default(def, sizeof def) == GHAL_OK)
        for (i = 0; i < c.count && i < max; i++)
            list[i].isDefault = !strcmp(list[i].name, def);
    return GHAL_OK;
}

/* ---------------------------------------------------------------------
 * Jobs
 * ------------------------------------------------------------------- */

struct gapi_print_job {
    int fd;
};

ghal_status gapi_print_job_open(const char *printer, const char *title,
                                gapi_doc_format format, int copies,
                                int duplex, gapi_print_job **job)
{
    Buf req = { 0 };
    char uri[GAPI_PRINT_NAME_SIZE + 32];
    gapi_print_job *j;
    ghal_status st;

    *job = NULL;
    if (!printer || !*printer || strlen(printer) >= GAPI_PRINT_NAME_SIZE ||
        strpbrk(printer, "/#? ") || copies < 1 ||
        (format != GAPI_DOC_POSTSCRIPT && format != GAPI_DOC_PDF))
        return GHAL_ERR_INVAL;
    snprintf(uri, sizeof uri, "ipp://localhost/printers/%s", printer);
    Begin(&req, IPP_PRINT_JOB);
    AttrStr(&req, TAG_URI, "printer-uri", uri);
    AttrStr(&req, TAG_NAME, "requesting-user-name", UserName());
    AttrStr(&req, TAG_NAME, "job-name", title && *title ? title : "GEOS");
    AttrStr(&req, TAG_MIMETYPE, "document-format",
            format == GAPI_DOC_PDF ? "application/pdf"
                                   : "application/postscript");
    Put8(&req, TAG_JOB);
    if (copies > 1)
        AttrInt(&req, TAG_INTEGER, "copies", copies);
    AttrStr(&req, TAG_KEYWORD, "sides",
            duplex ? "two-sided-long-edge" : "one-sided");
    Put8(&req, TAG_END);
    if (req.error) {
        free(req.b);
        return GHAL_ERR_NOMEM;
    }
    j = calloc(1, sizeof *j);
    if (!j) {
        free(req.b);
        return GHAL_ERR_NOMEM;
    }
    j->fd = Connect();
    if (j->fd < 0) {
        free(req.b);
        free(j);
        gapi_linux_set_error("no CUPS");
        return GHAL_ERR_UNSUPPORTED;
    }
    st = SendHead(j->fd, -1);
    if (st == GHAL_OK)
        st = SendChunk(j->fd, req.b, req.len);
    free(req.b);
    if (st != GHAL_OK) {
        gapi_print_job_cancel(j);
        gapi_linux_set_error("CUPS: the job could not be sent");
        return st;
    }
    *job = j;
    return GHAL_OK;
}

ghal_status gapi_print_job_write(gapi_print_job *job, const void *data,
                                 size_t size)
{
    if (!job)
        return GHAL_ERR_INVAL;
    return SendChunk(job->fd, data, size);
}

static void JobAttr(void *ctx, int group, int tag, const char *name,
                    const uint8_t *v, size_t n)
{
    if (group == TAG_JOB && tag == TAG_INTEGER && n == 4 &&
        !strcmp(name, "job-id"))
        *(uint32_t *)ctx = Get32(v);
}

ghal_status gapi_print_job_close(gapi_print_job *job, uint32_t *jobId)
{
    Buf body;
    int http, ipp;
    uint32_t id = 0;
    ghal_status st;

    if (jobId)
        *jobId = 0;
    if (!job)
        return GHAL_ERR_INVAL;
    st = gapi_unix_send(job->fd, "0\r\n\r\n", 5);
    if (st == GHAL_OK)
        st = ReadResponse(job->fd, &http, &body);
    close(job->fd);
    free(job);
    if (st != GHAL_OK)
        return st;
    ipp = Parse(&body, JobAttr, &id);
    free(body.b);
    st = StatusOf(http, ipp);
    if (st == GHAL_OK && jobId)
        *jobId = id;
    return st;
}

void gapi_print_job_cancel(gapi_print_job *job)
{
    if (!job)
        return;
    close(job->fd);             /* no last chunk: CUPS drops the job */
    free(job);
}
