/*
 * numericMeasure.c -- the measurement system, and numbers and distances
 * as text (after numericMeasure.asm, SBCS).  A number is a WWFixed; the
 * decimal separator is the one of the number format.
 */
#include "kernelC.h"
#include "localInt.h"

#include <graphics.h>
#include <initfile.h>
#include <string.h>

/* the units per point, as WWFixed (PointsPerUnit), and as DWFixed with 16
 * more bits (DPointsPerUnit), of each DistanceUnit */
static const WWFixedAsDWord pointsPerUnit[] = {
    0x00010000,			/* points	   1.000000 */
    0x00480000,			/* inches	  72.000000 */
    (28UL << 16) | 22705,	/* centimeters	  28.346465 */
    (2UL << 16) | 54699,	/* millimeters	   2.834646 */
    0x000c0000,			/* picas	  12.000000 */
    (1UL << 16) | 4299,		/* european points 1.0656 */
    (12UL << 16) | 51590	/* ciceros	  12.7872 */
};
static const struct { word frac; dword whole; } dPointsPerUnit[] = {
    { 0, 1UL << 16 },
    { 0, 72UL << 16 },
    { 60948, 22705 + (28UL << 16) },
    { 25755, 54699 + (2UL << 16) },
    { 0, 12UL << 16 },
    { 10591, 4299 + (1UL << 16) },
    { 61538, 51589 + (12UL << 16) }
};
/* the largest value of each unit that is less than 32767 points */
static const word unitMax[] = { 32766, 455, 1155, 11559, 2730, 30748, 2562 };
#define NUM_UNITS	DU_POINTS_OR_MILLIMETERS	/* RealDistanceUnit */

MeasurementType LocalGetMeasurementType(void)
{
    return LocalStringDigit(LS_measurementType);
}

void LocalSetMeasurementType(MeasurementType meas)
{
    char s[2];

    s[0] = (char) ('0' + meas);
    s[1] = 0;
    LocalSetString(LS_measurementType, s);
    InitFileWriteString(localizationCategory, "measurementSystem", s);
}

static char DecimalSeparator(void)
{
    LocalNumericFormat nf;

    LocalGetNumericFormat(&nf);
    return (char) nf.decimalSeparator;
}

/* SignedToAscii: the length with the null */
static word SignedToAscii(char *buf, sword v)
{
    char tmp[8];
    word n = 0, len = 0;
    word u;

    if (v < 0) {
	buf[len++] = '-';
	u = (word) -v;
    } else
	u = (word) v;
    do {
	tmp[n++] = (char) ('0' + u % 10);
	u /= 10;
    } while (u);
    while (n)
	buf[len++] = tmp[--n];
    buf[len++] = 0;
    return len;
}

/* BXTimes10: the next digit of the fraction */
static char NextDigit(word *frac)
{
    dword x = (dword) *frac * 10;

    *frac = (word) x;
    return (char) ('0' + (x >> 16));
}

/* LocalFixedToAscii: up to fracDigits digits of the fraction, rounded,
 * without trailing zeros */
void LocalFixedToAscii(char *buffer, WWFixedAsDWord value, word fracDigits)
{
    char *p = buffer, *q, sep, c;
    dword v = value;
    word whole, frac, add;

    if ((sdword) v < 0) {
	*p++ = '-';
	v = (dword) 0 - v;
    }
    whole = (word) (v >> 16);
    frac = (word) v;
    p += SignedToAscii(p, (sword) whole) - 1;
    if (!frac)
	return;
    sep = DecimalSeparator();
    *p++ = sep;
    while (fracDigits) {
	*p++ = NextDigit(&frac);
	if (!frac) {
	    *p = 0;
	    return;
	}
	fracDigits--;
    }
    /* out of digits: the next one rounds the rest */
    q = p - 1;
    if (NextDigit(&frac) >= '5') {
	for (;;) {
	    c = *q;
	    if (c == sep) {		/* up into the whole number */
		add = 1;
		break;
	    }
	    if (++c != '9' + 1) {
		*q++ = c;
		*q = 0;
		return;
	    }
	    q--;			/* a 9 that became 0: drop it */
	}
    } else {
	for (;;) {
	    c = *q;
	    if (c == sep) {		/* no fraction left */
		add = 0;
		break;
	    }
	    if (c != '0') {
		q[1] = 0;
		return;
	    }
	    q--;
	}
    }
    /* the whole number again, rounded, without "-0" */
    p = buffer;
    whole = (word) (whole + add);
    if (*p == '-' && whole)
	p++;
    (void) SignedToAscii(p, (sword) whole);
}

/* AsciiToUnsignedInt: 32767 at most */
static word AsciiToUnsigned(const char **pp)
{
    const char *p = *pp;
    dword v = 0;

    while (*p >= '0' && *p <= '9') {
	v = v * 10 + (dword) (*p++ - '0');
	if (v > 32767)
	    v = 32767;
    }
    *pp = p;
    return (word) v;
}

/* fractionTable: 65536 / 10^digits as 32.32, for 1 to 4 digits */
static const uint64_t fractionTable[] = {
    ((uint64_t) 6553 << 32) | (39321UL + (39321UL << 16)),
    ((uint64_t) 655 << 32) | (62914UL + (23592UL << 16)),
    ((uint64_t) 65 << 32) | (19398UL + (35127UL << 16)),
    ((uint64_t) 6 << 32) | (47815UL + (36280UL << 16))
};

/* LocalAsciiToFixed: four digits of the fraction, the fifth rounds */
WWFixedAsDWord LocalAsciiToFixed(const char *buffer, char **parseEnd)
{
    const char *p = buffer;
    Boolean neg = FALSE;
    word whole, frac = 0, digits = 0, f = 0;
    dword v;

    if (*p == '+')
	p++;
    else if (*p == '-') {
	neg = TRUE;
	p++;
    }
    whole = AsciiToUnsigned(&p);
    if (*p == DecimalSeparator()) {
	p++;
	while (digits < 4 && *p >= '0' && *p <= '9') {
	    f = (word) (f * 10 + (*p++ - '0'));
	    digits++;
	}
	if (digits == 4 && *p >= '0' && *p <= '9') {
	    char round = *p;

	    while (*p >= '0' && *p <= '9')
		p++;
	    if (round >= '5' && ++f == 10000) {
		whole++;
		f = 0;
		digits = 0;
	    }
	}
	if (f) {
	    uint64_t prod = (uint64_t) f * fractionTable[digits - 1];

	    frac = (word) (prod >> 32);
	    if (prod & 0x80000000UL)
		frac++;
	}
    }
    if (parseEnd)
	*parseEnd = (char *) p;
    v = ((dword) whole << 16) | frac;
    return neg ? (dword) 0 - v : v;
}

/* ConvertUnitsIfFuzzy */
static DistanceUnit RealUnits(DistanceUnit u, MeasurementType m)
{
    if (u < DU_POINTS_OR_MILLIMETERS)
	return u;
    if (m == MEASURE_US)
	return u == DU_POINTS_OR_MILLIMETERS ? DU_POINTS : DU_INCHES;
    return u == DU_POINTS_OR_MILLIMETERS ? DU_MILLIMETERS : DU_CENTIMETERS;
}

/* LocalDistanceToAscii: value in points, the text in distanceUnits (with
 * the unit's name); the length with the null */
word LocalDistanceToAscii(char *buffer, WWFixedAsDWord value,
			  DistanceUnit distanceUnits, word measurementType,
			  LocalDistanceFlags flags)
{
    DistanceUnit u = RealUnits(distanceUnits, (MeasurementType) measurementType);
    word digits, id;
    char *p;

    if (u >= NUM_UNITS)
	u = DU_POINTS;
    value = GrSDivWWFixed(value, pointsPerUnit[u]);
    digits = u == DU_MILLIMETERS ? 1 : u == DU_CENTIMETERS ? 2 : 3;
    if (flags & LDF_PASSING_DECIMAL_PLACES)
	digits = flags & LDF_DECIMAL_PLACES;
    LocalFixedToAscii(buffer, value, digits);
    if (value == 0x10000)
	flags &= ~LDF_PRINT_PLURAL_IF_NEEDED;
    p = buffer + strlen(buffer);
    if (!(flags & LDF_OMIT_UNITS_STRING)) {
	if (!(flags & LDF_FULL_NAMES))
	    id = (word) (LS_LocalUnitPointString + u);
	else if (flags & LDF_PRINT_PLURAL_IF_NEEDED)
	    id = (word) (LS_LocalUnitLongPluralStrings + u);
	else
	    id = (word) (LS_LocalUnitLongStrings + u);
	*p++ = ' ';
	LocalGetString(id, p,
		       (word) (LOCAL_DISTANCE_BUFFER_SIZE - (p - buffer)));
	p += strlen(p);
    }
    return (word) (p - buffer + 1);
}

/* LocalDistanceFromAscii: the number, in the units its text names ('"',
 * "pt", "in", ...) or else in distanceUnits, in points */
WWFixedAsDWord LocalDistanceFromAscii(const char *buffer,
				      DistanceUnit distanceUnits,
				      MeasurementType measurementType)
{
    DistanceUnit u = RealUnits(distanceUnits, measurementType);
    WWFixedAsDWord v;
    DWFixed i, j, r;
    char *end;
    sword whole;
    word mag, k;

    v = LocalAsciiToFixed(buffer, &end);
    while (*end == ' ')
	end++;
    if (*end == '"')
	u = DU_INCHES;
    else {
	LocalLockStrings();
	for (k = 0; k < NUM_UNITS; k++)
	    if (!LocalCmpStrings(end,
				 LocalString((word) (LS_LocalUnitPointString + k)),
				 0)) {
		u = (DistanceUnit) k;
		break;
	    }
	LocalUnlockStrings();
    }
    if (u >= NUM_UNITS)
	u = DU_POINTS;
    whole = (sword) (v >> 16);
    mag = (word) (whole < 0 ? -whole : whole);
    if (mag > unitMax[u])
	whole = (sword) (whole < 0 ? -(sword) unitMax[u] : (sword) unitMax[u]);
    i.DWF_frac = (word) v;
    i.DWF_int = whole;
    j.DWF_frac = dPointsPerUnit[u].frac;
    j.DWF_int = (sdword) dPointsPerUnit[u].whole;
    GrMulDWFixed(&i, &j, &r);
    return (WWFixedAsDWord) r.DWF_int;
}
