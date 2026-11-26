/*
 * floatStack.c -- the floating point stack (after floatStack.asm; the
 * routines on it are in the float*.c files of their asm files).
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

FP_THREAD_LOCAL FpValue fpStack[FP_STACK_MAX];
FP_THREAD_LOCAL int fpSP;

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

int fp_signbit(FpValue x)
{
    return (fp_bits(x) >> 63) != 0;
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

FpValue FromFloatNum(const FloatNum *n)
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

void ToFloatNum(FpValue x, FloatNum *n)
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

/* --- stack shuffling --- */

void FloatRot(void)              /* ( a b c -- b c a ) */
{
    FpValue c = FloatPopValue(), b = FloatPopValue(), a = FloatPopValue();
    FloatPushValue(b);
    FloatPushValue(c);
    FloatPushValue(a);
}

/* --- arithmetic --- */

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
