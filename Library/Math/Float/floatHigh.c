/*
 * floatHigh.c -- after floatHigh.asm: FloatSwap, FloatOver, FloatNegate,
 * FloatAdd, FloatMultiply, FloatMultiply10, FloatDivide, FloatEpsilon,
 * FloatTrunc, FloatDIV, Float10ToTheX.
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

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
