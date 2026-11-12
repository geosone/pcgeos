/*
 * dateTimeFields.c -- dates and times formatted by a format string (after
 * dateTimeFields.asm, SBCS).  The format string is text with tokens
 * between '|': "|LW|, |LM| |LD|, |LY|"; each token puts in one field (the
 * names and suffixes are strings of localStrings.asm).
 */
#include "kernelC.h"
#include "localInt.h"

/* tokenTable of dateTimeParse.asm (SBCS, without the Japanese ones) */
static const char tokenTable[][2] = {
    { 'D', 'D' },			/* the delimiter itself */
    { 'L', 'W' }, { 'S', 'W' },		/* weekday */
    { 'L', 'M' }, { 'S', 'M' }, { 'N', 'M' }, { 'Z', 'M' }, { 'P', 'M' },
    { 'L', 'D' }, { 'S', 'D' }, { 'Z', 'D' }, { 'P', 'D' },	/* date */
    { 'L', 'Y' }, { 'S', 'Y' },		/* year */
    { 'H', 'H' }, { 'Z', 'H' }, { 'P', 'H' },	/* 12-hour */
    { 'h', 'h' }, { 'Z', 'h' }, { 'P', 'h' },	/* 24-hour */
    { 'm', 'm' }, { 'Z', 'm' }, { 'P', 'm' },	/* minute */
    { 's', 's' }, { 'Z', 's' }, { 'P', 's' },	/* second */
    { 'a', 'p' }, { 'A', 'p' }, { 'A', 'P' }	/* am/pm */
};
#define NUM_TOKENS	(int) (sizeof(tokenTable) / sizeof(tokenTable[0]))

int LocalFindToken(char c1, char c2)
{
    int i;

    for (i = 0; i < NUM_TOKENS; i++)
	if (tokenTable[i][0] == c1 && tokenTable[i][1] == c2)
	    return i;
    return -1;
}

#define NO_PAD	0xff

/* InsertPadded2DigitNumber: the high digit, or the pad, or nothing */
static char *Number2(char *p, byte n, byte pad)
{
    byte high = (byte) (n / 10);

    if (high)
	*p++ = (char) ('0' + high);
    else if (pad != NO_PAD)
	*p++ = (char) pad;
    *p++ = (char) ('0' + n % 10);
    return p;
}

/* StoreResourceString: without the null */
static char *Store(char *p, word id)
{
    const char *s = LocalString(id);

    while (*s)
	*p++ = *s++;
    return p;
}

/* the hour of a 12-hour clock: 12 for 0 */
static byte Hour12(byte h)
{
    if (h >= 12)
	h = (byte) (h - 12);
    return h ? h : 12;
}

/* HandleToken: the strings locked */
static char *Token(char *p, int t, const LocalDateTime *dt)
{
    static const byte pads[3] = { NO_PAD, '0', ' ' };
    byte am = dt->LDT_hours < 12;

    switch (t) {
    case 0:	*p++ = TOKEN_DELIMITER; break;
    case 1:	p = Store(p, (word) (LS_SundayLongName + dt->LDT_weekday)); break;
    case 2:	p = Store(p, (word) (LS_SundayShortName + dt->LDT_weekday)); break;
    case 3:	p = Store(p, (word) (LS_JanuaryLongName + dt->LDT_month - 1)); break;
    case 4:	p = Store(p, (word) (LS_JanuaryShortName + dt->LDT_month - 1));
		break;
    case 5: case 6: case 7:
	p = Number2(p, dt->LDT_month, pads[t - 5]);
	break;
    case 8:
	p = Number2(p, dt->LDT_day, NO_PAD);
	p = Store(p, (word) (LS_Suffix_1 + dt->LDT_day - 1));
	break;
    case 9: case 10: case 11:
	p = Number2(p, dt->LDT_day, pads[t - 9]);
	break;
    case 12:
	p = Number2(p, (byte) (dt->LDT_year / 100), '0');
	p = Number2(p, (byte) (dt->LDT_year % 100), '0');
	break;
    case 13:
	p = Number2(p, (byte) (dt->LDT_year % 100), '0');
	break;
    case 14: case 15: case 16:
	p = Number2(p, Hour12(dt->LDT_hours), pads[t - 14]);
	break;
    case 17: case 18: case 19:
	p = Number2(p, dt->LDT_hours, pads[t - 17]);
	break;
    case 20: case 21: case 22:
	p = Number2(p, dt->LDT_minutes, pads[t - 20]);
	break;
    case 23: case 24: case 25:
	p = Number2(p, dt->LDT_seconds, pads[t - 23]);
	break;
    case 26:	p = Store(p, am ? LS_AMText : LS_PMText); break;
    case 27:	p = Store(p, am ? LS_AMCapText : LS_PMCapText); break;
    case 28:	p = Store(p, am ? LS_AMAllCapsText : LS_PMAllCapsText); break;
    default:	break;		/* not a token: nothing (the asm died) */
    }
    return p;
}

word LocalDateTimeFieldFormat(char *str, const char *format,
			      const LocalDateTime *dt)
{
    char *p = str;
    char c;

    LocalLockStrings();
    while ((c = *format++) != 0) {
	if (c != TOKEN_DELIMITER) {
	    *p++ = c;
	    continue;
	}
	if (!format[0] || !format[1])
	    break;
	p = Token(p, LocalFindToken(format[0], format[1]), dt);
	format += 2;
	if (*format)
	    format++;		/* the delimiter at its end */
    }
    LocalUnlockStrings();
    *p = 0;
    return (word) (p - str);
}

/* the fields of a TimerDateAndTime: the low byte of each, as CGetDateTime
 * of timerC.asm */
static void FromTDAT(LocalDateTime *dt, const TimerDateAndTime *t)
{
    dt->LDT_year = t->TDAT_year;
    dt->LDT_month = (byte) t->TDAT_month;
    dt->LDT_day = (byte) t->TDAT_day;
    dt->LDT_weekday = (byte) t->TDAT_dayOfWeek;
    dt->LDT_hours = (byte) t->TDAT_hours;
    dt->LDT_minutes = (byte) t->TDAT_minutes;
    dt->LDT_seconds = (byte) t->TDAT_seconds;
}

/* LocalCustomFormatDateTime: not language independent; the length without
 * the null */
word LocalCustomFormatDateTime(char *str, const char *format,
			       const TimerDateAndTime *dateTime)
{
    LocalDateTime dt;

    FromTDAT(&dt, dateTime);
    return LocalDateTimeFieldFormat(str, format, &dt);
}

/* LocalFormatDateTime: in one of the standard formats */
word LocalFormatDateTime(char *str, DateTimeFormat format,
			 const TimerDateAndTime *dateTime)
{
    LocalDateTime dt;
    word n;

    FromTDAT(&dt, dateTime);
    LocalLockStrings();
    n = LocalDateTimeFieldFormat(str, LocalString((word) (LS_DateLong + format)),
				 &dt);
    LocalUnlockStrings();
    return n;
}

/* LocalFormatFileDateTime: of a FileDateAndTime (the date in the low
 * word, the time in the high one) */
word LocalFormatFileDateTime(char *str, DateTimeFormat format,
			     const FileDateAndTime *dateTime)
{
    TimerDateAndTime t;
    word date = (word) *dateTime, time = (word) (*dateTime >> 16);

    t.TDAT_year = (word) (((date & FD_YEAR) >> FD_YEAR_OFFSET) + 1980);
    t.TDAT_month = (word) ((date & FD_MONTH) >> FD_MONTH_OFFSET);
    t.TDAT_day = (word) (date & FD_DAY);
    t.TDAT_hours = (word) ((time & FT_HOUR) >> FT_HOUR_OFFSET);
    t.TDAT_minutes = (word) ((time & FT_MINUTE) >> FT_MINUTE_OFFSET);
    t.TDAT_seconds = (word) ((time & FT_2SECOND) << 1);
    t.TDAT_dayOfWeek = (DayOfTheWeek) LocalCalcDayOfWeek(t.TDAT_year,
							 t.TDAT_month,
							 t.TDAT_day);
    return LocalFormatDateTime(str, format, &t);
}
