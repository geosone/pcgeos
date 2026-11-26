/*
 * floatFixed.c -- after floatFixed.asm: Floating point code that belongs
 * in fixed resources in order to eliminate the ~730 cycle overhead that
 * goes along with each floating point call.
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

static word ExponentWord(FpValue x)
{
    FloatNum n;
    ToFloatNum(x, &n);
    return n.F_exponent;
}

void FloatPushNumber(const FloatNum *n) { FloatPushValue(FromFloatNum(n)); }
void FloatPopNumber(FloatNum *n)        { ToFloatNum(FloatPopValue(), n); }
void FloatDup(void)  { FloatPushValue(FloatTopValue()); }
void FloatDrop(void) { (void)FloatPopValue(); }
void FloatAbs(void)    { FloatPushValue(fabs(FloatPopValue())); }

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
