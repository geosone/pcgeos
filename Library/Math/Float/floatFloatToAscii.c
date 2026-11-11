/*
 * floatFloatToAscii.c -- C translation of
 *     Library/Math/Float/floatFloatToAscii.asm  (Cheng 1991, witt 1993)
 *
 * Pilot for a 32-bit port of PC/GEOS.  SBCS only (DBCS would make the
 * buffer a wchar array; the logic is identical).
 *
 * Translation rules used:
 *   - register-passed state (es:di, cx, carry flag) -> explicit C variables,
 *     return values; carry flag -> int return (1 = "carry set").
 *   - the FFA_stackFrame stays the shared state, as in the asm.
 *   - FFA_startNumber/decimalPoint/endNumber/startExponent/saveDI held
 *     absolute *segment offsets* (di) in the asm.  In a flat 32-bit world
 *     they are stored as offsets relative to the start of the result buffer.
 *     (0 still means "none" for FFA_decimalPoint -- see README, open issue.)
 *   - known quirks of the original are preserved and marked "QUIRK".
 */
#include <assert.h>
#include <string.h>
#include "fpstack.h"
#include "floatFloatToAscii.h"

#ifdef DO_ERROR_CHECKING
#define EC(x) assert(x)
#else
#define EC(x) ((void)0)
#endif

#define NUM_DEC_DIGITS_IN_32_BITS 10

/* DwordToAsciiFlags (subset used here) */
#define DTAF_INCLUDE_LEADING_ZEROS        0x01
#define DTAF_INCLUDE_LEADING_ZEROS_FIXED  0x02
#define DTAF_NULL_TERMINATE               0x04
#define DTAF_INCLUDE_COMMAS               0x08

/* UI/uiFormat.ui: overflowStr / underflowStr / errorStr */
static const char overflowStr[]  = "overflow";
static const char underflowStr[] = "underflow";
static const char errorStr[]     = "error";

/* Output cursor: replaces es:di */
typedef struct {
    char *buf;      /* es:saveDI */
    int   di;       /* current offset into buf */
} Out;

#define F (frame->FFA_float)
#define P (frame->FFA_float.FFA_params)

static void PutChar(Out *o, char c) { o->buf[o->di++] = c; }

/* ---------------------------------------------------------------------- */

static void StuffChars(Out *o, const char *s)
{
    while (*s)
        PutChar(o, *s++);
}

static void StuffDecimalPoint(FFA_stackFrame *frame, Out *o)
{
    F.FFA_decimalPoint = (word)o->di;
    PutChar(o, fpDecimalSeparator);
}

static void StuffPreSign(FFA_stackFrame *frame, Out *o)
{
    int s = FloatSign();
    F.FFA_numSign = (word)(sword)s;
    StuffChars(o, s >= 0 ? P.prePositive : P.preNegative);
}

static void StuffPostSign(FFA_stackFrame *frame, Out *o)
{
    StuffChars(o, (sword)F.FFA_numSign >= 0 ? P.postPositive : P.postNegative);
}

static void FloatDoPreNumeric(FFA_stackFrame *frame, Out *o)
{
    if (!(P.formatFlags & FFAF_HEADER_PRESENT)) {
        StuffPreSign(frame, o);
    } else if (P.formatFlags & FFAF_SIGN_CHAR_TO_FOLLOW_HEADER) {
        StuffChars(o, P.header);
        StuffPreSign(frame, o);
    } else {
        StuffPreSign(frame, o);
        StuffChars(o, P.header);
    }

    if (P.decimalOffset != 0) {
        Float10ToTheX((signed char)P.decimalOffset);   /* cbw */
        FloatMultiply();
    }
    if (P.formatFlags & FFAF_PERCENT) {
        FloatMultiply10();
        FloatMultiply10();
    }
    F.FFA_startNumber = (word)o->di;
}

static void FloatDoPostNumeric(FFA_stackFrame *frame, Out *o)
{
    F.FFA_endNumber = (word)o->di;

    if (P.formatFlags & FFAF_PERCENT)
        PutChar(o, '%');

    if (!(P.formatFlags & FFAF_TRAILER_PRESENT)) {
        StuffPostSign(frame, o);
    } else if (P.formatFlags & FFAF_SIGN_CHAR_TO_PRECEDE_TRAILER) {
        StuffPostSign(frame, o);
        StuffChars(o, P.trailer);
    } else {
        StuffChars(o, P.trailer);
        StuffPostSign(frame, o);
    }
}

static void RemoveTrailingZeros(FFA_stackFrame *frame, Out *o)
{
    if ((P.formatFlags & FFAF_NO_TRAIL_ZEROS) && F.FFA_decCount) {
        for (int n = F.FFA_decCount; n > 0; n--) {
            if (o->di <= 0 || o->buf[o->di - 1] != '0')
                break;
            o->di--;
        }
    }
    /* trailing decimal point is always removed */
    if (o->di > 0 && o->buf[o->di - 1] == F.FFA_decimalChar) {
        o->di--;
        F.FFA_decimalPoint = 0;
    }
}

/* ConvertWordToAscii: unsigned 16-bit, no padding, no terminator */
static void ConvertWordToAscii(Out *o, word v)
{
    char tmp[5];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        PutChar(o, tmp[--n]);
}

/*
 * ConvertDwordToAscii -- returns the number of digits stored (cx),
 * commas are not counted.
 * numDigits (bx) is only used with DTAF_INCLUDE_LEADING_ZEROS_FIXED.
 */
static int ConvertDwordToAscii(Out *o, dword v, byte flags, int numDigits)
{
    char tmp[16];
    int cnt = 0;

    do {
        tmp[cnt++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);

    int total;
    if (flags & DTAF_INCLUDE_LEADING_ZEROS_FIXED)
        total = numDigits;
    else if (flags & DTAF_INCLUDE_LEADING_ZEROS)
        total = 10;
    else
        total = cnt;

    EC(total >= cnt);
    while (cnt < total)
        tmp[cnt++] = '0';

    int toComma = 0;
    if (flags & DTAF_INCLUDE_COMMAS) {
        toComma = total % 3;
        if (toComma == 0)
            toComma = 3;
    }

    for (int left = total; left > 0; left--) {
        PutChar(o, tmp[left - 1]);
        if ((flags & DTAF_INCLUDE_COMMAS) && --toComma == 0 && left != 1) {
            PutChar(o, fpThousandsSeparator);
            toComma = 3;
        }
    }
    if (flags & DTAF_NULL_TERMINATE)
        o->buf[o->di] = '\0';       /* stosb, but di not used afterwards */
    return total;
}

/* ---------------------------------------------------------------------- */

/* RoundNumber: X := sign(X) * (|X| + 5*10^d + eps) */
static void RoundNumber(FFA_stackFrame *frame)
{
    int ax = -((int)P.decimalLimit + 1);
    if (P.formatFlags & FFAF_SCIENTIFIC)
        ax += (sword)F.FFA_curExponent;

    int s = FloatSign();
    FloatAbs();
    Float10ToTheX(ax);
    Float5();
    FloatMultiply();
    FloatAdd();
    /* HACK (asm) - to guard against numbers that are 1 bit less than desired */
    FloatEpsilon();
    FloatAdd();
    if (s < 0)
        FloatNegate();
}

/* returns the decimal exponent (ax) */
static int GetDecimalExponent(FFA_stackFrame *frame)
{
    sword ax = 0;
    EC(FpDepth() >= 1);

    if (FloatSign() != 0) {
        EC(FloatSign() > 0);
        FloatDup();
        FloatLog();
        EC(FloatIsNoNAN1());
        FloatInt();
        dword d;
        (void)FloatFloatToDword(&d);
        ax = (sword)(word)d;            /* only ax is used */
    }
    F.FFA_decExponent = (word)ax;
    F.FFA_curExponent = (word)ax;

    if (P.formatFlags & FFAF_SCIENTIFIC)
        return ax;

    int mag = ax < 0 ? -ax : ax;
    if (mag < P.totalDigits) {
        if (ax < 0) {
            F.FFA_decExponent = 0;
            F.FFA_curExponent = 0;
        }
        return ax;
    }
    /* QUIRK: with FFAF_DONT_USE_SCIENTIFIC a *negative* exponent is kept */
    if ((P.formatFlags & FFAF_DONT_USE_SCIENTIFIC) &&
        ax <= MAX_DIGITS_FOR_HUGE_NUMBERS - 1)
        return ax;

    P.formatFlags |= FFAF_SCIENTIFIC;
    return ax;
}

static void InitCommaHandling(FFA_stackFrame *frame)
{
    if (!(P.formatFlags & FFAF_USE_COMMAS) ||
        (P.formatFlags & FFAF_SCIENTIFIC) || F.FFA_startDecCount)
        return;

    F.FFA_useCommas = 0xff;
    word ax = F.FFA_curExponent;
    EC((ax >> 8) == 0);
    ax = (word)((ax & 0xff00) | ((ax + 1) & 0x00ff));   /* inc al */
    byte rem = (byte)(ax % 3);
    F.FFA_charsToComma = rem ? rem : 3;
    F.FFA_commaChar = fpThousandsSeparator;
}

/* stack on entry: ( p -digit*p num ) -- see ConvertBase10Mantissa */
static void ConvertMantissa_ProcessDigit(FFA_stackFrame *frame, Out *o, int al)
{
    if (F.FFA_startSigCount) {
        F.FFA_sigCount++;
        PutChar(o, (char)('0' + al));
        if (F.FFA_startDecCount) {
            F.FFA_decCount++;
            goto done;
        }
        goto checkDecStart;
    }

    /* deal with lead 0s; they may be significant for fixed format */
    if (al == 0) {
        if (P.formatFlags & FFAF_SCIENTIFIC) {
            /* for scientific notation, count a zero if the number is 0 */
            if (F.FFA_numSign != 0)
                goto dropLead0;
        } else {
            FloatDup();
            FloatIntFrac();
            FloatDrop();
            if (!FloatEq0())
                goto dropLead0;
            /* fixed format number < 1: want 0 before the decimal point */
            if (F.FFA_curExponent != 0)
                goto done;
            al = 0;
        }
    }

    /* countSig: */
    EC(F.FFA_startSigCount == 0);
    F.FFA_startSigCount = 0xff;
    F.FFA_sigCount++;
    if (al != 0 || !(P.formatFlags & FFAF_NO_LEAD_ZERO))
        PutChar(o, (char)('0' + al));
    if (P.formatFlags & FFAF_SCIENTIFIC)
        goto startDecimal;

checkDecStart:
    if (F.FFA_curExponent != 0)
        goto done;

startDecimal:
    F.FFA_startDecCount = 0xff;
    F.FFA_useCommas = 0;                /* no commas in fraction */
    if (P.decimalLimit != 0)
        StuffDecimalPoint(frame, o);
    goto done;

dropLead0:
    F.FFA_decExponent--;
    return;

done:
    if (F.FFA_useCommas && --F.FFA_charsToComma == 0) {
        PutChar(o, F.FFA_commaChar);
        F.FFA_charsToComma = 3;
    }
}

/* ( fp: num 10^x ) -> returns 1 (carry) when done */
static int ConvertMantissa_CheckDone(FFA_stackFrame *frame)
{
    if (!F.FFA_startSigCount)
        return 0;
    if (F.FFA_sigCount == P.totalDigits)
        return 1;

    /* beyond DECIMAL_PRECISION there is no more information: zero the number */
    if (!F.FFA_noMoreSigInfo && F.FFA_sigCount > DECIMAL_PRECISION) {
        FloatSwap();
        FloatDrop();
        Float0();
        FloatSwap();
        F.FFA_noMoreSigInfo = 0xff;
    }
    if (!F.FFA_startDecCount)
        return 0;
    return F.FFA_decCount == P.decimalLimit;
}

/* returns base 10 exponent (cx) */
static int ConvertBase10Mantissa(FFA_stackFrame *frame, Out *o)
{
    InitCommaHandling(frame);
    Float10ToTheX((sword)F.FFA_decExponent);

    do {
        if (F.FFA_noMoreSigInfo) {
            ConvertMantissa_ProcessDigit(frame, o, 0);
        } else {
            FloatOver();                /* ( num p num ) */
            FloatOver();                /* ( num p num p ) */
            FloatDIV();                 /* ( num p digit ) */
            FloatDup();
            dword d;
            (void)FloatFloatToDword(&d);
            int digit = (byte)d;        /* al */
            FloatOver();                /* ( num p digit p ) */
            FloatMultiply();            /* ( num p digit*p ) */
            FloatNegate();
            FloatRot();                 /* ( p -digit*p num ) */
            ConvertMantissa_ProcessDigit(frame, o, digit);
            FloatAdd();                 /* ( p newNum ) */
            FloatSwap();
            Float10();
            FloatDivide();              /* ( newNum p/10 ) */
        }
        F.FFA_curExponent--;
    } while (!ConvertMantissa_CheckDone(frame));

    FloatDrop();                        /* drop 10^x */
    return (sword)F.FFA_decExponent;
}

/*
 * FloatFloatToAsciiFixed -- fast path for fixed format.
 * ( fp: X -- X ) in all cases.  Returns 1 (carry) if fixed format won't work;
 * the output cursor is then restored.
 */
static int FloatFloatToAsciiFixed(FFA_stackFrame *frame, Out *o)
{
    if ((signed char)P.decimalLimit > NUM_DEC_DIGITS_IN_32_BITS)
        return 1;

    int saveDI = o->di;
    int carry;
    int cl = 0;                         /* QUIRK: cx is 0 on entry from caller */
    dword v;

    FloatDup();
    RoundNumber(frame);

    /* integer portion */
    FloatIntFrac();
    FloatSwap();                        /* ( X frac int ) */
    if (FloatFloatToDword(&v)) {
        carry = 1;
        goto popNum;
    }

    if (v == 0) {
        if (!(P.formatFlags & FFAF_NO_LEAD_ZERO))
            PutChar(o, '0');            /* QUIRK: not counted in cl */
    } else {
        cl = ConvertDwordToAscii(o, v,
                 (P.formatFlags & FFAF_USE_COMMAS) ? DTAF_INCLUDE_COMMAS : 0, 0);
    }

    if (cl == P.totalDigits) {
        carry = 0;
        goto popNum;
    }
    if (cl > P.totalDigits) {
        /* QUIRK: overwrites *all* flags (commas, percent, ...) */
        P.formatFlags = FFAF_SCIENTIFIC;
        carry = 1;
        goto popNum;
    }

    /* fractional portion */
    {
        int ch = P.totalDigits - cl;
        int n = P.decimalLimit;
        if ((signed char)ch < (signed char)n)
            n = ch;
        if (n == 0) {
            carry = 0;
            goto popNum;
        }
        Float10ToTheX(n);
        FloatMultiply();
        FloatIntFrac();                 /* truncate at the decimal point */
        FloatDrop();

        StuffDecimalPoint(frame, o);

        if (FloatFloatToDword(&v)) {
            carry = 1;
            goto done;
        }
        F.FFA_decCount = (byte)ConvertDwordToAscii(o, v,
                                 DTAF_INCLUDE_LEADING_ZEROS_FIXED, n);
        RemoveTrailingZeros(frame, o);
        carry = 0;
        goto done;
    }

popNum:
    FloatDrop();                        /* drop fraction */
done:
    if (carry)
        o->di = saveDI;
    return carry;
}

/* ---------------------------------------------------------------------- */

int FloatFloatToAscii(FFA_stackFrame *frame, char *resultString,
                      const FloatNum *number)
{
    Out o;
    int cx = 0;

    o.buf = resultString;
    o.di = 0;

    if (P.formatFlags & FFAF_FLOAT_RESERVED) {
        /* date/time formatting: FloatFloatToDateTime (floatDateTime.asm),
         * not part of this pilot */
        return 0;
    }

#ifdef DO_ERROR_CHECKING
    EC(P.totalDigits >= 1);
    if (P.formatFlags & FFAF_DONT_USE_SCIENTIFIC)
        EC(P.totalDigits <= MAX_DIGITS_FOR_HUGE_NUMBERS);
    else
        EC(P.totalDigits <= DECIMAL_PRECISION);
    EC(P.decimalLimit <= DECIMAL_PRECISION);
    EC(memchr(P.preNegative, 0, SIGN_STR_LEN + 1));
    EC(memchr(P.postNegative, 0, SIGN_STR_LEN + 1));
    EC(memchr(P.prePositive, 0, SIGN_STR_LEN + 1));
    EC(memchr(P.postPositive, 0, SIGN_STR_LEN + 1));
    if (P.formatFlags & FFAF_HEADER_PRESENT)
        EC(memchr(P.header, 0, PAD_STR_LEN + 1));
    if (P.formatFlags & FFAF_TRAILER_PRESENT)
        EC(memchr(P.trailer, 0, PAD_STR_LEN + 1));
#endif

    /* initialize vars and such */
    F.FFA_saveDI = 0;
    F.FFA_decimalChar = fpDecimalSeparator;
    F.FFA_bufSize = (P.formatFlags & FFAF_DONT_USE_SCIENTIFIC)
                        ? FLOAT_TO_ASCII_HUGE_BUF_LEN
                        : FLOAT_TO_ASCII_NORMAL_BUF_LEN;
    F.FFA_startNumber = F.FFA_decimalPoint = F.FFA_endNumber = 0;
    F.FFA_numChars = F.FFA_startExponent = 0;
    F.FFA_numSign = 0;
    F.FFA_startSigCount = F.FFA_sigCount = F.FFA_noMoreSigInfo = 0;
    F.FFA_startDecCount = F.FFA_decCount = F.FFA_useCommas = 0;

    if (P.formatFlags & FFAF_FROM_ADDR)
        FloatPushNumber(number);

    if (!FloatIsNoNAN1()) {
        /* +/- infinity -> "overflow"/"underflow", else "error" */
        char sign;
        const char *s = errorStr;
        if (FloatIsInfinity(&sign))
            s = (sign == '+') ? overflowStr : underflowStr;
        strcpy(resultString, s);        /* LocalCopyString, incl. NULL */
        cx = 0;
        goto exit;
    }

    FloatDoPreNumeric(frame, &o);

    /* initialize the rest of the buffer to a stream of '0' characters */
    memset(o.buf + o.di, '0', F.FFA_bufSize - 1 - F.FFA_startNumber);

    FloatAbs();

    if ((P.formatFlags & FFAF_SCIENTIFIC) || FloatFloatToAsciiFixed(frame, &o)) {
        GetDecimalExponent(frame);
        /* zero is already rounded; rounding code would mess it up */
        if (F.FFA_numSign != 0) {
            RoundNumber(frame);
            GetDecimalExponent(frame);
        }
        cx = ConvertBase10Mantissa(frame, &o);
        RemoveTrailingZeros(frame, &o);

        if (P.formatFlags & FFAF_SCIENTIFIC) {
            char sign = '+';
            if ((sword)cx < 0) {
                sign = '-';
                cx = -cx;
            }
            F.FFA_startExponent = (word)o.di;
            PutChar(&o, 'E');
            PutChar(&o, sign);
            if ((word)cx < 10)          /* at least 2 digits */
                PutChar(&o, '0');
            ConvertWordToAscii(&o, (word)cx);
        }
    }

    /* done, tidy up */
    FloatDoPostNumeric(frame, &o);
    cx = o.di - F.FFA_saveDI;
    F.FFA_numChars = (word)cx;
    o.buf[o.di] = '\0';

exit:
    FloatDrop();
    return cx;
}

int FloatFloatToAscii_StdFormat(word formatFlags, byte totalDigits,
                                byte decimalLimit, char *resultString,
                                const FloatNum *number)
{
    FFA_stackFrame locals;

    EC(!(formatFlags & (FFAF_HEADER_PRESENT | FFAF_TRAILER_PRESENT |
                        FFAF_SIGN_CHAR_TO_FOLLOW_HEADER |
                        FFAF_SIGN_CHAR_TO_PRECEDE_TRAILER)));

    memset(&locals, 0, sizeof locals);
    locals.FFA_float.FFA_params.formatFlags  = formatFlags;
    locals.FFA_float.FFA_params.decimalOffset = 0;
    locals.FFA_float.FFA_params.totalDigits  = totalDigits;
    locals.FFA_float.FFA_params.decimalLimit = decimalLimit;
    locals.FFA_float.FFA_params.preNegative[0] = '-';
    return FloatFloatToAscii(&locals, resultString, number);
}

/* FloatGetNumDigitsInIntegerPart: pops X, returns digits in int(|X|) */
int FloatGetNumDigitsInIntegerPart(void)
{
    FloatAbs();
    FloatTrunc();
    FloatLog();
    if (!FloatIsNoNAN1()) {             /* log(0) */
        FloatDrop();
        return 1;
    }
    FloatTrunc();
    dword d;
    (void)FloatFloatToDword(&d);
    return (word)d + 1;
}
