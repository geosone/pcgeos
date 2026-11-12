/*
 * gapi_linux.c -- GAPI on Linux (CInclude/gapi.h): the host's services
 * through our own D-Bus client (gapi_dbus.c) on the system bus.
 *
 * Power: systemd-logind (org.freedesktop.login1.Manager), Can* first.
 *
 * The clock: systemd-timedated (org.freedesktop.timedate1.SetTime); polkit
 * decides whether the user may, and asks for a password when GAPI is
 * interactive.  Without timedated (no systemd), clock_settime, which
 * needs CAP_SYS_TIME.
 *
 * One connection to the system bus, opened on first use and opened again
 * after an error.  GEOS calls GAPI from one context at a time.
 */
#define _GNU_SOURCE
#include "gapi.h"
#include "gapi_dbus.h"
#include "gapi_linux.h"

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int interactive;
static char lastError[256];
static gapi_dbus *systemBus;

void gapi_set_interactive(int allow)
{
    interactive = allow != 0;
}

int gapi_host_is_geos(void)
{
    return 0;                   /* GEOS is one program of the host */
}

const char *gapi_last_error(void)
{
    return lastError;
}

void gapi_linux_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lastError, sizeof lastError, fmt, ap);
    va_end(ap);
}

static ghal_status SystemBus(gapi_dbus **c)
{
    ghal_status st = GHAL_OK;

    if (!systemBus)
        st = gapi_dbus_open(GAPI_DBUS_SYSTEM, &systemBus);
    *c = systemBus;
    return st;
}

/* call on the system bus; keeps the error text; drops the connection
 * when the transport failed */
static ghal_status SystemCall(gapi_dbus_msg *m)
{
    gapi_dbus *c;
    gapi_dbus_reply r;
    ghal_status st;

    lastError[0] = 0;
    if ((st = SystemBus(&c)) != GHAL_OK) {
        snprintf(lastError, sizeof lastError, "no system bus");
        return st;
    }
    if (interactive)
        m->flags |= GAPI_DBUS_ALLOW_INTERACTIVE_AUTH;
    st = gapi_dbus_call(c, m, interactive ? 300000 : -1, &r);
    if (st != GHAL_OK) {
        gapi_dbus_close(systemBus);
        systemBus = NULL;
        snprintf(lastError, sizeof lastError, "system bus: no reply");
        return st;
    }
    st = gapi_dbus_status(&r);
    if (st != GHAL_OK) {
        gapi_dbus_iter it;
        const char *text = "";
        gapi_dbus_iter_init(&it, &r);
        if (gapi_dbus_iter_type(&it) == 's')
            text = gapi_dbus_get_string(&it);
        snprintf(lastError, sizeof lastError, "%s: %s",
                 r.error ? r.error : "?", text);
        /* the host keeps the clock itself */
        if (r.error && strstr(r.error, "AutomaticTimeSyncEnabled"))
            st = GHAL_ERR_ACCESS;
    }
    gapi_dbus_reply_free(&r);
    return st;
}

static ghal_status ToEpoch(const ghal_datetime *dt, time_t *t)
{
    struct tm tm;

    if (!dt || dt->month < 1 || dt->month > 12 || dt->day < 1 ||
        dt->day > 31 || dt->hour > 23 || dt->minute > 59 || dt->second > 59)
        return GHAL_ERR_INVAL;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = dt->year - 1900;
    tm.tm_mon = dt->month - 1;
    tm.tm_mday = dt->day;
    tm.tm_hour = dt->hour;
    tm.tm_min = dt->minute;
    tm.tm_sec = dt->second;
    tm.tm_isdst = -1;               /* the host's time zone decides */
    *t = mktime(&tm);
    return *t == (time_t)-1 ? GHAL_ERR_INVAL : GHAL_OK;
}

ghal_status gapi_time_set_local(const ghal_datetime *dt)
{
    gapi_dbus_msg *m;
    struct timespec ts;
    time_t t;
    ghal_status st;

    if ((st = ToEpoch(dt, &t)) != GHAL_OK)
        return st;
    m = malloc(sizeof *m);
    if (!m)
        return GHAL_ERR_NOMEM;
    gapi_dbus_msg_init(m, "org.freedesktop.timedate1",
                       "/org/freedesktop/timedate1",
                       "org.freedesktop.timedate1", "SetTime", "xbb");
    gapi_dbus_put_int64(m, (int64_t)t * 1000000);   /* usec, UTC */
    gapi_dbus_put_bool(m, 0);                       /* not relative */
    gapi_dbus_put_bool(m, interactive);
    st = SystemCall(m);
    free(m);
    if (st != GHAL_ERR_UNSUPPORTED)
        return st;
    /* no timedated: the system call */
    ts.tv_sec = t;
    ts.tv_nsec = 0;
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        snprintf(lastError, sizeof lastError, "clock_settime: %s",
                 strerror(errno));
        return errno == EPERM ? GHAL_ERR_ACCESS : GHAL_ERR_HOST;
    }
    return GHAL_OK;
}

/* ---------------------------------------------------------------------
 * Power: login1
 * ------------------------------------------------------------------- */

static const char *const powerNames[] = {
    "PowerOff", "Reboot", "Suspend", "Hibernate"
};

ghal_status gapi_power_can(gapi_power_action a)
{
    gapi_dbus *c;
    gapi_dbus_msg *m;
    gapi_dbus_reply r;
    gapi_dbus_iter it;
    char member[32];
    const char *ans;
    ghal_status st;

    if ((unsigned)a > GAPI_POWER_HIBERNATE)
        return GHAL_ERR_INVAL;
    lastError[0] = 0;
    if ((st = SystemBus(&c)) != GHAL_OK)
        return GHAL_ERR_UNSUPPORTED;
    m = malloc(sizeof *m);
    if (!m)
        return GHAL_ERR_NOMEM;
    snprintf(member, sizeof member, "Can%s", powerNames[a]);
    gapi_dbus_msg_init(m, "org.freedesktop.login1", "/org/freedesktop/login1",
                       "org.freedesktop.login1.Manager", member, NULL);
    st = gapi_dbus_call(c, m, -1, &r);
    free(m);
    if (st != GHAL_OK) {
        gapi_dbus_close(systemBus);
        systemBus = NULL;
        return st;
    }
    st = gapi_dbus_status(&r);
    if (st == GHAL_OK) {
        gapi_dbus_iter_init(&it, &r);
        ans = gapi_dbus_get_string(&it);
        snprintf(lastError, sizeof lastError, "%s: %s", member, ans);
        /* yes, challenge (a password), no, na (not available) */
        if (it.error)
            st = GHAL_ERR_HOST;
        else if (!strcmp(ans, "yes"))
            st = GHAL_OK;
        else if (!strcmp(ans, "challenge"))
            st = interactive ? GHAL_OK : GHAL_ERR_ACCESS;
        else if (!strcmp(ans, "na"))
            st = GHAL_ERR_UNSUPPORTED;
        else
            st = GHAL_ERR_ACCESS;
    } else {
        snprintf(lastError, sizeof lastError, "%s",
                 r.error ? r.error : "?");
    }
    gapi_dbus_reply_free(&r);
    return st;
}

ghal_status gapi_power_do(gapi_power_action a)
{
    gapi_dbus_msg *m;
    ghal_status st;

    if ((unsigned)a > GAPI_POWER_HIBERNATE)
        return GHAL_ERR_INVAL;
    m = malloc(sizeof *m);
    if (!m)
        return GHAL_ERR_NOMEM;
    gapi_dbus_msg_init(m, "org.freedesktop.login1", "/org/freedesktop/login1",
                       "org.freedesktop.login1.Manager", powerNames[a], "b");
    gapi_dbus_put_bool(m, interactive);
    st = SystemCall(m);
    free(m);
    return st;
}

