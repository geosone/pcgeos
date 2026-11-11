/*
 * fpstack.h -- minimal 32/64-bit shim for the PC/GEOS math library's
 *              floating point stack (pilot port, see README.md).
 *
 * GEOS-80 numbers (FloatNum, 10 bytes: 4 mantissa words + sign/exponent word,
 * bias 0x3fff, explicit integer bit) have the layout of the x87 80-bit
 * extended format.  The stack holds IEEE `double` values (FpValue): the
 * same on every host and compiler (x86, arm64, armhf; GCC, Clang, Open
 * Watcom), as agreed for the port (PLAN.md, upstream #1239).  FloatNum is
 * converted arithmetically, never by copying bytes, so the host's
 * `long double` (80, 64 or 128 bits) does not matter.
 *
 * Consequences of double: 53-bit mantissa (15-17 significant digits) and
 * the range of double; GEOS-80 numbers beyond it become +/-inf or 0.
 */
#ifndef FPSTACK_H
#define FPSTACK_H

#include <stdint.h>

typedef uint8_t  byte;
typedef uint16_t word;
typedef int16_t  sword;
typedef uint32_t dword;
typedef int32_t  sdword;

typedef double FpValue;       /* one element of the float stack */

/* In-memory GEOS-80 layout (math.def: FloatNum). */
typedef struct {
    word F_mantissa_wd0;
    word F_mantissa_wd1;
    word F_mantissa_wd2;
    word F_mantissa_wd3;
    word F_exponent;          /* bit 15 = sign, bits 0-14 = biased exponent */
} FloatNum;

/* ---- stack primitives (Forth-style names as in the asm sources) ---- */
void   FpReset(void);
int    FpDepth(void);
void    FloatPushValue(FpValue x);
FpValue FloatPopValue(void);
FpValue FloatTopValue(void);
void   FloatPushNumber(const FloatNum *n);     /* FloatPushNumberFar */
void   FloatPopNumber(FloatNum *n);

void   FloatDup(void);
void   FloatDrop(void);
void   FloatSwap(void);
void   FloatOver(void);
void   FloatRot(void);

void   Float0(void);
void   Float1(void);
void   Float5(void);
void   Float10(void);

void   FloatAbs(void);
void   FloatNegate(void);
void   FloatAdd(void);
void   FloatMultiply(void);
void   FloatMultiply10(void);
void   FloatDivide(void);
void   FloatDIV(void);               /* trunc(X1/X2), with epsilon hack */
void   FloatLog(void);               /* log10 */
void   FloatIntFrac(void);           /* ( X -- int frac ), trunc toward 0 */
void   FloatInt(void);               /* floor */
void   FloatTrunc(void);             /* trunc, with epsilon hack */
void   FloatEpsilon(void);           /* ( X -- X ulp(X) ) */
void   Float10ToTheX(int x);         /* table-based, same as asm */

int    FloatSign(void);              /* -1 / 0 / +1, X unchanged */
int    FloatEq0(void);               /* pops X; 1 if X == 0 */
int    FloatIsNoNAN1(void);          /* 1 if top is not NaN/Inf (exp != 7fff) */
int    FloatIsInfinity(char *signChar); /* 1 if +/-inf, *signChar = '+'/'-' */

/* Pops X, rounds to nearest-even into a dword.  Returns 0 on success,
 * 1 ("carry set") if out of range.  Bit-compatible with
 * FloatFloatToDwordNoPop incl. its edge cases. */
int    FloatFloatToDword(dword *result);

/* Localization shim (LocalGetNumericFormat) */
extern char fpDecimalSeparator;
extern char fpThousandsSeparator;

#endif
