/*
 * dateTimeParse.c -- dates and times parsed by a format string (after
 * dateTimeParse.asm, SBCS), and the days of a month.
 *
 * Spaces in the text and in the format do not count; names compare
 * without case; numbers may be padded with spaces.  A field the format
 * does not have comes back as 0xff (the year as 0xffff), as CSetDateTime
 * of timerC.asm stored the asm's bytes.
 *
 * Port32: LocalCustomParseDateTime returns 0xffff if the text parsed, else
 * the offset of the text that did not match, as the C stub did;
 * LocalParseDateTime returns TRUE or FALSE as localize.h says (the C stub
 * returned the same as LocalCustomParseDateTime).
 */
#include "kernelC.h"
#include "localInt.h"

/* LimitRecord */
#define LR_ALL_DIGITS_PRESENT	0x8000
#define LR_ZERO_NOT_ALLOWED	0x4000
#define LR_LIMIT_VALUE		0x3fff

/* HourRecord: the flags are cleared when found */
#define HR_NO_AM_PM	0x80
#define HR_IS_AM	0x40
#define HR_HOUR		0x3f

/* monthLengths */
static const byte monthLengths[13] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static Boolean IsLeapYear(word year)
{
    if (year & 3)
	return FALSE;
    if (year % 100)
	return TRUE;
    return (Boolean) (((year / 100) & 3) == 0);
}

word LocalCalcDaysInMonth(word year, word month)
{
    word days;

    if (month > 12)
	return 0;
    days = monthLengths[month];
    if (month == 2 && IsLeapYear(year))
	days++;
    return days;
}

/* ParsePaddedNumber: up to maxDigits digits after spaces; the text is
 * left as it was if they are no number within the limits */
static Boolean ParseNumber(const char **text, word limit, int maxDigits,
			   word *value)
{
    const char *p = *text;
    Boolean found = FALSE;
    word v = 0;

    while (*p == ' ')
	p++;
    while (*p >= '0' && *p <= '9') {
	v = (word) (v * 10 + (*p++ - '0'));
	found = TRUE;
	if (!--maxDigits)
	    break;
    }
    if (!found || ((limit & LR_ALL_DIGITS_PRESENT) && maxDigits) ||
	((limit & LR_ZERO_NOT_ALLOWED) && !v) || v > (limit & LR_LIMIT_VALUE))
	return FALSE;
    *text = p;
    *value = v;
    return TRUE;
}

/* ParseResourceStrings: which of count strings from first starts the
 * text (without case) */
static Boolean ParseStrings(const char **text, word first, word count,
			    word *which)
{
    const char *s;
    word i, len;

    for (i = 0; i < count; i++) {
	s = LocalString((word) (first + i));
	len = LocalStringLength(s);
	if (!LocalCmpStringsNoCase(*text, s, len)) {
	    *text += len;
	    *which = i;
	    return TRUE;
	}
    }
    return FALSE;
}

typedef struct {
    word	year;
    byte	month, day, weekday, hour, minutes, seconds;
} ParseState;

static Boolean ParseYear(const char **p, ParseState *st, Boolean shortToo)
{
    word v;

    if (ParseNumber(p, LR_ALL_DIGITS_PRESENT | 9999, 4, &v)) {
	st->year = v;
	return TRUE;
    }
    if (!shortToo || !ParseNumber(p, LR_ALL_DIGITS_PRESENT | 99, 2, &v))
	return FALSE;
    /* a short year before 30 is in the 21st century */
    st->year = (word) (v + (v < 30 ? 2000 : 1900));
    return TRUE;
}

/* ParseToken: the strings locked */
static Boolean ParseToken(const char **p, int t, ParseState *st)
{
    word v;

    switch (t) {
    case 0:					/* ParseTokenDelimiter */
	if (**p != TOKEN_DELIMITER)
	    return FALSE;
	(*p)++;
	return TRUE;
    case 1: case 2:				/* weekday names */
	if (!ParseStrings(p, t == 1 ? LS_SundayLongName : LS_SundayShortName,
			  7, &v))
	    return FALSE;
	st->weekday = (byte) v;
	return TRUE;
    case 3: case 4:				/* month names */
	if (!ParseStrings(p, t == 3 ? LS_JanuaryLongName :
			  LS_JanuaryShortName, 12, &v))
	    return FALSE;
	st->month = (byte) (v + 1);
	return TRUE;
    case 5: case 6: case 7:			/* ParseNumericMonth */
	if (!ParseNumber(p, LR_ZERO_NOT_ALLOWED | 12, 2, &v))
	    return FALSE;
	st->month = (byte) v;
	return TRUE;
    case 8: case 9: case 10: case 11:		/* ParseLongDate, ShortDate */
	if (!ParseNumber(p, LR_ZERO_NOT_ALLOWED | 31, 2, &v))
	    return FALSE;
	st->day = (byte) v;
	if (t == 8)				/* the suffix of this day */
	    return ParseStrings(p, (word) (LS_Suffix_1 + v - 1), 1, &v);
	return TRUE;
    case 12: case 13:
	return ParseYear(p, st, (Boolean) (t == 13));
    case 14: case 15: case 16:			/* Parse12Hour */
    case 17: case 18: case 19:			/* Parse24Hour */
	if (!ParseNumber(p, t <= 16 ? LR_ZERO_NOT_ALLOWED | 12 : 24, 2, &v))
	    return FALSE;
	st->hour = (byte) ((st->hour & ~HR_HOUR) | v);
	return TRUE;
    case 20: case 21: case 22:
	if (!ParseNumber(p, 59, 2, &v))
	    return FALSE;
	st->minutes = (byte) v;
	return TRUE;
    case 23: case 24: case 25:
	if (!ParseNumber(p, 59, 2, &v))
	    return FALSE;
	st->seconds = (byte) v;
	return TRUE;
    case 26: case 27: case 28:			/* ParseAM_PM */
	if (!ParseStrings(p, LS_AMText, 2, &v))
	    return FALSE;
	st->hour &= (byte) ~HR_NO_AM_PM;
	if (v)
	    st->hour &= (byte) ~HR_IS_AM;
	return TRUE;
    default:
	return FALSE;
    }
}

/* ConvertHourRecord */
static byte ConvertHour(byte rec)
{
    byte h = (byte) (rec & HR_HOUR);

    if (rec == 0xff)
	return rec;			/* no hour */
    if (!(rec & HR_NO_AM_PM)) {
	if (rec & HR_IS_AM) {
	    if (h == 12)
		h = 0;			/* 12am */
	} else if (h < 12)
	    h = (byte) (h + 12);
    }
    return (byte) (h & HR_HOUR);
}

/* LocalCustomParseDateTimeReal: 0xffff, or the offset of the mismatch */
static word Parse(const char *str, const char *format, ParseState *st)
{
    const char *p = str;
    char c;
    int t;
    word ms;

    st->year = 0xffff;
    st->month = st->day = st->weekday = st->hour = st->minutes =
	st->seconds = 0xff;
    LocalLockStrings();
    for (;;) {
	while (*p == ' ')
	    p++;
	do
	    c = *format++;
	while (c == ' ');
	if (c == TOKEN_DELIMITER) {
	    if (!format[0] || !format[1])
		break;
	    t = LocalFindToken(format[0], format[1]);
	    format += 2;
	    if (*format)
		format++;		/* the delimiter at its end */
	    if (!ParseToken(&p, t, st))
		break;
	    continue;
	}
	if (c != *p)
	    break;
	if (!c) {
	    LocalUnlockStrings();
	    st->hour = ConvertHour(st->hour);
	    if (st->hour != 24)
		return 0xffff;
	    /* 24:00:00 is 0:00:00 */
	    ms = (word) (st->minutes | (st->seconds << 8));
	    if (ms != 0 && ms != 0xff00 && ms != 0x00ff && ms != 0xffff)
		return 0;
	    st->hour = 0;
	    return 0xffff;
	}
	p++;
    }
    LocalUnlockStrings();
    return (word) (p - str);
}

/* CSetDateTime */
static void ToTDAT(TimerDateAndTime *t, const ParseState *st)
{
    t->TDAT_year = st->year;
    t->TDAT_month = st->month;
    t->TDAT_day = st->day;
    t->TDAT_dayOfWeek = (DayOfTheWeek) st->weekday;
    t->TDAT_hours = st->hour;
    t->TDAT_minutes = st->minutes;
    t->TDAT_seconds = st->seconds;
}

word LocalCustomParseDateTime(const char *str, const char *format,
			      TimerDateAndTime *dateTime)
{
    ParseState st;
    word r = Parse(str, format, &st);

    if (r == 0xffff)
	ToTDAT(dateTime, &st);
    return r;
}

Boolean LocalParseDateTime(const char *str, DateTimeFormat format,
			   TimerDateAndTime *dateTime)
{
    char f[LOCAL_STRING_SIZE];

    LocalGetString((word) (LS_DateLong + format), f, sizeof(f));
    return (Boolean) (LocalCustomParseDateTime(str, f, dateTime) == 0xffff);
}
