/*
 * gapi_dbus.h -- a small D-Bus client of GEOS's own (Linux GAPI backend).
 *
 * It speaks the D-Bus wire protocol on the bus socket itself: no libdbus
 * or sd-bus, so the cross builds need no target library.  Enough for
 * method calls to the host's services (systemd's timedate1, login1,
 * locale1, polkit, NetworkManager, udisks):
 *
 *   - gapi_dbus_open: the system or session bus, AUTH EXTERNAL, Hello;
 *   - a call is built in a gapi_dbus_msg with the body's signature given
 *     up front, then the values in order (basic types, variants, arrays
 *     and structs);
 *   - gapi_dbus_call sends it and waits for its reply; signals and other
 *     messages that come in between are dropped;
 *   - the reply is read with a gapi_dbus_iter, which checks each value
 *     against the signature.
 *
 * A call blocks the calling thread (and on linux the GEOS context that
 * holds the baton) until the reply comes or timeoutMs runs out.
 * Not here: Unix fds, signals subscription, serving objects.
 */
#ifndef GAPI_DBUS_H
#define GAPI_DBUS_H

#include "ghal.h"

#include <stddef.h>
#include <stdint.h>

typedef enum { GAPI_DBUS_SYSTEM, GAPI_DBUS_SESSION } gapi_dbus_bus;

/* message types */
enum {
    GAPI_DBUS_METHOD_CALL = 1, GAPI_DBUS_METHOD_RETURN = 2,
    GAPI_DBUS_ERROR = 3, GAPI_DBUS_SIGNAL = 4
};
/* flags of a call */
#define GAPI_DBUS_NO_REPLY_EXPECTED         0x01
#define GAPI_DBUS_NO_AUTO_START             0x02
#define GAPI_DBUS_ALLOW_INTERACTIVE_AUTH    0x04

#define GAPI_DBUS_DEFAULT_TIMEOUT   25000   /* ms, as libdbus */
#define GAPI_DBUS_BODY_MAX          8192    /* body of a call */
#define GAPI_DBUS_MSG_MAX           (1L << 20)  /* a reply we accept */

typedef struct gapi_dbus gapi_dbus;

/* Connect to a bus: SYSTEM is $DBUS_SYSTEM_BUS_ADDRESS or
 * /run/dbus/system_bus_socket, SESSION is $DBUS_SESSION_BUS_ADDRESS.
 * GHAL_ERR_UNSUPPORTED if there is no such bus. */
ghal_status gapi_dbus_open(gapi_dbus_bus bus, gapi_dbus **conn);
void        gapi_dbus_close(gapi_dbus *conn);
/* the unique name the bus gave us (":1.42") */
const char *gapi_dbus_name(const gapi_dbus *conn);

/* ---- building a call ------------------------------------------------ */

typedef struct gapi_dbus_msg {
    const char *dest, *path, *iface, *member, *sig;
    uint8_t     flags;
    int         error;          /* the body overflowed */
    size_t      len;
    uint8_t     body[GAPI_DBUS_BODY_MAX];
} gapi_dbus_msg;

/* sig: the signature of the whole body ("" or NULL for none) */
void gapi_dbus_msg_init(gapi_dbus_msg *m, const char *dest, const char *path,
                        const char *iface, const char *member,
                        const char *sig);
void gapi_dbus_put_byte(gapi_dbus_msg *m, uint8_t v);
void gapi_dbus_put_bool(gapi_dbus_msg *m, int v);
void gapi_dbus_put_int16(gapi_dbus_msg *m, int16_t v);
void gapi_dbus_put_uint16(gapi_dbus_msg *m, uint16_t v);
void gapi_dbus_put_int32(gapi_dbus_msg *m, int32_t v);
void gapi_dbus_put_uint32(gapi_dbus_msg *m, uint32_t v);
void gapi_dbus_put_int64(gapi_dbus_msg *m, int64_t v);
void gapi_dbus_put_uint64(gapi_dbus_msg *m, uint64_t v);
void gapi_dbus_put_double(gapi_dbus_msg *m, double v);
void gapi_dbus_put_string(gapi_dbus_msg *m, const char *s);    /* s, o */
void gapi_dbus_put_signature(gapi_dbus_msg *m, const char *g); /* g */
/* a variant: its signature, then the value with the put calls */
void gapi_dbus_put_variant(gapi_dbus_msg *m, const char *sig);
/* an array of elements of the given type signature; pass the returned
 * mark to gapi_dbus_put_array_end after the elements */
size_t gapi_dbus_put_array_begin(gapi_dbus_msg *m, const char *elemSig);
void   gapi_dbus_put_array_end(gapi_dbus_msg *m, size_t mark);
/* a struct or dict entry starts aligned to 8 */
void gapi_dbus_put_struct(gapi_dbus_msg *m);

/* ---- the reply ------------------------------------------------------ */

typedef struct gapi_dbus_reply {
    int         type;           /* GAPI_DBUS_METHOD_RETURN or _ERROR */
    const char *error;          /* the error name, or NULL */
    const char *sig;            /* the body's signature ("" if none) */
    uint8_t    *data;           /* the whole message (owned) */
    size_t      bodyOff, bodyLen;
    int         bigEndian;
} gapi_dbus_reply;

void gapi_dbus_reply_free(gapi_dbus_reply *r);

/* Send a call and wait for its reply.  The transport status: the reply
 * may still be an error (r->type, r->error; gapi_dbus_status). */
ghal_status gapi_dbus_call(gapi_dbus *conn, const gapi_dbus_msg *m,
                           int timeoutMs, gapi_dbus_reply *r);
/* GHAL_OK for a return; for an error, what its name means:
 * GHAL_ERR_ACCESS (access denied, not authorized, interactive
 * authorization required), GHAL_ERR_UNSUPPORTED (no such service,
 * object or method), else GHAL_ERR_HOST */
ghal_status gapi_dbus_status(const gapi_dbus_reply *r);
/* Call and only care whether it worked (the reply is dropped). */
ghal_status gapi_dbus_call_simple(gapi_dbus *conn, const gapi_dbus_msg *m,
                                  int timeoutMs);

/* ---- reading values ------------------------------------------------- */

typedef struct gapi_dbus_iter {
    const gapi_dbus_reply *r;
    const char *sig;            /* the rest of the signature to read */
    const char *sigStart, *sigStop;     /* this level's signature */
    int         array;          /* the signature repeats until end */
    size_t      pos, end;       /* in r->data; alignment from 0 */
    int         error;          /* a type mismatch or a short body */
} gapi_dbus_iter;

void gapi_dbus_iter_init(gapi_dbus_iter *it, const gapi_dbus_reply *r);
/* the type code of the next value, 0 at the end (or after an error) */
int  gapi_dbus_iter_type(gapi_dbus_iter *it);
uint8_t     gapi_dbus_get_byte(gapi_dbus_iter *it);
int         gapi_dbus_get_bool(gapi_dbus_iter *it);
int16_t     gapi_dbus_get_int16(gapi_dbus_iter *it);
uint16_t    gapi_dbus_get_uint16(gapi_dbus_iter *it);
int32_t     gapi_dbus_get_int32(gapi_dbus_iter *it);
uint32_t    gapi_dbus_get_uint32(gapi_dbus_iter *it);
int64_t     gapi_dbus_get_int64(gapi_dbus_iter *it);
uint64_t    gapi_dbus_get_uint64(gapi_dbus_iter *it);
double      gapi_dbus_get_double(gapi_dbus_iter *it);
const char *gapi_dbus_get_string(gapi_dbus_iter *it);   /* s, o, g */
/* enter a variant, array, struct or dict entry: sub reads its content */
void gapi_dbus_recurse(gapi_dbus_iter *it, gapi_dbus_iter *sub);
/* skip the next complete value */
void gapi_dbus_skip(gapi_dbus_iter *it);

#endif /* GAPI_DBUS_H */
