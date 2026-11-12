/*
 * dateTimeFormat.c -- the standard date and time formats, the day of the
 * week, the time zone, and comparing and normalizing dates (after
 * dateTimeFormat.asm, SBCS).
 *
 * The standard formats are the strings DateLong... of localStrings.asm,
 * one per DateTimeFormat; geos.ini [localization] overrides them (as data,
 * under the keys of formatKeys), and LocalSetDateTimeFormat writes them
 * there.  The time zone is minutes to add to GMT, -8*60 and no daylight
 * saving time unless [localization] timezone and useDST say else.
 */
#include "kernelC.h"
#include "localInt.h"

#include <initfile.h>

/* formatKeyPointers */
static const char *const formatKeys[] = {
    "longDate", "longCondensedDate", "longDateNoWeekday",
    "longCondensedDateNoWeekday", "shortDate", "zeroPaddedShortDate",
    "monthDayLongDate", "monthDayLongDateNoWeekday", "monthDayShort",
    "monthYearLong", "monthYearShort", "year", "month", "day", "weekday",
    "hoursMinsSecsTime", "hoursMinsTime", "hoursTime", "minsSecsTime",
    "hoursMinsSecs24HourTime", "hoursMins24HourTime"
};
#define NUM_FORMATS	(sizeof(formatKeys) / sizeof(formatKeys[0]))

/* daysPerMonth of timerMisc.asm */
static const byte daysPerMonth[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* LocalCalcDayOfWeek: for 1904 through 2099; 0 is Sunday */
word LocalCalcDayOfWeek(word year, word month, word day)
{
    word years = (word) (year - 1904);
    word total = (word) (years >> 2);		/* the leap days */
    word m;

    if (!(years & 3) && (month & 0xff) < 3)
	total--;		/* not yet this year's */
    total = (word) (total + years + 5 + (day & 0xff));	/* 1/1/1904: Friday */
    for (m = (word) ((month & 0xff) - 1); m; m--)
	total = (word) (total + daysPerMonth[m - 1]);
    return (word) (total % 7);
}

void LocalGetDateTimeFormat(char *str, DateTimeFormat format)
{
    LocalGetString((word) (LS_DateLong + format), str, DATE_TIME_FORMAT_SIZE);
}

void LocalSetDateTimeFormat(const char *str, DateTimeFormat format)
{
    LocalSetString((word) (LS_DateLong + format), str);
    LocalWriteStringAsData(formatKeys[format], (word) (LS_DateLong + format));
}

/* DateTimeInitFormats */
void LocalDateTimeInitFormats(void)
{
    word f;

    for (f = NUM_FORMATS; f-- > 0; )
	(void) LocalReadStringAsData(formatKeys[f], (word) (LS_DateLong + f),
				     DATE_TIME_FORMAT_SIZE);
}

Boolean LocalIsLegalFormatChar(wchar ch, DateTimeFormat format)
{
    const char *p;
    Boolean r = FALSE;
    char c;

    LocalLockStrings();
    p = LocalString((word) (LS_DateLong + format));
    while ((c = *p++) != 0) {
	if (c == TOKEN_DELIMITER) {
	    /* a token: only the one of the delimiter itself counts */
	    if (!p[0] || !p[1] || !p[2])
		break;
	    p += 3;
	    if (LocalFindToken(p[-3], p[-2]) != LOCAL_TOKEN_TOKEN_DELIMITER)
		continue;
	}
	if ((byte) c == ch) {
	    r = TRUE;
	    break;
	}
    }
    LocalUnlockStrings();
    return r;
}

/* localTimezone, localDST */
static sword localTimezone = -8 * 60;
static Boolean localDST = FALSE;

static const char timezoneKey[] = "timezone";
static const char dstKey[] = "useDST";

sword LocalGetTimezone(Boolean *useDST)
{
    *useDST = localDST;
    return localTimezone;
}

void LocalSetTimezone(sword offsetGMT, Boolean useDST)
{
    localTimezone = offsetGMT;
    localDST = useDST;
    InitFileWriteInteger(localizationCategory, timezoneKey, (word) offsetGMT);
    InitFileWriteBoolean(localizationCategory, dstKey, useDST);
}

/* InitTimezone */
void LocalInitTimezone(void)
{
    word tz;
    Boolean dst;

    if (!InitFileReadInteger(localizationCategory, timezoneKey, &tz))
	localTimezone = (sword) tz;
    else
	localTimezone = -8 * 60;
    if (!InitFileReadBoolean(localizationCategory, dstKey, &dst))
	localDST = (Boolean) (dst != 0);
    else
	localDST = FALSE;
}

/* LocalCompareDateTimes: field by field, from the year down, unsigned */
sword LocalCompareDateTimes(TimerDateAndTime *timedate1,
			    TimerDateAndTime *timedate2)
{
    word a[7], b[7];
    int i;

    /* C89: no initializers of automatic arrays from variables */
    a[0] = timedate1->TDAT_year;	b[0] = timedate2->TDAT_year;
    a[1] = timedate1->TDAT_month;	b[1] = timedate2->TDAT_month;
    a[2] = timedate1->TDAT_day;		b[2] = timedate2->TDAT_day;
    a[3] = (word) timedate1->TDAT_dayOfWeek;
    b[3] = (word) timedate2->TDAT_dayOfWeek;
    a[4] = timedate1->TDAT_hours;	b[4] = timedate2->TDAT_hours;
    a[5] = timedate1->TDAT_minutes;	b[5] = timedate2->TDAT_minutes;
    a[6] = timedate1->TDAT_seconds;	b[6] = timedate2->TDAT_seconds;
    for (i = 0; i < 7; i++)
	if (a[i] != b[i])
	    return (sword) (a[i] < b[i] ? -1 : 1);
    return 0;
}

/* doNormalization: in signed bytes as the asm; *borrow -1, 0 or 1 */
static word Wrap(int v, int max, int min, int *borrow)
{
    v = (signed char) v;
    *borrow = 0;
    if (v < min) {
	*borrow = -1;
	v += max - min + 1;
    } else if (v > max) {
	*borrow = 1;
	v -= max - min + 1;
    }
    return (word) (v & 0xff);
}

/* LocalNormalizeDateTime: to GMT, timezone the minutes to add to it; dest
 * may be src */
void LocalNormalizeDateTime(TimerDateAndTime *destTDAT,
			    TimerDateAndTime *srcTDAT, sword timezone)
{
    TimerDateAndTime s = *srcTDAT, d;
    int q = timezone / 60, r = timezone % 60, b, day;
    word prevMonth, prevYear;

    d.TDAT_minutes = Wrap((byte) s.TDAT_minutes - r, 59, 0, &b);
    d.TDAT_hours = Wrap((byte) s.TDAT_hours - q + b, 23, 0, &b);
    day = (signed char) ((byte) s.TDAT_day + b);
    if (day < 1) {
	/* the days of the month before, and one month back */
	b = -1;
	prevMonth = (word) ((byte) s.TDAT_month - 1);
	prevYear = s.TDAT_year;
	if (!(prevMonth & 0xff)) {
	    prevMonth = 12;
	    prevYear--;
	}
	d.TDAT_day = (word) ((day + LocalCalcDaysInMonth(prevYear,
						       prevMonth & 0xff)) & 0xff);
    } else
	d.TDAT_day = Wrap(day,
			  LocalCalcDaysInMonth(s.TDAT_year,
					       (byte) s.TDAT_month),
			  1, &b);
    d.TDAT_month = Wrap((byte) s.TDAT_month + b, 12, 1, &b);
    d.TDAT_year = (word) (s.TDAT_year + b);
    d.TDAT_seconds = s.TDAT_seconds;
    d.TDAT_dayOfWeek = (DayOfTheWeek) LocalCalcDayOfWeek(d.TDAT_year,
							 d.TDAT_month,
							 d.TDAT_day);
    *destTDAT = d;
}
