/*
 * test_gapi_dbus.c -- GEOS's own D-Bus client (Gapi/Linux/gapi_dbus.c)
 * and the journal (gapi_journal.c).
 *
 *   - marshalling: every type written into a body and read back with
 *     the iterator, containers, skipping, type errors;
 *   - on the system bus, if there is one (else the test is skipped):
 *     Hello, GetId, ListNames, Peer.Ping, errors for unknown names and
 *     methods, a property of timedate1 if it is there;
 *   - one debug line to the journal and back to stderr.
 */
#include "gapi.h"
#include "gapi_dbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static void AsReply(gapi_dbus_reply *r, gapi_dbus_msg *m)
{
    const uint16_t one = 1;

    memset(r, 0, sizeof *r);
    r->type = GAPI_DBUS_METHOD_RETURN;
    r->sig = m->sig;
    r->data = m->body;
    r->bodyOff = 0;
    r->bodyLen = m->len;
    r->bigEndian = *(const uint8_t *)&one == 0;
}

static void Marshalling(void)
{
    static gapi_dbus_msg m;
    gapi_dbus_reply r;
    gapi_dbus_iter it, a, e, v, st;
    size_t mark, mark2;
    int n;

    gapi_dbus_msg_init(&m, "d", "/p", "i", "m",
                       "ybnqiuxtdsogvasa{sv}(iy)");
    gapi_dbus_put_byte(&m, 7);
    gapi_dbus_put_bool(&m, 1);
    gapi_dbus_put_int16(&m, -2);
    gapi_dbus_put_uint16(&m, 65000);
    gapi_dbus_put_int32(&m, -100000);
    gapi_dbus_put_uint32(&m, 4000000000u);
    gapi_dbus_put_int64(&m, -5000000000LL);
    gapi_dbus_put_uint64(&m, 18000000000000000000ULL);
    gapi_dbus_put_double(&m, 2.5);
    gapi_dbus_put_string(&m, "hello");
    gapi_dbus_put_string(&m, "/org/geos");
    gapi_dbus_put_signature(&m, "a{sv}");
    gapi_dbus_put_variant(&m, "u");
    gapi_dbus_put_uint32(&m, 42);
    mark = gapi_dbus_put_array_begin(&m, "s");
    gapi_dbus_put_string(&m, "a");
    gapi_dbus_put_string(&m, "bc");
    gapi_dbus_put_array_end(&m, mark);
    mark = gapi_dbus_put_array_begin(&m, "{sv}");
    gapi_dbus_put_struct(&m);
    gapi_dbus_put_string(&m, "k1");
    gapi_dbus_put_variant(&m, "x");
    gapi_dbus_put_int64(&m, 9);
    gapi_dbus_put_struct(&m);
    gapi_dbus_put_string(&m, "k2");
    gapi_dbus_put_variant(&m, "as");
    mark2 = gapi_dbus_put_array_begin(&m, "s");
    gapi_dbus_put_array_end(&m, mark2);         /* empty */
    gapi_dbus_put_array_end(&m, mark);
    gapi_dbus_put_struct(&m);
    gapi_dbus_put_int32(&m, 3);
    gapi_dbus_put_byte(&m, 4);
    CHECK(!m.error);

    AsReply(&r, &m);
    gapi_dbus_iter_init(&it, &r);
    CHECK(gapi_dbus_get_byte(&it) == 7);
    CHECK(gapi_dbus_get_bool(&it) == 1);
    CHECK(gapi_dbus_get_int16(&it) == -2);
    CHECK(gapi_dbus_get_uint16(&it) == 65000);
    CHECK(gapi_dbus_get_int32(&it) == -100000);
    CHECK(gapi_dbus_get_uint32(&it) == 4000000000u);
    CHECK(gapi_dbus_get_int64(&it) == -5000000000LL);
    CHECK(gapi_dbus_get_uint64(&it) == 18000000000000000000ULL);
    CHECK(gapi_dbus_get_double(&it) == 2.5);
    CHECK(!strcmp(gapi_dbus_get_string(&it), "hello"));
    CHECK(!strcmp(gapi_dbus_get_string(&it), "/org/geos"));
    CHECK(!strcmp(gapi_dbus_get_string(&it), "a{sv}"));
    gapi_dbus_recurse(&it, &v);
    CHECK(gapi_dbus_iter_type(&v) == 'u' && gapi_dbus_get_uint32(&v) == 42);
    CHECK(gapi_dbus_iter_type(&v) == 0);
    gapi_dbus_recurse(&it, &a);
    CHECK(!strcmp(gapi_dbus_get_string(&a), "a"));
    CHECK(!strcmp(gapi_dbus_get_string(&a), "bc"));
    CHECK(gapi_dbus_iter_type(&a) == 0);
    gapi_dbus_recurse(&it, &a);
    for (n = 0; gapi_dbus_iter_type(&a) == '{'; n++) {
        const char *k;
        gapi_dbus_recurse(&a, &e);
        k = gapi_dbus_get_string(&e);
        gapi_dbus_recurse(&e, &v);
        if (n == 0) {
            CHECK(!strcmp(k, "k1") && gapi_dbus_get_int64(&v) == 9);
        } else {
            gapi_dbus_iter s;
            CHECK(!strcmp(k, "k2") && gapi_dbus_iter_type(&v) == 'a');
            gapi_dbus_recurse(&v, &s);
            CHECK(gapi_dbus_iter_type(&s) == 0 && !s.error);
        }
        CHECK(!e.error && !v.error);
    }
    CHECK(n == 2);
    gapi_dbus_recurse(&it, &st);
    CHECK(gapi_dbus_get_int32(&st) == 3 && gapi_dbus_get_byte(&st) == 4);
    CHECK(gapi_dbus_iter_type(&it) == 0 && !it.error && !a.error);

    /* skipping to the struct, and a wrong type */
    gapi_dbus_iter_init(&it, &r);
    for (n = 0; n < 15; n++)
        gapi_dbus_skip(&it);
    CHECK(gapi_dbus_iter_type(&it) == '(' && !it.error);
    gapi_dbus_iter_init(&it, &r);
    (void)gapi_dbus_get_string(&it);            /* it is a byte */
    CHECK(it.error && gapi_dbus_iter_type(&it) == 0);

    /* overflow is caught */
    gapi_dbus_msg_init(&m, 0, 0, 0, 0, "as");
    mark = gapi_dbus_put_array_begin(&m, "s");
    for (n = 0; n < 2000; n++)
        gapi_dbus_put_string(&m, "overflowing");
    gapi_dbus_put_array_end(&m, mark);
    CHECK(m.error);
}

static gapi_dbus_msg msg;

static void Bus(gapi_dbus *c)
{
    gapi_dbus_reply r;
    gapi_dbus_iter it, a;
    int found = 0;
    ghal_status st;

    CHECK(gapi_dbus_name(c)[0] == ':');
    gapi_dbus_msg_init(&msg, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                       "org.freedesktop.DBus", "GetId", NULL);
    CHECK(gapi_dbus_call(c, &msg, -1, &r) == GHAL_OK);
    CHECK(gapi_dbus_status(&r) == GHAL_OK && !strcmp(r.sig, "s"));
    gapi_dbus_iter_init(&it, &r);
    CHECK(strlen(gapi_dbus_get_string(&it)) == 32);
    gapi_dbus_reply_free(&r);

    gapi_dbus_msg_init(&msg, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                       "org.freedesktop.DBus", "ListNames", NULL);
    CHECK(gapi_dbus_call(c, &msg, -1, &r) == GHAL_OK && !strcmp(r.sig, "as"));
    gapi_dbus_iter_init(&it, &r);
    gapi_dbus_recurse(&it, &a);
    while (gapi_dbus_iter_type(&a))
        if (!strcmp(gapi_dbus_get_string(&a), gapi_dbus_name(c)))
            found = 1;
    CHECK(found && !a.error);
    gapi_dbus_reply_free(&r);

    gapi_dbus_msg_init(&msg, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                       "org.freedesktop.DBus.Peer", "Ping", NULL);
    st = gapi_dbus_call_simple(c, &msg, -1);    /* the bus's policy */
    CHECK(st == GHAL_OK || st == GHAL_ERR_ACCESS);
    gapi_dbus_msg_init(&msg, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                       "org.freedesktop.DBus", "NoSuchMethod", NULL);
    CHECK(gapi_dbus_call_simple(c, &msg, -1) == GHAL_ERR_UNSUPPORTED);
    gapi_dbus_msg_init(&msg, "org.geos.NoSuchService", "/", "org.geos.X",
                       "Y", NULL);
    msg.flags = GAPI_DBUS_NO_AUTO_START;
    CHECK(gapi_dbus_call_simple(c, &msg, -1) == GHAL_ERR_UNSUPPORTED);
    /* arguments: NameHasOwner("org.freedesktop.DBus") */
    gapi_dbus_msg_init(&msg, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                       "org.freedesktop.DBus", "NameHasOwner", "s");
    gapi_dbus_put_string(&msg, "org.freedesktop.DBus");
    CHECK(gapi_dbus_call(c, &msg, -1, &r) == GHAL_OK);
    gapi_dbus_iter_init(&it, &r);
    CHECK(gapi_dbus_get_bool(&it) == 1);
    gapi_dbus_reply_free(&r);

    /* a property of timedate1, where it is */
    gapi_dbus_msg_init(&msg, "org.freedesktop.timedate1",
                       "/org/freedesktop/timedate1",
                       "org.freedesktop.DBus.Properties", "Get", "ss");
    gapi_dbus_put_string(&msg, "org.freedesktop.timedate1");
    gapi_dbus_put_string(&msg, "Timezone");
    st = gapi_dbus_call(c, &msg, -1, &r);
    CHECK(st == GHAL_OK);
    if (gapi_dbus_status(&r) == GHAL_OK) {
        gapi_dbus_iter v;
        gapi_dbus_iter_init(&it, &r);
        gapi_dbus_recurse(&it, &v);
        printf("timedate1 Timezone: %s\n", gapi_dbus_get_string(&v));
        CHECK(!v.error && !it.error);
    } else {
        printf("no timedate1 (%s)\n", r.error ? r.error : "?");
    }
    gapi_dbus_reply_free(&r);
}

int main(void)
{
    gapi_dbus *c;
    ghal_status st;

    Marshalling();
    st = gapi_dbus_open(GAPI_DBUS_SYSTEM, &c);
    if (st == GHAL_OK) {
        printf("system bus: %s\n", gapi_dbus_name(c));
        Bus(c);
        gapi_dbus_close(c);
    } else {
        printf("no system bus (%d): bus tests skipped\n", (int)st);
    }
    if (gapi_log_to_system() == GHAL_OK) {
        ghal_log(GHAL_LOG_DEBUG, "test_gapi_dbus: a line to the journal");
        ghal_log_set_sink(NULL);
    }
    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
           fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
