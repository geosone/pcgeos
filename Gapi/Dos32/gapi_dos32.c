/*
 * gapi_dos32.c -- GAPI on DOS (CInclude/gapi.h).  Here GEOS is the
 * system: these are the DOS and BIOS calls the asm kernel made itself.
 *
 * The clock: DOS's date and time (int 21h/2Bh, 2Dh), as RestoreMSDOSTime
 * of timerMisc.asm; DOS keeps the RTC in step on an AT.
 */
#include "gapi.h"

#include <dos.h>

ghal_status gapi_time_set_local(const ghal_datetime *dt)
{
    struct dosdate_t d;
    struct dostime_t t;

    if (!dt || dt->year < 1980 || dt->year > 2099 || dt->month < 1 ||
        dt->month > 12 || dt->day < 1 || dt->day > 31 || dt->hour > 23 ||
        dt->minute > 59 || dt->second > 59)
        return GHAL_ERR_INVAL;
    d.year = dt->year;
    d.month = dt->month;
    d.day = dt->day;
    d.dayofweek = 0;
    t.hour = dt->hour;
    t.minute = dt->minute;
    t.second = dt->second;
    t.hsecond = 0;
    if (_dos_settime(&t) != 0 || _dos_setdate(&d) != 0)
        return GHAL_ERR_INVAL;      /* DOS refused the values */
    return GHAL_OK;
}
