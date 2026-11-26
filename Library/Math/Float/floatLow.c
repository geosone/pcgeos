/*
 * floatLow.c -- after floatLow.asm: Software Floating Point Primitives
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

static FpValue fp_trunc(FpValue x) { return x < 0 ? ceil(x) : floor(x); }

static FpValue fp_round_even(FpValue x) /* x >= 0 */
{
    FpValue f = floor(x), d = x - f;
    if (d > 0.5 || (d == 0.5 && fmod(f, 2.0) != 0))
        f += 1;
    return f;
}

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
