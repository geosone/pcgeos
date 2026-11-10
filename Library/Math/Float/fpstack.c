/*
 * fpstack.c -- minimal floating point stack shim (pilot port).
 *
 * Mirrors the semantics of Library/Math/Float/ asm routines that
 * floatFloatToAscii.asm depends on.  Arithmetic is done by the x87 via
 * `long double` (round-to-nearest-even, 64-bit precision), which matches the
 * guard/round/sticky rounding of the GEOS software emulator for +,-,*,/.
 * log10 is the one place where last-bit differences to GEOS' series-based
 * FloatLog are possible (only used to *estimate* the decimal exponent).
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

#define FP_STACK_MAX 64

static _Thread_local long double fpStack[FP_STACK_MAX];
static _Thread_local int fpSP;

char fpDecimalSeparator   = '.';
char fpThousandsSeparator = ',';

void FpReset(void) { fpSP = 0; }
int  FpDepth(void) { return fpSP; }

void FloatPushLD(long double x)
{
    assert(fpSP < FP_STACK_MAX);
    fpStack[fpSP++] = x;
}

long double FloatPopLD(void)
{
    assert(fpSP > 0);
    return fpStack[--fpSP];
}

long double FloatTopLD(void)
{
    assert(fpSP > 0);
    return fpStack[fpSP - 1];
}

/* --- conversion between GEOS-80 memory layout and long double --- */

static long double FromFloatNum(const FloatNum *n)
{
    long double x = 0;
    _Static_assert(sizeof(FloatNum) == 10, "FloatNum must be 10 bytes");
    memcpy(&x, n, 10);          /* x87 extended: identical layout */
    return x;
}

static void ToFloatNum(long double x, FloatNum *n)
{
    memcpy(n, &x, 10);
}

static word ExponentWord(long double x)
{
    FloatNum n;
    ToFloatNum(x, &n);
    return n.F_exponent;
}

void FloatPushNumber(const FloatNum *n) { FloatPushLD(FromFloatNum(n)); }
void FloatPopNumber(FloatNum *n)        { ToFloatNum(FloatPopLD(), n); }

/* --- stack shuffling --- */

void FloatDup(void)  { FloatPushLD(FloatTopLD()); }
void FloatDrop(void) { (void)FloatPopLD(); }

void FloatSwap(void)
{
    long double b = FloatPopLD(), a = FloatPopLD();
    FloatPushLD(b);
    FloatPushLD(a);
}

void FloatOver(void)
{
    assert(fpSP >= 2);
    FloatPushLD(fpStack[fpSP - 2]);
}

void FloatRot(void)              /* ( a b c -- b c a ) */
{
    long double c = FloatPopLD(), b = FloatPopLD(), a = FloatPopLD();
    FloatPushLD(b);
    FloatPushLD(c);
    FloatPushLD(a);
}

/* --- constants --- */
void Float0(void)  { FloatPushLD(0.0L); }
void Float1(void)  { FloatPushLD(1.0L); }
void Float5(void)  { FloatPushLD(5.0L); }
void Float10(void) { FloatPushLD(10.0L); }

/* --- arithmetic --- */

void FloatAbs(void)    { FloatPushLD(fabsl(FloatPopLD())); }
void FloatNegate(void) { FloatPushLD(-FloatPopLD()); }

void FloatAdd(void)
{
    long double b = FloatPopLD(), a = FloatPopLD();
    FloatPushLD(a + b);
}

void FloatMultiply(void)
{
    long double b = FloatPopLD(), a = FloatPopLD();
    FloatPushLD(a * b);
}

void FloatMultiply10(void) { Float10(); FloatMultiply(); }

void FloatDivide(void)
{
    long double b = FloatPopLD(), a = FloatPopLD();
    FloatPushLD(a / b);
}

void FloatLog(void) { FloatPushLD(log10l(FloatPopLD())); }

void FloatIntFrac(void)
{
    long double x = FloatPopLD();
    long double i = truncl(x);
    FloatPushLD(i);
    FloatPushLD(x - i);
}

int FloatSign(void)
{
    long double x = FloatTopLD();
    return (x < 0) ? -1 : (x > 0) ? 1 : 0;
}

int FloatEq0(void) { return FloatPopLD() == 0.0L; }

/* FloatEpsilon: same exponent as X, mantissa = 1, normalized -> 1 ulp of X
 * (sign of X kept, since the asm keeps the exponent word). */
void FloatEpsilon(void)
{
    long double x = FloatTopLD();
    long double e;
    if (x == 0.0L) {
        e = 0x1p-16445L;        /* exp 0, mantissa 1: smallest denormal */
    } else {
        e = ldexpl(1.0L, ilogbl(x) - 63);
    }
    FloatPushLD(copysignl(e, x));
}

void FloatInt(void)              /* floor, built like the asm version */
{
    int s = FloatSign();
    FloatIntFrac();
    if (s >= 0) {
        FloatDrop();
        return;
    }
    if (FloatEq0())
        return;
    Float1();
    FloatPushLD(-FloatPopLD());   /* i - 1 */
    FloatAdd();
}

void FloatTrunc(void)
{
    int s = FloatSign();
    if (s != 0) {
        /* HACK (asm): guard against numbers that are 1 bit less than desired */
        FloatAbs();
        FloatEpsilon();
        FloatAdd();
        if (s < 0)
            FloatNegate();
    }
    FloatIntFrac();
    FloatDrop();
}

void FloatDIV(void)
{
    FloatDivide();
    FloatTrunc();
}

/* Float10ToTheX: same table + multiplication order as floatHigh.asm, so the
 * results are bit-identical to the GEOS emulator. */
static const FloatNum tenToTheOnes[10] = {
    {0, 0, 0,      0x8000, 0x3fff}, {0, 0, 0,      0xa000, 0x4002},
    {0, 0, 0,      0xc800, 0x4005}, {0, 0, 0,      0xfa00, 0x4008},
    {0, 0, 0,      0x9c40, 0x400c}, {0, 0, 0,      0xc350, 0x400f},
    {0, 0, 0,      0xf424, 0x4012}, {0, 0, 0x8000, 0x9896, 0x4016},
    {0, 0, 0x2000, 0xbebc, 0x4019}, {0, 0, 0x2800, 0xee6b, 0x401c},
};
static const FloatNum tenToTheTen      = {0, 0, 0xf900, 0x9502, 0x4020};
static const FloatNum tenToTheHundred  = {0xe759, 0xa61b, 0x692c, 0x924d, 0x414b};
static const FloatNum tenToTheThousand = {0xac01, 0xdd3d, 0xb1f9, 0xf38d, 0x4cf8};

#define DECIMAL_EXPONENT_LOWER_LIMIT (-4932)

static void IncorporatePwrOfTenMultiple(const FloatNum *pwr, int multiple)
{
    if (multiple == 0) {
        Float1();
    } else {
        FloatPushNumber(pwr);
        if (multiple > 1) {
            FloatDup();
            for (int i = multiple - 1; i > 0; i--) {
                FloatOver();
                FloatMultiply();
            }
            FloatSwap();
            FloatDrop();
        }
    }
    FloatMultiply();
}

void Float10ToTheX(int x)
{
    if (x < DECIMAL_EXPONENT_LOWER_LIMIT) {
        Float0();
        return;
    }
    int neg = x < 0;
    unsigned ax = neg ? (unsigned)-x : (unsigned)x;

    FloatPushNumber(&tenToTheOnes[ax % 10]);
    ax /= 10;
    if (ax) { IncorporatePwrOfTenMultiple(&tenToTheTen,      ax % 10); ax /= 10; }
    if (ax) { IncorporatePwrOfTenMultiple(&tenToTheHundred,  ax % 10); ax /= 10; }
    if (ax) { IncorporatePwrOfTenMultiple(&tenToTheThousand, ax % 10); }

    if (neg) {                   /* FloatInverse: 1 / x */
        Float1();
        FloatSwap();
        FloatDivide();
    }
}

int FloatIsNoNAN1(void)
{
    return (ExponentWord(FloatTopLD()) & 0x7fff) != 0x7fff;
}

int FloatIsInfinity(char *signChar)
{
    FloatNum n;
    ToFloatNum(FloatTopLD(), &n);
    *signChar = (n.F_exponent & 0x8000) ? '-' : '+';
    return (n.F_exponent & 0x7fff) == 0x7fff && n.F_mantissa_wd3 == 0x8000 &&
           n.F_mantissa_wd2 == 0 && n.F_mantissa_wd1 == 0 && n.F_mantissa_wd0 == 0;
}

/*
 * Bit-compatible with FloatFloatToDwordNoPop (floatLow.asm):
 *  - exponent check happens *before* rounding, so values in
 *    [2^31-0.5, 2^31) round to 0x80000000 without error,
 *  - -2^31 exactly is accepted (mgroeb 2000 fix),
 *  - rounding is round-half-even on the magnitude, then sign applied.
 */
int FloatFloatToDword(dword *result)
{
    FloatNum n;
    ToFloatNum(FloatPopLD(), &n);
    word exp = n.F_exponent & 0x7fff;

    if (exp == 0) {
        *result = 0;
        return 0;
    }
    if (exp > 0x401e) {
        *result = 0x80000000u;
        return 1;
    }
    if (n.F_exponent == 0x401e) {          /* +2^31 .. : out of range */
        *result = 0x80000000u;
        return 1;
    }
    if (n.F_exponent == 0xc01e) {          /* only exactly -2^31 is OK */
        if (n.F_mantissa_wd3 == 0x8000 && !n.F_mantissa_wd2 &&
            !n.F_mantissa_wd1 && !n.F_mantissa_wd0) {
            *result = 0x80000000u;
            return 0;
        }
        *result = 0x80000000u;
        return 1;
    }

    long double mag;
    FloatNum m = n;
    m.F_exponent = exp;                     /* |x| */
    mag = FromFloatNum(&m);
    dword v = (dword)nearbyintl(mag);       /* default mode: nearest-even */
    if (n.F_exponent & 0x8000)
        v = 0u - v;
    *result = v;
    return 0;
}
