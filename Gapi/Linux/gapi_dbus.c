/*
 * gapi_dbus.c -- a small D-Bus client of GEOS's own (gapi_dbus.h).
 *
 * The wire protocol of the D-Bus specification: a message is a fixed
 * header (endianness, type, flags, version 1, body length, serial), the
 * header fields as an array of (code, variant), padding to 8 and the
 * body.  Values are aligned to their size from the start of the message
 * (strings and arrays to 4, structs to 8).  We send in the host's byte
 * order and read either.
 */
#define _GNU_SOURCE
#include "gapi_dbus.h"
#include "gapi_unix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct gapi_dbus {
    int      fd;
    uint32_t serial;
    char     name[64];
    uint8_t *rbuf;              /* received, not yet a whole message */
    size_t   rlen, rcap;
};

enum {
    FIELD_PATH = 1, FIELD_INTERFACE, FIELD_MEMBER, FIELD_ERROR_NAME,
    FIELD_REPLY_SERIAL, FIELD_DESTINATION, FIELD_SENDER, FIELD_SIGNATURE
};

static int host_big_endian(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 0;
}

static size_t align_up(size_t n, size_t a)
{
    return (n + a - 1) & ~(a - 1);
}

static size_t align_of(int t)
{
    switch (t) {
    case 'n': case 'q': return 2;
    case 'b': case 'i': case 'u': case 'h': case 's': case 'o': case 'a':
        return 4;
    case 'x': case 't': case 'd': case '(': case '{': return 8;
    default: return 1;          /* y, g, v */
    }
}

/* the end of the complete type that starts at s */
static const char *sig_end(const char *s)
{
    int depth = 0;

    for (;;) {
        switch (*s) {
        case 0:
            return s;
        case 'a':
            s++;                /* its element follows */
            continue;
        case '(': case '{':
            depth++;
            break;
        case ')': case '}':
            depth--;
            break;
        }
        s++;
        if (depth <= 0)
            return s;
    }
}

/* ---------------------------------------------------------------------
 * Building
 * ------------------------------------------------------------------- */

static void pad(gapi_dbus_msg *m, size_t a)
{
    while (m->len % a) {
        if (m->len >= sizeof m->body) {
            m->error = 1;
            return;
        }
        m->body[m->len++] = 0;
    }
}

static void put_raw(gapi_dbus_msg *m, const void *p, size_t n, size_t a)
{
    pad(m, a);
    if (m->error || m->len + n > sizeof m->body) {
        m->error = 1;
        return;
    }
    memcpy(m->body + m->len, p, n);
    m->len += n;
}

void gapi_dbus_msg_init(gapi_dbus_msg *m, const char *dest, const char *path,
                        const char *iface, const char *member,
                        const char *sig)
{
    m->dest = dest;
    m->path = path;
    m->iface = iface;
    m->member = member;
    m->sig = sig ? sig : "";
    m->flags = 0;
    m->error = 0;
    m->len = 0;
}

void gapi_dbus_put_byte(gapi_dbus_msg *m, uint8_t v) { put_raw(m, &v, 1, 1); }
void gapi_dbus_put_bool(gapi_dbus_msg *m, int v)
{
    uint32_t b = v != 0;
    put_raw(m, &b, 4, 4);
}
void gapi_dbus_put_int16(gapi_dbus_msg *m, int16_t v) { put_raw(m, &v, 2, 2); }
void gapi_dbus_put_uint16(gapi_dbus_msg *m, uint16_t v) { put_raw(m, &v, 2, 2); }
void gapi_dbus_put_int32(gapi_dbus_msg *m, int32_t v) { put_raw(m, &v, 4, 4); }
void gapi_dbus_put_uint32(gapi_dbus_msg *m, uint32_t v) { put_raw(m, &v, 4, 4); }
void gapi_dbus_put_int64(gapi_dbus_msg *m, int64_t v) { put_raw(m, &v, 8, 8); }
void gapi_dbus_put_uint64(gapi_dbus_msg *m, uint64_t v) { put_raw(m, &v, 8, 8); }
void gapi_dbus_put_double(gapi_dbus_msg *m, double v) { put_raw(m, &v, 8, 8); }

void gapi_dbus_put_string(gapi_dbus_msg *m, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    put_raw(m, &n, 4, 4);
    put_raw(m, s, n + 1, 1);
}

void gapi_dbus_put_signature(gapi_dbus_msg *m, const char *g)
{
    size_t n = strlen(g);
    uint8_t b = (uint8_t)n;
    if (n > 255) {
        m->error = 1;
        return;
    }
    put_raw(m, &b, 1, 1);
    put_raw(m, g, n + 1, 1);
}

void gapi_dbus_put_variant(gapi_dbus_msg *m, const char *sig)
{
    gapi_dbus_put_signature(m, sig);
}

/* the mark holds the offset of the length and the element alignment */
size_t gapi_dbus_put_array_begin(gapi_dbus_msg *m, const char *elemSig)
{
    size_t a = align_of(elemSig[0]), mark;

    pad(m, 4);
    mark = m->len;
    gapi_dbus_put_uint32(m, 0);
    pad(m, a);
    return mark | (a << 24);
}

void gapi_dbus_put_array_end(gapi_dbus_msg *m, size_t mark)
{
    size_t off = mark & 0xffffff, a = mark >> 24;
    size_t first = align_up(off + 4, a);
    uint32_t n;

    if (m->error)
        return;
    n = (uint32_t)(m->len > first ? m->len - first : 0);
    memcpy(m->body + off, &n, 4);
}

void gapi_dbus_put_struct(gapi_dbus_msg *m)
{
    pad(m, 8);
}

/* the header of message m with the serial, then padding to 8 */
static size_t build_header(uint8_t *h, size_t cap, const gapi_dbus_msg *m,
                           uint32_t serial)
{
    gapi_dbus_msg *f = malloc(sizeof *f);   /* the field array, built as a body */
    uint32_t bodyLen = (uint32_t)m->len, alen;
    size_t len;
    int codes[5];
    const char *vals[5];
    const char *types = "osssg";
    int i;

    if (!f)
        return 0;
    gapi_dbus_msg_init(f, 0, 0, 0, 0, 0);
    codes[0] = FIELD_PATH; vals[0] = m->path;
    codes[1] = FIELD_INTERFACE; vals[1] = m->iface;
    codes[2] = FIELD_MEMBER; vals[2] = m->member;
    codes[3] = FIELD_DESTINATION; vals[3] = m->dest;
    codes[4] = FIELD_SIGNATURE; vals[4] = *m->sig ? m->sig : NULL;
    /* the fields start at offset 16 of the message: 16 bytes of fixed
     * part (with the array length) are put first, so alignment holds */
    f->len = 16;
    memset(f->body, 0, 16);
    for (i = 0; i < 5; i++) {
        char vs[2];
        if (!vals[i])
            continue;
        gapi_dbus_put_struct(f);
        gapi_dbus_put_byte(f, (uint8_t)codes[i]);
        vs[0] = types[i];
        vs[1] = 0;
        gapi_dbus_put_variant(f, vs);
        if (types[i] == 'g')
            gapi_dbus_put_signature(f, vals[i]);
        else
            gapi_dbus_put_string(f, vals[i]);
    }
    alen = (uint32_t)(f->len - 16);
    pad(f, 8);
    len = f->len;
    if (f->error || len > cap) {
        free(f);
        return 0;
    }
    f->body[0] = host_big_endian() ? 'B' : 'l';
    f->body[1] = GAPI_DBUS_METHOD_CALL;
    f->body[2] = m->flags;
    f->body[3] = 1;
    memcpy(f->body + 4, &bodyLen, 4);
    memcpy(f->body + 8, &serial, 4);
    memcpy(f->body + 12, &alen, 4);
    memcpy(h, f->body, len);
    free(f);
    return len;
}

/* ---------------------------------------------------------------------
 * Reading
 * ------------------------------------------------------------------- */

static uint16_t rd16(const gapi_dbus_iter *it, size_t p)
{
    const uint8_t *d = it->r->data + p;
    return it->r->bigEndian ? (uint16_t)(d[0] << 8 | d[1])
                            : (uint16_t)(d[1] << 8 | d[0]);
}

static uint32_t rd32(const gapi_dbus_iter *it, size_t p)
{
    const uint8_t *d = it->r->data + p;
    return it->r->bigEndian
        ? (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]
        : (uint32_t)d[3] << 24 | (uint32_t)d[2] << 16 | (uint32_t)d[1] << 8 | d[0];
}

static uint64_t rd64(const gapi_dbus_iter *it, size_t p)
{
    uint64_t a = rd32(it, p), b = rd32(it, p + 4);
    return it->r->bigEndian ? a << 32 | b : b << 32 | a;
}

void gapi_dbus_iter_init(gapi_dbus_iter *it, const gapi_dbus_reply *r)
{
    it->r = r;
    it->sig = it->sigStart = r->sig;
    it->sigStop = r->sig + strlen(r->sig);
    it->array = 0;
    it->pos = r->bodyOff;
    it->end = r->bodyOff + r->bodyLen;
    it->error = 0;
}

int gapi_dbus_iter_type(gapi_dbus_iter *it)
{
    if (it->error)
        return 0;
    if (it->array) {
        if (it->sig == it->sigStop || it->sig == it->sigStart) {
            if (it->pos >= it->end)
                return 0;
            it->sig = it->sigStart;
        }
    } else if (it->sig >= it->sigStop) {
        return 0;
    }
    return *it->sig;
}

/* the next value must be of type t; aligned, n bytes there.  Returns its
 * position or (size_t)-1 */
static size_t take(gapi_dbus_iter *it, int t, size_t n)
{
    size_t p;

    if (gapi_dbus_iter_type(it) != t) {
        it->error = 1;
        return (size_t)-1;
    }
    p = align_up(it->pos, align_of(t));
    if (p + n > it->end) {
        it->error = 1;
        return (size_t)-1;
    }
    it->pos = p + n;
    it->sig++;
    return p;
}

uint8_t gapi_dbus_get_byte(gapi_dbus_iter *it)
{
    size_t p = take(it, 'y', 1);
    return p == (size_t)-1 ? 0 : it->r->data[p];
}

int gapi_dbus_get_bool(gapi_dbus_iter *it)
{
    size_t p = take(it, 'b', 4);
    return p == (size_t)-1 ? 0 : rd32(it, p) != 0;
}

int16_t gapi_dbus_get_int16(gapi_dbus_iter *it)
{
    size_t p = take(it, 'n', 2);
    return p == (size_t)-1 ? 0 : (int16_t)rd16(it, p);
}

uint16_t gapi_dbus_get_uint16(gapi_dbus_iter *it)
{
    size_t p = take(it, 'q', 2);
    return p == (size_t)-1 ? 0 : rd16(it, p);
}

int32_t gapi_dbus_get_int32(gapi_dbus_iter *it)
{
    size_t p = take(it, 'i', 4);
    return p == (size_t)-1 ? 0 : (int32_t)rd32(it, p);
}

uint32_t gapi_dbus_get_uint32(gapi_dbus_iter *it)
{
    size_t p;
    int t = gapi_dbus_iter_type(it);
    p = take(it, t == 'h' ? 'h' : 'u', 4);
    return p == (size_t)-1 ? 0 : rd32(it, p);
}

int64_t gapi_dbus_get_int64(gapi_dbus_iter *it)
{
    size_t p = take(it, 'x', 8);
    return p == (size_t)-1 ? 0 : (int64_t)rd64(it, p);
}

uint64_t gapi_dbus_get_uint64(gapi_dbus_iter *it)
{
    size_t p = take(it, 't', 8);
    return p == (size_t)-1 ? 0 : rd64(it, p);
}

double gapi_dbus_get_double(gapi_dbus_iter *it)
{
    size_t p = take(it, 'd', 8);
    uint64_t u;
    double d;
    if (p == (size_t)-1)
        return 0;
    u = rd64(it, p);
    memcpy(&d, &u, 8);
    return d;
}

const char *gapi_dbus_get_string(gapi_dbus_iter *it)
{
    int t = gapi_dbus_iter_type(it);
    size_t p, n;
    const uint8_t *d = it->r->data;

    if (t == 'g') {
        if (it->pos + 1 > it->end)
            goto bad;
        n = d[it->pos];
        p = it->pos + 1;
    } else if (t == 's' || t == 'o') {
        p = align_up(it->pos, 4);
        if (p + 4 > it->end)
            goto bad;
        n = rd32(it, p);
        p += 4;
    } else {
        goto bad;
    }
    if (n > it->end || p + n + 1 > it->end || d[p + n] != 0)
        goto bad;
    it->pos = p + n + 1;
    it->sig++;
    return (const char *)d + p;
bad:
    it->error = 1;
    return "";
}

static void skip_one(gapi_dbus_iter *it);

void gapi_dbus_recurse(gapi_dbus_iter *it, gapi_dbus_iter *sub)
{
    int t = gapi_dbus_iter_type(it);
    const char *te;

    *sub = *it;
    sub->error = 1;
    if (!t)
        return;
    te = sig_end(it->sig);
    sub->error = 0;
    sub->array = 0;
    if (t == 'v') {
        const char *vs;
        size_t n;
        gapi_dbus_iter tmp;
        if (it->pos + 1 > it->end)
            goto bad;
        n = it->r->data[it->pos];
        if (it->pos + 2 + n > it->end || it->r->data[it->pos + 1 + n])
            goto bad;
        vs = (const char *)it->r->data + it->pos + 1;
        sub->sig = sub->sigStart = vs;
        sub->sigStop = vs + n;
        sub->pos = it->pos + 2 + n;
        /* its end: skip the value */
        tmp = *sub;
        skip_one(&tmp);
        if (tmp.error || tmp.sig != tmp.sigStop)
            goto bad;
        sub->end = tmp.pos;
        it->pos = tmp.pos;
    } else if (t == 'a') {
        size_t p = align_up(it->pos, 4), n, first;
        if (p + 4 > it->end)
            goto bad;
        n = rd32(it, p);
        first = align_up(p + 4, align_of(it->sig[1]));
        if (n > GAPI_DBUS_MSG_MAX || first + n > it->end)
            goto bad;
        sub->sig = sub->sigStart = it->sig + 1;
        sub->sigStop = te;
        sub->array = 1;
        sub->pos = first;
        sub->end = first + n;
        it->pos = first + n;
    } else if (t == '(' || t == '{') {
        gapi_dbus_iter tmp;
        sub->sig = sub->sigStart = it->sig + 1;
        sub->sigStop = te - 1;
        sub->pos = align_up(it->pos, 8);
        if (sub->pos > it->end)
            goto bad;
        tmp = *sub;
        while (gapi_dbus_iter_type(&tmp))
            skip_one(&tmp);
        if (tmp.error)
            goto bad;
        sub->end = tmp.pos;
        it->pos = tmp.pos;
    } else {
        goto bad;
    }
    it->sig = te;
    return;
bad:
    it->error = 1;
    sub->error = 1;
}

static void skip_one(gapi_dbus_iter *it)
{
    gapi_dbus_iter sub;

    switch (gapi_dbus_iter_type(it)) {
    case 'y': gapi_dbus_get_byte(it); break;
    case 'b': gapi_dbus_get_bool(it); break;
    case 'n': case 'q': (void)take(it, *it->sig, 2); break;
    case 'i': case 'u': case 'h': (void)take(it, *it->sig, 4); break;
    case 'x': case 't': case 'd': (void)take(it, *it->sig, 8); break;
    case 's': case 'o': case 'g': gapi_dbus_get_string(it); break;
    case 'v': case 'a': case '(': case '{': gapi_dbus_recurse(it, &sub); break;
    default: it->error = 1; break;
    }
}

void gapi_dbus_skip(gapi_dbus_iter *it)
{
    skip_one(it);
}

void gapi_dbus_reply_free(gapi_dbus_reply *r)
{
    free(r->data);
    memset(r, 0, sizeof *r);
}

/* the fields of a whole message in r->data */
static int parse_message(gapi_dbus_reply *r, size_t total,
                         uint32_t *replySerial)
{
    gapi_dbus_reply hdr;
    gapi_dbus_iter it, arr;
    uint32_t alen;

    r->bigEndian = r->data[0] == 'B';
    r->type = r->data[1];
    r->error = NULL;
    r->sig = "";
    *replySerial = 0;
    hdr = *r;
    hdr.sig = "a(yv)";
    hdr.bodyOff = 12;
    gapi_dbus_iter_init(&it, &hdr);
    alen = rd32(&it, 12);
    it.end = 16 + alen;
    if (it.end > total)
        return 0;
    gapi_dbus_recurse(&it, &arr);
    while (gapi_dbus_iter_type(&arr)) {
        gapi_dbus_iter st, v;
        uint8_t code;
        gapi_dbus_recurse(&arr, &st);
        code = gapi_dbus_get_byte(&st);
        gapi_dbus_recurse(&st, &v);
        if (st.error || v.error)
            return 0;
        if (code == FIELD_ERROR_NAME && *v.sig == 's')
            r->error = gapi_dbus_get_string(&v);
        else if (code == FIELD_REPLY_SERIAL && *v.sig == 'u')
            *replySerial = gapi_dbus_get_uint32(&v);
        else if (code == FIELD_SIGNATURE && *v.sig == 'g')
            r->sig = gapi_dbus_get_string(&v);
        if (v.error)
            return 0;
    }
    if (arr.error || it.error)
        return 0;
    r->bodyOff = align_up(16 + alen, 8);
    r->bodyLen = total - r->bodyOff;
    return 1;
}

/* the size of the message at the start of the buffer, 0 if not known
 * yet, (size_t)-1 if it is too big or broken */
static size_t message_size(const uint8_t *d, size_t n)
{
    gapi_dbus_reply r;
    gapi_dbus_iter it;
    size_t total;

    if (n < 16)
        return 0;
    if (d[0] != 'l' && d[0] != 'B')
        return (size_t)-1;
    memset(&r, 0, sizeof r);
    r.data = (uint8_t *)d;
    r.bigEndian = d[0] == 'B';
    r.sig = "";
    it.r = &r;
    total = align_up(16 + (size_t)rd32(&it, 12), 8) + rd32(&it, 4);
    return total > GAPI_DBUS_MSG_MAX ? (size_t)-1 : total;
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static ghal_status fill(gapi_dbus *c, long deadline)
{
    long k, left = deadline - now_ms();

    if (left <= 0)
        return GHAL_ERR_HOST;
    if (c->rcap - c->rlen < 4096) {
        size_t cap = c->rcap ? c->rcap * 2 : 8192;
        uint8_t *b = realloc(c->rbuf, cap);
        if (!b)
            return GHAL_ERR_NOMEM;
        c->rbuf = b;
        c->rcap = cap;
    }
    k = gapi_unix_recv(c->fd, c->rbuf + c->rlen, c->rcap - c->rlen,
                       (int)left);
    if (k <= 0)
        return GHAL_ERR_HOST;   /* timeout, error or the bus went away */
    c->rlen += (size_t)k;
    return GHAL_OK;
}

/* the next whole message */
static ghal_status read_message(gapi_dbus *c, long deadline,
                                gapi_dbus_reply *r, uint32_t *replySerial)
{
    size_t total;
    ghal_status st;

    for (;;) {
        total = message_size(c->rbuf, c->rlen);
        if (total == (size_t)-1)
            return GHAL_ERR_HOST;
        if (total && c->rlen >= total)
            break;
        if ((st = fill(c, deadline)) != GHAL_OK)
            return st;
    }
    memset(r, 0, sizeof *r);
    r->data = malloc(total + 1);
    if (!r->data)
        return GHAL_ERR_NOMEM;
    memcpy(r->data, c->rbuf, total);
    r->data[total] = 0;
    memmove(c->rbuf, c->rbuf + total, c->rlen - total);
    c->rlen -= total;
    if (!parse_message(r, total, replySerial)) {
        gapi_dbus_reply_free(r);
        return GHAL_ERR_HOST;
    }
    return GHAL_OK;
}

ghal_status gapi_dbus_call(gapi_dbus *c, const gapi_dbus_msg *m,
                           int timeoutMs, gapi_dbus_reply *r)
{
    uint8_t *buf;
    size_t hl;
    uint32_t serial, rs;
    long deadline;
    ghal_status st;

    memset(r, 0, sizeof *r);
    if (!c || m->error)
        return GHAL_ERR_INVAL;
    buf = malloc(1024 + m->len);
    if (!buf)
        return GHAL_ERR_NOMEM;
    serial = ++c->serial;
    hl = build_header(buf, 1024, m, serial);
    if (!hl) {
        free(buf);
        return GHAL_ERR_INVAL;
    }
    memcpy(buf + hl, m->body, m->len);
    st = gapi_unix_send(c->fd, buf, hl + m->len);
    free(buf);
    if (st != GHAL_OK || (m->flags & GAPI_DBUS_NO_REPLY_EXPECTED))
        return st;
    deadline = now_ms() + (timeoutMs < 0 ? GAPI_DBUS_DEFAULT_TIMEOUT
                                         : timeoutMs);
    for (;;) {
        if ((st = read_message(c, deadline, r, &rs)) != GHAL_OK)
            return st;
        if ((r->type == GAPI_DBUS_METHOD_RETURN ||
             r->type == GAPI_DBUS_ERROR) && rs == serial)
            return GHAL_OK;
        gapi_dbus_reply_free(r);        /* a signal or another reply */
    }
}

static int ends_with(const char *s, const char *t)
{
    size_t n = strlen(s), k = strlen(t);
    return n >= k && strcmp(s + n - k, t) == 0;
}

ghal_status gapi_dbus_status(const gapi_dbus_reply *r)
{
    const char *e = r->error ? r->error : "";

    if (r->type == GAPI_DBUS_METHOD_RETURN)
        return GHAL_OK;
    if (ends_with(e, ".AccessDenied") || ends_with(e, ".NotAuthorized") ||
        ends_with(e, ".InteractiveAuthorizationRequired") ||
        ends_with(e, ".AuthFailed"))
        return GHAL_ERR_ACCESS;
    if (ends_with(e, ".ServiceUnknown") || ends_with(e, ".NameHasNoOwner") ||
        ends_with(e, ".UnknownMethod") || ends_with(e, ".UnknownObject") ||
        ends_with(e, ".UnknownInterface") ||
        ends_with(e, ".UnknownProperty"))
        return GHAL_ERR_UNSUPPORTED;
    return GHAL_ERR_HOST;
}

ghal_status gapi_dbus_call_simple(gapi_dbus *c, const gapi_dbus_msg *m,
                                  int timeoutMs)
{
    gapi_dbus_reply r;
    ghal_status st = gapi_dbus_call(c, m, timeoutMs, &r);

    if (st == GHAL_OK)
        st = gapi_dbus_status(&r);
    gapi_dbus_reply_free(&r);
    return st;
}

/* ---------------------------------------------------------------------
 * Connecting
 * ------------------------------------------------------------------- */

/* one line of the authentication ("...\r\n"), without the "\r\n" */
static ghal_status auth_line(gapi_dbus *c, char *line, size_t n,
                             long deadline)
{
    for (;;) {
        uint8_t *e = c->rlen ? memmem(c->rbuf, c->rlen, "\r\n", 2) : NULL;
        ghal_status st;
        if (e) {
            size_t k = (size_t)(e - c->rbuf);
            if (k >= n)
                return GHAL_ERR_HOST;
            memcpy(line, c->rbuf, k);
            line[k] = 0;
            memmove(c->rbuf, e + 2, c->rlen - k - 2);
            c->rlen -= k + 2;
            return GHAL_OK;
        }
        if (c->rlen > 1024)
            return GHAL_ERR_HOST;
        if ((st = fill(c, deadline)) != GHAL_OK)
            return st;
    }
}

ghal_status gapi_dbus_open(gapi_dbus_bus bus, gapi_dbus **conn)
{
    const char *addr;
    char hex[32], uid[16], line[256], msg[64];
    gapi_dbus *c;
    gapi_dbus_msg *m;
    gapi_dbus_reply r;
    gapi_dbus_iter it;
    long deadline = now_ms() + 5000;
    ghal_status st;
    size_t i;

    *conn = NULL;
    if (bus == GAPI_DBUS_SYSTEM) {
        addr = getenv("DBUS_SYSTEM_BUS_ADDRESS");
        if (!addr || !*addr)
            addr = "unix:path=/run/dbus/system_bus_socket";
    } else {
        addr = getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!addr || !*addr)
            return GHAL_ERR_UNSUPPORTED;
    }
    c = calloc(1, sizeof *c);
    if (!c)
        return GHAL_ERR_NOMEM;
    c->fd = gapi_unix_connect(addr);
    if (c->fd < 0) {
        free(c);
        return GHAL_ERR_UNSUPPORTED;
    }
    /* AUTH EXTERNAL with our uid, in hex of its decimal digits */
    snprintf(uid, sizeof uid, "%u", (unsigned)getuid());
    for (i = 0; uid[i]; i++)
        snprintf(hex + 2 * i, 3, "%02x", (unsigned char)uid[i]);
    snprintf(msg, sizeof msg, "AUTH EXTERNAL %s\r\n", hex);
    st = gapi_unix_send(c->fd, "", 1);
    if (st == GHAL_OK)
        st = gapi_unix_send(c->fd, msg, strlen(msg));
    if (st == GHAL_OK)
        st = auth_line(c, line, sizeof line, deadline);
    if (st == GHAL_OK && strncmp(line, "OK ", 3) != 0)
        st = GHAL_ERR_ACCESS;
    if (st == GHAL_OK)
        st = gapi_unix_send(c->fd, "BEGIN\r\n", 7);
    if (st != GHAL_OK) {
        gapi_dbus_close(c);
        return st;
    }
    /* Hello: our unique name */
    m = malloc(sizeof *m);
    if (!m) {
        gapi_dbus_close(c);
        return GHAL_ERR_NOMEM;
    }
    gapi_dbus_msg_init(m, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                       "org.freedesktop.DBus", "Hello", NULL);
    st = gapi_dbus_call(c, m, 5000, &r);
    free(m);
    if (st == GHAL_OK)
        st = gapi_dbus_status(&r);
    if (st == GHAL_OK) {
        gapi_dbus_iter_init(&it, &r);
        snprintf(c->name, sizeof c->name, "%s", gapi_dbus_get_string(&it));
        if (it.error)
            st = GHAL_ERR_HOST;
    }
    gapi_dbus_reply_free(&r);
    if (st != GHAL_OK) {
        gapi_dbus_close(c);
        return st;
    }
    *conn = c;
    return GHAL_OK;
}

void gapi_dbus_close(gapi_dbus *c)
{
    if (!c)
        return;
    if (c->fd >= 0)
        close(c->fd);
    free(c->rbuf);
    free(c);
}

const char *gapi_dbus_name(const gapi_dbus *c)
{
    return c ? c->name : "";
}
