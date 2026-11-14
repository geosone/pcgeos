/*
 * gapi_linux.c -- GAPI on Linux (CInclude/gapi.h): the host's services
 * through our own D-Bus client (gapi_dbus.c) on the system bus.
 *
 * Power: systemd-logind (org.freedesktop.login1.Manager), Can* first;
 * the batteries and the mains from /sys/class/power_supply.
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
#include <dirent.h>

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

/* a line of /sys/class/power_supply/<name>/<attr>, without the newline;
 * FALSE if there is none */
static int SupplyRead(const char *dir, const char *name, const char *attr,
                      char *buf, size_t size)
{
    char path[512];
    FILE *f;
    size_t n;

    snprintf(path, sizeof path, "%s/%s/%s", dir, name, attr);
    if (!(f = fopen(path, "r")))
        return 0;
    if (!fgets(buf, (int)size, f))
        buf[0] = 0;
    fclose(f);
    n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = 0;
    return 1;
}

static long SupplyNum(const char *dir, const char *name, const char *attr)
{
    char buf[64];
    char *end;
    long v;

    if (!SupplyRead(dir, name, attr, buf, sizeof buf) || !buf[0])
        return -1;
    v = strtol(buf, &end, 10);
    return *end ? -1 : v;
}

/*
 * The supplies of type Mains (online: on the mains) and Battery, except
 * those of scope Device (a mouse's, a headset's).  With several
 * batteries, the charge is their energy (or charge) now over full, the
 * time left their energy over their power when discharging; else the
 * mean of their capacities.  Low: capacity_level Low or Critical.
 */
ghal_status gapi_power_status(gapi_power_info *s)
{
    const char *dir = getenv("GAPI_POWER_SUPPLY");
    DIR *d;
    struct dirent *de;
    char buf[64];
    long now = 0, full = 0, rate = 0, cap = 0;
    int caps = 0, nrg = 1, discharging = 0, mains = 0;

    if (!s)
        return GHAL_ERR_INVAL;
    s->ac = s->charging = s->permille = s->minutes = -1;
    s->battery = 0;
    s->low = 0;
    if (!dir || !*dir)
        dir = "/sys/class/power_supply";
    if (!(d = opendir(dir)))
        return GHAL_ERR_UNSUPPORTED;
    while ((de = readdir(d)) != NULL) {
        long n, f, p;
        if (de->d_name[0] == '.')
            continue;
        if (!SupplyRead(dir, de->d_name, "type", buf, sizeof buf))
            continue;
        if (!strcmp(buf, "Mains")) {
            mains = 1;
            if (SupplyNum(dir, de->d_name, "online") > 0)
                s->ac = 1;
            else if (s->ac < 0)
                s->ac = 0;
            continue;
        }
        if (strcmp(buf, "Battery"))
            continue;
        if (SupplyRead(dir, de->d_name, "scope", buf, sizeof buf) &&
            !strcmp(buf, "Device"))
            continue;
        if (SupplyNum(dir, de->d_name, "present") == 0)
            continue;
        s->battery = 1;
        if (SupplyRead(dir, de->d_name, "status", buf, sizeof buf)) {
            if (!strcmp(buf, "Charging"))
                s->charging = 1;
            else if (s->charging < 0 && buf[0] && strcmp(buf, "Unknown"))
                s->charging = 0;
            if (!strcmp(buf, "Discharging"))
                discharging = 1;
        }
        if (SupplyRead(dir, de->d_name, "capacity_level", buf, sizeof buf) &&
            (!strcmp(buf, "Low") || !strcmp(buf, "Critical")))
            s->low = 1;
        if ((n = SupplyNum(dir, de->d_name, "capacity")) >= 0) {
            cap += n;
            caps++;
        }
        n = SupplyNum(dir, de->d_name, "energy_now");
        f = SupplyNum(dir, de->d_name, "energy_full");
        p = SupplyNum(dir, de->d_name, "power_now");
        if (n < 0 || f <= 0) {          /* µAh, µA instead of µWh, µW */
            n = SupplyNum(dir, de->d_name, "charge_now");
            f = SupplyNum(dir, de->d_name, "charge_full");
            p = SupplyNum(dir, de->d_name, "current_now");
        }
        if (n >= 0 && f > 0) {
            now += n;
            full += f;
            if (p > 0)
                rate += p;
        } else
            nrg = 0;
    }
    closedir(d);
    if (!s->battery) {
        if (!mains)
            return s->ac < 0 ? GHAL_ERR_UNSUPPORTED : GHAL_OK;
        return GHAL_OK;
    }
    if (nrg && full > 0) {
        s->permille = (int)((double)now * 1000.0 / (double)full + 0.5);
        if (s->permille > 1000)
            s->permille = 1000;
        if (discharging && rate > 0)
            s->minutes = (int)((double)now * 60.0 / (double)rate + 0.5);
    } else if (caps)
        s->permille = (int)(cap * 10 / caps);
    if (s->ac < 0 && mains)
        s->ac = 0;
    if (s->ac < 0 && discharging)
        s->ac = 0;
    return GHAL_OK;
}

