/*
 * gapi_dos32.c -- GAPI on DOS (CInclude/gapi.h).  Here GEOS is the
 * system: these are the DOS and BIOS calls the asm kernel made itself.
 *
 * The clock: DOS's date and time (int 21h/2Bh, 2Dh), as RestoreMSDOSTime
 * of timerMisc.asm; DOS keeps the RTC in step on an AT.
 *
 * Power: the reset as EndGeos of bootBoot.asm did it in protected mode
 * (the warm start flag in the BIOS data area, then the reset line of the
 * keyboard controller); off through APM (int 15h/53xxh) as the power
 * drivers did.  Neither returns when it works.
 */
#include "gapi.h"

#include <dos.h>
#include <string.h>

#ifdef __DJGPP__
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#define outp(port, v)   outportb((port), (unsigned char)(v))
#define inp(port)       inportb(port)
#define poke16(a, v)    _farpokew(_dos_ds, (a), (v))
#else
#include <conio.h>
#include <i86.h>
#define poke16(a, v)    (*(volatile unsigned short *)(a) = (v))
#endif

#define BIOS_RESET_FLAG 0x472   /* 40:72 */
#define BRF_WARM_START  0x1234

/* DOS asks nobody and logs nowhere else */
void gapi_set_interactive(int allow)
{
    (void) allow;
}

int gapi_host_is_geos(void)
{
    return 1;                   /* on DOS, GEOS is the system */
}

ghal_status gapi_log_to_system(void)
{
    return GHAL_ERR_UNSUPPORTED;
}

const char *gapi_last_error(void)
{
    return "";
}

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

/* ---------------------------------------------------------------------
 * Power
 * ------------------------------------------------------------------- */

/* int 15h with ax, bx, cx in real mode; the carry flag */
static int Int15(unsigned short *ax, unsigned short *bx, unsigned short cx)
{
#ifdef __DJGPP__
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = *ax;
    r.x.bx = *bx;
    r.x.cx = cx;
    if (__dpmi_int(0x15, &r) != 0)
        return 1;
    *ax = r.x.ax;
    *bx = r.x.bx;
    return (r.x.flags & 1) != 0;
#else
    union REGS r;               /* DOS/32A passes it to real mode */
    memset(&r, 0, sizeof r);
    r.w.ax = *ax;
    r.w.bx = *bx;
    r.w.cx = cx;
    int386(0x15, &r, &r);
    *ax = r.w.ax;
    *bx = r.w.bx;
    return r.x.cflag != 0;
#endif
}

/* APM there (installation check: "PM" in bx)? */
static int HaveApm(void)
{
    unsigned short ax = 0x5300, bx = 0;
    return !Int15(&ax, &bx, 0) && bx == 0x504d;
}

ghal_status gapi_power_can(gapi_power_action a)
{
    switch (a) {
    case GAPI_POWER_REBOOT:
        return GHAL_OK;
    case GAPI_POWER_OFF:
        return HaveApm() ? GHAL_OK : GHAL_ERR_UNSUPPORTED;
    case GAPI_POWER_SUSPEND:
    case GAPI_POWER_HIBERNATE:
        return GHAL_ERR_UNSUPPORTED;
    default:
        return GHAL_ERR_INVAL;
    }
}

static void Reboot(void)
{
    unsigned n;

    poke16(BIOS_RESET_FLAG, BRF_WARM_START);
    _disable();
    for (n = 0xffff; n && (inp(0x64) & 2); n--)  /* input buffer full? */
        ;
    outp(0x64, 0xfe);           /* pulse the reset line */
    for (n = 0; n < 0x1000000; n++)
        (void)inp(0x80);
    _enable();                  /* still alive */
}

static void PowerOff(void)
{
    unsigned short ax, bx;

    ax = 0x5301; bx = 0;        /* connect the real mode interface */
    (void)Int15(&ax, &bx, 0);
    ax = 0x530e; bx = 0;        /* driver version 1.2 */
    (void)Int15(&ax, &bx, 0x0102);
    ax = 0x5307; bx = 1;        /* all devices: off */
    (void)Int15(&ax, &bx, 3);
}

ghal_status gapi_power_do(gapi_power_action a)
{
    ghal_status st = gapi_power_can(a);

    if (st != GHAL_OK)
        return st;
    if (a == GAPI_POWER_REBOOT)
        Reboot();
    else
        PowerOff();
    return GHAL_ERR_HOST;       /* it did not happen */
}

