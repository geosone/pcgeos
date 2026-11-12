/*
 * gapi_linux.c -- GAPI on Linux (CInclude/gapi.h).
 *
 * The clock: clock_settime(CLOCK_REALTIME), which needs CAP_SYS_TIME; a
 * user without it gets GHAL_ERR_ACCESS (later: systemd-timedated over
 * D-Bus, which asks polkit).  A running NTP client may set it back.
 */
#define _GNU_SOURCE
#include "gapi.h"

#include <errno.h>
#include <time.h>

ghal_status gapi_time_set_local(const ghal_datetime *dt)
{
    struct tm tm = { 0 };
    struct timespec ts;

    if (!dt || dt->month < 1 || dt->month > 12 || dt->day < 1 ||
        dt->day > 31 || dt->hour > 23 || dt->minute > 59 || dt->second > 59)
        return GHAL_ERR_INVAL;
    tm.tm_year = dt->year - 1900;
    tm.tm_mon = dt->month - 1;
    tm.tm_mday = dt->day;
    tm.tm_hour = dt->hour;
    tm.tm_min = dt->minute;
    tm.tm_sec = dt->second;
    tm.tm_isdst = -1;               /* the host's time zone decides */
    ts.tv_sec = mktime(&tm);
    ts.tv_nsec = 0;
    if (ts.tv_sec == (time_t)-1)
        return GHAL_ERR_INVAL;
    if (clock_settime(CLOCK_REALTIME, &ts) != 0)
        return errno == EPERM ? GHAL_ERR_ACCESS : GHAL_ERR_HOST;
    return GHAL_OK;
}
