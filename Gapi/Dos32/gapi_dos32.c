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

/* int 15h in real mode with r[] = ax, bx, cx, dx, which it returns;
 * the carry flag */
static int Int15Regs(unsigned short r4[4])
{
#ifdef __DJGPP__
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = r4[0];
    r.x.bx = r4[1];
    r.x.cx = r4[2];
    r.x.dx = r4[3];
    if (__dpmi_int(0x15, &r) != 0)
        return 1;
    r4[0] = r.x.ax;
    r4[1] = r.x.bx;
    r4[2] = r.x.cx;
    r4[3] = r.x.dx;
    return (r.x.flags & 1) != 0;
#else
    union REGS r;               /* DOS/32A passes it to real mode */
    memset(&r, 0, sizeof r);
    r.w.ax = r4[0];
    r.w.bx = r4[1];
    r.w.cx = r4[2];
    r.w.dx = r4[3];
    int386(0x15, &r, &r);
    r4[0] = r.w.ax;
    r4[1] = r.w.bx;
    r4[2] = r.w.cx;
    r4[3] = r.w.dx;
    return r.x.cflag != 0;
#endif
}

/* int 15h with ax, bx, cx; the carry flag */
static int Int15(unsigned short *ax, unsigned short *bx, unsigned short cx)
{
    unsigned short r[4];
    int c;

    r[0] = *ax;
    r[1] = *bx;
    r[2] = cx;
    r[3] = 0;
    c = Int15Regs(r);
    *ax = r[0];
    *bx = r[1];
    return c;
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

/* APM's power status (int 15h/530Ah, all batteries: bx 1): bh the AC
 * line (0 off, 1 on), bl the battery (0 high, 1 low, 2 critical, 3
 * charging), ch flags (bit 7: no battery), cl percent, dx the time left
 * (bit 15: minutes, else seconds); 0xff (0xffff) unknown */
ghal_status gapi_power_status(gapi_power_info *s)
{
    unsigned short r[4];

    if (!s)
        return GHAL_ERR_INVAL;
    s->ac = s->charging = s->permille = s->minutes = -1;
    s->battery = 0;
    s->low = 0;
    if (!HaveApm())
        return GHAL_ERR_UNSUPPORTED;
    r[0] = 0x530a;
    r[1] = 1;
    r[2] = r[3] = 0;
    if (Int15Regs(r))
        return GHAL_ERR_UNSUPPORTED;
    if ((r[1] >> 8) <= 1)
        s->ac = r[1] >> 8;
    if ((r[2] >> 8) == 0xff || !((r[2] >> 8) & 0x80)) {
        if ((r[1] & 0xff) != 0xff || (r[2] & 0xff) != 0xff)
            s->battery = 1;
    }
    if (s->battery) {
        switch (r[1] & 0xff) {
        case 1: case 2:
            s->low = 1;
            s->charging = 0;
            break;
        case 3:
            s->charging = 1;
            break;
        case 0:
            s->charging = 0;
            break;
        }
        if ((r[2] & 0xff) <= 100)
            s->permille = (r[2] & 0xff) * 10;
        if (r[3] != 0xffff)
            s->minutes = r[3] & 0x8000 ? r[3] & 0x7fff : (r[3] + 30) / 60;
    }
    return GHAL_OK;
}

/* ---------------------------------------------------------------------
 * Printing: GEOS's printer drivers do it themselves on DOS
 * ------------------------------------------------------------------- */

ghal_status gapi_print_list(gapi_printer *list, int max, int *count)
{
    (void)list;
    (void)max;
    *count = 0;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status gapi_print_default(char *name, size_t size)
{
    if (size)
        name[0] = 0;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status gapi_print_job_open(const char *printer, const char *title,
                                gapi_doc_format format, int copies,
                                int duplex, gapi_print_job **job)
{
    (void)printer; (void)title; (void)format; (void)copies; (void)duplex;
    *job = 0;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status gapi_print_job_write(gapi_print_job *job, const void *data,
                                 size_t size)
{
    (void)job; (void)data; (void)size;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status gapi_print_job_close(gapi_print_job *job, uint32_t *jobId)
{
    (void)job;
    if (jobId)
        *jobId = 0;
    return GHAL_ERR_UNSUPPORTED;
}

void gapi_print_job_cancel(gapi_print_job *job)
{
    (void)job;
}

