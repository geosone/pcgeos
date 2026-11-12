/*
 * timerMisc.c -- the date and time of GEOS (after timerMisc.asm and the C
 * stubs of timerC.asm).
 *
 * GEOS reads the host's local clock (ghal_time_get_local).  When GEOS is
 * the user's session ([system] hostRole = session), setting the date or
 * time sets the host's clock too (gapi_time_set_local: DOS's clock on
 * dos32, the system clock on linux), as the asm set DOS's clock, unless
 * TIME_SET_GEOS_TIME_ONLY.  As a guest (the default), or when the host
 * does not take it (no privilege), GEOS keeps an offset to the
 * host's clock that it adds from then on (until it exits).  A change
 * goes to GCNSLT_DATE_TIME (MSG_NOTIFY_DATE_TIME_CHANGE) and runs the
 * real-time timers that are due now.
 *
 * The timer interrupt cannot ask the host (DOS), so it counts from the
 * last time read: KernelRealTimeNow gives the minute for the real-time
 * timers from that time and the ticks since.
 */
#include "kernelC.h"
#include "ghal.h"
#include "gapi.h"

#include <timedate.h>

#define GCNSLT_DATE_TIME		4	/* gcnlist.goh */
#define KMSG_NOTIFY_DATE_TIME_CHANGE	697	/* gcnlist.goh */

#define SECS_PER_DAY	86400L
#define TIME_SET_GEOS_TIME_ONLY	0x20	/* timerInt.def */

Boolean kernelHostSession;

static int64_t offset;			/* seconds GEOS is ahead of the host */
static int64_t anchorSecs;		/* GEOS time at anchorTick */
static dword anchorTick;
static Boolean anchored;

/* days since 1/1/1980 of a date (proleptic Gregorian) */
static long DaysOf(long y, int m, int d)
{
    long era, yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 723120L;	/* 1/1/1980 */
}

static void DateOf(long days, ghal_datetime *dt)
{
    long z = days + 723120L, era, doe, yoe, y, doy, mp;

    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    dt->day = (uint8_t) (doy - (153 * mp + 2) / 5 + 1);
    dt->month = (uint8_t) (mp < 10 ? mp + 3 : mp - 9);
    dt->year = (uint16_t) (y + (dt->month <= 2));
    dt->weekday = (uint8_t) ((days + 2) % 7);	/* 1/1/1980: Tuesday */
}

static int64_t SecsOf(const ghal_datetime *dt)
{
    return (int64_t) DaysOf(dt->year, dt->month, dt->day) * SECS_PER_DAY +
	dt->hour * 3600L + dt->minute * 60L + dt->second;
}

static void TimeOf(int64_t secs, ghal_datetime *dt)
{
    long days = (long) (secs / SECS_PER_DAY), rest;

    if (secs < 0 && secs % SECS_PER_DAY)
	days--;
    rest = (long) (secs - (int64_t) days * SECS_PER_DAY);
    DateOf(days, dt);
    dt->hour = (uint8_t) (rest / 3600);
    dt->minute = (uint8_t) (rest / 60 % 60);
    dt->second = (uint8_t) (rest % 60);
}

/* the GEOS time in seconds since 1980; anchors the interrupt's clock */
static int64_t Now(void)
{
    ghal_datetime host;
    int64_t secs;
    dword tick = KernelTicks();

    if (ghal_time_get_local(&host) != GHAL_OK) {
	host.year = 1980;
	host.month = host.day = 1;
	host.hour = host.minute = host.second = 0;
    }
    ghal_crit_enter();
    secs = SecsOf(&host) + offset;
    anchorSecs = secs;
    anchorTick = tick;
    anchored = TRUE;
    ghal_crit_leave();
    return secs;
}

void KernelGetDateTime(ghal_datetime *dt)
{
    TimeOf(Now(), dt);
}

/* for the real-time timers: the TimerCompressedDate in the high word, the
 * hours and minutes in the low word; 0 before the time was read */
dword KernelRealTimeNow(dword tick)
{
    ghal_datetime dt;

    if (!anchored)
	return 0;
    TimeOf(anchorSecs + (tick - anchorTick) / TIMER_INTERRUPT_RATE, &dt);
    return ((dword) (((dt.year - 1980) << TCD_YEAR_OFFSET) |
		     (dt.month << TCD_MONTH_OFFSET) | dt.day) << 16) |
	((dword) dt.hour << 8) | dt.minute;
}

void TimerGetDateAndTime(TimerDateAndTime *dateAndTime)
{
    ghal_datetime dt;

    KernelGetDateTime(&dt);
    dateAndTime->TDAT_year = dt.year;
    dateAndTime->TDAT_month = dt.month;
    dateAndTime->TDAT_day = dt.day;
    dateAndTime->TDAT_dayOfWeek = (DayOfTheWeek) dt.weekday;
    dateAndTime->TDAT_hours = dt.hour;
    dateAndTime->TDAT_minutes = dt.minute;
    dateAndTime->TDAT_seconds = dt.second;
}

void TimerSetDateAndTime(word flags, const TimerDateAndTime *dateAndTime)
{
    ghal_datetime dt;
    int64_t old = Now(), secs;
    EventHandle ev;

    TimeOf(old, &dt);
    if (flags & TIME_SET_TIME) {
	dt.hour = (uint8_t) dateAndTime->TDAT_hours;
	dt.minute = (uint8_t) dateAndTime->TDAT_minutes;
	dt.second = (uint8_t) dateAndTime->TDAT_seconds;
    }
    if (flags & TIME_SET_DATE) {
	dt.year = dateAndTime->TDAT_year;
	dt.month = (uint8_t) dateAndTime->TDAT_month;
	dt.day = (uint8_t) dateAndTime->TDAT_day;
    }
    secs = SecsOf(&dt);
    if (kernelHostSession && !(flags & TIME_SET_GEOS_TIME_ONLY)) {
	ghal_status st = gapi_time_set_local(&dt);
	if (st == GHAL_OK) {
	    ghal_crit_enter();
	    offset = 0;			/* the host has it now */
	    old = secs;
	    ghal_crit_leave();
	} else {
	    static Boolean told;
	    if (!told) {
		told = TRUE;
		ghal_log(GHAL_LOG_INFO, "kernel: the host's clock was not "
			 "set (%d); GEOS keeps its own", (int) st);
	    }
	}
    }
    ghal_crit_enter();
    offset += secs - old;
    anchorSecs = secs;
    anchorTick = KernelTicks();
    TimerCheckRealTime(KernelRealTimeNow(anchorTick));
    ghal_crit_leave();
    TimerReap();
    CObjMessageP(MF_RECORD, 0, KMSG_NOTIFY_DATE_TIME_CHANGE, NULL, 0, &ev,
		 sizeof(ev));
    (void) GCNListSend(MANUFACTURER_ID_GEOWORKS, GCNSLT_DATE_TIME, ev, 0, 0);
}

TimerFileDateTime TimerGetFileDateTime(void)
{
    FileDateAndTime fdat = FileNow();
    TimerFileDateTime r;

    r.TFDT_fileDate = (word) fdat;
    r.TFDT_fileTime = (word) (fdat >> 16);
    return r;
}
