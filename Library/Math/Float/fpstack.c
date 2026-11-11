/*
 * fpstack.c -- minimal floating point stack shim (pilot port).
 *
 * Mirrors the semantics of Library/Math/Float/ asm routines that
 * floatFloatToAscii.asm depends on.  Arithmetic is IEEE double
 * (round-to-nearest-even), see fpstack.h.  log10 is only used to
 * *estimate* the decimal exponent.
 *
 * Only C89/C99 basics of <math.h> are used (floor, ceil, fabs, log10,
 * frexp, ldexp), so Open Watcom builds it too.
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

#define FP_STACK_MAX 64

/* one float stack per thread, as in GEOS; dos32 has a single thread */
#if defined(__WATCOMC__)
#define FP_THREAD_LOCAL
#else
#define FP_THREAD_LOCAL _Thread_local
#endif

static FP_THREAD_LOCAL FpValue fpStack[FP_STACK_MAX];
static FP_THREAD_LOCAL int fpSP;

/* --- math helpers (C99 functions Open Watcom lacks) --- */

/* NaN/infinity by bit pattern: comparisons with NaN are not reliable on
 * every compiler (Open Watcom: x != x is false for NaN).  IEEE double has
 * the same layout on all targets (byte order handled by the uint64). */
static uint64_t fp_bits(FpValue x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof u);
    return u;
}

static FpValue fp_from_bits(uint64_t u)
{
    FpValue x;
    memcpy(&x, &u, sizeof x);
    return x;
}

#define FP_EXP_MASK  0x7ff0000000000000ull
#define FP_MANT_MASK 0x000fffffffffffffull

static int fp_isnan(FpValue x)
{
    uint64_t u = fp_bits(x);
    return (u & FP_EXP_MASK) == FP_EXP_MASK && (u & FP_MANT_MASK) != 0;
}

static int fp_isinf(FpValue x)
{
    uint64_t u = fp_bits(x);
    return (u & FP_EXP_MASK) == FP_EXP_MASK && (u & FP_MANT_MASK) == 0;
}

static int fp_signbit(FpValue x)
{
    return (fp_bits(x) >> 63) != 0;
}

static FpValue fp_trunc(FpValue x) { return x < 0 ? ceil(x) : floor(x); }

static FpValue fp_copysign(FpValue mag, FpValue sgn)
{
    mag = fabs(mag);
    return fp_signbit(sgn) ? -mag : mag;
}

static int fp_ilogb(FpValue x)          /* x != 0, finite */
{
    int e;
    (void)frexp(x, &e);                 /* x = m * 2^e, 0.5 <= |m| < 1 */
    return e - 1;
}

static FpValue fp_round_even(FpValue x) /* x >= 0 */
{
    FpValue f = floor(x), d = x - f;
    if (d > 0.5 || (d == 0.5 && fmod(f, 2.0) != 0))
        f += 1;
    return f;
}

char fpDecimalSeparator   = '.';
char fpThousandsSeparator = ',';

void FpReset(void) { fpSP = 0; }
int  FpDepth(void) { return fpSP; }

void FloatPushValue(FpValue x)
{
    assert(fpSP < FP_STACK_MAX);
    fpStack[fpSP++] = x;
}

FpValue FloatPopValue(void)
{
    assert(fpSP > 0);
    return fpStack[--fpSP];
}

FpValue FloatTopValue(void)
{
    assert(fpSP > 0);
    return fpStack[fpSP - 1];
}

/* --- conversion between GEOS-80 and double, arithmetically --- */

typedef char FloatNumIs10Bytes[sizeof(FloatNum) == 10 ? 1 : -1];

static FpValue FromFloatNum(const FloatNum *n)
{
    int e = n->F_exponent & 0x7fff;
    int neg = (n->F_exponent & 0x8000) != 0;
    FpValue m, x;
    if (e == 0x7fff) {                  /* infinity or NaN */
        int nan = (n->F_mantissa_wd3 & 0x7fff) || n->F_mantissa_wd2 ||
                  n->F_mantissa_wd1 || n->F_mantissa_wd0;
        return fp_from_bits((neg ? 0x8000000000000000ull : 0) | FP_EXP_MASK
                            | (nan ? 0x0008000000000000ull : 0));
    }
    /* 64-bit mantissa with explicit integer bit; the conversion to double
     * rounds to nearest at the last step (ldexp is exact) */
    m = ldexp((FpValue)n->F_mantissa_wd3, 48) + ldexp((FpValue)n->F_mantissa_wd2, 32)
      + ldexp((FpValue)n->F_mantissa_wd1, 16) + (FpValue)n->F_mantissa_wd0;
    x = ldexp(m, (e ? e : 1) - 0x3fff - 63);
    return neg ? -x : x;
}

static void ToFloatNum(FpValue x, FloatNum *n)
{
    word sign = fp_signbit(x) ? 0x8000 : 0;
    int e;
    FpValue m;
    dword hi, lo;
    if (fp_isnan(x)) {                  /* NaN: GEOS "quiet NaN" pattern */
        n->F_mantissa_wd3 = 0xc000;
        n->F_mantissa_wd2 = n->F_mantissa_wd1 = n->F_mantissa_wd0 = 0;
        n->F_exponent = (word)(sign | 0x7fff);
        return;
    }
    x = fabs(x);
    if (x == 0 || fp_isinf(x)) {        /* zero or infinity */
        n->F_mantissa_wd3 = (word)(x == 0 ? 0 : 0x8000);
        n->F_mantissa_wd2 = n->F_mantissa_wd1 = n->F_mantissa_wd0 = 0;
        n->F_exponent = (word)(sign | (x == 0 ? 0 : 0x7fff));
        return;
    }
    m = frexp(x, &e);                   /* x = m * 2^e, 0.5 <= m < 1 */
    m = ldexp(m, 32);                   /* top 32 bits, exact */
    hi = (dword)m;
    lo = (dword)ldexp(m - (FpValue)hi, 32);
    n->F_mantissa_wd3 = (word)(hi >> 16);
    n->F_mantissa_wd2 = (word)hi;
    n->F_mantissa_wd1 = (word)(lo >> 16);
    n->F_mantissa_wd0 = (word)lo;
    n->F_exponent = (word)(sign | (e - 1 + 0x3fff));
}

static word ExponentWord(FpValue x)
{
    FloatNum n;
    ToFloatNum(x, &n);
    return n.F_exponent;
}

void FloatPushNumber(const FloatNum *n) { FloatPushValue(FromFloatNum(n)); }
void FloatPopNumber(FloatNum *n)        { ToFloatNum(FloatPopValue(), n); }

/* --- stack shuffling --- */

void FloatDup(void)  { FloatPushValue(FloatTopValue()); }
void FloatDrop(void) { (void)FloatPopValue(); }

void FloatSwap(void)
{
    FpValue b = FloatPopValue(), a = FloatPopValue();
    FloatPushValue(b);
    FloatPushValue(a);
}

void FloatOver(void)
{
    assert(fpSP >= 2);
    FloatPushValue(fpStack[fpSP - 2]);
}

void FloatRot(void)              /* ( a b c -- b c a ) */
{
    FpValue c = FloatPopValue(), b = FloatPopValue(), a = FloatPopValue();
    FloatPushValue(b);
    FloatPushValue(c);
    FloatPushValue(a);
}

/* --- constants --- */
void Float0(void)  { FloatPushValue(0.0); }
void Float1(void)  { FloatPushValue(1.0); }
void Float5(void)  { FloatPushValue(5.0); }
void Float10(void) { FloatPushValue(10.0); }

/* --- arithmetic --- */

void FloatAbs(void)    { FloatPushValue(fabs(FloatPopValue())); }
void FloatNegate(void) { FloatPushValue(-FloatPopValue()); }

void FloatAdd(void)
{
    FpValue b = FloatPopValue(), a = FloatPopValue();
    FloatPushValue(a + b);
}

void FloatMultiply(void)
{
    FpValue b = FloatPopValue(), a = FloatPopValue();
    FloatPushValue(a * b);
}

void FloatMultiply10(void) { Float10(); FloatMultiply(); }

void FloatDivide(void)
{
    FpValue b = FloatPopValue(), a = FloatPopValue();
    FloatPushValue(a / b);
}

void FloatLog(void) { FloatPushValue(log10(FloatPopValue())); }

void FloatIntFrac(void)
{
    FpValue x = FloatPopValue();
    FpValue i = fp_trunc(x);
    FloatPushValue(i);
    FloatPushValue(x - i);
}

int FloatSign(void)
{
    FpValue x = FloatTopValue();
    return (x < 0) ? -1 : (x > 0) ? 1 : 0;
}

int FloatEq0(void) { return FloatPopValue() == 0.0; }

/* FloatEpsilon: 1 ulp of X in the working precision (asm: same exponent,
 * mantissa 1 -> 1 ulp of GEOS-80; here of double).  Sign of X kept. */
void FloatEpsilon(void)
{
    FpValue x = FloatTopValue();
    FpValue e;
    if (x == 0.0) {
        e = ldexp(1.0, -1074);  /* smallest double denormal */
    } else {
        e = ldexp(1.0, fp_ilogb(x) - 52);   /* 1 ulp in double */
    }
    FloatPushValue(fp_copysign(e, x));
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
    FloatPushValue(-FloatPopValue());   /* i - 1 */
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
    return (ExponentWord(FloatTopValue()) & 0x7fff) != 0x7fff;
}

int FloatIsInfinity(char *signChar)
{
    FloatNum n;
    ToFloatNum(FloatTopValue(), &n);
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
    ToFloatNum(FloatPopValue(), &n);
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

    FpValue mag;
    FloatNum m = n;
    m.F_exponent = exp;                     /* |x| */
    mag = FromFloatNum(&m);
    dword v = (dword)fp_round_even(mag);    /* nearest-even, as the asm */
    if (n.F_exponent & 0x8000)
        v = 0u - v;
    *result = v;
    return 0;
}
